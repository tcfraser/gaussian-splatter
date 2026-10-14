#include "splat_varyings.hlsli"

float4 main(SplatVaryings input) : SV_Target0
{
    float2 offset = input.pixel_offset;
    float3 inverse_covariance = input.inverse_covariance;

    // Calculate distance 
    // -- Squared distance in the ellipse's coordinates: d^2 = offset^T * inverse_covariance * offset.
    // -- With offset = (x, y) and inverse_covariance = (a, b, c),
    // -- the symmetric matrix the calculation is:
    // -- d^2 = [x, y] . [a, b] . [x] =  a(x^2) + 2b(x*y) + c(y^2).
    // --                [b, c]   [y]
    float distance_squared = inverse_covariance.x * offset.x * offset.x
                           + 2.0 * inverse_covariance.y * offset.x * offset.y
                           + inverse_covariance.z * offset.y * offset.y;
    // If pixel is too far away from center, then skip rendering
    if (distance_squared > 9.0) {
        discard;
    }

    // Calculate effective opacity
    // -- Opacity is the peak alpha at the center when d = 0,
    // -- otherwise it should be exp(-d^2 / 2).
    float intensity = exp(-0.5 * max(distance_squared, 0.0));
    float new_alpha = saturate(input.color_opacity.a) * intensity;

    return float4(input.color_opacity.rgb, new_alpha);
}
