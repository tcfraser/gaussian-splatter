#pragma once

#include <SDL3/SDL.h>
#include "shader.h"

// Owns the demo's GPU resources; the window and device belong to main.cpp.
struct TriangleDemo {
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* vertex_buffer = nullptr;
};

// Call once with an empty TriangleDemo; the target format must match the texture used for rendering.
bool init_triangle_demo(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, TriangleDemo& demo);

// Borrows the command buffer and texture for this call; main.cpp submits the recorded commands.
void render_triangle_demo(const TriangleDemo& demo, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target_texture);
void destroy_triangle_demo(SDL_GPUDevice* device, TriangleDemo& demo);
