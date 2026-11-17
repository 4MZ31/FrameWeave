// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS outputs and canvas (engine/PushCanvas.h): the presented picture sampled into an output of its
// own size and layout (a 1080p stream from a 4K picture, a 9:16 recording, the game shrunk into a box),
// then coded to NV12/P010 like RgbToNv12.hlsl. Shrinking averages taps x taps bilinear samples per
// output pixel (a box filter); outside the game picture's rectangle the output is the background colour.
#include "HdrColor.hlsli"
Texture2D<float4> rgb : register(t0);
RWTexture2D<float> yPlane : register(u0);
RWTexture2D<float2> uvPlane : register(u1);
cbuffer Constants : register(b0) {
    uint outW, outH, hdrMode, ditherBits;   // hdrMode as RgbToNv12 reserved.x; ditherBits: asfloat dither step
    float dstX, dstY, dstW, dstH;           // the picture's rectangle on the output (pixels)
    float srcX, srcY, srcW, srcH;           // the part of the presented picture it shows (pixels)
    uint texW, texH, tapsXY, background;    // presented size; taps x | taps y << 8; 0xRRGGBB sRGB
};
float3 convert(float3 c) {
    if (hdrMode) {
        float y = dot(c, float3(.2627, .6780, .0593));
        return round(float3(64 + 876 * y, 512 + 896 * (c.b - y) / 1.8814, 512 + 896 * (c.r - y) / 1.4746)) * 64.0 / 65535.0;
    }
    float y = dot(c, float3(0.2126, 0.7152, 0.0722));
    return float3((16 + 219 * y) / 255.0, (128 + 224 * (c.b - y) / 1.8556) / 255.0, (128 + 224 * (c.r - y) / 1.5748) / 255.0);
}
// Bilinear sample of the presented picture at pixel position p (centres at .5), in its own encoding.
float3 tap(float2 p) {
    const float2 q = clamp(p - 0.5, 0.0, float2(texW - 1, texH - 1));
    const uint2 a = uint2(q);
    const uint2 b = min(a + 1, uint2(texW - 1, texH - 1));
    const float2 f = q - float2(a);
    const float3 c = lerp(lerp(rgb[a].rgb, rgb[uint2(b.x, a.y)].rgb, f.x), lerp(rgb[uint2(a.x, b.y)].rgb, rgb[b].rgb, f.x), f.y);
    return c;
}
float3 backgroundColor() {
    const float3 s = float3((background >> 16) & 0xff, (background >> 8) & 0xff, background & 0xff) / 255.0;
    if (hdrMode == 1) {
        // The picture is linear (1.0 = 80 nits) for PQ output: sRGB white at 203 nits.
        const float3 lin = lerp(pow((s + 0.055) / 1.055, 2.4), s / 12.92, step(s, 0.04045));
        return lin * (203.0 / 80.0);
    }
    return s;
}
// What output pixel o shows, ready for convert() (as RgbToNv12's inputColor).
float3 colorAt(uint2 o) {
    const float2 p = float2(o) + 0.5;
    float3 c;
    if (p.x < dstX || p.y < dstY || p.x >= dstX + dstW || p.y >= dstY + dstH) c = backgroundColor();
    else {
        const uint tx = clamp(tapsXY & 0xff, 1u, 4u), ty = clamp((tapsXY >> 8) & 0xff, 1u, 4u);
        const float2 scale = float2(srcW / dstW, srcH / dstH);
        float3 sum = 0;
        [loop] for (uint j = 0; j < ty; ++j)
            [loop] for (uint i = 0; i < tx; ++i) {
                const float2 at = float2(o) + float2((i + 0.5) / tx, (j + 0.5) / ty);
                sum += tap(float2(srcX, srcY) + (at - float2(dstX, dstY)) * scale);
            }
        c = sum / (tx * ty);
    }
    return hdrMode == 1 ? HdrEncodePq(c) : saturate(c);
}
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= outW || id.y >= outH) return;
    const float ditherStep = asfloat(ditherBits);
    const float3 c00 = colorAt(id.xy);
    yPlane[id.xy] = convert(c00).x + OutputDither(id.xy, ditherStep);
    if ((id.x & 1) == 0 && (id.y & 1) == 0) {
        const float3 c = (c00 + colorAt(id.xy + uint2(1, 0)) + colorAt(id.xy + uint2(0, 1)) + colorAt(id.xy + uint2(1, 1))) * 0.25;
        uvPlane[id.xy / 2] = convert(saturate(c)).yz + OutputDither(id.xy / 2, ditherStep);
    }
}
