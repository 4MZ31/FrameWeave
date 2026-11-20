// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS still rule (pipeline/PushHudStill.h): does each pixel's surrounding mostly stand still?
// Two small steps, chosen by `stage`, run once for every real frame:
//   0  per block of 16 x 16 pixels, how many of its pixels are the same in both real frames
//      (the state's z, written by PushHudLockUpdate): a whole number up to 256;
//   1  per block, over the 5 x 5 blocks around it: 1 when half or more of their pixels are the
//      same, 0 when the surrounding mostly moves. Counted in whole numbers (at most 6400, exact in
//      a float), so this step and the rule in plain C++ can never differ by a rounding.
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
        uint still = 0;
        for (uint y = 0; y < 16; ++y)
            for (uint x = 0; x < 16; ++x) {
                const uint2 p = id.xy * 16 + uint2(x, y);
                if (p.x >= width || p.y >= height) continue;
                still += sourceTex[p].z > 0.5 ? 1 : 0;
            }
        outputTex[id.xy] = float(still);
        return;
    }
    uint still = 0, total = 0;
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            const int2 q = int2(id.xy) + int2(dx, dy);
            if (q.x < 0 || q.y < 0 || q.x >= int(blocksWide) || q.y >= int(blocksHigh)) continue;
            still += uint(sourceTex[q].x + 0.5);
            // The last column and row of blocks may be cut by the picture's edge.
            total += min(16u, width - uint(q.x) * 16u) * min(16u, height - uint(q.y) * 16u);
        }
    outputTex[id.xy] = float(still) < 0.5 * float(total) ? 0.0 : 1.0;   // hudstill::kSurround
}
