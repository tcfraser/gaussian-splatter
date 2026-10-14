#pragma once

#include <SDL3/SDL.h>

// SDL creates compute pipelines directly from bytecode instead of an SDL_GPUShader handle.
struct ComputeShader {
    void* code = nullptr;
    size_t code_size = 0;
};

// Owns the graphics shader handles and compute bytecode; demos borrow them when creating pipelines.
struct Shaders {
    SDL_GPUShader* triangle_vertex = nullptr;
    SDL_GPUShader* triangle_fragment = nullptr;
    ComputeShader project_splat_compute;
    SDL_GPUShader* splat_quad_vertex = nullptr;
    SDL_GPUShader* draw_splat_fragment = nullptr;
};

// Call once with an empty Shaders; compiled filenames are listed in shader.cpp.
bool load_shaders(SDL_GPUDevice* device, Shaders& shaders);
void destroy_shaders(SDL_GPUDevice* device, Shaders& shaders);
