#include "splat_varyings.hlsli"

float4 main(SplatVaryings input) : SV_Target0
{
    float2 offset = input.pixel_offset;
    float3 inverse_covariance = input.inverse_covariance;
    // Squared distance in the ellipse's coordinates: d^T * inverse(C) * d.
    float radius_squared = inverse_covariance.x * offset.x * offset.x
        + 2.0 * inverse_covariance.y * offset.x * offset.y
        + inverse_covariance.z * offset.y * offset.y;
    if (radius_squared > 9.0) {
        discard;
    }

    // Opacity is the peak alpha at the center; exp(-r^2 / 2) makes it fade toward the ellipse's boundary.
    float alpha = saturate(input.color_opacity.a) * exp(-0.5 * max(radius_squared, 0.0));
    // The pipeline multiplies this RGB by alpha when blending into the target.
    return float4(input.color_opacity.rgb, alpha);
}
