// Increments each element in place. Paired with compute_fill (which writes i*2+1)
// and a barrier between the two dispatches to test UAV->UAV ordering.

RWStructuredBuffer<uint> Buf : register(u0, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    Buf[tid.x] = Buf[tid.x] + 1;
}
