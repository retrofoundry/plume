// Position + vertex-color passthrough — the shared shader for the graphics workloads.

struct VSInput {
    [[vk::location(0)]] float3 pos : POSITION;
    [[vk::location(1)]] float4 color : COLOR;
};

struct VSOutput {
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

VSOutput VSMain(VSInput input) {
    VSOutput o;
    o.pos = float4(input.pos, 1.0);
    o.color = input.color;
    return o;
}

float4 PSMain(VSOutput input) : SV_TARGET {
    return input.color;
}
