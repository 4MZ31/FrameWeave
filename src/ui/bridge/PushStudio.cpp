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
#include "veyra/engine/MicrophoneCapture.h"
#include "veyra/engine/PushAudioMix.h"
#include "veyra/sink/RecordingAudioTap.h"

#include <cmath>
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

} // namespace

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
    return m;
}

} // namespace veyra::ui
