#pragma once

#include <SDL3/SDL.h>
#include "shader.h"
#include "splat_format.h"

#include <vector>

// Owns the input splats, projected results, and both pipelines; main.cpp owns the device and frame textures.
struct SplatRenderer {
    SDL_GPUComputePipeline* projection_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* splat_buffer = nullptr;
    SDL_GPUBuffer* projected_splat_buffer = nullptr;
    Uint32 splat_count = 0;
};

// Initialize an empty renderer; borrows splats only during initialization and copies them to GPU storage.
// Positions and covariance must already be in the fixed camera's coordinates: +Z forward, +Y up.
bool init_splat_renderer(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, const std::vector<CovarianceSplat>& splats, SplatRenderer& renderer);
void render_splats(const SplatRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target_texture, Uint32 width, Uint32 height);
void destroy_splat_renderer(SDL_GPUDevice* device, SplatRenderer& renderer);
