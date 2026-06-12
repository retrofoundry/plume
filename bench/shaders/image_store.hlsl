// Compute kernel that writes a per-pixel gradient into a RWTexture2D (storage image / UAV).
// Distinct from the RWStructuredBuffer path the compute_dispatch workload covers: storage
// images use a different descriptor type, layout, and barrier path in every backend.
// The 16x16 dispatch hardcodes /15.0 so the corners are exactly 0.0 and 1.0.

RWTexture2D<float4> Output : register(u0, space0);

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    Output[tid.xy] = float4(tid.x / 15.0, tid.y / 15.0, 0.5, 1.0);
}
