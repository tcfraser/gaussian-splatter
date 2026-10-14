#pragma once

#include <SDL3/SDL.h>

#include "shader.h"

// Matches shaders/radix_sort.hlsli; unsigned keys are sorted in ascending order, with equal keys kept stable.
constexpr Uint32 RADIX_SORT_THREADS = 256;
constexpr Uint32 RADIX_SORT_BITS = 4;
constexpr Uint32 RADIX_SORT_BUCKETS = 16;
constexpr Uint32 RADIX_SORT_MAX_PAIRS = 65535 * RADIX_SORT_THREADS;

struct SortPair {
    Uint32 key;
    Uint32 index;
};

// Owns two interchangeable pair buffers and the small count/offset buffers used by the radix passes.
struct RadixSort {
    SDL_GPUComputePipeline* histogram_pipeline = nullptr;
    SDL_GPUComputePipeline* scan_blocks_pipeline = nullptr;
    SDL_GPUComputePipeline* scan_sums_pipeline = nullptr;
    SDL_GPUComputePipeline* scatter_pipeline = nullptr;
    SDL_GPUBuffer* pair_buffers[2] = {};
    SDL_GPUBuffer* histogram_buffer = nullptr;
    SDL_GPUBuffer* offset_buffer = nullptr;
    SDL_GPUBuffer* block_sum_buffer = nullptr;
    SDL_GPUBuffer* block_offset_buffer = nullptr;
    Uint32 pair_count = 0;
    Uint32 group_count = 0;
    Uint32 histogram_count = 0;
    Uint32 scan_group_count = 0;
};

// Initialize an empty sorter for a fixed, nonzero number of pairs.
bool init_radix_sort(SDL_GPUDevice* device, const RadixSortShaders& shaders, Uint32 pair_count, RadixSort& sorter);
// The caller fills pair_buffers[0] before each sort; returns the final pair buffer or nullptr on failure.
// Records GPU work only: indices are arbitrary payloads and no data is read back to the CPU.
SDL_GPUBuffer* sort_pairs(const RadixSort& sorter, SDL_GPUCommandBuffer* commands);
void destroy_radix_sort(SDL_GPUDevice* device, RadixSort& sorter);
