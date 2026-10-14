#include "shader.h"

#include <string>

static SDL_GPUShader* load_shader(SDL_GPUDevice* device, const char* filename, SDL_GPUShaderStage stage, Uint32 num_uniform_buffers = 0)
{
    // build.sh compiles the HLSL sources and places the bytecode beside main.exe.
    const char* base_path = SDL_GetBasePath();
    if (!base_path) {
        return nullptr;
    }
    std::string path = std::string(base_path) + filename;
    size_t size = 0;
    void* code = SDL_LoadFile(path.c_str(), &size);
    if (!code) {
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info = {};
    info.code = static_cast<const Uint8*>(code);
    info.code_size = size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXBC;
    info.stage = stage;
    info.num_uniform_buffers = num_uniform_buffers;
    // These shaders use vertex attributes and optional uniforms; other resource counts remain zero.
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    SDL_free(code);
    return shader;
}

bool load_shaders(SDL_GPUDevice* device, Shaders& shaders)
{
    // Keep the filenames explicit; these match the outputs declared in build.sh.
    shaders.triangle_vertex = load_shader(device, "triangle.vert.dxbc", SDL_GPU_SHADERSTAGE_VERTEX);
    if (!shaders.triangle_vertex) {
        return false;
    }
    shaders.triangle_fragment = load_shader(device, "triangle.frag.dxbc", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!shaders.triangle_fragment) {
        destroy_shaders(device, shaders);
        return false;
    }
    shaders.project_splat_vertex = load_shader(device, "project_splat.vert.dxbc", SDL_GPU_SHADERSTAGE_VERTEX, 1);
    if (!shaders.project_splat_vertex) {
        destroy_shaders(device, shaders);
        return false;
    }
    shaders.draw_splat_fragment = load_shader(device, "draw_splat.frag.dxbc", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!shaders.draw_splat_fragment) {
        destroy_shaders(device, shaders);
        return false;
    }
    return true;
}

void destroy_shaders(SDL_GPUDevice* device, Shaders& shaders)
{
    if (shaders.draw_splat_fragment) {
        SDL_ReleaseGPUShader(device, shaders.draw_splat_fragment);
    }
    if (shaders.project_splat_vertex) {
        SDL_ReleaseGPUShader(device, shaders.project_splat_vertex);
    }
    if (shaders.triangle_fragment) {
        SDL_ReleaseGPUShader(device, shaders.triangle_fragment);
    }
    if (shaders.triangle_vertex) {
        SDL_ReleaseGPUShader(device, shaders.triangle_vertex);
    }
    shaders = {};
}
