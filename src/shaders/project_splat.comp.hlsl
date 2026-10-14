// The shared uniform data for describing the camera.
// -- b0 indicates a constant buffer bound to slot 0.
// -- space2 is SDL's uniform buffer space for compute shaders (https://wiki.libsdl.org/SDL3/SDL_CreateGPUComputePipeline).
cbuffer Projection : register(b0, space2)
{
    float2 viewport_size; // (width, height) of viewport in pixels
    float focal_length;   // focal length of camera
    float near_plane;     // minimum distance to camera to allow rendering
};

// SDL's Direct3D storage buffers use raw byte access: t0/space0 is the input, u0/space1 is the output.
ByteAddressBuffer splats : register(t0, space0);
RWByteAddressBuffer projected_splats : register(u0, space1);

// These structs define the buffer layouts for typed loads and stores; keep them in sync with the C++ structs.
struct CovarianceSplat {
    float3 position;
    uint rgba;           // Four bytes: red, green, blue, opacity, from least to most significant.
    float3 covariance_a; // xx, xy, xz
    float3 covariance_b; // yy, yz, zz
};

struct ProjectedSplat {
    float4 color_opacity;
    float3 inverse_covariance;
    float2 center;
    float2 half_extent;
};

ProjectedSplat project_splat(CovarianceSplat input)
{
    // Keep RGBA packed in storage; expand the channels only while computing this splat's projection.
    float4 color_opacity = float4(
        input.rgba & 255u,
        (input.rgba >> 8) & 255u,
        (input.rgba >> 16) & 255u,
        (input.rgba >> 24) & 255u) / 255.0;

    // Invalid splats produce zero opacity, which tells the vertex shader to collapse their quads.
    ProjectedSplat output = (ProjectedSplat)0;
    if (input.position.z <= near_plane || color_opacity.a <= 0.0) {
        return output;
    }

    // Positions and covariance are already in the fixed camera's coordinate system: +Z forward, +Y up.
    float3x3 covariance = float3x3(
        input.covariance_a.x, input.covariance_a.y, input.covariance_a.z,
        input.covariance_a.y, input.covariance_b.x, input.covariance_b.y,
        input.covariance_a.z, input.covariance_b.y, input.covariance_b.z);

    float inverse_z = 1.0 / input.position.z;
    float projected_scale = focal_length * inverse_z;

    // The 2x3 Jacobian that describes the relationship between the 3D spatial coords to the 2D screen coords
    // -- note that pixel coordinates have +Y down.
    float3 jacobian_x = float3(projected_scale, 0.0, -projected_scale * input.position.x * inverse_z);
    float3 jacobian_y = float3(0.0, -projected_scale, projected_scale * input.position.y * inverse_z);

    // Project the covariance with C = J * Sigma * transpose(J) where Sigma is 3D covariance and C is 2D covariance.
    float covariance_xx = dot(jacobian_x, mul(covariance, jacobian_x));
    float covariance_xy = dot(jacobian_x, mul(covariance, jacobian_y));
    float covariance_yy = dot(jacobian_y, mul(covariance, jacobian_y));

    // Here we need to worry about subpixel gaussian and how they get sampled.
    // The solution is to add a little bit of blur so the smallest gaussians are captured by subpixel sampling
    covariance_xx += 0.3;
    covariance_yy += 0.3;
    float determinant = covariance_xx * covariance_yy - covariance_xy * covariance_xy;
    if (covariance_xx <= 0.0 || covariance_yy <= 0.0 || determinant <= 0.000001) {
        return output;
    }

    // Approximate the width and height for a bounding box around the projected ellipse
    // -- three standard deviations suffice here.
    output.half_extent = 3.0 * sqrt(float2(covariance_xx, covariance_yy));
    output.center = 0.5 * viewport_size + projected_scale * float2(input.position.x, -input.position.y);
    output.color_opacity = color_opacity;
    output.inverse_covariance = float3(covariance_yy, -covariance_xy, covariance_xx) / determinant;
    return output;
}

// One thread projects one splat; the CPU dispatches enough groups of 64 to cover the input buffer.
[numthreads(64, 1, 1)]
void main(uint3 thread_id : SV_DispatchThreadID)
{
    uint byte_count;
    splats.GetDimensions(byte_count);
    uint splat_id = thread_id.x;

    // We dispatch whole groups of 64 threads, so any thread past the last splat does nothing.
    if (splat_id >= byte_count / sizeof(CovarianceSplat)) {
        return;
    }

    // sizeof locates the record; DXC derives its field offsets and reads the whole struct.
    CovarianceSplat input = splats.Load<CovarianceSplat>(splat_id * sizeof(CovarianceSplat));

    // Do the actual computation for the projection
    ProjectedSplat output = project_splat(input);

    // Write the whole result even for invalid splats, so no result from a previous frame survives.
    projected_splats.Store<ProjectedSplat>(splat_id * sizeof(ProjectedSplat), output);
}
