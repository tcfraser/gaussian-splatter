#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "shader.h"
#include "splat_format.h"
#include "splat_format_handler.h"
#include "splat_renderer.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

static bool load_plant(std::vector<CovarianceSplat>& splats)
{
    // Resolve the example relative to build/main.exe, independently of the working directory.
    const char* base_path = SDL_GetBasePath();
    if (!base_path) {
        return false;
    }
    const std::string path = std::string(base_path) + "../examples/plant/plant.splat";
    std::string error;
    std::vector<QuaternionicSplat> quaternionic_splats;
    if (!load_splat_file(path.c_str(), quaternionic_splats, error)) {
        return SDL_SetError("%s", error.c_str());
    }
    if (!convert_splats(quaternionic_splats, splats, error)) {
        return SDL_SetError("%s", error.c_str());
    }
    SDL_Log("Loaded plant: %zu splats, %zu bytes per file record, %zu bytes per GPU input record.",
            splats.size(), sizeof(QuaternionicSplat), sizeof(CovarianceSplat));
    // The source vector is released here; splats belongs to the caller.
    return true;
}

static void place_plant_for_fixed_camera(std::vector<CovarianceSplat>& splats)
{
    // Loading rejects empty scenes; the plant's metadata gives its longest extent as one unit.
    float minimum[3];
    float maximum[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        minimum[axis] = maximum[axis] = splats.front().position[axis];
    }
    for (const CovarianceSplat& splat : splats) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], splat.position[axis]);
            maximum[axis] = std::max(maximum[axis], splat.position[axis]);
        }
    }
    const float center[3] = {
        0.5f * minimum[0] + 0.5f * maximum[0],
        0.5f * minimum[1] + 0.5f * maximum[1],
        0.5f * minimum[2] + 0.5f * maximum[2],
    };
    for (CovarianceSplat& splat : splats) {
        // A half turn around X maps the plant's -Y up to +Y up; place its center at Z = 2 in front of the camera.
        splat.position[0] -= center[0];
        splat.position[1] = -(splat.position[1] - center[1]);
        splat.position[2] = 2.0f - (splat.position[2] - center[2]);
        // Sigma' = A * Sigma * transpose(A), with A = diag(1, -1, -1); only xy and xz change sign.
        splat.covariance[1] = -splat.covariance[1];
        splat.covariance[2] = -splat.covariance[2];
    }
}

int main(int, char**)
{
    // Attempt SDL initialization
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    // Attempt window creation
    SDL_Window* window = SDL_CreateWindow("Gaussian Splat Renderer", 800, 600, 0);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    // Attempt setting up GPU device with Direct3D 12 API
    SDL_GPUDevice* device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL, false, "direct3d12");
    if (!device) {
        SDL_Log("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    // Let the GPU device handle the created window.
    if (!SDL_ClaimWindowForGPUDevice(device, window)) {
        SDL_Log("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Attempt shader loading
    Shaders shaders = {};
    bool running = load_shaders(device, shaders);
    if (!running) {
        SDL_Log("Shader loading failed: %s", SDL_GetError());
    }

    // CPU vectors own the scene during loading and preprocessing; the renderer copies the result into its GPU buffer.
    SplatRenderer renderer = {};
    if (running) {
        std::vector<CovarianceSplat> splats;
        running = load_plant(splats);
        if (running) {
            place_plant_for_fixed_camera(splats);
            SDL_GPUTextureFormat target_format = SDL_GetGPUSwapchainTextureFormat(device, window);
            running = init_splat_renderer(device, target_format, shaders, splats, renderer);
        }
        if (!running) {
            SDL_Log("Plant setup failed: %s", SDL_GetError());
        }
        // On success the transfer buffer already has a copy, so the CPU vector can be released before the render loop.
    }

    int exit_code = running ? 0 : 1;

    while (running) {
        // Handle closing the application from user input
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        // Actually leave the application loop
        if (!running) {
            break;
        }

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
        if (!commands) {
            SDL_Log("SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            exit_code = 1;
            break;
        }

        // The swapchain supplies the texture that will be displayed in the window.
        SDL_GPUTexture* swapchain_texture = nullptr;
        Uint32 width = 0;
        Uint32 height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain_texture, &width, &height)) {
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

        // Let the renderer record its rendering into this frame's texture.
        render_splats(renderer, commands, swapchain_texture, width, height);

        // Submission executes the commands and presents the acquired swapchain image.
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            SDL_Log("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
            exit_code = 1;
            break;
        }
    }

    SDL_WaitForGPUIdle(device);
    // Release all content resources while the device that owns them is still alive.
    destroy_splat_renderer(device, renderer);
    destroy_shaders(device, shaders);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
