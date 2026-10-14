#ifndef RADIX_SORT_HLSLI
#define RADIX_SORT_HLSLI

// Keep these constants and the 8-byte pair layout in sync with radix_sort.h.
#define RADIX_SORT_THREADS 256
#define RADIX_SORT_BITS 4
#define RADIX_SORT_BUCKETS 16

// The sorter compares unsigned keys and carries indices unchanged; neither field has scene-specific meaning.
struct SortPair {
    uint key;
    uint index;
};

#endif
