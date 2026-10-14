#include "triangle_demo.h"
#include "shader.h"

#include <cmath>
#include <cstddef>
#include <random>

constexpr Uint32 NUM_TRIANGLES = 1000;
constexpr Uint32 NUM_VERTICES = 3 * NUM_TRIANGLES;
constexpr float TRIANGLE_SCALE = 0.05f;

struct Vertex {
    float position[2];
    float color[4];
};

static SDL_GPUGraphicsPipeline* create_triangle_pipeline(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders)
{
    // Describe how the GPU should read one Vertex from the buffer.
    SDL_GPUVertexBufferDescription buffer_description = {};
    buffer_description.slot = 0;
    buffer_description.pitch = sizeof(Vertex);
    buffer_description.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[] = {
        {0, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, offsetof(Vertex, position)},
        {1, 0, SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, offsetof(Vertex, color)},
    };

    SDL_GPUColorTargetDescription color_target = {};
    color_target.format = target_format;

    // A pipeline combines the shaders, vertex layout, and drawing settings.
    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = shaders.triangle_vertex;
    info.fragment_shader = shaders.triangle_fragment;
    info.vertex_input_state.vertex_buffer_descriptions = &buffer_description;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 2;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &color_target;
    info.target_info.num_color_targets = 1;

    return SDL_CreateGPUGraphicsPipeline(device, &info);
}

static SDL_GPUBuffer* create_triangle_buffer(SDL_GPUDevice* device)
{
    const Uint32 size = NUM_VERTICES * sizeof(Vertex);
    SDL_GPUBufferCreateInfo buffer_info = {};
    buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
    buffer_info.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(device, &buffer_info);
    if (!buffer) {
        return nullptr;
    }

    // The CPU writes into a temporary transfer buffer, which the GPU copies into its vertex buffer.
    SDL_GPUTransferBufferCreateInfo transfer_info = {};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
    if (!transfer) {
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    Vertex* vertices = static_cast<Vertex*>(SDL_MapGPUTransferBuffer(device, transfer, false));
    if (!vertices) {
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }

    const Vertex triangle[] = {
        {{0.0f, 2.0f / 3.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{-0.5f, -2.0f / 3.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{0.5f, -2.0f / 3.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };

    // Seed once per startup, then choose one offset and angle for each triangle.
    std::mt19937 random_engine{std::random_device{}()};
    std::uniform_real_distribution<float> random_offset(-1.0f, 1.0f);
    std::uniform_real_distribution<float> random_angle(0.0f, 2.0f * 3.14159265f);
    for (Uint32 i = 0; i < NUM_TRIANGLES; ++i) {
        const float offset_x = random_offset(random_engine);
        const float offset_y = random_offset(random_engine);
        const float angle = random_angle(random_engine);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);

        for (Uint32 j = 0; j < 3; ++j) {
            Vertex vertex = triangle[j];
            const float x = vertex.position[0] * TRIANGLE_SCALE;
            const float y = vertex.position[1] * TRIANGLE_SCALE;
            // Rotate around the triangle's local origin, then translate; keep the vertex color unchanged.
            vertex.position[0] = cosine * x - sine * y + offset_x;
            vertex.position[1] = sine * x + cosine * y + offset_y;
            vertices[3 * i + j] = vertex;
        }
    }
    SDL_UnmapGPUTransferBuffer(device, transfer);

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

    // SDL keeps the transfer buffer alive until the GPU finishes using it.
    SDL_ReleaseGPUTransferBuffer(device, transfer);
    if (!submitted) {
        SDL_ReleaseGPUBuffer(device, buffer);
        return nullptr;
    }
    return buffer;
}

bool init_triangle_demo(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, TriangleDemo& demo)
{
    demo.pipeline = create_triangle_pipeline(device, target_format, shaders);
    if (!demo.pipeline) {
        return false;
    }
    demo.vertex_buffer = create_triangle_buffer(device);
    if (!demo.vertex_buffer) {
        destroy_triangle_demo(device, demo);
        return false;
    }
    return true;
}

void render_triangle_demo(const TriangleDemo& demo, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target_texture)
{
    SDL_GPUColorTargetInfo target = {};
    target.texture = target_texture;
    target.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    target.load_op = SDL_GPU_LOADOP_CLEAR;
    target.store_op = SDL_GPU_STOREOP_STORE;

    // Record drawing commands that reuse the vertex buffer uploaded during startup.
    SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(render_pass, demo.pipeline);
    SDL_GPUBufferBinding binding = {demo.vertex_buffer, 0};
    SDL_BindGPUVertexBuffers(render_pass, 0, &binding, 1);
    SDL_DrawGPUPrimitives(render_pass, NUM_VERTICES, 1, 0, 0);
    SDL_EndGPURenderPass(render_pass);
}

void destroy_triangle_demo(SDL_GPUDevice* device, TriangleDemo& demo)
{
    if (demo.vertex_buffer) {
        SDL_ReleaseGPUBuffer(device, demo.vertex_buffer);
    }
    if (demo.pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(device, demo.pipeline);
    }
    demo = {};
}
