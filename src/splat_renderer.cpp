#include "splat_renderer.h"
#include "camera.h"
#include "radix_sort.h"
#include "splat_format.h"

#include <cstring>
#include <vector>

constexpr Uint32 PROJECTION_THREADS = 64; // Matches numthreads in project_splat.comp.hlsl.
// This renderer dispatches along X only; Direct3D 12 allows at most 65,535 groups along one axis.
constexpr Uint32 MAX_SPLATS = 65535 * PROJECTION_THREADS;

// Matches shaders/projected_splat.hlsli; projection writes these records and the vertex shader reads them by sorted index.
struct ProjectedSplat {
    float color_opacity[4];
    float inverse_covariance[3];
    float center[2];
    float half_extent[2];
    Uint32 depth_key;
};

static SDL_GPUComputePipeline* create_projection_pipeline(SDL_GPUDevice* device, const Shaders& shaders)
{
    SDL_GPUComputePipelineCreateInfo info = {};
    info.code = static_cast<const Uint8*>(shaders.project_splat_compute.code);
    info.code_size = shaders.project_splat_compute.code_size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
    info.num_readonly_storage_buffers = 1;
    info.num_readwrite_storage_buffers = 2;
    info.num_uniform_buffers = 1;
    info.threadcount_x = PROJECTION_THREADS;
    info.threadcount_y = 1;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

static SDL_GPUGraphicsPipeline* create_splat_pipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders)
{
    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = target_format;
    // Straight-alpha blending: result = source_color * source_alpha + destination_color * (1 - source_alpha).
    color_target.blend_state.enable_blend = true;
    color_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    color_target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    color_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    color_target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    color_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = shaders.splat_quad_vertex;
    info.fragment_shader = shaders.draw_splat_fragment;
    // SV_InstanceID selects the sorted index in storage; SV_VertexID generates the four corners without vertex attributes.
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    // Back-to-front alpha blending uses draw order, with no depth testing or depth writes.
    info.depth_stencil_state.enable_depth_test = false;
    info.depth_stencil_state.enable_depth_write = false;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;
    return SDL_CreateGPUGraphicsPipeline(device, &info);
}

static SDL_GPUBuffer* create_splat_buffer(SDL_GPUDevice* device, const std::vector<CovarianceSplat>& splats)
{
    // init_splat_renderer checks the count before either GPU buffer size is narrowed to Uint32.
    const Uint32 size = static_cast<Uint32>(splats.size() * sizeof(CovarianceSplat));
    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    buffer_info.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!buffer) {
        return nullptr;
    }

    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer) {
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
    if (!mapped) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    // CPU preprocessing is complete; the transfer buffer receives the exact CovarianceSplat bytes.
    std::memcpy(mapped, splats.data(), size);
    SDL_UnmapGPUTransferBuffer(device, transfer);

    // The CPU has filled the transfer buffer; this command buffer asks the GPU to copy it into the compute input buffer.
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (!commands) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
    if (!copy_pass) {
        SDL_CancelGPUCommandBuffer(commands);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    SDL_GPUTransferBufferLocation source = {transfer, 0};
    SDL_GPUBufferRegion destination = {buffer, 0, size};
    SDL_UploadToGPUBuffer(copy_pass, &source, &destination, false);
    SDL_EndGPUCopyPass(copy_pass);
    bool submitted = SDL_SubmitGPUCommandBuffer(commands);

    // SDL defers destruction until the GPU has finished using the transfer buffer.
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    if (!submitted) {
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    return buffer;
}

bool init_splat_renderer(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, const std::vector<CovarianceSplat>& splats, SplatRenderer& renderer)
{
    // The dispatch limit also keeps the 40-byte input and 48-byte output buffer sizes within Uint32.
    if (splats.empty() || splats.size() > MAX_SPLATS) {
        return SDL_SetError("Expected between 1 and %u splats for the projection dispatch.", MAX_SPLATS);
    }
    renderer.splat_count = static_cast<Uint32>(splats.size());
    renderer.projection_pipeline = create_projection_pipeline(device, shaders);
    if (!renderer.projection_pipeline) {
        destroy_splat_renderer(device, renderer);
        return false;
    }
    renderer.pipeline = create_splat_pipeline(device, target_format, shaders);
    if (!renderer.pipeline) {
        destroy_splat_renderer(device, renderer);
        return false;
    }
    renderer.splat_buffer = create_splat_buffer(device, splats);
    if (!renderer.splat_buffer) {
        destroy_splat_renderer(device, renderer);
        return false;
    }

    if (!init_radix_sort(device, shaders.radix_sort, renderer.splat_count, renderer.sorter)) {
        destroy_splat_renderer(device, renderer);
        return false;
    }

    // Projection writes these records once per frame; vertex storage reads follow the sorted indices.
    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    buffer_info.size = static_cast<Uint32>(splats.size() * sizeof(ProjectedSplat));
    renderer.projected_splat_buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!renderer.projected_splat_buffer) {
        destroy_splat_renderer(device, renderer);
        return false;
    }
    return true;
}

bool render_splats(const SplatRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target_texture, const Camera& camera)
{
    // Push the current camera every frame; the static splat buffer remains in world coordinates.
    SDL_PushGPUComputeUniformData(commands, 0, &camera, sizeof(camera));
    SDL_GPUStorageBufferReadWriteBinding output_bindings[2] = {};
    output_bindings[0].buffer = renderer.projected_splat_buffer;
    output_bindings[1].buffer = renderer.sorter.pair_buffers[0];
    // Every result is overwritten; cycling lets SDL use another allocation if a previous frame still reads this one.
    output_bindings[0].cycle = true;
    output_bindings[1].cycle = true;
    SDL_GPUComputePass* compute_pass = SDL_BeginGPUComputePass(commands, nullptr, 0, output_bindings, 2);
    if (!compute_pass) {
        return false;
    }
    SDL_BindGPUComputePipeline(compute_pass, renderer.projection_pipeline);
    SDL_BindGPUComputeStorageBuffers(compute_pass, 0, &renderer.splat_buffer, 1);
    // Dispatch whole groups of 64 threads; the shader skips any thread past the last splat.
    SDL_DispatchGPUCompute(compute_pass, (renderer.splat_count + PROJECTION_THREADS - 1) / PROJECTION_THREADS, 1, 1);
    // Finish projection before the sorter reads its keys.
    SDL_EndGPUComputePass(compute_pass);

    SDL_GPUBuffer* render_order = renderer.sorter.pair_buffers[0];
    // Comment out this single call to draw in the original input order instead of sorting by depth.
    render_order = sort_pairs(renderer.sorter, commands);
    if (!render_order) {
        return false;
    }

    SDL_PushGPUVertexUniformData(commands, 0, &camera, sizeof(camera));

    SDL_GPUColorTargetInfo target = {};
    target.texture = target_texture;
    target.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    if (!render_pass) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(render_pass, renderer.pipeline);
    SDL_GPUBuffer* vertex_buffers[] = {renderer.projected_splat_buffer, render_order};
    SDL_BindGPUVertexStorageBuffers(render_pass, 0, vertex_buffers, 2);
    // Four vertices per splat form two triangles in a strip; each instance starts a new strip.
    SDL_DrawGPUPrimitives(render_pass, 4, renderer.splat_count, 0, 0);
    SDL_EndGPURenderPass(render_pass);
    return true;
}

void destroy_splat_renderer(SDL_GPUDevice* device, SplatRenderer& renderer)
{
    destroy_radix_sort(device, renderer.sorter);
    if (renderer.projected_splat_buffer) {
        SDL_ReleaseGPUBuffer(device, renderer.projected_splat_buffer);
    }
    if (renderer.splat_buffer) {
        SDL_ReleaseGPUBuffer(device, renderer.splat_buffer);
    }
    if (renderer.pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(device, renderer.pipeline);
    }
    if (renderer.projection_pipeline) {
        SDL_ReleaseGPUComputePipeline(device, renderer.projection_pipeline);
    }
    renderer = {};
}
