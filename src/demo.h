#pragma once

#include <SDL3/SDL.h>

#include "camera.h"
#include "shader.h"
#include "splat_renderer.h"

// Owns the active demo's camera and renderer resources; main.cpp owns the GPU device.
struct Demo {
    Camera camera = {};
    SplatRenderer renderer = {};
};

// Load into an empty demo; CPU scene data is released after the renderer copies it for upload.
bool load_demo(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, Demo& demo);
void update_demo(Demo& demo, const Timer& timer);
// Safe for an empty or partially loaded demo; call before destroying the GPU device.
void unload_demo(SDL_GPUDevice* device, Demo& demo);
