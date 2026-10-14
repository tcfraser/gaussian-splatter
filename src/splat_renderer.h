#pragma once

#include <SDL3/SDL.h>
#include "camera.h"
#include "radix_sort.h"
#include "shader.h"
#include "splat_format.h"

#include <vector>

// Owns splat storage, projection/draw pipelines, and the generic sorter; main.cpp owns the device and frame textures.
struct SplatRenderer {
    SDL_GPUComputePipeline* projection_pipeline = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* splat_buffer = nullptr;
    SDL_GPUBuffer* projected_splat_buffer = nullptr;
    RadixSort sorter;
    Uint32 splat_count = 0;
};

// Initialize an empty renderer; borrows splats only during initialization and copies them to GPU storage.
// Positions and covariance are stored in world coordinates; the compute shader applies the current camera.
bool init_splat_renderer(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, const std::vector<CovarianceSplat>& splats, SplatRenderer& renderer);
bool render_splats(const SplatRenderer& renderer, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target_texture, const Camera& camera);
void destroy_splat_renderer(SDL_GPUDevice* device, SplatRenderer& renderer);
