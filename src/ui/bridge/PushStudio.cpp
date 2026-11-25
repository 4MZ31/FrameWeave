// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// QML bridge: the PushFPS streaming mode (qml/Veyra/StudioPage.qml) audio mixer.
//
// The mix itself (game fader and mute, microphone filters, fader and mute) is kept in preferences
// (mixGameDb, mixGameMuted, mixMicDb, mixMicMuted, micHighPass, micEqOn, micEq, micGate, micGateDb,
// micCompressor; validated in Preferences.cpp) and applied process-wide by applyPushMix
// (engine/PushAudioMix.h). While the streaming mode is on screen, pushSetStudioMeters keeps the
// game sound tap and (when the microphone is used for recordings or streams) the microphone open
// so their meters move before anything records, as in OBS; the page turns them off when it leaves.
#include "../QmlPlayerBridge_p.h"
#include <windows.h>
#include "veyra/engine/MicrophoneCapture.h"
#include "veyra/engine/PushAudioMix.h"
#include "veyra/engine/PushAuxAudio.h"
#include "veyra/engine/PushLayerSources.h"
#include "veyra/engine/PushStreamOverlay.h"
#include "veyra/source/ScreenCaptureSource.h"
#include "veyra/Brand.h"

#include <QBuffer>
#include <QByteArray>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QPainter>
#include <QPainterPath>
#include "veyra/sink/RecordingAudioTap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>

namespace veyra::ui {
namespace {

// The game sound for the meter: what the recorder would take, after the game fader.
struct GameMeterTap final : sink::RecordingAudioSink {
    void renderedPcm(const float* pcm, size_t frames, const sink::AudioFormat& format, int64_t, const void*) override {
        if (format.valid()) engine::pushGameMeter().feed(pcm, frames, format.channels, engine::pushGameGain());
    }
};
// Keeps the microphone open for its meter; the capture thread meters every packet itself.
struct MicMeterSink final : engine::MicrophoneSink {
    void microphonePcm(const float*, size_t, int64_t) override {}
};
std::shared_ptr<GameMeterTap> gameTap;
std::shared_ptr<MicMeterSink> micTap;

double level(float db) { return std::isfinite(db) ? double(db) : -100.0; }

QFont sceneFont(int pixels, int weight, bool italic = false) {
    QFont font(QStringLiteral("Bahnschrift"));
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(std::max(8, pixels));
    font.setWeight(QFont::Weight(weight));
    font.setItalic(italic);
    return font;
}
// Straight-alpha RGBA bytes of a painted picture (RecordingLabel layout).
engine::PushOverlayImage imageOf(const QImage& painted) {
    const QImage image = painted.convertToFormat(QImage::Format_RGBA8888);
    engine::PushOverlayImage out;
    out.width = uint32_t(image.width()); out.height = uint32_t(image.height());
    out.rgba.resize(size_t(out.width) * out.height * 4);
    for (int y = 0; y < image.height(); ++y)
        std::memcpy(&out.rgba[size_t(y) * out.width * 4], image.constScanLine(y), size_t(out.width) * 4);
    return out;
}
// The FrameWeave wordmark: Frame in white, Weave in logo green.
void drawWordmark(QPainter& painter, const QPointF& rightBaseline, int pixels) {
    const QFont font = sceneFont(pixels, QFont::Bold, true);
    const QFontMetricsF m(font);
    const QString head = QStringLiteral("Frame"), tail = QStringLiteral("Weave");
    const qreal width = m.horizontalAdvance(head) + m.horizontalAdvance(tail);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255, 230));
    painter.drawText(QPointF(rightBaseline.x() - width, rightBaseline.y()), head);
    painter.setPen(QColor(141, 242, 55));
    painter.drawText(QPointF(rightBaseline.x() - m.horizontalAdvance(tail), rightBaseline.y()), tail);
}
// Draws `image` to cover `area` (cropped to keep its shape).
void drawCover(QPainter& painter, const QImage& image, const QRectF& area) {
    if (image.isNull()) return;
    const qreal scale = std::max(area.width() / image.width(), area.height() / image.height());
    const QSizeF size(image.width() * scale, image.height() * scale);
    painter.drawImage(QRectF(area.center() - QPointF(size.width() / 2, size.height() / 2), size), image);
}

} // namespace

QString QmlPlayerBridge::pushPickImage() {
    return QFileDialog::getOpenFileName(nullptr, tr("选择图片"), QString(), tr("图片 (*.png *.jpg *.jpeg *.webp *.bmp)"));
}

void QmlPlayerBridge::pushSetCardMute(bool muted) {
    if (engine::pushCardMute() == muted) return;
    engine::setPushCardMute(muted);
    veyra::log::info("push-studio", std::format("card mute {}", muted ? "on" : "off"));
}

// A recording that starts while Streaming mode's state would surprise: said once, at the start.
// The scene and the mixer belong to Streaming mode but hold in every mode, and outside that page
// nothing shows them. Until 3.2.3 nothing said it either (a clip was simply silent).
void QmlPlayerBridge::pushRecordingNotices(bool sound, bool microphone, bool comparisonClip) {
    const bool studioPage = impl_->currentPage == QLatin1String("live");
    const auto mix = engine::pushMix();
    if (microphone && engine::mixGain(mix.micDb, mix.micMuted) <= 0.0f) {
        emit notice(tr("混音器里的麦克风已静音：这次录制不会有麦克风声音（直播模式 → 音频混音器）"), true);
        veyra::log::info("push-studio", "recording started with the microphone muted in the mixer");
    }
    if (sound && engine::pushGameFaderGain() <= 0.0f) {
        emit notice(tr("混音器里的游戏声音已静音：这次录制不会有游戏声音（直播模式 → 音频混音器）"), true);
        veyra::log::info("push-studio", "recording started with the game muted in the mixer");
    } else if (!comparisonClip && !studioPage && !impl_->pushCardScene.isEmpty()) {
        // On the Streaming mode page the card is in view; a comparison clip never shows it.
        emit notice(tr("当前场景是图卡“%1”：录制里会是这张图卡，而不是游戏画面（直播模式 → 场景）").arg(impl_->pushCardScene), true);
        veyra::log::info("push-studio", "recording started outside streaming mode with a card scene shown");
    }
}

bool QmlPlayerBridge::pushSetStreamScene(const QVariantMap& scene) {
    const bool card = scene.value("type").toString() == QLatin1String("card");
    impl_->pushCardScene = card ? scene.value("name").toString().left(40) : QString();
    if (card && impl_->pushCardScene.isEmpty()) impl_->pushCardScene = scene.value("title").toString().left(40);
    if (card && impl_->pushCardScene.isEmpty()) impl_->pushCardScene = QStringLiteral("card");
    const QString overlayPath = scene.value("overlayImage").toString(), overlayText = scene.value("overlayText").toString().left(80);
    if (!card && overlayPath.isEmpty() && overlayText.trimmed().isEmpty()) {
        engine::setPushStreamOverlay(nullptr, uint32_t(impl_->prefInt("studioTransitionMs", 300)));
        veyra::log::info("push-studio", "scene game (no overlay)");
        return true;
    }
    // The size recordings and streams are made at: the presented picture.
    const auto out = impl_->snapshot.metrics.resolution.output;
    const int W = out.width >= 64 && out.height >= 64 ? int(out.width) & ~1 : 1920;
    const int H = out.width >= 64 && out.height >= 64 ? int(out.height) & ~1 : 1080;
    QImage canvas(W, H, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    if (card) {
        // Background: the chosen picture, or the PushFPS graphite with a green glow.
        const QImage background(scene.value("image").toString());
        if (!background.isNull()) {
            drawCover(painter, background, QRectF(0, 0, W, H));
            QLinearGradient shade(0, 0, 0, H);
            shade.setColorAt(0.0, QColor(10, 12, 15, 90));
            shade.setColorAt(1.0, QColor(10, 12, 15, 200));
            painter.fillRect(QRectF(0, 0, W, H), shade);
        } else {
            QLinearGradient base(0, 0, W, H);
            base.setColorAt(0.0, QColor(22, 26, 32));
            base.setColorAt(1.0, QColor(10, 12, 15));
            painter.fillRect(QRectF(0, 0, W, H), base);
            QRadialGradient glow(QPointF(W * 0.5, H * 0.62), H * 0.8);
            glow.setColorAt(0.0, QColor(141, 242, 55, 46));
            glow.setColorAt(1.0, QColor(141, 242, 55, 0));
            painter.fillRect(QRectF(0, 0, W, H), glow);
        }
        const QString title = scene.value("title").toString().left(60), subtitle = scene.value("subtitle").toString().left(120);
        QFont titleFont = sceneFont(H / 9, QFont::Bold, true);
        QFontMetricsF tm(titleFont);
        while (tm.horizontalAdvance(title) > W * 0.88 && titleFont.pixelSize() > 16) { titleFont.setPixelSize(titleFont.pixelSize() - 4); tm = QFontMetricsF(titleFont); }
        painter.setFont(titleFont);
        painter.setPen(QColor(255, 255, 255));
        const qreal titleY = H * 0.5 + (subtitle.isEmpty() ? tm.ascent() / 3 : 0);
        painter.drawText(QPointF((W - tm.horizontalAdvance(title)) / 2, titleY), title);
        // A short green rule under the title.
        painter.fillRect(QRectF(W / 2.0 - H * 0.06, titleY + tm.descent() + H * 0.02, H * 0.12, std::max(3, H / 240)), QColor(141, 242, 55));
        if (!subtitle.isEmpty()) {
            const QFont subFont = sceneFont(H / 24, QFont::Medium);
            const QFontMetricsF sm(subFont);
            painter.setFont(subFont);
            painter.setPen(QColor(205, 210, 216));
            painter.drawText(QRectF(W * 0.08, titleY + tm.descent() + H * 0.06, W * 0.84, sm.height() * 2.4), Qt::AlignHCenter | Qt::TextWordWrap, subtitle);
        }
        drawWordmark(painter, QPointF(W - H * 0.04, H - H * 0.045), H / 26);
    } else {
        // Game scene: an image and/or a text tag in one corner.
        const QString corner = scene.value("corner").toString();
        const bool right = corner == QLatin1String("tr") || corner == QLatin1String("br");
        const bool bottom = corner == QLatin1String("bl") || corner == QLatin1String("br");
        const qreal margin = H / 30.0;
        qreal x = right ? W - margin : margin, y = bottom ? H - margin : margin;
        const QImage logo(overlayPath);
        if (!logo.isNull()) {
            const qreal scale = std::min(H / 7.0 / logo.height(), W / 4.0 / logo.width());
            const QSizeF size(logo.width() * scale, logo.height() * scale);
            const QRectF rect(right ? x - size.width() : x, bottom ? y - size.height() : y, size.width(), size.height());
            painter.drawImage(rect, logo);
            y = bottom ? rect.top() - margin * 0.4 : rect.bottom() + margin * 0.4;
        }
        if (!overlayText.trimmed().isEmpty()) {
            const QFont font = sceneFont(H / 28, QFont::Bold);
            const QFontMetricsF m(font);
            const qreal pad = m.height() * 0.45, w = m.horizontalAdvance(overlayText) + 2 * pad, h = m.height() + pad;
            const QRectF pill(right ? x - w : x, bottom ? y - h : y, w, h);
            QPainterPath path;
            path.addRoundedRect(pill, h / 2, h / 2);
            painter.fillPath(path, QColor(14, 15, 18, 215));
            painter.setPen(QPen(QColor(141, 242, 55), std::max(1.5, H / 540.0)));
            painter.drawPath(path);
            painter.setFont(font);
            painter.setPen(QColor(255, 255, 255));
            painter.drawText(pill, Qt::AlignCenter, overlayText);
        }
    }
    painter.end();
    engine::setPushStreamOverlay(std::make_shared<engine::PushOverlayImage>(imageOf(canvas)), uint32_t(impl_->prefInt("studioTransitionMs", 300)));
    veyra::log::info("push-studio", std::format("scene {} {}x{}", card ? "card" : "game overlay", W, H));
    return true;
}

void QmlPlayerBridge::applyPushMix() {
    engine::PushMixSettings s;
    s.gameDb = float(impl_->prefInt("mixGameDb", 0));
    s.gameMuted = impl_->prefBool("mixGameMuted", false);
    s.micDb = float(impl_->prefInt("mixMicDb", 0));
    s.micMuted = impl_->prefBool("mixMicMuted", false);
    s.micHighPass = impl_->prefBool("micHighPass", false);
    s.micEq = impl_->prefBool("micEqOn", false);
    const QStringList parts = impl_->prefString("micEq").split(QLatin1Char(','));
    if (parts.size() == engine::kMicEqBands)
        for (int i = 0; i < engine::kMicEqBands; ++i) s.micEqDb[std::size_t(i)] = parts[i].toFloat();
    s.micGate = impl_->prefBool("micGate", false);
    s.micGateDb = float(impl_->prefInt("micGateDb", -45));
    s.micCompressor = impl_->prefBool("micCompressor", false);
    engine::setPushMix(s);
    engine::PushAuxSettings aux;
    aux.desktop = impl_->prefBool("mixDesktopOn", false);
    aux.desktopDb = float(impl_->prefInt("mixDesktopDb", 0));
    aux.desktopMuted = impl_->prefBool("mixDesktopMuted", false);
    aux.input = impl_->prefBool("mixAuxOn", false);
    aux.inputId = impl_->prefString("mixAuxDevice").toStdWString();
    aux.inputDb = float(impl_->prefInt("mixAuxDb", 0));
    aux.inputMuted = impl_->prefBool("mixAuxMuted", false);
    engine::setPushAux(aux);
}

void QmlPlayerBridge::pushSetStudioMeters(bool on) {
    const bool wantMic = on && (impl_->prefBool("recordMic", false) || impl_->prefBool("streamMic", false));
    if (on && !gameTap) {
        gameTap = std::make_shared<GameMeterTap>();
        sink::addRecordingAudioSink(gameTap);
    } else if (!on && gameTap) {
        sink::removeRecordingAudioSink(gameTap.get());
        gameTap.reset();
    }
    if (wantMic && !micTap) {
        micTap = std::make_shared<MicMeterSink>();
        engine::attachMicrophone(micTap, impl_->prefString("recordMicDevice").toStdWString());
    } else if (!wantMic && micTap) {
        engine::detachMicrophone(micTap.get());
        micTap.reset();
    }
    engine::setPushAuxMeters(on);
    veyra::log::info("push-studio", std::format("meters {} microphone={}", on ? "on" : "off", wantMic));
}

QVariantMap QmlPlayerBridge::pushStudioLevels() {
    const auto game = engine::pushGameMeter().take();
    const auto mic = engine::pushMicMeter().take();
    const auto status = engine::microphoneStatus();
    QVariantMap m;
    m["gamePeakDb"] = level(game.peakDb);
    m["gameRmsDb"] = level(game.rmsDb);
    m["micPeakDb"] = level(mic.peakDb);
    m["micRmsDb"] = level(mic.rmsDb);
    m["micOpen"] = status.open;
    m["micDevice"] = QString::fromStdWString(status.device);
    m["micError"] = status.error.empty() ? QString() : uiText(status.error);
    const auto desktop = engine::pushDesktopMeter().take();
    const auto input = engine::pushInputMeter().take();
    const auto aux = engine::pushAuxStatus();
    m["desktopPeakDb"] = level(desktop.peakDb);
    m["desktopRmsDb"] = level(desktop.rmsDb);
    m["desktopOpen"] = aux.desktopOpen;
    m["desktopError"] = QString::fromStdWString(aux.desktopError);
    m["auxPeakDb"] = level(input.peakDb);
    m["auxRmsDb"] = level(input.rmsDb);
    m["auxOpen"] = aux.inputOpen;
    m["auxDevice"] = QString::fromStdWString(aux.inputName);
    m["auxError"] = QString::fromStdWString(aux.inputError);
    return m;
}

// --- scene sources (engine/PushLayerSources.h) ---------------------------------------------------
QVariantList QmlPlayerBridge::pushCameras() {
    QVariantList out;
    for (const auto& c : engine::pushCameras())
        out.push_back(QVariantMap{{"id", QString::fromStdWString(c.id)}, {"name", QString::fromStdWString(c.name)}});
    return out;
}

QVariantList QmlPlayerBridge::pushWindows() {
    QVariantList out;
    for (const auto& t : source::ScreenCaptureSource::targets(source::ScreenTargetKind::Window)) {
        if (t.name.empty() || t.processId == GetCurrentProcessId()) continue;
        out.push_back(QVariantMap{{"id", QString::number(qulonglong(t.handle))}, {"name", QString::fromStdWString(t.name)}});
        if (out.size() >= 60) break;
    }
    return out;
}

QString QmlPlayerBridge::pushPickMedia() {
    return QFileDialog::getOpenFileName(nullptr, tr("选择视频"), QString(), tr("视频 (*.mp4 *.mov *.mkv *.webm *.avi *.wmv)"));
}

void QmlPlayerBridge::pushSetSceneSources(const QVariantList& sources) {
    std::vector<engine::PushSourceSpec> specs;
    for (const auto& item : sources) {
        const QVariantMap m = item.toMap();
        engine::PushSourceSpec spec;
        spec.id = m.value("id").toString().left(40).toStdString();
        const QString kind = m.value("kind").toString();
        spec.kind = kind == QLatin1String("window") ? engine::PushSourceKind::Window
                  : kind == QLatin1String("media") ? engine::PushSourceKind::Media : engine::PushSourceKind::Camera;
        spec.target = m.value("target").toString().left(1024).toStdWString();
        if (spec.target.empty() || spec.id.empty()) continue;
        if (spec.kind == engine::PushSourceKind::Media && !QFileInfo(QString::fromStdWString(spec.target)).isFile()) continue;
        spec.mirror = m.value("mirror").toBool();
        spec.loop = m.value("loop", true).toBool();
        const QString shape = m.value("shape").toString();
        spec.shape = shape == QLatin1String("circle") ? engine::PushSourceShape::Circle
                   : shape == QLatin1String("round") ? engine::PushSourceShape::Rounded : engine::PushSourceShape::Rectangle;
        // Size: the layer's width as a share of the picture's; margin: the gap to the edges.
        const double size = std::clamp(m.value("size", 0.25).toDouble(), 0.05, 1.0);
        const double margin = std::clamp(m.value("margin", 0.025).toDouble(), 0.0, 0.2);
        const QString corner = m.value("corner").toString();
        engine::PushLayerPlace place;
        place.w = float(size);
        if (corner == QLatin1String("c")) { place.x = 0.5f; place.y = 0.5f; place.anchor = 4; }
        else if (corner == QLatin1String("custom")) {
            // Free placement: the source's top-left corner at (x, y), fractions of the picture.
            place.x = float(std::clamp(m.value("x", 0.7).toDouble(), -0.5, 1.0));
            place.y = float(std::clamp(m.value("y", 0.7).toDouble(), -0.5, 1.0));
            place.anchor = 0;
            // Height as a share of the picture's; 0 keeps the source's own shape (the layout editor
            // sets it only when the source was stretched with Ctrl held).
            place.h = float(std::clamp(m.value("h", 0.0).toDouble(), 0.0, 1.0));
        } else {
            const bool right = corner == QLatin1String("tr") || corner == QLatin1String("br") || corner.isEmpty();
            const bool bottom = corner == QLatin1String("bl") || corner == QLatin1String("br") || corner.isEmpty();
            // The margin in pixels is the same horizontally and vertically (16:9 picture).
            place.x = float(right ? 1.0 - margin * 9.0 / 16.0 : margin * 9.0 / 16.0);
            place.y = float(bottom ? 1.0 - margin : margin);
            place.anchor = (right ? 1 : 0) + (bottom ? 2 : 0);
        }
        spec.place = place;
        spec.cropLeft = float(std::clamp(m.value("cropL", 0.0).toDouble(), 0.0, 0.45));
        spec.cropTop = float(std::clamp(m.value("cropT", 0.0).toDouble(), 0.0, 0.45));
        spec.cropRight = float(std::clamp(m.value("cropR", 0.0).toDouble(), 0.0, 0.45));
        spec.cropBottom = float(std::clamp(m.value("cropB", 0.0).toDouble(), 0.0, 0.45));
        // Camera: the resolution asked for (0 automatic; the camera gives the closest it offers).
        if (spec.kind == engine::PushSourceKind::Camera) {
            const int wanted = m.value("resolution", 0).toInt();
            spec.cameraHeight = wanted == 720 || wanted == 1080 || wanted == 1440 || wanted == 2160 ? uint32_t(wanted) : 0u;
        }
        specs.push_back(std::move(spec));
    }
    pushSourceReference();
    engine::setPushSources(specs, uint32_t(impl_->prefInt("studioTransitionMs", 300)));
}

void QmlPlayerBridge::pushSourceReference() {
    // The presented picture is what recordings and streams are made from, unless a larger output
    // size is set for either.
    const auto out = impl_->snapshot.metrics.resolution.output;
    const uint32_t shown = out.width > 0 ? uint32_t(out.width) : 0u;
    const uint32_t record = uint32_t(std::clamp(impl_->prefInt("recordWidth", 0), 0, 8192));
    const uint32_t stream = uint32_t(std::clamp(impl_->prefInt("streamWidth", 0), 0, 8192));
    engine::setPushSourceReferenceWidth(std::max({shown, record, stream}));
}

int QmlPlayerBridge::pushSourceSerial(const QString& id) {
    const uint64_t serial = engine::pushSourceSerial(id.toStdString());
    return serial ? int(serial % 2000000000ull) + 1 : 0;
}

QVariantList QmlPlayerBridge::pushSourceStatus() {
    QVariantList out;
    for (const auto& st : engine::pushSourceStatus()) {
        // width x height: what the camera, window or file sends (the layer itself may be smaller: it
        // is fitted to the size it is shown); sizes: what a camera offers, largest first.
        QVariantList sizes;
        for (const auto& size : st.sizes)
            sizes.push_back(QVariantMap{{"width", int(size.width)}, {"height", int(size.height)}, {"fps", size.fps}});
        out.push_back(QVariantMap{{"id", QString::fromStdString(st.id)}, {"running", st.running}, {"error", QString::fromStdWString(st.error)},
                                  {"width", int(st.captureWidth ? st.captureWidth : st.width)}, {"height", int(st.captureHeight ? st.captureHeight : st.height)},
                                  {"layerWidth", int(st.width)}, {"layerHeight", int(st.height)}, {"fps", st.fps}, {"sizes", sizes}});
    }
    return out;
}

QString QmlPlayerBridge::pushSourcePreview(const QString& id) {
    const auto frame = engine::pushSourceFrame(id.toStdString());
    if (!frame || frame->width < 2 || frame->height < 2 || frame->rgba.size() < size_t(frame->width) * frame->height * 4) return {};
    const QImage image(frame->rgba.data(), int(frame->width), int(frame->height), int(frame->width) * 4, QImage::Format_RGBA8888);
    const QImage thumb = image.scaledToWidth(std::min(240, int(frame->width)), Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);
    QImage flat(thumb.size(), QImage::Format_RGB32);
    flat.fill(QColor(20, 22, 26));
    QPainter painter(&flat);
    painter.drawImage(0, 0, thumb);
    painter.end();
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    flat.save(&buffer, "JPG", 75);
    return QStringLiteral("data:image/jpeg;base64,") + QString::fromLatin1(bytes.toBase64());
}

} // namespace veyra::ui
