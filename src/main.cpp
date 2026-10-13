#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <cstddef>
#include <string>

struct Vertex {
    float position[2];
    float color[4];
};

static SDL_GPUShader* load_shader(SDL_GPUDevice* device, const char* filename, SDL_GPUShaderStage stage)
{
    // Load the compiled shader from beside main.exe, regardless of the working directory.
    const char* base_path = SDL_GetBasePath();
    if (!base_path) {
        return nullptr;
    }
    std::string path = std::string(base_path) + filename;
    size_t size = 0;
    void* code = SDL_LoadFile(path.c_str(), &size);
    if (!code) {
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info = {};
    info.code = static_cast<const Uint8*>(code);
    info.code_size = size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXBC;
    info.stage = stage;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    SDL_free(code);
    return shader;
}

static SDL_GPUGraphicsPipeline* create_triangle_pipeline(SDL_GPUDevice* device, SDL_Window* window)
{
    SDL_GPUShader* vertex_shader = load_shader(device, "triangle.vert.dxbc", SDL_GPU_SHADERSTAGE_VERTEX);
    if (!vertex_shader) {
        return nullptr;
    }
    SDL_GPUShader* fragment_shader = load_shader(device, "triangle.frag.dxbc", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!fragment_shader) {
        SDL_ReleaseGPUShader(device, vertex_shader);
        return nullptr;
    }

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
    color_target.format = SDL_GetGPUSwapchainTextureFormat(device, window);

    // A pipeline combines the shaders, vertex layout, and drawing settings.
    SDL_GPUGraphicsPipelineCreateInfo info = {};
    info.vertex_shader = vertex_shader;
    info.fragment_shader = fragment_shader;
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

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    SDL_ReleaseGPUShader(device, vertex_shader);
    SDL_ReleaseGPUShader(device, fragment_shader);
    return pipeline;
}

static SDL_GPUBuffer* create_triangle_buffer(SDL_GPUDevice* device)
{
    const Uint32 size = 3 * sizeof(Vertex);
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

    // Positions now use normalized coordinates: the center is (0, 0), and the edges are -1 and +1.
    vertices[0] = {{0.0f, 2.0f / 3.0f}, {1.0f, 0.0f, 0.0f, 1.0f}};
    vertices[1] = {{-0.5f, -2.0f / 3.0f}, {0.0f, 1.0f, 0.0f, 1.0f}};
    vertices[2] = {{0.5f, -2.0f / 3.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};
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

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Hello, SDL3!", 800, 600, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXBC, false, "direct3d12");
    if (!device) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // Claiming the window lets this device present images to it, with VSync enabled by default.
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GPUGraphicsPipeline* pipeline = create_triangle_pipeline(device, window);
    SDL_GPUBuffer* vertex_buffer = pipeline ? create_triangle_buffer(device) : nullptr;
    bool running = pipeline && vertex_buffer;
    int exit_code = running ? 0 : 1;
    if (!running) {
        SDL_Log("Triangle setup failed: %s", SDL_GetError());
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        if (!running) {
            break;
        }

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
        if (!commands) {
            SDL_Log("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            exit_code = 1;
            break;
        }

        // The swapchain supplies the image that will be displayed in the window.
        SDL_GPUTexture* swapchain_texture = nullptr;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain_texture, nullptr, nullptr)) {
            SDL_Log("SDL_WaitAndAcquireGPUSwapchainTexture failed: %s", SDL_GetError());
            SDL_CancelGPUCommandBuffer(commands);
            exit_code = 1;
            break;
        }
        if (!swapchain_texture) {
            // A minimized window may have no image available to draw into.
            SDL_CancelGPUCommandBuffer(commands);
            SDL_Delay(10);
            continue;
        }

        SDL_GPUColorTargetInfo target = {};
        target.texture = swapchain_texture;
        target.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;

        // Record drawing commands that reuse the vertex buffer uploaded during startup.
        SDL_GPURenderPass* render_pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        SDL_BindGPUGraphicsPipeline(render_pass, pipeline);
        SDL_GPUBufferBinding binding = {vertex_buffer, 0};
        SDL_BindGPUVertexBuffers(render_pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(render_pass, 3, 1, 0, 0);
        SDL_EndGPURenderPass(render_pass);

        // Submission executes the commands and presents the acquired swapchain image.
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            SDL_Log("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
            exit_code = 1;
            break;
        }
    }

    SDL_WaitForGPUIdle(device);
    if (vertex_buffer) {
        SDL_ReleaseGPUBuffer(device, vertex_buffer);
    }
    if (pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
    }
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
