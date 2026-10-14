// Shared vertex-output/fragment-input layout keeps the shader interface identical in both stages.
struct SplatVaryings {
    nointerpolation float4 color_opacity : TEXCOORD0;
    // The inverse 2D covariance has three unique entries: xx, xy, yy.
    nointerpolation float3 inverse_covariance : TEXCOORD1;
    noperspective float2 pixel_offset : TEXCOORD2;
    float4 position : SV_Position;
};
