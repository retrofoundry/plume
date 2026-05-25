// Fullscreen-triangle draw that writes two render targets with distinct colors.
// Verifies multiple-render-target output (SV_Target0 + SV_Target1) is routed correctly.

struct VSOutput {
    float4 pos : SV_POSITION;
};

struct PSOutput {
    float4 target0 : SV_Target0;
    float4 target1 : SV_Target1;
};

VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput o;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return o;
}

PSOutput PSMain(VSOutput input) {
    PSOutput o;
    o.target0 = float4(1.0, 0.0, 0.0, 1.0); // red
    o.target1 = float4(0.0, 1.0, 0.0, 1.0); // green
    return o;
}
