#version 440
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 uvRect;
};

layout(binding = 1) uniform sampler2D picture;

void main() {
    // Premultiplied, as Qt Quick blends.
    fragColor = vec4(texture(picture, vTex).rgb, 1.0) * qt_Opacity;
}
