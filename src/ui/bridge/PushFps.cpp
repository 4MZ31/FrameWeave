// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// QML bridge: PushFPS features that only read engine numbers or reuse existing
// settings paths - the latency readout and "Optimise for my PC" measurements,
// game-preset HUD regions, and sharing clips (clipboard, Discord).
#include "../QmlPlayerBridge_p.h"
#include "veyra/engine/PushBenchmarkProbe.h"
#include "veyra/Brand.h"
#include "veyra/RuntimePaths.h"
#include "veyra/ngx/AnrProvider.h"
#include <QDir>
#include <QDirIterator>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QClipboard>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

namespace veyra::ui {
namespace {
QVariant optionalNumber(const std::optional<double>& v) { return v && std::isfinite(*v) ? QVariant(*v) : QVariant(); }
// Discord's upload limit without server boosts or Nitro (2024 onwards).
constexpr qint64 kDiscordUploadLimit = 10ll * 1024 * 1024;
}

QVariantMap QmlPlayerBridge::pushLiveStats() const {
    const auto& s = impl_->snapshot;
    const auto& flow = s.metrics.flow;
    const bool facadeApplying = impl_->facade.applying();
    QVariantMap m;
    m["running"] = s.running;
    m["capture"] = s.capture;
    m["failed"] = s.failed || s.gpuFailed;
    m["sessionId"] = QVariant::fromValue(qulonglong(s.sessionId));
    m["desiredRevision"] = QVariant::fromValue(qulonglong(s.desired.revision));
    m["appliedRevision"] = QVariant::fromValue(qulonglong(s.applied.revision));
    m["frameRevision"] = QVariant::fromValue(qulonglong(flow.latest.frame.settingsRevision));
    m["applying"] = facadeApplying || s.applying;
    // Frames are being produced with the settings the interface asked for.
    m["settled"] = s.running && !facadeApplying && !s.applying && s.desired.revision == s.applied.revision &&
                   flow.latest.frame.settingsRevision == s.applied.revision;
    m["submitFps"] = optionalNumber(s.submissionFps);
    m["submitLowFps"] = optionalNumber(s.submissionLow1Fps);
    m["sourceFps"] = s.fps;
    m["contentFps"] = s.contentFps;
    m["captureFps"] = s.captureFps;
    m["captureReceived"] = QVariant::fromValue(qulonglong(s.captureReceived));
    m["captureDropped"] = QVariant::fromValue(qulonglong(s.captureDropped));
    m["latencyMs"] = optionalNumber(flow.softwareLatencyMs);
    m["latencyP95Ms"] = optionalNumber(flow.softwareLatencyP95Ms);
    m["latencySamples"] = QVariant::fromValue(qulonglong(flow.latencySamples));
    m["fgActive"] = s.fgActive;
    m["srActive"] = s.srActive;
    m["nrActive"] = s.nrActive;
    m["fgBudgetLimited"] = s.fgBudgetLimited;
    m["backendWarning"] = QString::fromStdWString(s.backendWarning);
    m["displayRefreshHz"] = impl_->displayRefreshHz;
    m["gpuUtilization"] = gpuUtilizationKnown() ? QVariant(gpuUtilization()) : QVariant();
    m["sourceWidth"] = int(s.sourceWidth);
    m["sourceHeight"] = int(s.sourceHeight);
    m["outputWidth"] = int(s.metrics.resolution.output.width);
    m["outputHeight"] = int(s.metrics.resolution.output.height);
    m["hdr"] = s.sourceHdr;
    return m;
}

void QmlPlayerBridge::pushBenchmarkBegin() { engine::pushBenchmarkProbe().begin(); }

QVariantMap QmlPlayerBridge::pushBenchmarkEnd() {
    const auto r = engine::pushBenchmarkProbe().end();
    veyra::log::info("push-benchmark", std::format("window valid={} seconds={:.2f} fps={:.2f} low1={:.2f} low01={:.2f} jitterMs={:.3f} "
        "presents={} generated={} gaps={} latencyMean={:.2f} p50={:.2f} p95={:.2f} samples={}",
        r.valid, r.seconds, r.fps, r.low1Fps, r.low01Fps, r.intervalStdMs, r.presents, r.generatedPresents, r.gaps,
        r.latencyMeanMs, r.latencyP50Ms, r.latencyP95Ms, r.latencySamples));
    return {{"valid", r.valid}, {"seconds", r.seconds}, {"fps", r.fps}, {"low1Fps", r.low1Fps}, {"low01Fps", r.low01Fps},
            {"jitterMs", r.intervalStdMs}, {"presents", QVariant::fromValue(qulonglong(r.presents))},
            {"generatedPresents", QVariant::fromValue(qulonglong(r.generatedPresents))},
            {"realPresents", QVariant::fromValue(qulonglong(r.realPresents))}, {"gaps", QVariant::fromValue(qulonglong(r.gaps))},
            {"latencyMeanMs", r.latencyMeanMs}, {"latencyP50Ms", r.latencyP50Ms}, {"latencyP95Ms", r.latencyP95Ms},
            {"latencySamples", QVariant::fromValue(qulonglong(r.latencySamples))}};
}

bool QmlPlayerBridge::pushSetExclusionRegions(const QVariantList& regions, double feather) {
    std::array<engine::ProtectionRect, 4> rects{};
    size_t count = 0;
    for (const auto& item : regions) {
        if (count >= rects.size()) break;
        const auto r = item.toMap();
        const auto num = [&](const char* key) {
            const double v = r.value(key).toDouble();
            return float(std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0);
        };
        engine::ProtectionRect q{std::min(num("left"), num("right")), std::min(num("top"), num("bottom")),
                                 std::max(num("left"), num("right")), std::max(num("top"), num("bottom")), r.value("ellipse").toBool()};
        if (q.right - q.left < 0.005f || q.bottom - q.top < 0.005f) continue;
        rects[count++] = q;
    }
    const float px = float(std::isfinite(feather) ? std::clamp(feather, 0.0, 64.0) : 8.0);
    if (count == 0 && !impl_->chain.firstOf(engine::EffectType::Exclusion)) return true;  // nothing to turn off
    const bool ok = editProtection([&](engine::ChainNode& node) {
        node.protection.regions = rects;
        node.protection.featherPixels = px;
        node.protection.enabled = count > 0;
        node.enabled = count > 0;
        return true;
    }, true);
    veyra::log::info("push-presets", std::format("exclusion regions={} feather={:.1f} accepted={}", count, px, ok));
    return ok;
}

bool QmlPlayerBridge::pushCopyText(const QString& text) {
    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard || text.size() > 1 << 20) return false;
    clipboard->setText(text);
    return true;
}

bool QmlPlayerBridge::pushCopyFile(const QString& path) {
    const QFileInfo info(path);
    auto* clipboard = QGuiApplication::clipboard();
    if (!clipboard || !info.isFile()) { emit notice(tr("文件不存在：%1").arg(path), true); return false; }
    auto* data = new QMimeData;
    data->setUrls({QUrl::fromLocalFile(info.absoluteFilePath())});
    clipboard->setMimeData(data);  // the clipboard owns it
    return true;
}

bool QmlPlayerBridge::pushOpenDiscord() {
    if (QDesktopServices::openUrl(QUrl(QStringLiteral("discord://-/channels/@me")))) return true;
    return QDesktopServices::openUrl(QUrl(QStringLiteral("https://discord.com/app")));
}

bool QmlPlayerBridge::pushShareToDiscord(const QString& path) {
    const QString webhook = impl_->prefString("pushDiscordWebhook").trimmed();
    const QFileInfo info(path);
    if (webhook.isEmpty()) { emit pushShareFinished(false, tr("请先在 设置 → 播放 中填写 Discord Webhook 地址")); return false; }
    if (!info.isFile()) { emit pushShareFinished(false, tr("文件不存在：%1").arg(path)); return false; }
    if (info.size() > kDiscordUploadLimit) {
        emit pushShareFinished(false, tr("片段有 %1 MB，Discord 默认只接收 10 MB 以内的文件。已复制到剪贴板，可在 Discord 中粘贴（会提示压缩或使用 Nitro）")
                                       .arg(double(info.size()) / (1024.0 * 1024.0), 0, 'f', 1));
        pushCopyFile(path);
        return false;
    }
    auto* file = new QFile(info.absoluteFilePath());
    if (!file->open(QIODevice::ReadOnly)) { delete file; emit pushShareFinished(false, tr("无法读取文件：%1").arg(path)); return false; }
    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart payload;
    payload.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"payload_json\""));
    payload.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    payload.setBody(QJsonDocument(QJsonObject{{"content", QStringLiteral("%1 · %2").arg(QString::fromLatin1(brand::kProductName), info.completeBaseName())}})
                        .toJson(QJsonDocument::Compact));
    multi->append(payload);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"files[0]\"; filename=\"%1\"").arg(info.fileName().replace(QLatin1Char('"'), QLatin1Char('_'))));
    part.setHeader(QNetworkRequest::ContentTypeHeader, info.suffix().compare(QStringLiteral("mkv"), Qt::CaseInsensitive) == 0
                       ? QStringLiteral("video/x-matroska") : QStringLiteral("video/mp4"));
    part.setBodyDevice(file);
    file->setParent(multi);
    multi->append(part);
    auto* network = findChild<QNetworkAccessManager*>(QStringLiteral("pushfps-share"));
    if (!network) { network = new QNetworkAccessManager(this); network->setObjectName(QStringLiteral("pushfps-share")); }
    QNetworkRequest request{QUrl(webhook)};
    request.setTransferTimeout(120000);
    auto* reply = network->post(request, multi);
    multi->setParent(reply);
    const QString name = info.fileName();
    connect(reply, &QNetworkReply::finished, this, [this, reply, name] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
        veyra::log::info("push-share", std::format("discord webhook status={} error={}", status, int(reply->error())));
        if (ok) emit pushShareFinished(true, tr("已发送到 Discord：%1").arg(name));
        else if (status == 413) emit pushShareFinished(false, tr("Discord 拒绝了文件：太大"));
        else if (status == 401 || status == 404) emit pushShareFinished(false, tr("Discord Webhook 地址无效或已被删除"));
        else emit pushShareFinished(false, tr("发送到 Discord 失败（%1）").arg(status ? QString::number(status) : reply->errorString()));
        reply->deleteLater();
    });
    return true;
}

QString QmlPlayerBridge::pushImportTextFile(const QString& title) {
    const QString path = QFileDialog::getOpenFileName(nullptr, title, QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                                      tr("PushFPS 游戏预设 (*.pushfps);;所有文件 (*)"));
    if (path.isEmpty()) return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256 * 1024) { emit notice(tr("无法读取文件：%1").arg(path), true); return {}; }
    return QString::fromUtf8(file.readAll());
}

bool QmlPlayerBridge::pushExportTextFile(const QString& title, const QString& suggestedName, const QString& text) {
    QString base = suggestedName;
    for (const QChar c : QStringLiteral("\\/:*?\"<>|")) base.replace(c, QLatin1Char('_'));
    const QString path = QFileDialog::getSaveFileName(nullptr, title,
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(base + QStringLiteral(".pushfps")),
        tr("PushFPS 游戏预设 (*.pushfps)"));
    if (path.isEmpty()) return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text.toUtf8()) < 0) {
        emit notice(tr("无法保存文件：%1").arg(path), true);
        return false;
    }
    emit notice(tr("已保存：%1").arg(QDir::toNativeSeparators(path)), false);
    return true;
}

// --- Apex Neural Rendering (ANR) ------------------------------------------------
namespace {
QString anrDirectory() { return QString::fromStdWString(ngx::AnrProvider::directory(runtime::localRuntimeDirectory().wstring())); }
// The folder of the package that holds ANR's frame generation DLL (Apex ships it as
// "...\Apex Neural Rendering FG\dlssg\nvngx_dlssg.dll"; the DLL's folder is self-contained).
QString findAnrDll(const QString& root) {
    QDirIterator it(root, {QStringLiteral("nvngx_dlssg.dll")}, QDir::Files, QDirIterator::Subdirectories);
    QString best;
    while (it.hasNext()) {
        const QString path = it.next();
        // Prefer ANR's own copy over any other nvngx_dlssg.dll a package might carry.
        if (best.isEmpty() || path.contains(QStringLiteral("Apex"), Qt::CaseInsensitive)) best = path;
    }
    return best;
}
bool copyTree(const QString& from, const QString& to, int& files) {
    QDir source(from);
    if (!QDir().mkpath(to)) return false;
    for (const QFileInfo& info : source.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString target = QDir(to).filePath(info.fileName());
        if (info.isDir()) { if (!copyTree(info.filePath(), target, files)) return false; continue; }
        QFile::remove(target);
        if (!QFile::copy(info.filePath(), target)) return false;
        ++files;
    }
    return true;
}
}

QVariantMap QmlPlayerBridge::pushAnrState() const {
    const QString dir = anrDirectory();
    return {{"installed", ngx::AnrProvider::available(runtime::localRuntimeDirectory().wstring())},
            {"folder", QDir::toNativeSeparators(dir)}, {"name", QString::fromLatin1(brand::kAnrName)},
            {"maker", QString::fromLatin1(brand::kAnrMaker)}, {"url", QString::fromLatin1(brand::kAnrUrl)}};
}

bool QmlPlayerBridge::pushInstallAnr() {
    const QString picked = QFileDialog::getOpenFileName(nullptr, tr("选择从 Apex Compute 下载的 ANR 压缩包"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        tr("ANR 安装包 (*.zip nvngx_dlssg.dll);;所有文件 (*)"));
    if (picked.isEmpty()) return false;
    QTemporaryDir staging;
    QString root = QFileInfo(picked).absolutePath();
    if (picked.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive)) {
        if (!staging.isValid()) { emit notice(tr("无法创建临时文件夹"), true); return false; }
        const QString tar = QDir(QProcessEnvironment::systemEnvironment().value(QStringLiteral("SystemRoot"), QStringLiteral("C:/Windows")))
                                .filePath(QStringLiteral("System32/tar.exe"));
        QProcess unzip;
        unzip.start(tar, {QStringLiteral("-xf"), picked, QStringLiteral("-C"), staging.path()});
        if (!unzip.waitForFinished(120000) || unzip.exitStatus() != QProcess::NormalExit || unzip.exitCode() != 0) {
            emit notice(tr("无法解压 ANR 压缩包：%1").arg(QFileInfo(picked).fileName()), true);
            return false;
        }
        root = staging.path();
    }
    const QString dll = findAnrDll(root);
    if (dll.isEmpty()) { emit notice(tr("这个文件里没有 ANR 的 nvngx_dlssg.dll，请选择 Apex 的 anr-fg 安装包"), true); return false; }
    int files = 0;
    if (!copyTree(QFileInfo(dll).absolutePath(), anrDirectory(), files)) {
        emit notice(tr("无法复制 ANR 文件到 %1").arg(QDir::toNativeSeparators(anrDirectory())), true);
        return false;
    }
    refreshComponentList();
    emit notice(tr("ANR 已安装（%1 个文件）。重新启动 PushFPS 后，可在补帧方式中选择 ANR").arg(files), false);
    return true;
}

bool QmlPlayerBridge::pushOpenAnrFolder() {
    const QString dir = anrDirectory();
    QDir().mkpath(dir);
    return QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

QString QmlPlayerBridge::pushLinkUrl(const QString& key) const {
    if (key == QLatin1String("repo")) return QString::fromLatin1(brand::kRepositoryUrl);
    if (key == QLatin1String("veyra")) return QString::fromLatin1(brand::kUpstreamUrl);
    if (key == QLatin1String("anr")) return QString::fromLatin1(brand::kAnrUrl);
    if (key == QLatin1String("anrReleases")) return QString::fromLatin1(brand::kAnrReleasesUrl);
    if (key == QLatin1String("apex")) return QString::fromLatin1(brand::kApexUrl);
    return {};
}

bool QmlPlayerBridge::pushOpenLink(const QString& key) {
    const QString url = pushLinkUrl(key);
    return !url.isEmpty() && QDesktopServices::openUrl(QUrl(url));
}

}
