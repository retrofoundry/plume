// Fullscreen-triangle draw that samples an explicit mip level (mip 1) via SampleLevel.
// Used to verify mip-level selection independent of derivatives.

[[vk::binding(0, 0)]] Texture2D<float4> tex : register(t0);
[[vk::binding(1, 0)]] SamplerState samp : register(s1);

struct VSOutput {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput o;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.uv = uv;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    return tex.SampleLevel(samp, input.uv, 1.0);
}
