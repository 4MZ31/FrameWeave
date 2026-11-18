// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS ambient fill, step 2 (pipeline/PushAmbientPass.h): the small picture blurred with a
// Gaussian (sigma 3 texels of 96 x 54, about 3 % of the picture's width), edges repeated. The present
// shader then enlarges it over the whole window.
Texture2D<float4> smallTex : register(t0);
RWTexture2D<float4> blurredTex : register(u0);
cbuffer Constants : register(b0) {
    uint width, height, pad0, pad1;
    uint pad2, pad3, pad4, pad5;
};
static const int kRadius = 6;
static const float kSigma = 3.0;

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const int2 last = int2(width, height) - 1;
    float3 sum = 0;
    float weight = 0;
    for (int dy = -kRadius; dy <= kRadius; ++dy)
        for (int dx = -kRadius; dx <= kRadius; ++dx) {
            const float w = exp(-float(dx * dx + dy * dy) / (2.0 * kSigma * kSigma));
            sum += smallTex[clamp(int2(id.xy) + int2(dx, dy), int2(0, 0), last)].rgb * w;
            weight += w;
        }
    blurredTex[id.xy] = float4(sum / weight, 1);
}
