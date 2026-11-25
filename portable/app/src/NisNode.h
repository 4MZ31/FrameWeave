// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#pragma once
// The picture upscaled with NVIDIA Image Scaling (NIS 1.0.3, third_party/nis, MIT), the upscaler the
// Windows build offers as super resolution choice 6. FrameView uses this node in place of its usual one
// while upscaling is on and the picture is shown larger than it is. Per refresh, through Qt's renderer:
//   1. the picture as frame.frag makes it (interpolation, colour, Crosshair Hold), at its own size, into
//      a texture of ours;
//   2. NIS (shaders/nis.comp.in) from that texture into one up to twice as large per side;
//   3. that drawn into the window, as FrameView would have drawn the picture (shaders/blit.*).
// Step 2 is a compute shader: it needs Vulkan, Metal, Direct3D or OpenGL 4.3 (`supported`). A larger
// factor than NIS's 2 is finished by the window's own bilinear scaling in step 3, as on Windows.
#include <QByteArray>
#include <QRectF>
#include <QSGRenderNode>
#include <QSize>

#include <array>

class QQuickWindow;
class QRhi;
class QRhiBuffer;
class QRhiComputePipeline;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;
class QSGTexture;

namespace pushfps {

// What FrameView hands the node for one refresh.
struct NisFrame {
    std::array<QSGTexture*, 11> textures{};  // frame.frag's bindings 1..11
    std::array<bool, 11> nearest{};          // sampled without filtering (the motion fields)
    QByteArray uniforms;                    // frame.frag's uniform block; matrix and opacity are set here
    QSize picture;                          // the picture's size in pixels
    QSize output;                           // NIS's output: between the picture's size and twice it
    QRectF rect;                            // where the picture stands in the item
    QRectF source;                          // the part of the picture shown, as shares of it
    float sharpness = 0.5f;                 // 0 = none, 1 = strongest (NVScalerUpdateConfig)
    // The live comparison, drawn by the step into the window (blit.frag): on, the original's part, the line.
    std::array<float, 4> compare{};
    std::array<float, 4> compareLine{};
};

class NisNode : public QSGRenderNode {
public:
    explicit NisNode(QQuickWindow* window);
    ~NisNode() override;

    // Whether this window's renderer can run the compute step.
    static bool supported(QQuickWindow* window);

    void set(const NisFrame& frame);
    // Something could not be made (a shader, a texture, a pipeline): FrameView draws without upscaling.
    bool failed() const { return failed_; }

    void prepare() override;
    void render(const RenderState* state) override;
    void releaseResources() override;
    RenderingFlags flags() const override;
    QRectF rect() const override { return frame_.rect; }

private:
    bool ensure(QRhi* rhi);
    void drop();

    QQuickWindow* window_ = nullptr;
    NisFrame frame_;
    bool failed_ = false;
    bool coefficientsUploaded_ = false;
    QSize configuredPicture_, configuredOutput_;

    QRhiBuffer* quad_ = nullptr;
    QRhiBuffer* frameUniforms_ = nullptr;
    QRhiBuffer* nisUniforms_ = nullptr;
    QRhiBuffer* blitUniforms_ = nullptr;
    QRhiSampler* linear_ = nullptr;
    QRhiSampler* nearest_ = nullptr;
    QRhiTexture* picture_ = nullptr;   // step 1's result, the picture's size
    QRhiTexture* scaled_ = nullptr;    // step 2's result
    QRhiTexture* coefScale_ = nullptr;
    QRhiTexture* coefUsm_ = nullptr;
    QRhiTextureRenderTarget* pictureTarget_ = nullptr;
    QRhiRenderPassDescriptor* picturePass_ = nullptr;
    QRhiShaderResourceBindings* frameBindings_ = nullptr;
    QRhiShaderResourceBindings* nisBindings_ = nullptr;
    QRhiShaderResourceBindings* blitBindings_ = nullptr;
    QRhiGraphicsPipeline* framePipeline_ = nullptr;
    QRhiComputePipeline* nisPipeline_ = nullptr;
    QRhiGraphicsPipeline* blitPipeline_ = nullptr;
    QRhiRenderPassDescriptor* blitPass_ = nullptr;  // the window's, as the blit pipeline was made for it
    std::array<QRhiTexture*, 11> boundTextures_{};
};

}  // namespace pushfps
