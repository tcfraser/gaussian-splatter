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
constexpr float FRAMING_MARGIN = 1.1f;
constexpr float ORBIT_DISTANCE_SCALE = 0.5f; // 1.0 fits the full scene bounds; smaller values move closer.

static bool load_scene(const char* path, std::vector<CovarianceSplat>& splats)
{
    // Relative paths follow the process's working directory, just like other command-line file arguments.
    std::string error;
    std::vector<QuaternionicSplat> quaternionic_splats;
    if (!load_splat_file(path, quaternionic_splats, error)) {
        return SDL_SetError("%s", error.c_str());
    }
    if (!convert_splats(quaternionic_splats, splats, error)) {
        return SDL_SetError("%s", error.c_str());
    }
    SDL_Log("Loaded %s: %zu splats, %zu bytes per file record, %zu bytes per GPU input record.",
            path, splats.size(), sizeof(QuaternionicSplat), sizeof(CovarianceSplat));
    // The source vector is released here; splats belongs to the caller.
    return true;
}

static float center_splats_in_world(std::vector<CovarianceSplat>& splats)
{
    // Loading rejects empty scenes; find the bounding box of this file's splat centers.
    double minimum[3];
    double maximum[3];
    for (std::size_t axis = 0; axis < 3; ++axis) {
        minimum[axis] = maximum[axis] = splats.front().position[axis];
    }
    for (const CovarianceSplat& splat : splats) {
        for (std::size_t axis = 0; axis < 3; ++axis) {
            const double position = splat.position[axis];
            minimum[axis] = std::min(minimum[axis], position);
            maximum[axis] = std::max(maximum[axis], position);
        }
    }
    const double center[3] = {
        0.5 * minimum[0] + 0.5 * maximum[0],
        0.5 * minimum[1] + 0.5 * maximum[1],
        0.5 * minimum[2] + 0.5 * maximum[2],
    };
    double radius = 0.0;
    for (CovarianceSplat& splat : splats) {
        // Center at the origin and keep the demo's existing -Y-up input convention, rotating half a turn around X.
        splat.position[0] = static_cast<float>(splat.position[0] - center[0]);
        splat.position[1] = static_cast<float>(-(splat.position[1] - center[1]));
        splat.position[2] = static_cast<float>(-(splat.position[2] - center[2]));
        // Sigma' = A * Sigma * transpose(A), with A = diag(1, -1, -1); only xy and xz change sign.
        splat.covariance[1] = -splat.covariance[1];
        splat.covariance[2] = -splat.covariance[2];

        // Include each Gaussian's size, so even a scene with a single splat has a useful viewing distance.
        // The sum of the diagonal variances bounds the variance in any direction; allow three standard deviations.
        const double variance_sum = static_cast<double>(splat.covariance[0]) + splat.covariance[3] + splat.covariance[5];
        const double splat_radius = 3.0 * std::sqrt(variance_sum);
        const double x = splat.position[0];
        const double y = splat.position[1];
        const double z = splat.position[2];
        radius = std::max(radius, std::sqrt(x * x + y * y + z * z) + splat_radius);
    }
    return static_cast<float>(radius);
}

bool load_demo(SDL_GPUDevice* device, SDL_GPUTextureFormat target_format, const Shaders& shaders, const char* path, Demo& demo)
{
    std::vector<CovarianceSplat> splats;
    if (!load_scene(path, splats)) {
        return false;
    }
    demo.scene_radius = center_splats_in_world(splats);
    if (!std::isfinite(demo.scene_radius) || demo.scene_radius <= 0.0f) {
        return SDL_SetError("The scene bounds are outside the supported float range.");
    }

    demo.camera = {};
    // The renderer copies the scene into its transfer buffer; the CPU vector is released on return.
    return init_splat_renderer(device, target_format, shaders, splats, demo.renderer);
}

void update_demo(Demo& demo, const Timer& timer)
{
    Camera& camera = demo.camera;
    // Start with a distance that fits the scene bounds inside the narrower field of view.
    const float aspect_ratio = camera.viewport_size[0] / camera.viewport_size[1];
    const float half_fov = std::atan(std::tan(0.5f * camera.vertical_fov_radians) * std::min(1.0f, aspect_ratio));
    // A tangent from the camera to the sphere gives sin(half_fov) = radius / distance.
    const float fitted_distance = FRAMING_MARGIN * demo.scene_radius / std::sin(half_fov);
    const float orbit_radius = ORBIT_DISTANCE_SCALE * fitted_distance;
    // Approximate the object's front and back using the existing scene radius.
    camera.near_plane = std::max(0.01f * demo.scene_radius, orbit_radius - demo.scene_radius);
    camera.far_plane = orbit_radius + demo.scene_radius;

    // Elapsed real time determines the angle, so orbit speed is independent of frame rate.
    const double phase = std::fmod(timer.elapsed_seconds, ORBIT_PERIOD_SECONDS) / ORBIT_PERIOD_SECONDS;
    const float angle = static_cast<float>(2.0 * PI * phase);
    const float sine = std::sin(angle);
    const float cosine = std::cos(angle);

    camera.position[0] = orbit_radius * sine;
    camera.position[1] = 0.0f;
    camera.position[2] = -orbit_radius * cosine;

    // Forward points from the camera to the scene at the origin; right and up complete an orthonormal camera basis.
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
