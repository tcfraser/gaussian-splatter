#include "shader.h"

#include <string>

static void* load_shader_code(const char* filename, size_t* size)
{
    // build.sh compiles the HLSL sources and places the bytecode beside main.exe.
    const char* base_path = SDL_GetBasePath();
    if (!base_path) {
        return nullptr;
    }
    std::string path = std::string(base_path) + filename;
    return SDL_LoadFile(path.c_str(), size);
}

static SDL_GPUShader* load_shader(SDL_GPUDevice* device, const char* filename, SDL_GPUShaderStage stage, Uint32 num_uniform_buffers = 0)
{
    size_t size = 0;
    void* code = load_shader_code(filename, &size);
    if (!code) {
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info = {};
    info.code = static_cast<const Uint8*>(code);
    info.code_size = size;
    info.entrypoint = "main";
    info.format = SDL_GPU_SHADERFORMAT_DXIL;
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
    shaders.triangle_vertex = load_shader(device, "triangle.vert.dxil", SDL_GPU_SHADERSTAGE_VERTEX);
    if (!shaders.triangle_vertex) {
        return false;
    }
    shaders.triangle_fragment = load_shader(device, "triangle.frag.dxil", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!shaders.triangle_fragment) {
        destroy_shaders(device, shaders);
        return false;
    }
    shaders.project_splat_compute.code = load_shader_code("project_splat.comp.dxil", &shaders.project_splat_compute.code_size);
    if (!shaders.project_splat_compute.code) {
        destroy_shaders(device, shaders);
        return false;
    }
    shaders.splat_quad_vertex = load_shader(device, "splat_quad.vert.dxil", SDL_GPU_SHADERSTAGE_VERTEX, 1);
    if (!shaders.splat_quad_vertex) {
        destroy_shaders(device, shaders);
        return false;
    }
    shaders.draw_splat_fragment = load_shader(device, "draw_splat.frag.dxil", SDL_GPU_SHADERSTAGE_FRAGMENT);
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
    if (shaders.splat_quad_vertex) {
        SDL_ReleaseGPUShader(device, shaders.splat_quad_vertex);
    }
    SDL_free(shaders.project_splat_compute.code);
    if (shaders.triangle_fragment) {
        SDL_ReleaseGPUShader(device, shaders.triangle_fragment);
    }
    if (shaders.triangle_vertex) {
        SDL_ReleaseGPUShader(device, shaders.triangle_vertex);
    }
    shaders = {};
}
