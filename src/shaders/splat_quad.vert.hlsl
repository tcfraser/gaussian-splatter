#include "splat_varyings.hlsli"
#include "camera.hlsli"
#include "projected_splat.hlsli"
#include "radix_sort.hlsli"

// Build a quad from the center and bounds already computed for this splat.
// The shared uniform data for describing the camera
// register(b0, space1) tells the GPU where the CPU has sent the uniform data.
// -- b0 indicates a constant buffer bound to slot 0
// -- space1 must be used for all vertex shaders (see https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader)
cbuffer CameraUniform : register(b0, space1)
{
    Camera camera;
};

// Vertex storage buffers: follow the sorted index rather than moving the projected records themselves.
ByteAddressBuffer projected_splats : register(t0, space0);
ByteAddressBuffer render_order : register(t1, space0);

SplatVaryings main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    SortPair pair = render_order.Load<SortPair>(instance_id * sizeof(SortPair));
    ProjectedSplat input = projected_splats.Load<ProjectedSplat>(pair.index * sizeof(ProjectedSplat));
    SplatVaryings output = (SplatVaryings)0;
    // Invalid splats collapse to a point outside the clip volume.
    output.position = float4(0.0, 0.0, -1.0, 1.0);
    if (input.color_opacity.a <= 0.0) {
        return output;
    }

    // Four corners form a triangle strip: triangles (0, 1, 2) and (2, 1, 3).
    const float2 corners[4] = {
        float2(-1.0, -1.0), float2(1.0, -1.0),
        float2(-1.0, 1.0), float2(1.0, 1.0)
    };

    output.pixel_offset = corners[vertex_id] * input.half_extent;
    float2 pixel_position = input.center + output.pixel_offset;

    // Convert pixels to clip coordinates.
    output.position = float4(2.0 * pixel_position.x / camera.viewport_size.x - 1.0, 1.0 - 2.0 * pixel_position.y / camera.viewport_size.y, 0.5, 1.0);
    output.color_opacity = input.color_opacity;
    output.inverse_covariance = input.inverse_covariance;
    return output;
}
