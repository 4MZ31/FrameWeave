// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS still rule (pipeline/PushHudStill.h): how much of each pixel's surrounding stands still.
// Two small steps, chosen by `stage`, run once for every real frame:
//   0  per block of 16 x 16 pixels, the share of its pixels that are the same in both real frames
//      (the state's z, written by PushHudLockUpdate);
//   1  per block, the mean of that share over the 5 x 5 blocks around it.
Texture2D<float4> sourceTex : register(t0);   // stage 0: the state; stage 1: the blocks of stage 0
RWTexture2D<float> outputTex : register(u0);
cbuffer Constants : register(b0) {
    uint width, height, stage, blocksWide;
    uint blocksHigh, pad1, pad2, pad3;
};
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= blocksWide || id.y >= blocksHigh) return;
    if (stage == 0) {
        uint still = 0, total = 0;
        for (uint y = 0; y < 16; ++y)
            for (uint x = 0; x < 16; ++x) {
                const uint2 p = id.xy * 16 + uint2(x, y);
                if (p.x >= width || p.y >= height) continue;
                ++total;
                still += sourceTex[p].z > 0.5 ? 1 : 0;
            }
        outputTex[id.xy] = total > 0 ? float(still) / float(total) : 0.0;
        return;
    }
    float sum = 0;
    uint count = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const int2 q = int2(id.xy) + int2(dx, dy);
            if (q.x < 0 || q.y < 0 || q.x >= int(blocksWide) || q.y >= int(blocksHigh)) continue;
            sum += sourceTex[q].x;
            ++count;
        }
    outputTex[id.xy] = count > 0 ? sum / float(count) : 0.0;
}
