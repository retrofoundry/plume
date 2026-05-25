// Reads a constant buffer from descriptor set 0 (space0) and another from set 1
// (space1), writes their sum to a UAV in set 0. Exercises binding multiple descriptor
// sets (setIndex 0 and 1).

cbuffer ParamsA : register(b0, space0) {
    float4 colorA;
};
RWStructuredBuffer<float4> Output : register(u1, space0);

cbuffer ParamsB : register(b0, space1) {
    float4 colorB;
};

[numthreads(1, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    Output[0] = colorA + colorB;
}
