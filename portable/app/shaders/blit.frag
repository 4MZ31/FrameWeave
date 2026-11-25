#version 440
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;

// Must match blit.vert and NisNode.cpp (std140, 128 bytes).
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 uvRect;
    vec4 compare;      // as frame.frag's: x > 0.5 = on, y .. z the original's part (picture u), w > 0.5 = the line
    vec4 compareLine;  // dark edges x .. y, white core z .. w (picture u)
};

layout(binding = 1) uniform sampler2D picture;
// The live comparison: the picture as frame.frag drew it before upscaling, where the original is shown
// (frame.frag already left the picture controls out there), so that side has no NIS either.
layout(binding = 2) uniform sampler2D before;

void main() {
    bool original = compare.x > 0.5 && vTex.x >= compare.y && vTex.x < compare.z;
    vec3 color = original ? texture(before, vTex).rgb : texture(picture, vTex).rgb;
    if (compare.w > 0.5 && vTex.x >= compareLine.x && vTex.x < compareLine.y)
        color = vTex.x >= compareLine.z && vTex.x < compareLine.w ? vec3(1.0) : vec3(0.0);
    // Premultiplied, as Qt Quick blends.
    fragColor = vec4(color, 1.0) * qt_Opacity;
}
