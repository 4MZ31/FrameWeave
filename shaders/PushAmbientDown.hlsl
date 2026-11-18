// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS ambient fill, step 1 (pipeline/PushAmbientPass.h, engine/PushAmbient.h): the frame averaged
// down to a small picture. Every output texel is the mean of taps x taps bilinear samples spread
// evenly over its own share of the frame, so a 4K frame is covered almost completely and nothing
// flickers when the picture moves (a handful of sparse samples would).
Texture2D<float4> sourceTex : register(t0);
RWTexture2D<float4> smallTex : register(u0);
SamplerState pointClamp : register(s0);
SamplerState linearClamp : register(s1);
cbuffer Constants : register(b0) {
    uint width, height, taps, pad0;
    uint pad1, pad2, pad3, pad4;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) return;
    const uint n = clamp(taps, 1u, 16u);
    const float2 cell = 1.0 / float2(width, height);
    const float2 origin = float2(id.xy) * cell;
    const float2 step = cell / float(n);
    float3 sum = 0;
    for (uint y = 0; y < n; ++y)
        for (uint x = 0; x < n; ++x)
            sum += sourceTex.SampleLevel(linearClamp, origin + (float2(x, y) + 0.5) * step, 0).rgb;
    const float3 mean = sum / float(n * n);
    // A frame can hold values that are not numbers (a provider's first frame); keep them out of the bars.
    smallTex[id.xy] = float4(any(isnan(mean)) || any(isinf(mean)) ? float3(0, 0, 0) : mean, 1);
}
