// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFlow full-resolution rebuild (src/pushflow/PushFlowBackend.cpp). RIFE ends with
//   out = warp(img0, grid0) * mask + warp(img1, grid1) * (1 - mask)
// and PushFlow's models also output grid0, grid1 ([1, padH, padW, 2], normalized -1..1, GridSample
// convention) and mask ([1, 1, padH, padW]). When the network ran at a reduced size (the Fast tier), this
// pass enlarges the motion (grids) and the mask, not the picture: every output pixel samples the
// FULL-RESOLUTION real frames where the motion points, so texture, text and edges keep their detail.
// It mirrors rebuild_scaled() in scripts/pushflow/prepare_runtime.py, which checks it on the CPU.
//
// The ghost guard (include/veyra/pushflow/PushFlowMotion.h has the reasoning and the reference this
// file is tested against, testPushFlowGuard): where the two samples differ and the mask cannot choose,
// the blend is a mixture of two pictures; the guard gives that pixel to one sample, the real frame
// nearer in time. guardHi = 0 is off, and then the picture is made by the statements it always was.
Texture2D<float4> previous : register(t0);
Texture2D<float4> current : register(t1);
RWStructuredBuffer<float> grid0 : register(u0);
RWStructuredBuffer<float> grid1 : register(u1);
RWStructuredBuffer<float> mask : register(u2);
RWTexture2D<float4> output : register(u3);
// Guard only: [0] pixels looked at (one per 16 x 16 block), [1] of those, pixels given to one sample.
// Running totals; the backend reads the difference.
RWStructuredBuffer<uint> guardStats : register(u4);
cbuffer Constants : register(b0) {
    uint srcW, srcH, padW, padH;
    uint modelW, modelH, encoding, alignCorners;
    uint maskForFirst;
    float guardLo, guardHi;   // the level's range (PushFlowMotion.h guardRange); guardHi 0 = no guard
    uint guardPrevious;       // 1: the previous real frame is the nearer one (time <= 0.5)
};
float3 encodeColor(float3 c) {
    if (encoding == 1) {
        c = saturate(c);
        return lerp(c * 12.92, 1.055 * pow(c, 1.0 / 2.4) - 0.055, step(0.0031308, c));
    }
    if (encoding == 2) {
        const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
        const float3 y = pow(saturate(max(c, 0.0) * (80.0 / 10000.0)), m1);
        return pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
    }
    return saturate(c);
}
float3 decodeColor(float3 e) {
    e = saturate(e);
    if (encoding == 1) return lerp(e / 12.92, pow((e + 0.055) / 1.055, 2.4), step(0.04045, e));
    if (encoding == 2) {
        const float m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
        const float3 p = pow(e, 1.0 / m2);
        return pow(max(p - c1, 0.0) / (c2 - c3 * p), 1.0 / m1) * (10000.0 / 80.0);
    }
    return e;
}
// Bilinear tap of a full-resolution frame at a continuous pixel position (pixel centres at integers).
float3 tapFrame(Texture2D<float4> t, float2 q) {
    q = clamp(q, 0.0, float2(srcW - 1, srcH - 1));
    const uint2 a = uint2(q);
    const uint2 b = min(a + 1, uint2(srcW - 1, srcH - 1));
    const float2 f = q - float2(a);
    return lerp(lerp(t[a].rgb, t[uint2(b.x, a.y)].rgb, f.x), lerp(t[uint2(a.x, b.y)].rgb, t[b].rgb, f.x), f.y);
}
// Bilinear sample of a model-size field at a continuous model-pixel position, clamped to the model area.
float2 tapGrid(RWStructuredBuffer<float> g, float2 q) {
    q = clamp(q, 0.0, float2(modelW - 1, modelH - 1));
    const uint2 a = uint2(q);
    const uint2 b = min(a + 1, uint2(modelW - 1, modelH - 1));
    const float2 f = q - float2(a);
    const uint i00 = (a.y * padW + a.x) * 2, i10 = (a.y * padW + b.x) * 2, i01 = (b.y * padW + a.x) * 2, i11 = (b.y * padW + b.x) * 2;
    const float2 v00 = float2(g[i00], g[i00 + 1]), v10 = float2(g[i10], g[i10 + 1]);
    const float2 v01 = float2(g[i01], g[i01 + 1]), v11 = float2(g[i11], g[i11 + 1]);
    return lerp(lerp(v00, v10, f.x), lerp(v01, v11, f.x), f.y);
}
float tapMask(float2 q) {
    q = clamp(q, 0.0, float2(modelW - 1, modelH - 1));
    const uint2 a = uint2(q);
    const uint2 b = min(a + 1, uint2(modelW - 1, modelH - 1));
    const float2 f = q - float2(a);
    return lerp(lerp(mask[a.y * padW + a.x], mask[a.y * padW + b.x], f.x), lerp(mask[b.y * padW + a.x], mask[b.y * padW + b.x], f.x), f.y);
}
// Normalized grid position -> full-resolution source pixel position.
float2 gridToSource(float2 g) {
    const float2 pad = float2(padW, padH);
    const float2 xm = alignCorners != 0 ? (g + 1.0) * 0.5 * (pad - 1.0) : ((g + 1.0) * pad - 1.0) * 0.5;
    return (xm + 0.5) * float2(srcW, srcH) / float2(modelW, modelH) - 0.5;
}
[numthreads(16, 16, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= srcW || id.y >= srcH) return;
    const float2 q = (float2(id.xy) + 0.5) * float2(modelW, modelH) / float2(srcW, srcH) - 0.5;
    const float3 a = encodeColor(tapFrame(previous, gridToSource(tapGrid(grid0, q))));
    const float3 b = encodeColor(tapFrame(current, gridToSource(tapGrid(grid1, q))));
    const float m = saturate(tapMask(q));
    float3 c = maskForFirst != 0 ? a * m + b * (1.0 - m) : a * (1.0 - m) + b * m;
    if (guardHi > 0.0) {
        const float w0 = maskForFirst != 0 ? m : 1.0 - m;
        const float3 apart = abs(a - b);
        const float ghost = min(w0, 1.0 - w0) * max(apart.r, max(apart.g, apart.b));
        const float share = smoothstep(guardLo, guardHi, ghost);
        const float w1 = w0 + ((guardPrevious != 0 ? 1.0 : 0.0) - w0) * share;
        c = a * w1 + b * (1.0 - w1);
        if ((id.x & 15) == 8 && (id.y & 15) == 8) {
            InterlockedAdd(guardStats[0], 1u);
            if (share >= 0.5) InterlockedAdd(guardStats[1], 1u);
        }
    }
    output[id.xy] = float4(decodeColor(c), 1.0);
}
