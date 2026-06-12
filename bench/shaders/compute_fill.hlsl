// Writes a known value (i*2+1) per thread so the result can be read back and verified.

RWStructuredBuffer<uint> Output : register(u0, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    Output[tid.x] = tid.x * 2u + 1u;
}
