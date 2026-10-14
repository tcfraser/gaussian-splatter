#include "splat_varyings.hlsli"

// Build a quad from the center and bounds already computed for this splat.
// The shared uniform data for describing the camera
// register(b0, space1) tells the GPU where the CPU has sent the uniform data.
// -- b0 indicates a constant buffer bound to slot 0
// -- space1 must be used for all vertex shaders (see https://wiki.libsdl.org/SDL3/SDL_CreateGPUShader)
cbuffer Projection : register(b0, space1)
{
    float2 viewport_size; // (width, height) of viewport in pixels
    float focal_length;   // used by the compute shader; retained here to share the CPU uniform layout
    float near_plane;     // used by the compute shader
};

// The compute shader's output buffer supplies these attributes, advancing once per splat instance.
struct ProjectedSplatInput {
    float4 color_opacity : TEXCOORD0;
    float3 inverse_covariance : TEXCOORD1;
    float2 center : TEXCOORD2;
    float2 half_extent : TEXCOORD3;
};

SplatVaryings main(ProjectedSplatInput input, uint vertex_id : SV_VertexID)
{
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
    output.position = float4(2.0 * pixel_position.x / viewport_size.x - 1.0, 1.0 - 2.0 * pixel_position.y / viewport_size.y, 0.5, 1.0);
    output.color_opacity = input.color_opacity;
    output.inverse_covariance = input.inverse_covariance;
    return output;
}
