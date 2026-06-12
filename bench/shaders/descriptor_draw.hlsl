// Fullscreen-triangle draw whose output = baseColor * tint, both read from a constant
// buffer. Exercises a constant-buffer descriptor binding in the pixel shader.

cbuffer Params : register(b0, space0) {
    float4 baseColor;
    float4 tint;
};

struct VSOutput {
    float4 pos : SV_POSITION;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput o;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    return baseColor * tint;
}
