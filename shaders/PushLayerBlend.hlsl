// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS streaming mode live layers (engine/PushStreamOverlay.h LiveLayerBlend): a webcam, window or
// video frame (straight-alpha sRGB RGBA8, any size) scaled into a rectangle of the recorder's NV12/P010
// picture and blended over it. Downscaling averages taps x taps bilinear samples per output pixel (a box
// filter), so a 1280-wide camera drawn 300 pixels wide does not shimmer. The picture under the
// rectangle is read from a copy (bgY, bgUv: the rectangle at their top left). SDR: BT.709 limited
// range from sRGB. HDR: white at 203 nits (BT.2408), BT.2020 PQ 10-bit, as the scene picture.
#include "HdrColor.hlsli"
Texture2D<float4> layer : register(t0);
Texture2D<float> bgY : register(t1);
Texture2D<float2> bgUv : register(t2);
RWTexture2D<float> outY : register(u0);
RWTexture2D<float2> outUv : register(u1);
cbuffer Constants : register(b0) {
    uint x0, y0, w, h;          // the rectangle in the picture (even) and its size
    uint srcW, srcH, flags;     // flags bit 0: HDR; bits 8-15: taps per axis (1-4)
    float opacity;
};
float3 coded(float3 srgb) {
    if (flags & 1) {
        const float3 lin = lerp(pow((srgb + 0.055) / 1.055, 2.4), srgb / 12.92, step(srgb, 0.04045));
        const float3 e = HdrEncodePq(lin * (203.0 / 80.0));
        const float y = dot(e, float3(.2627, .6780, .0593));
        return float3(64 + 876 * y, 512 + 896 * (e.b - y) / 1.8814, 512 + 896 * (e.r - y) / 1.4746) * 64.0 / 65535.0;
    }
    const float y = dot(srgb, float3(0.2126, 0.7152, 0.0722));
    return float3((16 + 219 * y) / 255.0, (128 + 224 * (srgb.b - y) / 1.8556) / 255.0, (128 + 224 * (srgb.r - y) / 1.5748) / 255.0);
}
float codes(float v) { return (flags & 1) ? round(v * 65535.0 / 64.0) * 64.0 / 65535.0 : v; }
// Premultiplied bilinear sample at source pixel position p (pixel centres at .5).
float4 tap(float2 p) {
    const float2 q = clamp(p - 0.5, 0.0, float2(srcW - 1, srcH - 1));
    const uint2 a = uint2(q);
    const uint2 b = min(a + 1, uint2(srcW - 1, srcH - 1));
    const float2 f = q - float2(a);
    float4 c00 = layer[a], c10 = layer[uint2(b.x, a.y)], c01 = layer[uint2(a.x, b.y)], c11 = layer[b];
    c00.rgb *= c00.a; c10.rgb *= c10.a; c01.rgb *= c01.a; c11.rgb *= c11.a;
    return lerp(lerp(c00, c10, f.x), lerp(c01, c11, f.x), f.y);
}
// The layer's premultiplied colour over output pixel o of the rectangle.
float4 sampleAt(uint2 o) {
    const uint taps = clamp((flags >> 8) & 0xff, 1u, 4u);
    const float2 scale = float2(srcW, srcH) / float2(w, h);
    float4 sum = 0;
    [loop] for (uint j = 0; j < taps; ++j)
        [loop] for (uint i = 0; i < taps; ++i)
            sum += tap((float2(o) + (float2(i, j) + 0.5) / taps) * scale);
    return sum / (taps * taps);
}

[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= w || id.y >= h) return;
    const uint2 p = uint2(x0, y0) + id.xy;
    const float4 s = sampleAt(id.xy);
    const float a = saturate(s.a * opacity);
    float luma = bgY[id.xy];
    if (s.a > 0.0001) luma = lerp(luma, coded(saturate(s.rgb / s.a)).x, a);
    outY[p] = codes(luma);
    if ((id.x & 1) != 0 || (id.y & 1) != 0) return;
    // Chroma: the 2x2 output pixels it covers, colour weighted by alpha.
    float4 sum = s;
    sum += sampleAt(min(id.xy + uint2(1, 0), uint2(w - 1, h - 1)));
    sum += sampleAt(min(id.xy + uint2(0, 1), uint2(w - 1, h - 1)));
    sum += sampleAt(min(id.xy + uint2(1, 1), uint2(w - 1, h - 1)));
    float2 chroma = bgUv[id.xy / 2];
    if (sum.a > 0.0001) chroma = lerp(chroma, coded(saturate(sum.rgb / sum.a)).yz, saturate(sum.a * 0.25 * opacity));
    outUv[p / 2] = float2(codes(chroma.x), codes(chroma.y));
}
