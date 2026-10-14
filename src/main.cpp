#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "shader.h"
#include "gaussian_splat.h"

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

    // Attempt demo setup only if shaders properly loaded
    GaussianSplatDemo demo = {};
    if (running) {
        SDL_GPUTextureFormat target_format = SDL_GetGPUSwapchainTextureFormat(device, window);
        running = init_gaussian_splat(device, target_format, shaders, demo);
        if (!running) {
            SDL_Log("Demo setup failed: %s", SDL_GetError());
        }
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

        // Let the demo record its rendering into this frame's texture.
        render_gaussian_splat(demo, commands, swapchain_texture, width, height);

        // Submission executes the commands and presents the acquired swapchain image.
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            SDL_Log("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
            exit_code = 1;
            break;
        }
    }

    SDL_WaitForGPUIdle(device);
    // Release all content resources while the device that owns them is still alive.
    destroy_gaussian_splat(device, demo);
    destroy_shaders(device, shaders);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
