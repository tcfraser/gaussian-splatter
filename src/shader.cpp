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

static SDL_GPUShader* load_shader(SDL_GPUDevice* device, const char* filename, SDL_GPUShaderStage stage, Uint32 num_uniform_buffers = 0, Uint32 num_storage_buffers = 0)
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
    info.num_storage_buffers = num_storage_buffers;
    SDL_GPUShader* shader = SDL_CreateGPUShader(device, &info);
    SDL_free(code);
    return shader;
}

static bool load_compute_shader(const char* filename, ComputeShader& shader)
{
    shader.code = load_shader_code(filename, &shader.code_size);
    return shader.code != nullptr;
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
    if (!load_compute_shader("radix_histogram.comp.dxil", shaders.radix_sort.histogram)
        || !load_compute_shader("radix_scan_blocks.comp.dxil", shaders.radix_sort.scan_blocks)
        || !load_compute_shader("radix_scan_sums.comp.dxil", shaders.radix_sort.scan_sums)
        || !load_compute_shader("radix_scatter.comp.dxil", shaders.radix_sort.scatter)) {
        destroy_shaders(device, shaders);
        return false;
    }
    shaders.splat_quad_vertex = load_shader(device, "splat_quad.vert.dxil", SDL_GPU_SHADERSTAGE_VERTEX, 1, 2);
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
    SDL_free(shaders.radix_sort.histogram.code);
    SDL_free(shaders.radix_sort.scan_blocks.code);
    SDL_free(shaders.radix_sort.scan_sums.code);
    SDL_free(shaders.radix_sort.scatter.code);
    if (shaders.triangle_fragment) {
        SDL_ReleaseGPUShader(device, shaders.triangle_fragment);
    }
    if (shaders.triangle_vertex) {
        SDL_ReleaseGPUShader(device, shaders.triangle_vertex);
    }
    shaders = {};
}
