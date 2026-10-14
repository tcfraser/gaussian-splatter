#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "camera.h"
#include "demo.h"
#include "shader.h"
#include "splat_renderer.h"

int main(int argc, char** argv)
{
    if (argc != 2) {
        SDL_Log("Usage: main.exe <path-to-file.splat>");
        return 1;
    }

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

    // The demo prepares the requested file and owns the resulting renderer resources.
    Demo demo = {};
    if (running) {
        SDL_GPUTextureFormat target_format = SDL_GetGPUSwapchainTextureFormat(device, window);
        running = load_demo(device, target_format, shaders, argv[1], demo);
        if (!running) {
            SDL_Log("Demo loading failed: %s", SDL_GetError());
        }
    }

    int exit_code = running ? 0 : 1;

    // Main owns timing; timestamps are sampled after swapchain waits to include their elapsed time.
    Timer timer = {};
    start_timer(timer, SDL_GetTicksNS());

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

        // Update once per drawable frame; real elapsed time also includes any interval spent minimized.
        update_timer(timer, SDL_GetTicksNS());
        update_camera_projection(demo.camera, width, height);
        update_demo(demo, timer);

        // Let the renderer upload the camera and record its rendering into this frame's texture.
        if (!render_splats(demo.renderer, commands, swapchain_texture, demo.camera)) {
            SDL_Log("Rendering failed: %s", SDL_GetError());
            exit_code = 1;
            running = false;
        }

        // Submission executes the commands and presents the acquired swapchain image.
        // An acquired swapchain image must be submitted even if recording failed; SDL forbids cancellation here.
        if (!SDL_SubmitGPUCommandBuffer(commands)) {
            SDL_Log("SDL_SubmitGPUCommandBuffer failed: %s", SDL_GetError());
            exit_code = 1;
            break;
        }
    }

    SDL_WaitForGPUIdle(device);
    // Release all content resources while the device that owns them is still alive.
    unload_demo(device, demo);
    destroy_shaders(device, shaders);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exit_code;
}
