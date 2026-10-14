#include "camera.h"

#include <cmath>

void start_timer(Timer& timer, std::uint64_t time_ns)
{
    timer = {};
    timer.previous_time_ns = time_ns;
}

void update_timer(Timer& timer, std::uint64_t time_ns)
{
    // Subtract integer timestamps before converting to seconds to retain precision between nearby frames.
    const double delta_seconds = static_cast<double>(time_ns - timer.previous_time_ns) * 1.0e-9;
    timer.delta_seconds = static_cast<float>(delta_seconds);
    timer.elapsed_seconds += delta_seconds;
    timer.previous_time_ns = time_ns;
    ++timer.frame_count;
}

void update_camera_projection(Camera& camera, std::uint32_t width, std::uint32_t height)
{
    camera.viewport_size[0] = static_cast<float>(width);
    camera.viewport_size[1] = static_cast<float>(height);
    camera.principal_point[0] = 0.5f * camera.viewport_size[0];
    camera.principal_point[1] = 0.5f * camera.viewport_size[1];
    // A single focal length assumes square pixels; viewport dimensions determine the horizontal field of view.
    camera.focal_length = 0.5f * camera.viewport_size[1] / std::tan(0.5f * camera.vertical_fov_radians);
}
