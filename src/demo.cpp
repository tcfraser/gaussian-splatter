#include "demo.h"
#include "camera.h"
#include "shader.h"
#include "splat_format.h"
#include "splat_format_handler.h"
#include "splat_renderer.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

constexpr double PI = 3.14159265358979323846;
constexpr double ORBIT_PERIOD_SECONDS = 10.0;
constexpr float ORBIT_RADIUS = 1.0f;

static bool load_plant(std::vector<CovarianceSplat>& splats)
{
    // Resolve the example relative to build/main.exe, independently of the working directory.
    const char* base_path = SDL_GetBasePath();
    if (!base_path) {
        return false;
    }
    const std::string path = std::string(base_path) + "../examples/plant/plant.splat";
    std::string error;
    std::vector<QuaternionicSplat> quaternionic_splats;
    if (!load_splat_file(path.c_str(), quaternionic_splats, error)) {
        return SDL_SetError("%s", error.c_str());
    }
    if (!convert_splats(quaternionic_splats, splats, error)) {
        return SDL_SetError("%s", error.c_str());
    }
    SDL_Log("Loaded plant: %zu splats, %zu bytes per file record, %zu bytes per GPU input record.",
            splats.size(), sizeof(QuaternionicSplat), sizeof(CovarianceSplat));
    // The source vector is released here; splats belongs to the caller.
    return true;
}

static void place_plant_in_world(std::vector<CovarianceSplat>& splats)
{
    // Loading rejects empty scenes; the plant's metadata gives its longest extent as one unit.
    float minimum[3];
    float maximum[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        minimum[axis] = maximum[axis] = splats.front().position[axis];
    }
    for (const CovarianceSplat& splat : splats) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            minimum[axis] = std::min(minimum[axis], splat.position[axis]);
            maximum[axis] = std::max(maximum[axis], splat.position[axis]);
        }
    }
    const float center[3] = {
        0.5f * minimum[0] + 0.5f * maximum[0],
        0.5f * minimum[1] + 0.5f * maximum[1],
        0.5f * minimum[2] + 0.5f * maximum[2],
    };
    for (CovarianceSplat& splat : splats) {
        // Center the plant at the world origin and rotate half a turn around X to make +Y point up.
        splat.position[0] -= center[0];
        splat.position[1] = -(splat.position[1] - center[1]);
        splat.position[2] = -(splat.position[2] - center[2]);
        // Sigma' = A * Sigma * transpose(A), with A = diag(1, -1, -1); only xy and xz change sign.
        splat.covariance[1] = -splat.covariance[1];
        splat.covariance[2] = -splat.covariance[2];
    }
}

bool load_demo(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, Demo& demo)
{
    std::vector<CovarianceSplat> splats;
    if (!load_plant(splats)) {
        return false;
    }
    place_plant_in_world(splats);

    demo.camera = {};
    demo.camera.position[2] = -ORBIT_RADIUS;
    // The renderer copies the scene into its transfer buffer; the CPU vector is released on return.
    return init_splat_renderer(device, target_format, shaders, splats, demo.renderer);
}

void update_demo(Demo& demo, const Timer& timer)
{
    Camera& camera = demo.camera;
    // Elapsed real time determines the angle, so orbit speed is independent of frame rate.
    const double phase = std::fmod(timer.elapsed_seconds, ORBIT_PERIOD_SECONDS) / ORBIT_PERIOD_SECONDS;
    const float angle = static_cast<float>(2.0 * PI * phase);
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);

    camera.position[0] = ORBIT_RADIUS * sine;
    camera.position[1] = 0.0f;
    camera.position[2] = -ORBIT_RADIUS * cosine;

    // Forward points from the camera to the plant at the origin; right and up complete an orthonormal camera basis.
    camera.forward[0] = -sine;
    camera.forward[1] = 0.0f;
    camera.forward[2] = cosine;
    camera.right[0] = cosine;
    camera.right[1] = 0.0f;
    camera.right[2] = sine;
    camera.up[0] = 0.0f;
    camera.up[1] = 1.0f;
    camera.up[2] = 0.0f;
}

void unload_demo(SDL_GPUDevice* device, Demo& demo)
{
    destroy_splat_renderer(device, demo.renderer);
    demo = {};
}
