#ifndef CAMERA_HLSLI
#define CAMERA_HLSLI

// Matches the 80-byte Camera in camera.h; float3/scalar pairs occupy complete 16-byte uniform rows.
struct Camera {
    float3 position;
    float near_plane;
    float3 right;
    float far_plane;
    float3 up;
    float focal_length;
    float3 forward;
    float vertical_fov_radians;
    float2 viewport_size;
    float2 principal_point;
};

#endif
