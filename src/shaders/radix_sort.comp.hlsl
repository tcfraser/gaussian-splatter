#include "radix_sort.hlsli"

// ================================= [Radix Sort Compute Shader] ==========================================
//  The purpose of this shader is to sort `(key, index)` pairs by ascending unsigned key value using radix sort.
//
//  Each key is a 32-bit unsigned integer, sorted four bits at a time over eight passes.
//  RADIX_SORT_BITS = 4 sets the number of bits per pass, and RADIX_SORT_BUCKETS = 16 provides one bucket for each possible digit.
//  The current four-bit digit is extracted using the CPU-supplied bit_shift:
//      digit = (key >> bit_shift) & 15;    // 15 is ...00001111 in binary, selecting four bits.
//  For each frame's sort, the CPU advances bit_shift through these values, processing the least significant bits first:
//      bit_shift = 0, 4, 8, 12, 16, 20, 24, 28
//  Each pass preserves input order among pairs with the same digit, retaining the ordering established by previous passes.
//
//  Each thread group consists of RADIX_SORT_THREADS = 256 threads.
//  In histogram and scatter, each valid thread processes the pair at this position in the current input buffer:
//      pair_id = group_id.x * RADIX_SORT_THREADS + local_id
//  The masks record which of the 16 buckets each pair belongs to; pairs remain unmoved until scatter.
//
//  Each group has its own groupshared arrays:
//      1) bucket_masks : records which local input positions belong to each bucket.
//      2) scan_values  : temporary storage for computing prefix sums of histogram counts or block totals.
//
//  The challenge is to convert all groups' bucket counts into non-overlapping output ranges.
//  The scans calculate those ranges, and scatter moves the pairs into them.
//  Each pair's destination is the sum of three counts:
//      1) Elements with smaller current digits across all groups.
//      2) Elements with the same current digit in earlier input groups.
//      3) Elements with the same current digit earlier within this group.
//  Earlier means earlier in the current input array, regardless of the groups' execution or completion order.
//
//  The functions have the following jobs:
//      1) build_bucket_masks : records bucket membership within a group.
//      2) histogram          : counts the pairs each group contributes to each bucket.
//      3) exclusive_scan     : computes an exclusive prefix sum within one thread group, plus its total.
//      4) scan_blocks        : scans blocks of histogram counts and writes each block's total.
//      5) scan_sums          : scans those block totals to calculate offsets between blocks.
//      6) scatter            : writes each pair into its unique output position.
//  build_bucket_masks and exclusive_scan are helpers called by the dispatched entry points.

// Four entry points implement one stable radix pass: histogram, scan_blocks, scan_sums, scatter.
// The CPU dispatches them in separate compute passes, then swaps the input and output pair buffers.
cbuffer RadixUniform : register(b0, space2)
{
    uint pair_count;        // The number of (key, index) pairs being sorted.
    uint group_count;       // The number of input-pair groups: ceil(pair_count / RADIX_SORT_THREADS).
    uint bit_shift;         // Advances from 0 to 28 in steps of RADIX_SORT_BITS = 4; the extraction mask is RADIX_SORT_BUCKETS - 1 = 15.
    uint histogram_count;   // The number of (bucket, group) count entries: RADIX_SORT_BUCKETS * group_count; there are 16 distinct digits.
};

// Input/output hold pairs for histogram/scatter and uint counts/offsets for the scans.
ByteAddressBuffer input_buffer : register(t0, space0);
ByteAddressBuffer offsets : register(t1, space0);       // Used only by scatter.
ByteAddressBuffer block_offsets : register(t2, space0); // Used only by scatter.
RWByteAddressBuffer output_buffer : register(u0, space1);
RWByteAddressBuffer block_sums : register(u1, space1);   // Used only by scan_blocks.

// Each bucket has one bit per input slot: eight 32-bit words cover a group of 256 pairs.
// This records input order explicitly, independently of the order in which threads execute atomics.
#define MASK_WORDS (RADIX_SORT_THREADS / 32)
groupshared uint bucket_masks[RADIX_SORT_BUCKETS * MASK_WORDS];
groupshared uint scan_values[RADIX_SORT_THREADS];

SortPair build_bucket_masks(uint pair_id, uint local_id)
{
    if (local_id < RADIX_SORT_BUCKETS * MASK_WORDS) {
        bucket_masks[local_id] = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    SortPair pair = (SortPair)0;
    if (pair_id < pair_count) {
        pair = input_buffer.Load<SortPair>(pair_id * sizeof(SortPair));
        uint digit = (pair.key >> bit_shift) & (RADIX_SORT_BUCKETS - 1);
        InterlockedOr(bucket_masks[digit * MASK_WORDS + local_id / 32], 1u << (local_id % 32));
    }
    // Even unused threads in the last group must reach both barriers.
    GroupMemoryBarrierWithGroupSync();
    return pair;
}

[numthreads(RADIX_SORT_THREADS, 1, 1)]
void histogram(uint3 group_id : SV_GroupID, uint local_id : SV_GroupIndex)
{
    build_bucket_masks(group_id.x * RADIX_SORT_THREADS + local_id, local_id);
    if (local_id < RADIX_SORT_BUCKETS) {
        uint count = 0;
        [unroll]
        for (uint word = 0; word < MASK_WORDS; ++word) {
            count += countbits(bucket_masks[local_id * MASK_WORDS + word]);
        }
        // Bucket-major order lets one exclusive scan account for both earlier buckets and earlier groups.
        output_buffer.Store((local_id * group_count + group_id.x) * 4, count);
    }
}

// An exclusive prefix sum returns the sum of values BEFORE this thread, plus the total for the whole group.
// Doubling the distance each step builds the prefix in log2(256) shared-memory rounds.
uint exclusive_scan(uint value, uint local_id, out uint total)
{
    uint sum = value;
    scan_values[local_id] = sum;
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint distance = 1; distance < RADIX_SORT_THREADS; distance *= 2) {
        uint previous = 0;
        if (local_id >= distance) {
            previous = scan_values[local_id - distance];
        }
        GroupMemoryBarrierWithGroupSync(); // Finish all reads before replacing this round's values.
        sum += previous;
        scan_values[local_id] = sum;
        GroupMemoryBarrierWithGroupSync();
    }
    total = scan_values[RADIX_SORT_THREADS - 1];
    GroupMemoryBarrierWithGroupSync(); // All threads must read the total before another scan reuses shared memory.
    return sum - value;
}

[numthreads(RADIX_SORT_THREADS, 1, 1)]
void scan_blocks(uint3 group_id : SV_GroupID, uint local_id : SV_GroupIndex)
{
    uint element = group_id.x * RADIX_SORT_THREADS + local_id;
    uint value = 0;
    if (element < histogram_count) {
        value = input_buffer.Load(element * 4);
    }
    uint total;
    uint prefix = exclusive_scan(value, local_id, total);
    if (element < histogram_count) {
        output_buffer.Store(element * 4, prefix);
    }
    if (local_id == 0) {
        block_sums.Store(group_id.x * 4, total);
    }
}

[numthreads(RADIX_SORT_THREADS, 1, 1)]
void scan_sums(uint local_id : SV_GroupIndex)
{
    uint count = (histogram_count + RADIX_SORT_THREADS - 1) / RADIX_SORT_THREADS;
    uint carry = 0;
    // Only one total per 256 histogram entries is scanned here, at most 4096 totals at the sorter's limit.
    // One group processes successive chunks, so no cross-group synchronization or spin waiting is needed.
    for (uint base = 0; base < count; base += RADIX_SORT_THREADS) {
        uint element = base + local_id;
        uint value = 0;
        if (element < count) {
            value = input_buffer.Load(element * 4);
        }
        uint total;
        uint prefix = exclusive_scan(value, local_id, total);
        if (element < count) {
            output_buffer.Store(element * 4, carry + prefix);
        }
        carry += total;
    }
}

[numthreads(RADIX_SORT_THREADS, 1, 1)]
void scatter(uint3 group_id : SV_GroupID, uint local_id : SV_GroupIndex)
{
    uint pair_id = group_id.x * RADIX_SORT_THREADS + local_id;
    SortPair pair = build_bucket_masks(pair_id, local_id);
    if (pair_id >= pair_count) {
        return;
    }
    uint digit = (pair.key >> bit_shift) & (RADIX_SORT_BUCKETS - 1);
    uint word = local_id / 32;
    uint lower_bits = (1u << (local_id % 32)) - 1u;
    uint local_rank = countbits(bucket_masks[digit * MASK_WORDS + word] & lower_bits);
    for (uint earlier_word = 0; earlier_word < word; ++earlier_word) {
        local_rank += countbits(bucket_masks[digit * MASK_WORDS + earlier_word]);
    }

    uint histogram_id = digit * group_count + group_id.x;
    uint bucket_start = offsets.Load(histogram_id * 4)
                      + block_offsets.Load((histogram_id / RADIX_SORT_THREADS) * 4);
    // Stable placement preserves the order of equal digits, including the lower bits sorted by earlier passes.
    output_buffer.Store<SortPair>((bucket_start + local_rank) * sizeof(SortPair), pair);
}
