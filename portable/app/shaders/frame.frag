#version 440
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// PushFPS frame generation on the GPU (Linux / macOS). Follows portable/core/src/Interpolate.cpp line by
// line; the CI GPU check compares the two.
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float phase;       // 0 = frame A, 1 = frame B
    float mode;        // 0 = show A as it is, 1 = generate between A and B
    float hudOn;       // Live HUD Lock mask present
    vec2 frameSize;    // picture size in pixels
    vec2 fieldGrid;    // motion field blocks (x, y)
    float blockSize;   // pixels per block
    float errLow;
    float errHigh;
    float brightness;
    float contrast;
    float saturation;
    float gamma;
    float pad0;
    vec4 holdZone;     // Crosshair Hold: x, y and size of its square in picture pixels; w > 0.5 = a crosshair is held
};

layout(binding = 1) uniform sampler2D texA;
layout(binding = 2) uniform sampler2D texB;
layout(binding = 3) uniform sampler2D fieldF;   // A -> B: per block two texels (x hi, x lo), (y hi, y lo), 1/16 px
layout(binding = 4) uniform sampler2D fieldK;   // B -> A
layout(binding = 5) uniform sampler2D hudMask;
// Crosshair Hold (portable/core CrosshairHold.h, Interpolate.cpp): the square of each real frame with the
// crosshair filled in, which is what frame generation samples, and per pixel how much of the background's
// change shows through a held pixel (1 where nothing is held).
layout(binding = 6) uniform sampler2D holdA;
layout(binding = 7) uniform sampler2D holdB;
layout(binding = 8) uniform sampler2D holdShows;

vec2 fetchVector(sampler2D f, ivec2 ij) {
    vec4 x = floor(texelFetch(f, ivec2(ij.x * 2, ij.y), 0) * 255.0 + 0.5);
    vec4 y = floor(texelFetch(f, ivec2(ij.x * 2 + 1, ij.y), 0) * 255.0 + 0.5);
    return vec2(x.r * 256.0 + x.g - 32768.0, y.r * 256.0 + y.g - 32768.0) / 16.0;
}

// Bilinear between block centres, clamped (MotionField::sample).
vec2 vectorAt(sampler2D f, vec2 p) {
    vec2 g = clamp(p / blockSize - 0.5, vec2(0.0), fieldGrid - 1.0);
    ivec2 i0 = ivec2(g);
    ivec2 last = ivec2(fieldGrid) - 1;
    ivec2 i1 = min(i0 + 1, last);
    vec2 fr = g - vec2(i0);
    vec2 a = fetchVector(f, i0);
    vec2 b = fetchVector(f, ivec2(i1.x, i0.y));
    vec2 c = fetchVector(f, ivec2(i0.x, i1.y));
    vec2 d = fetchVector(f, i1);
    return mix(mix(a, b, fr.x), mix(c, d, fr.x), fr.y);
}

// Bilinear colour at pixel coordinates (centres at +0.5), 0..255 like the CPU reference.
vec3 sampleAt(sampler2D t, vec2 p) {
    return texture(t, p / frameSize).rgb * 255.0;
}

// Do the four pixels of a bilinear sample at q all lie in the square?
bool inHold(vec2 q) {
    vec2 z = q - 0.5 - holdZone.xy;
    return z.x >= 0.0 && z.y >= 0.0 && z.x <= holdZone.z - 1.0 && z.y <= holdZone.z - 1.0;
}
// A real frame as frame generation takes it: the filled square inside it, the frame itself everywhere else.
// (Explicit level 0 and a select, no branch: the choice differs from pixel to pixel.)
vec3 takeFrom(sampler2D frame, sampler2D filled, vec2 q) {
    vec3 own = sampleAt(frame, q);
    if (holdZone.w < 0.5) return own;
    vec3 given = textureLod(filled, (q - holdZone.xy) / holdZone.z, 0.0).rgb * 255.0;
    return inHold(q) ? given : own;
}

void main() {
    vec2 p = vTex * frameSize;
    vec3 color;
    if (mode < 0.5) {
        color = sampleAt(texA, p);
    } else {
        float t = phase;
        vec2 vf = vectorAt(fieldF, p);
        vec2 vk = -vectorAt(fieldK, p);
        vec3 a1 = takeFrom(texA, holdA, p - t * vf);
        vec3 b1 = takeFrom(texB, holdB, p + (1.0 - t) * vf);
        vec3 a2 = takeFrom(texA, holdA, p - t * vk);
        vec3 b2 = takeFrom(texB, holdB, p + (1.0 - t) * vk);
        float e1 = dot(abs(a1 - b1), vec3(1.0 / 3.0));
        float e2 = dot(abs(a2 - b2), vec3(1.0 / 3.0));
        float w1 = 1.0 / ((e1 + 4.0) * (e1 + 4.0));
        float w2 = 1.0 / ((e2 + 4.0) * (e2 + 4.0));
        vec3 c = (mix(a1, b1, t) * w1 + mix(a2, b2, t) * w2) / (w1 + w2);
        float occlusion = smoothstep(errLow, errHigh, min(e1, e2));
        bool firstBetter = e1 <= e2;
        vec3 nearSample = t < 0.5 ? (firstBetter ? a1 : a2) : (firstBetter ? b1 : b2);
        c = mix(c, nearSample, occlusion);
        if (holdZone.w > 0.5) {
            // A held pixel: the real frames' own pixel, plus the share of the background's change that shows
            // through it. Where nothing is held the two real pixels are the given ones and the share is 1,
            // so the statement leaves the generated pixel as it is.
            vec2 uv = (p - holdZone.xy) / holdZone.z;
            vec3 given = mix(textureLod(holdA, uv, 0.0).rgb, textureLod(holdB, uv, 0.0).rgb, t) * 255.0;
            float shows = textureLod(holdShows, uv, 0.0).r;
            vec3 real = mix(sampleAt(texA, p), sampleAt(texB, p), t);
            vec3 heldPixel = real + shows * (c - given);
            c = inHold(p) ? heldPixel : c;
        }
        if (hudOn > 0.5) {
            float lockWeight = texture(hudMask, vTex).r;
            vec3 held = t < 0.5 ? sampleAt(texA, p) : sampleAt(texB, p);
            c = mix(c, held, lockWeight);
        }
        color = c;
    }
    color /= 255.0;
    // Picture: brightness, contrast, saturation, gamma (neutral: 0, 1, 1, 1).
    color = (color - 0.5) * contrast + 0.5 + brightness;
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, saturation);
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / max(gamma, 0.1)));
    fragColor = vec4(color, 1.0) * qt_Opacity;
}
