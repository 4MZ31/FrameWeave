#version 440
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// The upscaled picture (NisNode.cpp) drawn into the window: a unit square placed by qt_Matrix, sampling
// the part of the picture that is shown (uvRect: x, y, width, height as shares of it).
layout(location = 0) in vec4 qt_VertexPosition;
layout(location = 1) in vec2 qt_VertexTexCoord;
layout(location = 0) out vec2 vTex;

// Must match blit.frag and NisNode.cpp (std140, 96 bytes).
layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 uvRect;
};

void main() {
    vTex = uvRect.xy + qt_VertexTexCoord * uvRect.zw;
    gl_Position = qt_Matrix * qt_VertexPosition;
}
