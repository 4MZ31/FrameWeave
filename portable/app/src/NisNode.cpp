// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "NisNode.h"

#include "../../../third_party/nis/NIS_Config.h"

#include <QFile>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>

#include <cstddef>
#include <cstring>

namespace pushfps {
namespace {

// frame.frag's block: the matrix first, the opacity after it (FrameView.cpp `Uniforms`).
constexpr int kMatrixBytes = 64;
constexpr int kOpacityOffset = 64;
// NISConfig as the shader reads it: 28 values of 4 bytes (nis.comp.in).
constexpr int kNisConfigBytes = 28 * 4;
static_assert(offsetof(NISConfig, reserved1) == kNisConfigBytes - 4, "NISConfig layout changed");
// blit.vert's block: matrix, opacity, then the part of the picture shown at 80 (vec4 alignment).
constexpr int kBlitBytes = 96;
constexpr int kBlitUvOffset = 80;
// NIS's coefficient tables: float4 rows of half floats, kFilterSize / 4 texels wide, one row per phase.
constexpr int kCoefWidth = int(kFilterSize / 4), kCoefHeight = int(kPhaseCount);
static_assert(sizeof(coef_scale_fp16) == size_t(kCoefWidth) * kCoefHeight * 8 && sizeof(coef_usm_fp16) == sizeof(coef_scale_fp16));
// One 256-thread group per 32 x 24 output block (nis.comp.in).
constexpr int kBlockWidth = 32, kBlockHeight = 24;

QRhi* rhiOf(QQuickWindow* window) {
    if (!window) return nullptr;
    QSGRendererInterface* ri = window->rendererInterface();
    return ri ? static_cast<QRhi*>(ri->getResource(window, QSGRendererInterface::RhiResource)) : nullptr;
}

QShader shaderAt(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QShader::fromSerialized(f.readAll()) : QShader();
}

// A unit square as a triangle strip: position and texture coordinate are the same (x, y), y down.
constexpr float kQuad[] = {0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 0, 1, 1, 1, 1};

template <typename T> void release(T*& resource) {
    if (resource) resource->deleteLater();
    resource = nullptr;
}

// A pipeline keeps the bindings it was made with as its layout: those go with it, unless they are the
// ones in use.
void releasePipeline(QRhiGraphicsPipeline*& pipeline, QRhiShaderResourceBindings* inUse) {
    if (pipeline && pipeline->shaderResourceBindings() && pipeline->shaderResourceBindings() != inUse)
        pipeline->shaderResourceBindings()->deleteLater();
    release(pipeline);
}

}  // namespace

NisNode::NisNode(QQuickWindow* window) : window_(window) {}

NisNode::~NisNode() { drop(); }

bool NisNode::supported(QQuickWindow* window) {
    QRhi* rhi = rhiOf(window);
    return rhi && rhi->isFeatureSupported(QRhi::Compute) && rhi->isTextureFormatSupported(QRhiTexture::RGBA16F);
}

void NisNode::set(const NisFrame& frame) {
    frame_ = frame;
    markDirty(QSGNode::DirtyMaterial);
}

QSGRenderNode::RenderingFlags NisNode::flags() const {
    // Only QRhi calls, inside the rectangle. Not depth-aware, so the scene graph draws it in order with
    // the items over it.
    return BoundedRectRendering | NoExternalRendering;
}

void NisNode::releaseResources() { drop(); }

void NisNode::drop() {
    release(quad_);
    release(frameUniforms_);
    release(nisUniforms_);
    release(blitUniforms_);
    release(linear_);
    release(nearest_);
    releasePipeline(framePipeline_, frameBindings_);
    release(nisPipeline_);
    release(blitPipeline_);
    release(frameBindings_);
    release(nisBindings_);
    release(blitBindings_);
    release(pictureTarget_);
    release(picturePass_);
    release(picture_);
    release(scaled_);
    release(coefScale_);
    release(coefUsm_);
    blitPass_ = nullptr;  // the window's own
    boundTextures_ = {};
    coefficientsUploaded_ = false;
    configuredPicture_ = configuredOutput_ = QSize();
}

// Makes what is missing or no longer fits. False when something cannot be made.
bool NisNode::ensure(QRhi* rhi) {
    const auto made = [](auto* r) { return r && r->create(); };

    if (!quad_) {
        quad_ = rhi->newBuffer(QRhiBuffer::Immutable, QRhiBuffer::VertexBuffer, sizeof(kQuad));
        if (!made(quad_)) return false;
        coefficientsUploaded_ = false;  // the quad is uploaded with the coefficients
    }
    if (!frameUniforms_) {
        frameUniforms_ = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, quint32(frame_.uniforms.size()));
        nisUniforms_ = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kNisConfigBytes);
        blitUniforms_ = rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kBlitBytes);
        if (!made(frameUniforms_) || !made(nisUniforms_) || !made(blitUniforms_)) return false;
    }
    if (!linear_) {
        linear_ = rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        nearest_ = rhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest, QRhiSampler::None, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        if (!made(linear_) || !made(nearest_)) return false;
    }
    if (!coefScale_) {
        coefScale_ = rhi->newTexture(QRhiTexture::RGBA16F, QSize(kCoefWidth, kCoefHeight));
        coefUsm_ = rhi->newTexture(QRhiTexture::RGBA16F, QSize(kCoefWidth, kCoefHeight));
        if (!made(coefScale_) || !made(coefUsm_)) return false;
        coefficientsUploaded_ = false;
    }

    // Step 1's texture, its render target and the pipeline that draws into it.
    if (!picture_ || configuredPicture_ != frame_.picture) {
        releasePipeline(framePipeline_, frameBindings_);
        release(frameBindings_);
        release(pictureTarget_);
        release(picturePass_);
        release(picture_);
        release(nisBindings_);
        boundTextures_ = {};
        picture_ = rhi->newTexture(QRhiTexture::RGBA8, frame_.picture, 1, QRhiTexture::RenderTarget);
        if (!made(picture_)) return false;
        pictureTarget_ = rhi->newTextureRenderTarget({picture_});
        picturePass_ = pictureTarget_->newCompatibleRenderPassDescriptor();
        pictureTarget_->setRenderPassDescriptor(picturePass_);
        if (!pictureTarget_->create()) return false;
        configuredPicture_ = frame_.picture;
    }
    // Step 2's texture.
    if (!scaled_ || configuredOutput_ != frame_.output) {
        release(nisBindings_);
        release(blitBindings_);
        release(scaled_);
        scaled_ = rhi->newTexture(QRhiTexture::RGBA8, frame_.output, 1, QRhiTexture::UsedWithLoadStore);
        if (!made(scaled_)) return false;
        configuredOutput_ = frame_.output;
    }

    // The textures frame.frag samples change with every new real frame: new bindings for them.
    std::array<QRhiTexture*, 11> now{};
    for (size_t i = 0; i < now.size(); ++i) {
        now[i] = frame_.textures[i] ? frame_.textures[i]->rhiTexture() : nullptr;
        if (!now[i]) return false;
    }
    if (!frameBindings_ || now != boundTextures_) {
        QRhiShaderResourceBindings* bindings = rhi->newShaderResourceBindings();
        std::vector<QRhiShaderResourceBinding> list;
        list.push_back(QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, frameUniforms_));
        for (size_t i = 0; i < now.size(); ++i)
            list.push_back(QRhiShaderResourceBinding::sampledTexture(int(i) + 1, QRhiShaderResourceBinding::FragmentStage, now[i],
                                                                     frame_.nearest[i] ? nearest_ : linear_));
        bindings->setBindings(list.cbegin(), list.cend());
        if (!bindings->create()) { delete bindings; return false; }
        // The pipeline keeps the bindings it was made with as its layout; later ones only need the same layout.
        if (frameBindings_ && (!framePipeline_ || frameBindings_ != framePipeline_->shaderResourceBindings())) frameBindings_->deleteLater();
        frameBindings_ = bindings;
        boundTextures_ = now;
    }
    if (!framePipeline_) {
        const QShader vs = shaderAt(QStringLiteral(":/shaders/frame.vert.qsb")), fs = shaderAt(QStringLiteral(":/shaders/frame.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) return false;
        framePipeline_ = rhi->newGraphicsPipeline();
        framePipeline_->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{4 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
        framePipeline_->setVertexInputLayout(layout);
        framePipeline_->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        framePipeline_->setShaderResourceBindings(frameBindings_);
        framePipeline_->setRenderPassDescriptor(picturePass_);
        if (!framePipeline_->create()) return false;
    }

    // Step 2: NIS.
    if (!nisBindings_) {
        nisBindings_ = rhi->newShaderResourceBindings();
        const auto stage = QRhiShaderResourceBinding::ComputeStage;
        nisBindings_->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, stage, nisUniforms_),
            QRhiShaderResourceBinding::sampler(1, stage, linear_),
            QRhiShaderResourceBinding::texture(2, stage, picture_),
            QRhiShaderResourceBinding::texture(3, stage, coefScale_),
            QRhiShaderResourceBinding::texture(4, stage, coefUsm_),
            QRhiShaderResourceBinding::imageStore(5, stage, scaled_, 0),
        });
        if (!nisBindings_->create()) return false;
        release(nisPipeline_);
    }
    if (!nisPipeline_) {
        const QShader cs = shaderAt(QStringLiteral(":/shaders/nis.comp.qsb"));
        if (!cs.isValid()) return false;
        nisPipeline_ = rhi->newComputePipeline();
        nisPipeline_->setShaderStage({QRhiShaderStage::Compute, cs});
        nisPipeline_->setShaderResourceBindings(nisBindings_);
        if (!nisPipeline_->create()) return false;
    }

    // Step 3: into the window, with the window's own render pass.
    if (!blitBindings_) {
        blitBindings_ = rhi->newShaderResourceBindings();
        blitBindings_->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, blitUniforms_),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, scaled_, linear_),
        });
        if (!blitBindings_->create()) return false;
        release(blitPipeline_);
    }
    QRhiRenderTarget* windowTarget = renderTarget();
    QRhiRenderPassDescriptor* windowPass = windowTarget ? windowTarget->renderPassDescriptor() : nullptr;
    if (!windowPass) return false;
    if (blitPipeline_ && (!blitPass_ || !windowPass->isCompatible(blitPass_))) release(blitPipeline_);
    if (!blitPipeline_) {
        const QShader vs = shaderAt(QStringLiteral(":/shaders/blit.vert.qsb")), fs = shaderAt(QStringLiteral(":/shaders/blit.frag.qsb"));
        if (!vs.isValid() || !fs.isValid()) return false;
        blitPipeline_ = rhi->newGraphicsPipeline();
        blitPipeline_->setShaderStages({{QRhiShaderStage::Vertex, vs}, {QRhiShaderStage::Fragment, fs}});
        QRhiVertexInputLayout layout;
        layout.setBindings({{4 * sizeof(float)}});
        layout.setAttributes({{0, 0, QRhiVertexInputAttribute::Float2, 0}, {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}});
        blitPipeline_->setVertexInputLayout(layout);
        blitPipeline_->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        QRhiGraphicsPipeline::TargetBlend blend;
        blend.enable = true;  // premultiplied, as Qt Quick blends (the item's opacity)
        blend.srcColor = QRhiGraphicsPipeline::One;
        blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blend.srcAlpha = QRhiGraphicsPipeline::One;
        blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
        blitPipeline_->setTargetBlends({blend});
        blitPipeline_->setFlags(QRhiGraphicsPipeline::UsesScissor);
        blitPipeline_->setSampleCount(windowTarget->sampleCount());
        blitPipeline_->setShaderResourceBindings(blitBindings_);
        blitPipeline_->setRenderPassDescriptor(windowPass);
        if (!blitPipeline_->create()) return false;
        blitPass_ = windowPass;
    }
    return true;
}

void NisNode::prepare() {
    QRhi* rhi = rhiOf(window_);
    QRhiCommandBuffer* cb = commandBuffer();
    if (failed_ || !rhi || !cb || frame_.picture.isEmpty() || frame_.output.isEmpty() || frame_.uniforms.size() < kOpacityOffset + 4) return;

    QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
    // The textures FrameView made from the engine's pictures are uploaded here, as its material does.
    for (QSGTexture* t : frame_.textures)
        if (t) t->commitTextureOperations(rhi, updates);
    if (!ensure(rhi)) {
        failed_ = true;
        updates->release();
        return;
    }
    if (!coefficientsUploaded_) {
        updates->uploadStaticBuffer(quad_, kQuad);
        const auto table = [](const uint16_t (&rows)[kPhaseCount][kFilterSize]) {
            return QRhiTextureUploadDescription({0, 0, QRhiTextureSubresourceUploadDescription(QByteArray(reinterpret_cast<const char*>(rows), sizeof(rows)))});
        };
        updates->uploadTexture(coefScale_, table(coef_scale_fp16));
        updates->uploadTexture(coefUsm_, table(coef_usm_fp16));
        coefficientsUploaded_ = true;
    }

    // Step 1: the picture at its own size, its top row in the texture's first row on every API.
    QMatrix4x4 toPicture = rhi->clipSpaceCorrMatrix();
    if (rhi->isYUpInFramebuffer()) toPicture.ortho(0, 1, 0, 1, -1, 1);
    else toPicture.ortho(0, 1, 1, 0, -1, 1);
    QByteArray block = frame_.uniforms;
    std::memcpy(block.data(), toPicture.constData(), kMatrixBytes);
    const float one = 1.0f;
    std::memcpy(block.data() + kOpacityOffset, &one, sizeof(float));
    updates->updateDynamicBuffer(frameUniforms_, 0, quint32(block.size()), block.constData());

    // Step 2's settings: the whole picture in, the whole texture out.
    const int pw = frame_.picture.width(), ph = frame_.picture.height();
    const int ow = frame_.output.width(), oh = frame_.output.height();
    NISConfig config{};
    if (!NVScalerUpdateConfig(config, frame_.sharpness, 0, 0, uint32_t(pw), uint32_t(ph), uint32_t(pw), uint32_t(ph), 0, 0,
                              uint32_t(ow), uint32_t(oh), uint32_t(ow), uint32_t(oh), NISHDRMode::None)) {
        failed_ = true;
        updates->release();
        return;
    }
    updates->updateDynamicBuffer(nisUniforms_, 0, kNisConfigBytes, &config);

    // Step 3's place: the unit square onto the picture's rectangle in the item, the shown part sampled.
    QMatrix4x4 toWindow;
    if (const QMatrix4x4* p = projectionMatrix()) toWindow = *p;
    if (const QMatrix4x4* m = matrix()) toWindow *= *m;
    toWindow.translate(float(frame_.rect.x()), float(frame_.rect.y()));
    toWindow.scale(float(frame_.rect.width()), float(frame_.rect.height()));
    char blit[kBlitBytes] = {};
    std::memcpy(blit, toWindow.constData(), kMatrixBytes);
    const float opacity = float(inheritedOpacity());
    std::memcpy(blit + kOpacityOffset, &opacity, sizeof(float));
    const float uv[4] = {float(frame_.source.x()), float(frame_.source.y()), float(frame_.source.width()), float(frame_.source.height())};
    std::memcpy(blit + kBlitUvOffset, uv, sizeof(uv));
    updates->updateDynamicBuffer(blitUniforms_, 0, kBlitBytes, blit);

    const QRhiCommandBuffer::VertexInput quad(quad_, 0);
    cb->beginPass(pictureTarget_, Qt::black, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(framePipeline_);
    cb->setViewport(QRhiViewport(0, 0, float(pw), float(ph)));
    cb->setShaderResources(frameBindings_);
    cb->setVertexInput(0, 1, &quad);
    cb->draw(4);
    cb->endPass();

    cb->beginComputePass();
    cb->setComputePipeline(nisPipeline_);
    cb->setShaderResources(nisBindings_);
    cb->dispatch((ow + kBlockWidth - 1) / kBlockWidth, (oh + kBlockHeight - 1) / kBlockHeight, 1);
    cb->endComputePass();
}

void NisNode::render(const RenderState* state) {
    QRhiCommandBuffer* cb = commandBuffer();
    QRhiRenderTarget* target = renderTarget();
    if (failed_ || !cb || !target || !blitPipeline_ || !blitBindings_ || !quad_) return;
    const QSize size = target->pixelSize();
    cb->setGraphicsPipeline(blitPipeline_);
    cb->setViewport(QRhiViewport(0, 0, float(size.width()), float(size.height())));
    if (state && state->scissorEnabled()) {
        const QRect s = state->scissorRect();
        cb->setScissor(QRhiScissor(s.x(), s.y(), s.width(), s.height()));
    } else {
        cb->setScissor(QRhiScissor(0, 0, size.width(), size.height()));
    }
    cb->setShaderResources(blitBindings_);
    const QRhiCommandBuffer::VertexInput quad(quad_, 0);
    cb->setVertexInput(0, 1, &quad);
    cb->draw(4);
}

}  // namespace pushfps
