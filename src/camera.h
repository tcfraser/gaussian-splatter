#pragma once

#include <cstdint>

// CPU-only frame timing, owned by main.cpp and updated by camera.cpp.
struct Timer {
    float delta_seconds = 0.0f;             // Real time since the previous frame update.
    double elapsed_seconds = 0.0;           // Double precision keeps the animation clock accurate over long runs.
    std::uint64_t frame_count = 0;
    std::uint64_t previous_time_ns = 0;
};

// Matches Camera in shaders/camera.hlsli: each float3 plus scalar fills one 16-byte uniform row.
// Right, up, and forward are perpendicular unit vectors expressed in world coordinates.
struct Camera {
    float position[3] = {};
    float near_plane = 0.1f;
    float right[3] = {1.0f, 0.0f, 0.0f};
    float far_plane = 100.0f;
    float up[3] = {0.0f, 1.0f, 0.0f};
    float focal_length = 0.0f;              // Pixels; derived from vertical FOV and viewport height each frame.
    float forward[3] = {0.0f, 0.0f, 1.0f};
    float vertical_fov_radians = 1.04719755f; // 60 degrees.
    float viewport_size[2] = {};
    float principal_point[2] = {};          // The image point on the viewing axis, in pixels.
};

// Timestamps come from SDL_GetTicksNS() in main.cpp; initialize after scene loading to exclude startup time.
void start_timer(Timer& timer, std::uint64_t time_ns);
void update_timer(Timer& timer, std::uint64_t time_ns);

// Update projection for the current swapchain dimensions, preserving the camera's position and orientation.
void update_camera_projection(Camera& camera, std::uint32_t width, std::uint32_t height);
