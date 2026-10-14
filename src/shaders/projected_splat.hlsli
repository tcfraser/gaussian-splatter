#ifndef PROJECTED_SPLAT_HLSLI
#define PROJECTED_SPLAT_HLSLI

// Matches the 48-byte ProjectedSplat in splat_renderer.cpp; raw buffer fields have no implicit vec3 padding.
struct ProjectedSplat {
    float4 color_opacity;
    float3 inverse_covariance;
    float2 center;
    float2 half_extent;
    uint depth_key;
};

#endif
