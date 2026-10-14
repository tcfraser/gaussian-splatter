// Shared vertex-output/fragment-input layout keeps the shader interface identical in both stages.
struct SplatVaryings {
    nointerpolation float4 color_opacity : TEXCOORD0;
    // The 2D invariance covariance matrix is symemtric and has three unique entries:.
    // [ xx xy
    //   xy yy ]
    // the shared convention is to store them as [xx, xy, yy] as three floats
    nointerpolation float3 inverse_covariance : TEXCOORD1;
    noperspective float2 pixel_offset : TEXCOORD2;
    float4 position : SV_Position;
};
