#include "splat_varyings.hlsli"

// SDL maps vertex uniform slot 0 to b0 in space1 for DXBC shaders.
cbuffer Projection : register(b0, space1)
{
    float2 viewport_size;
    float focal_length;
    float near_plane;
};

// Attributes advance once per instance, so every vertex of a quad sees the same splat.
struct SplatInput {
    float3 position : TEXCOORD0;
    float3 color : TEXCOORD1;
    float opacity : TEXCOORD2;
    float3 covariance_a : TEXCOORD3; // xx, xy, xz
    float3 covariance_b : TEXCOORD4; // yy, yz, zz
};

SplatVaryings main(SplatInput input, uint vertex_id : SV_VertexID)
{
    SplatVaryings output = (SplatVaryings)0;
    // Invalid splats collapse to a point outside the clip volume.
    output.position = float4(0.0, 0.0, -1.0, 1.0);
    if (input.position.z <= near_plane || input.opacity <= 0.0) {
        return output;
    }

    // Positions and covariance are already in the fixed camera's coordinate system: +Z forward, +Y up.
    float3x3 covariance = float3x3(
        input.covariance_a.x, input.covariance_a.y, input.covariance_a.z,
        input.covariance_a.y, input.covariance_b.x, input.covariance_b.y,
        input.covariance_a.z, input.covariance_b.y, input.covariance_b.z);
    float inverse_z = 1.0 / input.position.z;
    float projected_scale = focal_length * inverse_z;

    // Pixel coordinates have +Y down; these are the two rows of the perspective projection's Jacobian J.
    // A Jacobian describes how a small 3D displacement changes the projected 2D position.
    float3 jacobian_x = float3(projected_scale, 0.0, -projected_scale * input.position.x * inverse_z);
    float3 jacobian_y = float3(0.0, -projected_scale, projected_scale * input.position.y * inverse_z);

    // Project the covariance with C = J * Sigma * transpose(J), using all six 3D covariance entries.
    float covariance_xx = dot(jacobian_x, mul(covariance, jacobian_x));
    float covariance_xy = dot(jacobian_x, mul(covariance, jacobian_y));
    float covariance_yy = dot(jacobian_y, mul(covariance, jacobian_y));
    // A small variance in pixel units prevents subpixel splats from degenerating into points or lines.
    covariance_xx += 0.3;
    covariance_yy += 0.3;
    float determinant = covariance_xx * covariance_yy - covariance_xy * covariance_xy;
    if (covariance_xx <= 0.0 || covariance_yy <= 0.0 || determinant <= 0.000001) {
        return output;
    }

    // Three standard deviations bound the visible ellipse; its orientation is encoded by covariance_xy.
    float2 half_extent = 3.0 * sqrt(float2(covariance_xx, covariance_yy));
    const float2 corners[6] = {
        float2(-1.0, -1.0), float2(1.0, -1.0), float2(1.0, 1.0),
        float2(-1.0, -1.0), float2(1.0, 1.0), float2(-1.0, 1.0)
    };
    float2 center = 0.5 * viewport_size + projected_scale * float2(input.position.x, -input.position.y);
    output.pixel_offset = corners[vertex_id] * half_extent;
    float2 pixel_position = center + output.pixel_offset;

    // Convert pixels to clip coordinates; depth testing is disabled, so every valid quad uses the same depth.
    output.position = float4(2.0 * pixel_position.x / viewport_size.x - 1.0, 1.0 - 2.0 * pixel_position.y / viewport_size.y, 0.5, 1.0);
    output.color_opacity = float4(input.color, input.opacity);
    output.inverse_covariance = float3(covariance_yy, -covariance_xy, covariance_xx) / determinant;
    return output;
}
