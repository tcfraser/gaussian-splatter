// SDL matches vertex attribute locations 0 and 1 to TEXCOORD0 and TEXCOORD1.
struct VertexInput {
    float2 position : TEXCOORD0;
    float4 color : TEXCOORD1;
};

struct VertexOutput {
    // Keep color first so its register matches the fragment shader's input.
    float4 color : TEXCOORD0;
    float4 position : SV_Position;
};

VertexOutput main(VertexInput input)
{
    VertexOutput output;
    output.position = float4(input.position, 0.0, 1.0);
    output.color = input.color;
    return output;
}
