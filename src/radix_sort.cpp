#include "radix_sort.h"
#include "shader.h"

#include <utility>

// Matches RadixUniform in radix_sort.comp.hlsl: four uints fill one 16-byte uniform row.
struct RadixUniform {
    Uint32 pair_count;
    Uint32 group_count;
    Uint32 bit_shift;
    Uint32 histogram_count;
};

static SDL_GPUComputePipeline* create_sort_pipeline(SDL_GPUDevice* device, const ComputeShader& shader, const char* entrypoint, Uint32 reads, Uint32 writes)
{
    SDL_GPUComputePipelineCreateInfo info = {};
    info.code = static_cast<const Uint8*>(shader.code);
    info.code_size = shader.code_size;
    info.entrypoint = entrypoint;
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
    info.num_readonly_storage_buffers = reads;
    info.num_readwrite_storage_buffers = writes;
    info.num_uniform_buffers = 1;
    info.threadcount_x = RADIX_SORT_THREADS;
    info.threadcount_y = 1;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

static SDL_GPUBuffer* create_sort_buffer(SDL_GPUDevice* device, Uint32 size, bool graphics_read = false)
{
    SDL_GPUBufferCreateInfo info = {};
    info.size = size;
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    if (graphics_read) {
        info.usage |= SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    }
    return SDL_CreateGPUBuffer(device, &info);
}

bool init_radix_sort(SDL_GPUDevice* device, const RadixSortShaders& shaders, Uint32 pair_count, RadixSort& sorter)
{
    // This limit keeps dispatches within Direct3D's X dimension and every buffer size within Uint32.
    if (pair_count == 0 || pair_count > RADIX_SORT_MAX_PAIRS) {
        return SDL_SetError("Radix sort expects between 1 and %u pairs.", RADIX_SORT_MAX_PAIRS);
    }
    sorter.pair_count = pair_count;
    sorter.group_count = (pair_count + RADIX_SORT_THREADS - 1) / RADIX_SORT_THREADS;
    sorter.histogram_count = RADIX_SORT_BUCKETS * sorter.group_count;
    sorter.scan_group_count = (sorter.histogram_count + RADIX_SORT_THREADS - 1) / RADIX_SORT_THREADS;

    sorter.histogram_pipeline = create_sort_pipeline(device, shaders.histogram, "histogram", 1, 1);
    if (!sorter.histogram_pipeline) {
        destroy_radix_sort(device, sorter);
        return false;
    }
    sorter.scan_blocks_pipeline = create_sort_pipeline(device, shaders.scan_blocks, "scan_blocks", 1, 2);
    if (!sorter.scan_blocks_pipeline) {
        destroy_radix_sort(device, sorter);
        return false;
    }
    sorter.scan_sums_pipeline = create_sort_pipeline(device, shaders.scan_sums, "scan_sums", 1, 1);
    if (!sorter.scan_sums_pipeline) {
        destroy_radix_sort(device, sorter);
        return false;
    }
    sorter.scatter_pipeline = create_sort_pipeline(device, shaders.scatter, "scatter", 3, 1);
    if (!sorter.scatter_pipeline) {
        destroy_radix_sort(device, sorter);
        return false;
    }

    const Uint32 pair_bytes = static_cast<Uint32>(pair_count * sizeof(SortPair));
    const Uint32 histogram_bytes = static_cast<Uint32>(sorter.histogram_count * sizeof(Uint32));
    const Uint32 block_bytes = static_cast<Uint32>(sorter.scan_group_count * sizeof(Uint32));
    for (SDL_GPUBuffer*& buffer : sorter.pair_buffers) {
        buffer = create_sort_buffer(device, pair_bytes, true);
        if (!buffer) {
            destroy_radix_sort(device, sorter);
            return false;
        }
    }
    sorter.histogram_buffer = create_sort_buffer(device, histogram_bytes);
    sorter.offset_buffer = create_sort_buffer(device, histogram_bytes);
    sorter.block_sum_buffer = create_sort_buffer(device, block_bytes);
    sorter.block_offset_buffer = create_sort_buffer(device, block_bytes);
    if (!sorter.histogram_buffer || !sorter.offset_buffer || !sorter.block_sum_buffer || !sorter.block_offset_buffer) {
        destroy_radix_sort(device, sorter);
        return false;
    }
    return true;
}

static bool dispatch_sort_stage(SDL_GPUCommandBuffer* commands, SDL_GPUComputePipeline* pipeline, SDL_GPUBuffer* const* inputs, Uint32 input_count, SDL_GPUBuffer* const* outputs, Uint32 output_count, Uint32 groups, bool cycle)
{
    SDL_GPUStorageBufferReadWriteBinding bindings[2] = {};
    for (Uint32 i = 0; i < output_count; ++i) {
        bindings[i].buffer = outputs[i];
        bindings[i].cycle = cycle;
    }
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, output_count);
    if (!pass) {
        return false;
    }
    SDL_BindGPUComputePipeline(pass, pipeline);
    SDL_BindGPUComputeStorageBuffers(pass, 0, inputs, input_count);
    SDL_DispatchGPUCompute(pass, groups, 1, 1);
    // Pass boundaries make each stage's writes visible to the next stage; dispatches alone do not synchronize.
    // https://wiki.libsdl.org/SDL3/SDL_DispatchGPUCompute
    SDL_EndGPUComputePass(pass);
    return true;
}

SDL_GPUBuffer* sort_pairs(const RadixSort& sorter, SDL_GPUCommandBuffer* commands)
{
    SDL_GPUBuffer* input = sorter.pair_buffers[0];
    SDL_GPUBuffer* output = sorter.pair_buffers[1];
    for (Uint32 shift = 0; shift < 32; shift += RADIX_SORT_BITS) {
        RadixUniform uniform = {sorter.pair_count, sorter.group_count, shift, sorter.histogram_count};
        SDL_PushGPUComputeUniformData(commands, 0, &uniform, sizeof(uniform));

        // Cycle scratch buffers only on their first write this frame; subsequent passes reuse the same allocations.
        const bool first_pass = shift == 0;
        if (!dispatch_sort_stage(commands, sorter.histogram_pipeline, &input, 1, &sorter.histogram_buffer, 1, sorter.group_count, first_pass)) {
            return nullptr;
        }
        SDL_GPUBuffer* scan_outputs[] = {sorter.offset_buffer, sorter.block_sum_buffer};
        if (!dispatch_sort_stage(commands, sorter.scan_blocks_pipeline, &sorter.histogram_buffer, 1, scan_outputs, 2, sorter.scan_group_count, first_pass)) {
            return nullptr;
        }
        if (!dispatch_sort_stage(commands, sorter.scan_sums_pipeline, &sorter.block_sum_buffer, 1, &sorter.block_offset_buffer, 1, 1, first_pass)) {
            return nullptr;
        }
        SDL_GPUBuffer* scatter_inputs[] = {input, sorter.offset_buffer, sorter.block_offset_buffer};
        // The caller already cycled buffer 0 while filling it; the first scatter cycles buffer 1.
        if (!dispatch_sort_stage(commands, sorter.scatter_pipeline, scatter_inputs, 3, &output, 1, sorter.group_count, first_pass)) {
            return nullptr;
        }
        std::swap(input, output);
    }
    return input;
}

void destroy_radix_sort(SDL_GPUDevice* device, RadixSort& sorter)
{
    SDL_GPUBuffer* buffers[] = {sorter.pair_buffers[0], sorter.pair_buffers[1], sorter.histogram_buffer, sorter.offset_buffer, sorter.block_sum_buffer, sorter.block_offset_buffer};
    for (SDL_GPUBuffer* buffer : buffers) {
        if (buffer) {
            SDL_ReleaseGPUBuffer(device, buffer);
        }
    }
    SDL_GPUComputePipeline* pipelines[] = {sorter.histogram_pipeline, sorter.scan_blocks_pipeline, sorter.scan_sums_pipeline, sorter.scatter_pipeline};
    for (SDL_GPUComputePipeline* pipeline : pipelines) {
        if (pipeline) {
            SDL_ReleaseGPUComputePipeline(device, pipeline);
        }
    }
    sorter = {};
}
