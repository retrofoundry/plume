// Reads a StructuredBuffer<float4> SRV (t0) and writes 2x its value to a UAV (u1).
// Exercises reading a structured buffer in a shader (distinct from the cbuffer reads
// the descriptor_draw workload covers).

StructuredBuffer<float4> Input : register(t0, space0);
RWStructuredBuffer<float4> Output : register(u1, space0);

[numthreads(1, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    Output[0] = Input[0] * 2.0;
}
