#pragma once

#include <SDL3/SDL.h>
#include "shader.h"

struct GaussianSplat {
    float position[3];
    float color[3];
    float opacity[1];
    // The symmetric 3D covariance matrix is packed as xx, xy, xz, yy, yz, zz.
    float covariance[6];
};

// Owns the splat data and pipeline; main.cpp owns the device and frame textures.
struct GaussianSplatDemo {
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUBuffer* splat_buffer = nullptr;
};

bool init_gaussian_splat(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, GaussianSplatDemo& demo);
void render_gaussian_splat(const GaussianSplatDemo& demo, SDL_GPUCommandBuffer* commands, SDL_GPUTexture* target_texture, Uint32 width, Uint32 height);
void destroy_gaussian_splat(SDL_GPUDevice* device, GaussianSplatDemo& demo);
