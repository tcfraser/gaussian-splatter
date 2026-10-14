#pragma once

#include <SDL3/SDL.h>

// Owns the shader handles; other modules borrow them when creating pipelines.
struct Shaders {
    SDL_GPUShader* triangle_vertex = nullptr;
    SDL_GPUShader* triangle_fragment = nullptr;
    SDL_GPUShader* project_splat_vertex = nullptr;
    SDL_GPUShader* draw_splat_fragment = nullptr;
};

// Call once with an empty Shaders; compiled filenames are listed in shader.cpp.
bool load_shaders(SDL_GPUDevice* device, Shaders& shaders);
void destroy_shaders(SDL_GPUDevice* device, Shaders& shaders);
