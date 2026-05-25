// Fullscreen-triangle draw that outputs a push-constant color. Used by the format
// workloads to write a known value into a non-RGBA8 render target for readback.

struct PushConstants {
    float4 color;
};
[[vk::push_constant]] PushConstants pc;

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
    return pc.color;
}
