#include "splat_renderer.h"
#include "splat_format.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

constexpr Uint32 PROJECTION_THREADS = 64; // Matches numthreads in project_splat.comp.hlsl.
// This renderer dispatches along X only; Direct3D 12 allows at most 65,535 groups along one axis.
constexpr Uint32 MAX_SPLATS = 65535 * PROJECTION_THREADS;
constexpr float PI = 3.14159265f;

// Matches the 16-byte Projection uniform buffer in the compute and vertex shaders.
struct Projection {
    float viewport_size[2];
    float focal_length;
    float near_plane;
};

// Written by the compute shader and read as instance attributes by the vertex shader; all positions are in pixels.
struct ProjectedSplat {
    float color_opacity[4];
    float inverse_covariance[3];
    float center[2];
    float half_extent[2];
};

static SDL_GPUComputePipeline* create_projection_pipeline(SDL_GPUDevice* device, const Shaders& shaders)
{
    SDL_GPUComputePipelineCreateInfo info = {};
    info.code = static_cast<const Uint8*>(shaders.project_splat_compute.code);
    info.code_size = shaders.project_splat_compute.code_size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
    info.num_readonly_storage_buffers = 1;
    info.num_readwrite_storage_buffers = 1;
    info.num_uniform_buffers = 1;
    info.threadcount_x = PROJECTION_THREADS;
    info.threadcount_y = 1;
    info.threadcount_z = 1;
    return SDL_CreateGPUComputePipeline(device, &info);
}

static SDL_GPUGraphicsPipeline* create_splat_pipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders)
{
    // Each instance reads one ProjectedSplat; SV_VertexID selects the four quad corners in the shader.
    SDL_GPUVertexBufferDescription buffer_description = {};
    buffer_description.slot = 0;
    buffer_description.pitch = sizeof(ProjectedSplat);
    buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    SDL_GPUVertexAttribute attributes[] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(ProjectedSplat, color_opacity)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(ProjectedSplat, inverse_covariance)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(ProjectedSplat, center)},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(ProjectedSplat, half_extent)},
    };

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
    info.vertex_input_state.vertex_buffer_descriptions = &buffer_description;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 4;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    // Transparent splats are drawn in their original order, with no depth testing or depth writes.
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
    // The dispatch limit also keeps the 40-byte input and 44-byte output buffer sizes within Uint32.
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

    // Compute writes this buffer, then the graphics pipeline reads it directly as instance data.
    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = static_cast<Uint32>(splats.size() * sizeof(ProjectedSplat));
    renderer.projected_splat_buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!renderer.projected_splat_buffer) {
        destroy_splat_renderer(device, renderer);
        return false;
    }
    return true;
}

void render_splats(const SplatRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target_texture, Uint32 width, Uint32 height)
{
    // A 60-degree vertical field of view; using the actual texture height gives focal length in pixels.
    Projection projection = {};
    projection.viewport_size[0] = static_cast<float>(width);
    projection.viewport_size[1] = static_cast<float>(height);
    projection.focal_length = 0.5f * static_cast<float>(height) / std::tan(PI / 6.0f);
    projection.near_plane = 0.1f;

    SDL_PushGPUComputeUniformData(commands, 0, &projection, sizeof(projection));
    SDL_GPUStorageBufferReadWriteBinding output_binding = {};
    output_binding.buffer = renderer.projected_splat_buffer;
    // Every result is overwritten; cycling lets SDL use another allocation if a previous frame still reads this one.
    output_binding.cycle = true;
    SDL_GPUComputePass* compute_pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &output_binding, 1);
    SDL_BindGPUComputePipeline(compute_pass, renderer.projection_pipeline);
    SDL_BindGPUComputeStorageBuffers(compute_pass, 0, &renderer.splat_buffer, 1);
    // Dispatch whole groups of 64 threads; the shader skips any thread past the last splat.
    SDL_DispatchGPUCompute(compute_pass, (renderer.splat_count + PROJECTION_THREADS - 1) / PROJECTION_THREADS, 1, 1);
    // End the compute pass before rendering so SDL can make its writes visible to the vertex input stage.
    SDL_EndGPUComputePass(compute_pass);

    SDL_PushGPUVertexUniformData(commands, 0, &projection, sizeof(projection));

    SDL_GPUColorTargetInfo target = {};
    target.texture = target_texture;
    target.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(render_pass, renderer.pipeline);
    SDL_GPUBufferBinding binding = {renderer.projected_splat_buffer, 0};
    SDL_BindGPUVertexBuffers(render_pass, 0, &binding, 1);
    // Four vertices per splat form two triangles in a strip; each instance starts a new strip.
    SDL_DrawGPUPrimitives(render_pass, 4, renderer.splat_count, 0, 0);
    SDL_EndGPURenderPass(render_pass);
}

void destroy_splat_renderer(SDL_GPUDevice* device, SplatRenderer& renderer)
{
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
