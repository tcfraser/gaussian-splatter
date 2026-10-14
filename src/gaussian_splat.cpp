#include "gaussian_splat.h"

#include <cmath>
#include <cstddef>
#include <random>

constexpr Uint32 NUM_SPLATS = 1000;
constexpr float PI = 3.14159265f;

// Matches the 16-byte Projection uniform buffer in project_splat.vert.hlsl.
struct Projection {
    float viewport_size[2];
    float focal_length;
    float near_plane;
};

static SDL_GPUGraphicsPipeline* create_splat_pipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders)
{
    // Each instance reads one GaussianSplat; SV_VertexID selects the six quad vertices in the shader.
    SDL_GPUVertexBufferDescription buffer_description = {};
    buffer_description.slot = 0;
    buffer_description.pitch = sizeof(GaussianSplat);
    buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;
    SDL_GPUVertexAttribute attributes[] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GaussianSplat, position)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GaussianSplat, color)},
        {2, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT, offsetof(GaussianSplat, opacity)},
        {3, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GaussianSplat, covariance)},
        {4, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, offsetof(GaussianSplat, covariance) + 3 * sizeof(float)},
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
    info.vertex_shader = shaders.project_splat_vertex;
    info.fragment_shader = shaders.draw_splat_fragment;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer_description;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 5;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
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

static SDL_GPUBuffer* create_splat_buffer(SDL_GPUDevice* device)
{
    const Uint32 size = NUM_SPLATS * sizeof(GaussianSplat);
    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
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
    GaussianSplat* splats = static_cast<GaussianSplat*>(SDL_MapGPUTransferBuffer(device, transfer, false));
    if (!splats) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    // The fixed camera sits at the origin, looks along +Z, and has +Y pointing up.
    // Covariance diagonal values are variances: squared standard deviations along the ellipsoid's axes.
    const GaussianSplat base_splat = {
        {0.0f, 0.0f, 4.0f},
        {1.0f, 1.0f, 1.0f},
        {0.7f},
        {0.07f * 0.07f, 0.0f, 0.0f, 0.025f * 0.025f, 0.0f, 0.035f * 0.035f},
    };
    const float variances[] = {base_splat.covariance[0], base_splat.covariance[3], base_splat.covariance[5]};

    std::mt19937 random_engine{std::random_device{}()};
    std::uniform_real_distribution<float> random_offset(-1.0f, 1.0f);
    std::uniform_real_distribution<float> random_color(0.15f, 1.0f);
    std::uniform_real_distribution<float> random_opacity(0.5f, 1.0f);
    std::uniform_real_distribution<float> random_scale(0.5f, 1.5f);
    std::uniform_real_distribution<float> random_angle(0.0f, 2.0f * PI);
    for (Uint32 i = 0; i < NUM_SPLATS; ++i) {
        GaussianSplat splat = base_splat;
        splat.position[0] += 2.5f * random_offset(random_engine);
        splat.position[1] += 1.6f * random_offset(random_engine);
        splat.position[2] += random_offset(random_engine);
        for (Uint32 channel = 0; channel < 3; ++channel) {
            splat.color[channel] *= random_color(random_engine);
        }
        splat.opacity[0] *= random_opacity(random_engine);

        const float angle = random_angle(random_engine);
        const float tilt = random_angle(random_engine);
        const float scale = random_scale(random_engine);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        const float ct = std::cos(tilt);
        const float st = std::sin(tilt);
        const float rotation[3][3] = {
            {c * ct, -s, c * st},
            {s * ct, c, s * st},
            {-st, 0.0f, ct},
        };

        // Sigma = R * diag(variances) * transpose(R); rotating positive variances keeps the covariance valid.
        Uint32 component = 0;
        for (Uint32 row = 0; row < 3; ++row) {
            for (Uint32 column = row; column < 3; ++column) {
                float covariance = 0.0f;
                for (Uint32 axis = 0; axis < 3; ++axis) {
                    covariance += rotation[row][axis] * variances[axis] * rotation[column][axis];
                }
                splat.covariance[component++] = covariance * scale * scale;
            }
        }
        splats[i] = splat;
    }
    SDL_UnmapGPUTransferBuffer(device, transfer);

    // The CPU has filled the transfer buffer; this command buffer asks the GPU to copy it into the instance buffer.
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
    if (!commands) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(commands);
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

bool init_gaussian_splat(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, GaussianSplatDemo& demo)
{
    demo.pipeline = create_splat_pipeline(device, target_format, shaders);
    if (!demo.pipeline) {
        return false;
    }
    demo.splat_buffer = create_splat_buffer(device);
    if (!demo.splat_buffer) {
        destroy_gaussian_splat(device, demo);
        return false;
    }
    return true;
}

void render_gaussian_splat(const GaussianSplatDemo& demo, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target_texture, Uint32 width, Uint32 height)
{
    // A 60-degree vertical field of view; using the actual texture height gives focal length in pixels.
    Projection projection = {};
    projection.viewport_size[0] = static_cast<float>(width);
    projection.viewport_size[1] = static_cast<float>(height);
    projection.focal_length = 0.5f * static_cast<float>(height) / std::tan(PI / 6.0f);
    projection.near_plane = 0.1f;
    SDL_PushGPUVertexUniformData(commands, 0, &projection, sizeof(projection));

    SDL_GPUColorTargetInfo target = {};
    target.texture = target_texture;
    target.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(render_pass, demo.pipeline);
    SDL_GPUBufferBinding binding = {demo.splat_buffer, 0};
    SDL_BindGPUVertexBuffers(render_pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(render_pass, 6, NUM_SPLATS, 0, 0);
    SDL_EndGPURenderPass(render_pass);
}

void destroy_gaussian_splat(SDL_GPUDevice* device, GaussianSplatDemo& demo)
{
    if (demo.splat_buffer) {
        SDL_ReleaseGPUBuffer(device, demo.splat_buffer);
    }
    if (demo.pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(device, demo.pipeline);
    }
    demo = {};
}
