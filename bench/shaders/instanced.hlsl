// Fullscreen-triangle draw, instanced. Each instance emits value (instanceID+1)*0.1 in
// the red channel; with additive blending the target accumulates the sum over instances.
// Two instances -> 0.1 + 0.2 = 0.3. Exercises instanceCount and SV_InstanceID together.

struct VSOutput {
    float4 pos : SV_POSITION;
    float value : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput o;
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    o.pos = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    o.value = (float(instanceID) + 1.0) * 0.1;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    return float4(input.value, 0.0, 0.0, 0.0);
}
