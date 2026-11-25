// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#pragma once
// The preview shares the Windows build's interface parts (qml/Veyra: the theme, rows, buttons, the
// title bar, the navigation, the status strip). Those read a few things from the Windows bridge, the
// context object `veyra`. This object answers the same names from the preview's own controller, and
// only those: what is playing, its name, the live figures the status strip shows, and the interface
// preferences. Anything the Windows interface can do beyond that is not here.
#include <QObject>
#include <QString>
#include <QVariantMap>

namespace pushfps {

class AppController;

class VeyraShim : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool hasSource READ hasSource NOTIFY sourceChanged)
    Q_PROPERTY(bool captureRecovering READ captureRecovering CONSTANT)
    // The preview never hands the mouse to a game (PushLatencyBadge.qml asks before it can be dragged).
    Q_PROPERTY(bool gameInputActive READ gameInputActive CONSTANT)
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY sourceChanged)
    Q_PROPERTY(QString appName READ appName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY preferencesChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion NOTIFY preferencesChanged)
    Q_PROPERTY(int uiDeviceGeneration READ uiDeviceGeneration CONSTANT)
public:
    explicit VeyraShim(AppController* app, QObject* parent = nullptr);

    bool hasSource() const;
    bool captureRecovering() const { return false; }
    bool gameInputActive() const { return false; }
    QString sourceTitle() const;
    QString appName() const { return QStringLiteral("FrameWeave"); }
    QString version() const;
    QVariantMap preferences() const;
    bool reducedMotion() const;
    int uiDeviceGeneration() const { return 0; }

    // The figures under the Windows bridge's names (PushStatusStrip.qml). A figure the preview does not
    // know is left out, so the strip shows a dash for it and never a made-up number.
    Q_INVOKABLE QVariantMap pushLiveStats() const;
    Q_INVOKABLE void setPreference(const QString& key, const QVariant& value);
    Q_INVOKABLE void logUi(const QString&, const QString&) const {}

signals:
    void sourceChanged();
    void preferencesChanged();

private:
    AppController* app_;
};

}  // namespace pushfps
