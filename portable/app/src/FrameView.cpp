// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "FrameView.h"

#include "NisNode.h"
#include "VideoFrames.h"

#include <QMutex>
#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGImageNode>
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGRendererInterface>
#include <QSGTexture>
#include <QScreen>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <array>

using namespace veyra::portable;

namespace pushfps {
namespace {

QMutex sharedLock;
std::shared_ptr<Engine> sharedEngine;
std::shared_ptr<Pick> fixedPick;

// Uniform block, std140 (frame.vert / frame.frag).
struct Uniforms {
    float matrix[16];
    float opacity;
    float phase;
    float mode;
    float hudOn;
    float frameSize[2];
    float fieldGrid[2];
    float blockSize;
    float errLow;
    float errHigh;
    float brightness;
    float contrast;
    float saturation;
    float gamma;
    float pad0;
    float holdZone[4];   // Crosshair Hold: x, y, size of its square in picture pixels; [3] > 0.5 = held
};
static_assert(sizeof(Uniforms) == 144, "std140 layout of frame.vert/frame.frag");
static_assert(offsetof(Uniforms, holdZone) == 128, "vec4 alignment");
static_assert(offsetof(Uniforms, frameSize) == 80, "vec2 alignment");

class FrameMaterial : public QSGMaterial {
public:
    FrameMaterial() { setFlag(Blending, false); }
    QSGMaterialType* type() const override { static QSGMaterialType t; return &t; }
    QSGMaterialShader* createShader(QSGRendererInterface::RenderMode) const override;
    int compare(const QSGMaterial* other) const override {
        // One node, one material: never batched with anything else.
        return this == other ? 0 : (this < other ? -1 : 1);
    }
    QSGTexture* textures[8] = {};
    Uniforms u{};
};

class FrameShader : public QSGMaterialShader {
public:
    FrameShader() {
        setShaderFileName(VertexStage, QStringLiteral(":/shaders/frame.vert.qsb"));
        setShaderFileName(FragmentStage, QStringLiteral(":/shaders/frame.frag.qsb"));
    }
    bool updateUniformData(RenderState& state, QSGMaterial* newMaterial, QSGMaterial*) override {
        auto* m = static_cast<FrameMaterial*>(newMaterial);
        QByteArray* buf = state.uniformData();
        if (buf->size() < int(sizeof(Uniforms))) return false;
        Uniforms u = m->u;
        std::memcpy(u.matrix, state.combinedMatrix().constData(), sizeof(u.matrix));
        u.opacity = state.opacity();
        std::memcpy(buf->data(), &u, sizeof(Uniforms));
        return true;
    }
    void updateSampledImage(RenderState& state, int binding, QSGTexture** texture, QSGMaterial* newMaterial, QSGMaterial*) override {
        auto* m = static_cast<FrameMaterial*>(newMaterial);
        if (binding < 1 || binding > 8) return;
        QSGTexture* t = m->textures[binding - 1];
        if (!t) return;
        t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
        *texture = t;
    }
};

QSGMaterialShader* FrameMaterial::createShader(QSGRendererInterface::RenderMode) const { return new FrameShader; }

class FrameNode : public QSGGeometryNode {
public:
    FrameNode() : geometry_(QSGGeometry::defaultAttributes_TexturedPoint2D(), 4) {
        setGeometry(&geometry_);
        setMaterial(&material_);
    }
    FrameMaterial material_;
    QSGGeometry geometry_;
};

// Motion field, two opaque texels per block: (x high, x low, 0) then (y high, y low, 0), each a 16-bit fixed
// point value (1/16 px, offset 32768). Alpha stays 255 because Qt Quick premultiplies image textures by
// alpha on upload; data in the alpha channel (an earlier layout) was destroyed by that.
QImage fieldImage(const MotionField& f) {
    QImage img(f.blocksX * 2, f.blocksY, QImage::Format_RGB32);
    auto enc = [](float v) { return int(std::clamp(std::lround((std::isfinite(v) ? v : 0.f) * 16.0f) + 32768L, 0L, 65535L)); };
    for (int j = 0; j < f.blocksY; ++j) {
        auto* row = reinterpret_cast<QRgb*>(img.scanLine(j));
        for (int i = 0; i < f.blocksX; ++i) {
            const size_t k = size_t(j) * size_t(f.blocksX) + size_t(i);
            const int x = enc(f.vx[k]), y = enc(f.vy[k]);
            row[i * 2 + 0] = qRgb(x >> 8, x & 255, 0);
            row[i * 2 + 1] = qRgb(y >> 8, y & 255, 0);
        }
    }
    return img;
}

// Wraps the engine's picture without copying; the QImage keeps the picture alive.
QImage frameImage(const std::shared_ptr<const Bgra>& b) {
    auto* keep = new std::shared_ptr<const Bgra>(b);
    return QImage(const_cast<uchar*>(b->px.data()), b->width, b->height, b->width * 4, QImage::Format_RGB32,
                  [](void* p) { delete static_cast<std::shared_ptr<const Bgra>*>(p); }, keep);
}

// Crosshair Hold: the square of one real frame as frame generation takes it. Wraps the pair's picture
// without copying; the QImage keeps the pair alive.
QImage holdImage(const std::shared_ptr<const HoldPair>& pair, bool second) {
    const Bgra& z = second ? pair->filledB : pair->filledA;
    auto* keep = new std::shared_ptr<const HoldPair>(pair);
    return QImage(const_cast<uchar*>(z.px.data()), z.width, z.height, z.width * 4, QImage::Format_RGB32,
                  [](void* p) { delete static_cast<std::shared_ptr<const HoldPair>*>(p); }, keep);
}

QImage maskImage(const Gray& g) {
    QImage img(g.width, g.height, QImage::Format_Grayscale8);
    for (int y = 0; y < g.height; ++y) std::memcpy(img.scanLine(y), g.px.data() + size_t(y) * size_t(g.width), size_t(g.width));
    return img;
}

}  // namespace

void FrameView::setEngine(std::shared_ptr<Engine> engine) {
    QMutexLocker lk(&sharedLock);
    sharedEngine = std::move(engine);
}

void FrameView::setFixedPick(std::shared_ptr<Pick> pick) {
    QMutexLocker lk(&sharedLock);
    fixedPick = std::move(pick);
}

FrameView::FrameView(QQuickItem* parent) : QQuickItem(parent) { setFlag(ItemHasContents, true); }

FrameView::~FrameView() = default;

void FrameView::setBrightness(qreal v) { if (v != brightness_) { brightness_ = v; emit pictureChanged(); update(); } }
void FrameView::setContrast(qreal v) { if (v != contrast_) { contrast_ = v; emit pictureChanged(); update(); } }
void FrameView::setSaturation(qreal v) { if (v != saturation_) { saturation_ = v; emit pictureChanged(); update(); } }
void FrameView::setGamma(qreal v) { if (v != gamma_) { gamma_ = v; emit pictureChanged(); update(); } }
void FrameView::setFill(int v) {
    v = std::clamp(v, 0, 2);
    if (v == fill_) return;
    fill_ = v;
    emit pictureChanged();
    update();
}
void FrameView::setUpscale(bool v) { if (v != upscale_) { upscale_ = v; emit pictureChanged(); update(); } }
QSize FrameView::upscaleTargetSize(int index) {
    switch (index) {
    case 1: return {2560, 1440};
    case 2: return {3840, 2160};
    case 4: return {5120, 2880};
    case 5: return {6144, 3456};
    }
    return {};
}

void FrameView::setUpscaleTarget(int v) {
    if (upscaleTargetSize(v).isEmpty()) v = 2;
    if (v != upscaleTarget_) { upscaleTarget_ = v; emit pictureChanged(); update(); }
}
void FrameView::setSharpness(qreal v) {
    v = std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.5;
    if (v != sharpness_) { sharpness_ = v; emit pictureChanged(); update(); }
}

void FrameView::reportUpscale(int state, const QString& sizes) {
    if (state == reportedState_ && sizes == reportedSizes_) return;
    reportedState_ = state;
    reportedSizes_ = sizes;
    QMetaObject::invokeMethod(this, [this, state, sizes] {
        if (state == upscaleState_ && sizes == upscaleSizes_) return;
        upscaleState_ = state;
        upscaleSizes_ = sizes;
        emit upscaleStateChanged();
    }, Qt::QueuedConnection);
}

void FrameView::setRunning(bool v) {
    if (v == running_) return;
    running_ = v;
    emit runningChanged();
    update();
}

void FrameView::itemChange(ItemChange change, const ItemChangeData& value) {
    if (change == ItemSceneChange) {
        disconnect(swapped_);
        if (value.window) {
            // Redraw every refresh while running: frameSwapped comes from the render thread, the update
            // must be asked for on the GUI thread.
            swapped_ = connect(value.window, &QQuickWindow::frameSwapped, this, [this] { if (running_) update(); }, Qt::QueuedConnection);
            update();
        }
    }
    QQuickItem::itemChange(change, value);
}

void FrameView::releaseResources() {
    // Called on the GUI thread when the window goes away; textures must die on the render thread.
    // The scene graph deletes the node; the textures are dropped on the next sync (or leak with the
    // window, which takes its graphics device with it).
    QQuickItem::releaseResources();
}

void FrameView::dropTextures() {
    for (auto& c : cache_) delete c.texture;
    cache_.clear();
    delete black_;
    black_ = nullptr;
}

QSGTexture* FrameView::cached(quint64 key, const std::function<QImage()>& make, bool nearest) {
    for (auto& c : cache_)
        if (c.key == key) { c.lastUse = frameCounter_; return c.texture; }
    const QImage image = make();
    if (image.isNull()) return nullptr;
    QSGTexture* t = window()->createTextureFromImage(image);
    if (!t) return nullptr;
    t->setFiltering(nearest ? QSGTexture::Nearest : QSGTexture::Linear);
    t->setMipmapFiltering(QSGTexture::None);
    t->setHorizontalWrapMode(QSGTexture::ClampToEdge);
    t->setVerticalWrapMode(QSGTexture::ClampToEdge);
    // Keep the newest few: two frames, their fields, mask and Crosshair Hold's three, and the next pair coming in.
    if (cache_.size() >= 18) {
        auto oldest = std::min_element(cache_.begin(), cache_.end(), [](const CachedTexture& a, const CachedTexture& b) { return a.lastUse < b.lastUse; });
        delete oldest->texture;
        cache_.erase(oldest);
    }
    cache_.push_back({key, t, frameCounter_});
    return t;
}

QSGNode* FrameView::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    ++frameCounter_;
    QQuickWindow* win = window();
    if (!win) return old;
    std::shared_ptr<Engine> engine;
    std::shared_ptr<Pick> fixed;
    {
        QMutexLocker lk(&sharedLock);
        engine = sharedEngine;
        fixed = fixedPick;
    }
    Pick pick;
    if (fixed) {
        pick = *fixed;
    } else if (engine) {
        // The picture drawn now appears at the next refresh.
        const double hz = win->screen() && win->screen()->refreshRate() > 1 ? win->screen()->refreshRate() : 60.0;
        pick = engine->pick(nowNs() + qint64(1e9 / hz));
    }
    if (!pick.valid() || width() <= 0 || height() <= 0) {
        delete old;
        if (!pick.valid()) dropTextures();
        reportUpscale(upscale_ ? UpscaleWaiting : UpscaleOff, {});
        return nullptr;
    }
    // The picture in the item: whole with black bars, filling it with the sides cut, or stretched.
    // source is the part of the picture drawn, as shares of it.
    const double fw = pick.a->width, fh = pick.a->height;
    QRectF rect, source(0, 0, 1, 1);
    if (fill_ == 2) {
        rect = QRectF(0, 0, width(), height());
    } else if (fill_ == 1) {
        rect = QRectF(0, 0, width(), height());
        const double scale = std::max(width() / fw, height() / fh);
        const double sw = std::clamp(width() / (fw * scale), 0.0, 1.0), sh = std::clamp(height() / (fh * scale), 0.0, 1.0);
        source = QRectF((1 - sw) / 2, (1 - sh) / 2, sw, sh);
    } else {
        const double scale = std::min(width() / fw, height() / fh);
        rect = QRectF((width() - fw * scale) / 2, (height() - fh * scale) / 2, fw * scale, fh * scale);
    }
    const quint64 keyA = pick.aId << 3, keyB = pick.bId << 3;

    const bool software = win->rendererInterface()->graphicsApi() == QSGRendererInterface::Software;
    if (software) {
        reportUpscale(upscale_ ? UpscaleUnsupported : UpscaleOff, {});
        // No shaders on the software renderer: the nearer real frame.
        auto* node = dynamic_cast<QSGImageNode*>(old);
        if (!node) { delete old; node = win->createImageNode(); }
        const bool useB = pick.generated() && pick.t >= 0.5f;
        QSGTexture* t = cached(useB ? keyB : keyA, [&] { return frameImage(useB ? pick.b : pick.a); }, false);
        if (!t) return node;
        node->setTexture(t);
        node->setOwnsTexture(false);
        node->setRect(rect);
        node->setSourceRect(QRectF(source.x() * fw, source.y() * fh, source.width() * fw, source.height() * fh));
        node->setFiltering(QSGTexture::Linear);
        node->markDirty(QSGNode::DirtyMaterial);
        return node;
    }

    if (!black_) {
        QImage b(1, 1, QImage::Format_RGBA8888);
        b.fill(Qt::black);
        black_ = win->createTextureFromImage(b);
    }
    QSGTexture* ta = cached(keyA, [&] { return frameImage(pick.a); }, false);
    QSGTexture* tb = pick.b != pick.a ? cached(keyB, [&] { return frameImage(pick.b); }, false) : ta;
    const bool generate = pick.generated() && ta && tb;
    QSGTexture *tf = black_, *tk = black_, *tm = black_;
    bool hud = false;
    if (generate) {
        const quint64 pairKey = ((pick.aId * 1000003ULL) ^ pick.bId) << 3;
        tf = cached(pairKey | 1, [&] { return fieldImage(pick.fields->forward); }, true);
        tk = cached(pairKey | 2, [&] { return fieldImage(pick.fields->backward); }, true);
        if (pick.fields->hudMask && !pick.fields->hudMask->empty()) {
            tm = cached(pairKey | 3, [&] { return maskImage(*pick.fields->hudMask); }, false);
            hud = tm != nullptr;
        }
        if (!tf || !tk) { tf = tk = black_; }
    }
    // Crosshair Hold's part of this pair, when a crosshair is held (CrosshairHold.h).
    QSGTexture *tha = black_, *thb = black_, *ths = black_;
    bool holding = false;
    if (generate && tf != black_ && pick.hold && pick.hold->size > 1 && pick.hold->filledA.width == pick.hold->size &&
        pick.hold->filledB.width == pick.hold->size && pick.hold->shows.width == pick.hold->size) {
        const quint64 pairKey = ((pick.aId * 1000003ULL) ^ pick.bId) << 3;
        tha = cached(pairKey | 4, [&] { return holdImage(pick.hold, false); }, false);
        thb = cached(pairKey | 5, [&] { return holdImage(pick.hold, true); }, false);
        ths = cached(pairKey | 6, [&] { return maskImage(pick.hold->shows); }, false);
        holding = tha && thb && ths;
        if (!holding) tha = thb = ths = black_;
    }
    const bool interpolating = generate && tf != black_ && tk != black_;
    const std::array<QSGTexture*, 8> textures{ta ? ta : black_, tb ? tb : black_, tf, tk, tm ? tm : black_, tha, thb, ths};
    Uniforms u{};
    u.phase = pick.t;
    u.mode = interpolating ? 1.f : 0.f;
    u.hudOn = interpolating && hud ? 1.f : 0.f;
    u.frameSize[0] = float(fw);
    u.frameSize[1] = float(fh);
    u.fieldGrid[0] = interpolating ? float(pick.fields->forward.blocksX) : 1.f;
    u.fieldGrid[1] = interpolating ? float(pick.fields->forward.blocksY) : 1.f;
    u.blockSize = interpolating ? float(pick.fields->forward.blockSize) : 16.f;
    u.errLow = 24.f;
    u.errHigh = 48.f;
    u.brightness = float(brightness_);
    u.contrast = float(contrast_);
    u.saturation = float(saturation_);
    u.gamma = float(gamma_);
    const bool held = interpolating && holding;
    u.holdZone[0] = held ? float(pick.hold->x) : 0.f;
    u.holdZone[1] = held ? float(pick.hold->y) : 0.f;
    u.holdZone[2] = held ? float(pick.hold->size) : 1.f;
    u.holdZone[3] = held ? 1.f : 0.f;

    // Upscaling: by how much the shown part of the picture is enlarged on screen, never more than NIS's
    // 2 per side nor past the size chosen (fitted to the picture's shape, as on Windows). What is left
    // above 2 the window scales as it always did.
    const qreal dpr = win->effectiveDevicePixelRatio();
    const double shown = std::min(rect.width() * dpr / (source.width() * fw), rect.height() * dpr / (source.height() * fh));
    const QSize target = upscaleTargetSize(upscaleTarget_);
    const double targetW = target.width(), targetH = target.height();
    const double scale = std::min({2.0, shown, std::min(targetW / fw, targetH / fh)});
    const QSize output(int(std::lround(fw * scale)), int(std::lround(fh * scale)));
    int state = UpscaleOff;
    if (upscale_) {
        if (auto* nis = dynamic_cast<NisNode*>(old); nis && nis->failed()) nisFailed_ = true;
        if (nisFailed_) state = UpscaleFailed;
        else if (!NisNode::supported(win)) state = UpscaleUnsupported;
        else if (scale < 1.05) state = UpscaleNotNeeded;  // a few per cent larger is not worth the work
        else state = UpscaleRunning;
    }
    reportUpscale(state, state == UpscaleRunning ? QStringLiteral("%1 × %2 → %3 × %4").arg(int(fw)).arg(int(fh)).arg(output.width()).arg(output.height())
                                                 : QString());

    if (state == UpscaleRunning) {
        auto* nis = dynamic_cast<NisNode*>(old);
        if (!nis) { delete old; nis = new NisNode(win); }
        NisFrame frame;
        frame.textures = textures;
        frame.nearest = {false, false, true, true, false, false, false, false};  // the two motion fields
        frame.uniforms = QByteArray(reinterpret_cast<const char*>(&u), sizeof(Uniforms));
        frame.picture = QSize(int(fw), int(fh));
        frame.output = output;
        frame.rect = rect;
        frame.source = source;
        frame.sharpness = float(sharpness_);
        nis->set(frame);
        return nis;
    }

    auto* node = dynamic_cast<FrameNode*>(old);
    if (!node) { delete old; node = new FrameNode; }
    FrameMaterial& m = node->material_;
    for (size_t i = 0; i < textures.size(); ++i) m.textures[i] = textures[i];
    m.u = u;
    QSGGeometry::updateTexturedRectGeometry(&node->geometry_, rect, source);
    node->markDirty(QSGNode::DirtyGeometry | QSGNode::DirtyMaterial);
    return node;
}

}  // namespace pushfps
