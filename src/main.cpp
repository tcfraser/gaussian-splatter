#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

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

    SDL_Renderer* renderer = SDL_CreateRenderer(window, "direct3d11");
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Match drawing to the display's refresh rate.
    SDL_SetRenderVSync(renderer, 1);

    // Each vertex: pixel position, RGBA color (0 to 1), unused texture coordinates.
    SDL_Vertex vertices[] = {
        {{400.0f, 100.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {}},
        {{200.0f, 500.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {}},
        {{600.0f, 500.0f}, {0.0f, 0.0f, 1.0f, 1.0f}, {}},
    };

    bool running = true;
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

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0);
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
