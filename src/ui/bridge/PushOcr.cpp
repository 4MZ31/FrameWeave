// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
//
// PushFPS: reading the text that is on the picture (ui/PushOcr.h), for the interface.
//
// One frame of the picture PushFPS shows is asked from the engine the way a screenshot is (the same
// single request the engine holds; `pushfps-read-frame-<time>` in PushFPS's data folder), read on a
// worker thread with Windows' own recogniser, and deleted by that worker whatever the reading gave.
// Only when the user asks; nothing in the background, nothing leaves the PC for the reading. The
// text itself is never written to the log: lengths and counts only.
#include "../QmlPlayerBridge_p.h"

#include <QUrl>

#include <chrono>
#include <filesystem>
#include <format>
#include <future>
#include <string>

namespace veyra::ui {
namespace {
constexpr qint64 kFrameWaitMs = 6000;   // a frame that has not come by then is not coming (nothing is playing)

QString frameFolder(const std::filesystem::path& dataDir) {
    return QDir::fromNativeSeparators(QString::fromStdWString(dataDir.wstring()));
}
// Frames an earlier reading left behind (PushFPS was closed in the middle of one).
void removeLeftFrames(const QString& folder) {
    QDir dir(folder);
    for (const QString& name : dir.entryList({QStringLiteral("pushfps-read-frame-*")}, QDir::Files)) dir.remove(name);
}
// A language tag as Windows writes them ("ja", "en-US", "zh-Hans-CN"), or none for the user's own languages.
bool plausibleTag(const QString& tag) {
    if (tag.size() > 35) return false;
    for (const QChar ch : tag) {
        const auto c = ch.unicode();
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    return true;
}
template <class T> bool jobDone(std::future<T>& job) {
    return job.valid() && job.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
}

QVariantMap QmlPlayerBridge::pushReadText() const { return impl_->readState; }

// Asks Windows which languages it can read, once and off the interface's thread: the answer arrives
// in pushReadText.languages.
void QmlPlayerBridge::pushReadTextPrepare() {
    auto& i = *impl_;
    if (i.readPhase == 0) removeLeftFrames(frameFolder(i.dataDir));
    if (i.readLanguagesAsked) return;
    i.readLanguagesAsked = true;
    i.readLanguagesJob = std::async(std::launch::async, [] { return ocr::languages(); });
}

bool QmlPlayerBridge::pushReadTextStart(const QString& languageTag, int region) {
    auto& i = *impl_;
    if (i.readPhase != 0) return false;
    const auto refuse = [&](const char* why) {
        i.readState[QStringLiteral("phase")] = QStringLiteral("failed");
        i.readState[QStringLiteral("error")] = QString::fromLatin1(why);
        i.readState[QStringLiteral("text")] = QString();
        i.readState[QStringLiteral("lines")] = QVariantList();
        i.readState[QStringLiteral("count")] = 0;
        veyra::log::info("push-read", std::string("refused reason=") + why);
        emit pushReadTextChanged();
        return false;
    };
    if (!hasSource()) return refuse("no-picture");
    // The engine holds one request for a frame at a time: a screenshot or an export under way keeps it.
    if (!i.pendingScreenshot.isEmpty() || !i.pendingFrameExport.isEmpty() || imageBatchActive()) return refuse("busy");
    if (!plausibleTag(languageTag)) return refuse("no-language");
    const QString folder = frameFolder(i.dataDir);
    if (folder.isEmpty() || !QDir().mkpath(folder)) return refuse("failed");
    removeLeftFrames(folder);
    const QString path = QDir::fromNativeSeparators(
        QDir(folder).filePath(QStringLiteral("pushfps-read-frame-%1.png").arg(QDateTime::currentMSecsSinceEpoch())));
    i.engine.saveFrame(wideOf(QDir::toNativeSeparators(path)));
    i.readFrame = path;
    i.readLanguage = languageTag;
    i.readRegion = std::clamp(region, 0, 3);
    i.readStartedAt = QDateTime::currentMSecsSinceEpoch();
    i.readDeadline = i.readStartedAt + kFrameWaitMs;
    i.readPhase = 1;
    i.readState[QStringLiteral("phase")] = QStringLiteral("frame");
    i.readState[QStringLiteral("error")] = QString();
    i.readState[QStringLiteral("text")] = QString();
    i.readState[QStringLiteral("lines")] = QVariantList();
    i.readState[QStringLiteral("count")] = 0;
    i.readState[QStringLiteral("region")] = i.readRegion;
    veyra::log::info("push-read", std::format("requested region={} language={}", i.readRegion,
                                              languageTag.isEmpty() ? std::string("own") : languageTag.toStdString()));
    emit pushReadTextChanged();
    return true;
}

// Forgets what was read. A reading under way is finished (its frame has to be deleted) and then dropped.
void QmlPlayerBridge::pushReadTextClear() {
    auto& i = *impl_;
    i.readDropResult = i.readPhase != 0;
    const QVariant languages = i.readState.value(QStringLiteral("languages"));
    const QVariant known = i.readState.value(QStringLiteral("languagesKnown"));
    i.readState.clear();
    i.readState[QStringLiteral("phase")] = i.readPhase != 0 ? QStringLiteral("frame") : QStringLiteral("idle");
    if (languages.isValid()) i.readState[QStringLiteral("languages")] = languages;
    if (known.isValid()) i.readState[QStringLiteral("languagesKnown")] = known;
    emit pushReadTextChanged();
}

bool QmlPlayerBridge::pushOpenLanguageSettings() {
    veyra::log::info("push-read", "opening Windows language settings");
    return QDesktopServices::openUrl(QUrl(QStringLiteral("ms-settings:regionlanguage")));
}

// From the bridge's timer.
void QmlPlayerBridge::tickPushReadText() {
    auto& i = *impl_;
    if (jobDone(i.readLanguagesJob)) {
        QVariantList list;
        for (const auto& language : i.readLanguagesJob.get())
            list << QVariantMap{{QStringLiteral("tag"), QString::fromStdWString(language.tag)},
                                {QStringLiteral("name"), QString::fromStdWString(language.name)}};
        i.readState[QStringLiteral("languages")] = list;
        i.readState[QStringLiteral("languagesKnown")] = true;
        veyra::log::info("push-read", std::format("recognition languages={}", list.size()));
        emit pushReadTextChanged();
    }
    if (i.readPhase == 1) {
        const QString written = QDir::fromNativeSeparators(QString::fromStdWString(i.snapshot.savedFramePath));
        // With an HDR output the engine saves the frame as JPEG XR beside the name asked for.
        const QString hdr = QFileInfo(i.readFrame).path() + QLatin1Char('/') + QFileInfo(i.readFrame).completeBaseName() + QStringLiteral(".jxr");
        const bool plain = written.compare(i.readFrame, Qt::CaseInsensitive) == 0, wide = written.compare(hdr, Qt::CaseInsensitive) == 0;
        if (!written.isEmpty() && (plain || wide)) {
            const std::wstring file = wideOf(QDir::toNativeSeparators(written)), tag = i.readLanguage.toStdWString();
            const int region = i.readRegion;
            i.readPhase = 2;
            i.readJob = std::async(std::launch::async, [file, tag, region] {
                auto result = ocr::readFile(file, tag, ocr::regionFor(region));
                std::error_code ignored;
                std::filesystem::remove(file, ignored);   // whatever the reading gave
                return result;
            });
            if (!i.readDropResult) {
                i.readState[QStringLiteral("phase")] = QStringLiteral("reading");
                emit pushReadTextChanged();
            }
        } else if (QDateTime::currentMSecsSinceEpoch() > i.readDeadline) {
            // The request is taken back, so no frame is written later (unless the engine's one request
            // has meanwhile become another's: a batch of pictures started in these seconds).
            if (i.pendingScreenshot.isEmpty() && i.pendingFrameExport.isEmpty() && !imageBatchActive()) i.engine.saveFrame(std::wstring());
            removeLeftFrames(frameFolder(i.dataDir));
            i.readPhase = 0;
            veyra::log::warn("push-read", "no frame arrived");
            if (i.readDropResult) { i.readDropResult = false; i.readState[QStringLiteral("phase")] = QStringLiteral("idle"); }
            else {
                i.readState[QStringLiteral("phase")] = QStringLiteral("failed");
                i.readState[QStringLiteral("error")] = QStringLiteral("no-frame");
            }
            emit pushReadTextChanged();
        }
    } else if (i.readPhase == 2 && jobDone(i.readJob)) {
        const ocr::Result result = i.readJob.get();
        i.readPhase = 0;
        const QString text = QString::fromStdWString(ocr::joinLines(result.lines));
        veyra::log::info("push-read", std::format("done ok={} error={} lines={} chars={} language={} size={}x{} hdr={} ms={}", result.ok ? 1 : 0,
                                                  QString::fromStdWString(result.error).toStdString(), result.lines.size(), text.size(),
                                                  QString::fromStdWString(result.language).toStdString(), result.width, result.height,
                                                  result.hdr ? 1 : 0, QDateTime::currentMSecsSinceEpoch() - i.readStartedAt));
        if (i.readDropResult) {
            i.readDropResult = false;
            i.readState[QStringLiteral("phase")] = QStringLiteral("idle");
        } else {
            QVariantList lines;
            for (const auto& line : result.lines)
                lines << QVariantMap{{QStringLiteral("text"), QString::fromStdWString(line.text)}, {QStringLiteral("x"), line.x},
                                     {QStringLiteral("y"), line.y}, {QStringLiteral("w"), line.w}, {QStringLiteral("h"), line.h}};
            i.readState[QStringLiteral("phase")] = result.ok ? QStringLiteral("done") : QStringLiteral("failed");
            i.readState[QStringLiteral("error")] = QString::fromStdWString(result.error);
            i.readState[QStringLiteral("text")] = text;
            i.readState[QStringLiteral("lines")] = lines;
            i.readState[QStringLiteral("count")] = int(lines.size());
            i.readState[QStringLiteral("language")] = QString::fromStdWString(result.language);
        }
        emit pushReadTextChanged();
    }
}
}  // namespace veyra::ui
