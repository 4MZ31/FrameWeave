// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
// QML bridge: PushFPS local control. The rules are ui/PushControl.h (who may ask, what may be asked,
// what is answered: pure and unit-tested). Here is the socket, the token and what each action does.
//
// Off unless switched on (preference pushControl). The socket listens on 127.0.0.1 only. The token is
// 32 random bytes from the operating system, kept encrypted for this Windows user with DPAPI under
// the preference pushControlToken, never given to the interface (it can be copied to the clipboard
// by its button and replaced), and never logged. An action does what the same button in PushFPS
// does, through the same function, so nothing here can do more than the interface can.
#include "../QmlPlayerBridge_p.h"
#include "veyra/ui/PushControl.h"

#include <QClipboard>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <functional>
#include <memory>

#include <windows.h>
#include <wincrypt.h>

namespace veyra::ui {
namespace {

QByteArray protectToken(const QByteArray& plain) {
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"FrameWeave control token", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return result;
}

QByteArray unprotectToken(const QByteArray& blob) {
    if (blob.isEmpty()) return {};
    DATA_BLOB in{DWORD(blob.size()), reinterpret_cast<BYTE*>(const_cast<char*>(blob.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    QByteArray result(reinterpret_cast<const char*>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

// 32 bytes from the operating system's generator, as base64url without padding (43 characters).
QByteArray makeToken() {
    quint32 words[8]{};
    QRandomGenerator::system()->fillRange(words);
    const QByteArray raw(reinterpret_cast<const char*>(words), int(sizeof words));
    return raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

// The listening socket. It has no signals or slots of its own (so no Q_OBJECT): only lambdas on the
// server's and the sockets' signals, all on the interface thread, which is also where the bridge's
// functions must be called.
class ControlServer final : public QObject {
public:
    using Handler = std::function<QByteArray(control::Action, int)>;
    ControlServer(Handler handler, QObject* parent) : QObject(parent), handler_(std::move(handler)) {
        server_.setMaxPendingConnections(8);
        clock_.start();
        connect(&server_, &QTcpServer::newConnection, this, [this] { accept(); });
    }
    ~ControlServer() override { server_.close(); }

    // Loopback only: QHostAddress::LocalHost is 127.0.0.1, never a network address.
    bool listen(quint16 port) {
        if (server_.isListening() && server_.serverPort() == port) return true;
        server_.close();
        error_.clear();
        if (!server_.listen(QHostAddress::LocalHost, port)) {
            error_ = server_.errorString();
            log::warn("push-control", std::format("listen on 127.0.0.1:{} failed: {}", port, error_.toStdString()));
            return false;
        }
        log::info("push-control", std::format("listening on 127.0.0.1:{}", server_.serverPort()));
        return true;
    }
    void close() {
        if (server_.isListening()) log::info("push-control", "closed");
        server_.close();
        error_.clear();
    }
    bool listening() const { return server_.isListening(); }
    quint16 port() const { return server_.serverPort(); }
    QString error() const { return error_; }
    void setToken(const QByteArray& token) { token_ = token; }

private:
    struct Pending { QByteArray bytes; bool finished = false; };

    static void answer(QTcpSocket* socket, int status, const std::string& body) {
        const auto text = control::response(status, body);
        socket->write(text.data(), qint64(text.size()));
        socket->disconnectFromHost();
    }
    void accept() {
        while (auto* socket = server_.nextPendingConnection()) {
            if (active_ >= 8) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                answer(socket, 503, control::errorJson("busy"));
                continue;
            }
            ++active_;
            socket->setReadBufferSize(qint64(control::kMaxRequest) + 8);
            auto pending = std::make_shared<Pending>();
            connect(socket, &QTcpSocket::disconnected, this, [this, socket] { --active_; socket->deleteLater(); });
            QTimer::singleShot(3000, socket, [socket, pending] {
                if (pending->finished) return;
                pending->finished = true;
                answer(socket, 408, control::errorJson("request timeout"));
            });
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, pending] {
                if (pending->finished) return;
                pending->bytes += socket->readAll();
                const std::string_view received(pending->bytes.constData(), std::size_t(pending->bytes.size()));
                const auto state = control::framing(received);
                if (state == control::Framing::Incomplete) return;
                pending->finished = true;
                if (state == control::Framing::TooLarge) { answer(socket, 413, control::errorJson("request too large")); return; }
                const qint64 now = clock_.elapsed();
                // Locked after wrong tokens, or too many a second: nothing is looked at, nothing is told.
                if (!limiter_.allow(now)) { answer(socket, 429, control::errorJson("too many requests")); return; }
                const auto decision = control::decide(received, server_.serverPort(),
                                                      std::string_view(token_.constData(), std::size_t(token_.size())));
                if (decision.wrongToken) {
                    limiter_.wrongToken(now);
                    log::warn("push-control", limiter_.locked(now) ? "wrong token: locked for half a minute" : "wrong token");
                }
                if (decision.status != 200) { answer(socket, decision.status, control::errorJson(decision.error)); return; }
                const QByteArray body = handler_ ? handler_(decision.action, decision.scene) : QByteArray();
                answer(socket, 200, body.isEmpty() ? control::resultJson(false) : body.toStdString());
            });
        }
    }

    QTcpServer server_;
    Handler handler_;
    QByteArray token_;
    QString error_;
    control::Limiter limiter_;
    QElapsedTimer clock_;
    int active_ = 0;
};

ControlServer* serverOf(QObject* object) { return static_cast<ControlServer*>(object); }

const char* actionName(control::Action action) {
    switch (action) {
    case control::Action::Status: return "status";
    case control::Action::RecordStart: return "record-start";
    case control::Action::RecordStop: return "record-stop";
    case control::Action::ReplaySave: return "replay-save";
    case control::Action::Marker: return "marker";
    case control::Action::Screenshot: return "screenshot";
    case control::Action::Scene: return "scene";
    default: return "none";
    }
}

}  // namespace

// The token in force, decrypted ("" when there is none or it cannot be read by this Windows user).
static QByteArray controlToken(const QVariantMap& prefs) {
    const QByteArray blob = QByteArray::fromBase64(prefs.value(QStringLiteral("pushControlToken")).toString().toLatin1());
    const QByteArray token = unprotectToken(blob);
    return control::validToken(std::string_view(token.constData(), std::size_t(token.size()))) ? token : QByteArray();
}

// How many scenes streaming mode has: the rule of StudioPage.qml `scenes` (the saved list when it is a
// list with a game scene in it, else the four it starts with). Change both together.
static int sceneCount(const QVariantMap& prefs) {
    const auto saved = QJsonDocument::fromJson(prefs.value(QStringLiteral("studioScenes")).toString().toUtf8());
    if (saved.isArray()) {
        const QJsonArray list = saved.array();
        bool game = false;
        for (const auto& item : list)
            if (item.toObject().value(QStringLiteral("type")).toString() == QLatin1String("game")) { game = true; break; }
        if (game) return int(list.size());
    }
    return 4;
}

QVariantMap QmlPlayerBridge::pushControlState() const {
    const auto* server = serverOf(impl_->controlServer);
    QVariantMap state;
    state["enabled"] = impl_->prefBool("pushControl", false);
    state["listening"] = server && server->listening();
    state["port"] = std::clamp(impl_->prefInt("pushControlPort", int(control::kDefaultPort)), 1024, 65535);
    // A token that is there and can be read by this Windows user: a saved one from another account, or
    // a damaged one, is no token (it cannot be copied and nothing can sign in with it).
    state["hasToken"] = !controlToken(impl_->prefs).isEmpty();
    state["error"] = server ? server->error() : QString();
    return state;
}

// Starts or stops listening as the preferences say. A token is made the first time it is switched on.
void QmlPlayerBridge::pushApplyControl() {
    const bool wanted = impl_->prefBool("pushControl", false);
    auto* server = serverOf(impl_->controlServer);
    if (!wanted) {
        if (server) server->close();
        emit pushControlChanged();
        return;
    }
    QByteArray token = controlToken(impl_->prefs);
    if (token.isEmpty()) {
        // Saved but not readable (the settings came from another Windows account, or the entry is
        // damaged): a new one is made, and it is said, because what held the old one stops working.
        const bool unreadable = !impl_->prefs.value(QStringLiteral("pushControlToken")).toString().isEmpty();
        if (unreadable) {
            veyra::log::warn("push-control", "the saved token could not be read; a new one is made");
            emit notice(tr("本机控制保存的令牌无法读取，已生成新的令牌：请在设置里重新复制"), true);
        }
        token = makeToken();
        const QByteArray blob = protectToken(token);
        if (blob.isEmpty()) {
            veyra::log::error("push-control", "the token could not be protected; local control stays off");
            emit notice(tr("无法安全保存本机控制的令牌，本机控制未开启"), true);
            emit pushControlChanged();
            return;
        }
        impl_->prefs[QStringLiteral("pushControlToken")] = QString::fromLatin1(blob.toBase64());
        if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "control token not saved");
    }
    if (!server) {
        server = new ControlServer([this](control::Action action, int scene) { return pushControlHandle(int(action), scene); }, this);
        impl_->controlServer = server;
    }
    server->setToken(token);
    const int port = std::clamp(impl_->prefInt("pushControlPort", int(control::kDefaultPort)), 1024, 65535);
    if (!server->listen(quint16(port)))
        emit notice(tr("本机控制无法使用端口 %1：可能被别的程序占用，请换一个端口").arg(port), true);
    emit pushControlChanged();
}

bool QmlPlayerBridge::pushSetControlEnabled(bool on) {
    if (!setPreference(QStringLiteral("pushControl"), on)) return false;
    return !on || pushControlState().value("listening").toBool();
}

// The token goes to the clipboard and nowhere else: not into the interface, not into the log.
bool QmlPlayerBridge::pushCopyControlToken() {
    const QByteArray token = controlToken(impl_->prefs);
    auto* clipboard = QGuiApplication::clipboard();
    if (token.isEmpty() || !clipboard) { emit notice(tr("还没有令牌：请先开启本机控制"), true); return false; }
    clipboard->setText(QString::fromLatin1(token));
    return true;
}

// A new token: whatever held the old one stops working at once.
bool QmlPlayerBridge::pushNewControlToken() {
    const QByteArray token = makeToken();
    const QByteArray blob = protectToken(token);
    if (blob.isEmpty()) { emit notice(tr("无法安全保存本机控制的令牌，本机控制未开启"), true); return false; }
    const auto previous = impl_->prefs;
    impl_->prefs[QStringLiteral("pushControlToken")] = QString::fromLatin1(blob.toBase64());
    if (!impl_->savePrefs()) { impl_->prefs = previous; emit notice(tr("偏好保存失败，已保留原设置"), true); return false; }
    if (auto* server = serverOf(impl_->controlServer)) server->setToken(token);
    veyra::log::info("push-control", "a new token was made");
    emit pushControlChanged();
    return true;
}

// One request that passed every check (ui/PushControl.h). Each action is the interface's own function.
QByteArray QmlPlayerBridge::pushControlHandle(int actionNumber, int scene) {
    const auto action = control::Action(actionNumber);
    if (action != control::Action::Status) veyra::log::info("push-control", std::format("action={} scene={}", actionName(action), scene));
    const auto result = [](bool done) { return QByteArray::fromStdString(control::resultJson(done)); };
    switch (action) {
    case control::Action::Status: {
        const auto& s = impl_->snapshot;
        const auto stats = pushLiveStats();
        control::Facts facts;
        const std::string version = VEYRA_DISPLAY_VERSION;
        const std::string source = sourceKind().toStdString();
        facts.version = version;
        facts.source = source;
        facts.playing = hasSource() && s.running;
        facts.frameGeneration = s.fgActive;
        const auto submit = stats.value("submitFps"), latency = stats.value("latencyMs");
        facts.fps = facts.playing && submit.isValid() && !submit.isNull() ? submit.toDouble() : -1;
        facts.latencyMs = facts.playing && latency.isValid() && !latency.isNull() ? latency.toDouble() : -1;
        facts.recording = recording();
        facts.recordingSeconds = facts.recording ? int(impl_->recordingState.value("seconds").toDouble()) : 0;
        facts.markers = facts.recording ? impl_->recordingState.value("markers").toInt() : 0;
        facts.replay = replayEnabled();
        facts.streaming = pushStreamStatus().value("active").toBool();
        return QByteArray::fromStdString(control::statusJson(facts));
    }
    case control::Action::RecordStart:
        if (recording()) return result(true);
        startRecording(QStringLiteral("control"));
        return result(recording());
    case control::Action::RecordStop:
        if (recording()) stopRecording(QStringLiteral("control"));
        return result(!recording());
    // "ok" only for what was taken (ui/PushControl.h): until 4.1.0 a replay the engine refused, a
    // screenshot that was not asked for and a scene that does not exist all answered {"ok":true}.
    case control::Action::ReplaySave:
        return result(saveReplay(QStringLiteral("control")));
    case control::Action::Marker:
        return result(pushAddMarker(QStringLiteral("control")));
    case control::Action::Screenshot:
        return result(takeScreenshot());
    case control::Action::Scene:
        if (!control::sceneExists(scene, sceneCount(impl_->prefs))) return result(false);
        emit pushControlScene(scene);
        return result(true);
    default:
        return result(false);
    }
}

}  // namespace veyra::ui
