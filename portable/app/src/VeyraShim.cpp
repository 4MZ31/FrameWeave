// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#include "VeyraShim.h"

#include "AppController.h"

namespace pushfps {

VeyraShim::VeyraShim(AppController* app, QObject* parent) : QObject(parent), app_(app) {
    connect(app_, &AppController::runningChanged, this, &VeyraShim::sourceChanged);
    connect(app_, &AppController::preferencesChanged, this, &VeyraShim::preferencesChanged);
}

bool VeyraShim::hasSource() const { return app_->running(); }
QString VeyraShim::sourceTitle() const { return app_->sourceTitle(); }
QString VeyraShim::version() const { return app_->version(); }
QVariantMap VeyraShim::preferences() const { return app_->preferences(); }
bool VeyraShim::reducedMotion() const { return app_->preferences().value(QStringLiteral("reducedMotion")).toBool(); }

QVariantMap VeyraShim::pushLiveStats() const {
    QVariantMap out;
    if (!app_->running()) return out;
    const QVariantMap st = app_->stats();
    const auto copy = [&](const char* from, const char* to) {
        const QVariant v = st.value(QLatin1String(from));
        if (v.isValid()) out.insert(QLatin1String(to), v);
    };
    // Nothing has arrived yet: no figure is a reading.
    if (st.value(QStringLiteral("framesRead")).toLongLong() <= 0) return out;
    copy("width", "sourceWidth");
    copy("height", "sourceHeight");
    copy("transportFps", "captureFps");
    copy("contentFps", "contentFps");
    // The preview counts what it shows as one figure: everything shown goes in realFps.
    copy("outputFps", "realFps");
    // ... and it is also what reaches the screen each second, the latency readout's frame rate
    // (PushLatencyBadge.qml). There is no 1% low or 95th percentile here: those stay out.
    copy("outputFps", "submitFps");
    copy("latencyMs", "latencyMs");
    out.insert(QStringLiteral("running"), true);
    return out;
}

void VeyraShim::setPreference(const QString& key, const QVariant& value) { app_->setPreference(key, value); }

}  // namespace pushfps
