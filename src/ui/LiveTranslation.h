#pragma once

#include "TranslationAssets.h"
#include <QElapsedTimer>
#include <QSettings>
#include <QTimer>

namespace veyra::ui {
class LiveTranslation final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int provider READ provider WRITE setProvider NOTIFY changed)
    Q_PROPERTY(QString sourceLanguage READ sourceLanguage WRITE setSourceLanguage NOTIFY changed)
    Q_PROPERTY(QString targetLanguage READ targetLanguage WRITE setTargetLanguage NOTIFY changed)
    Q_PROPERTY(bool showOriginal READ showOriginal WRITE setShowOriginal NOTIFY changed)
    Q_PROPERTY(QString apiBase READ apiBase WRITE setApiBase NOTIFY changed)
    Q_PROPERTY(QString apiModel READ apiModel WRITE setApiModel NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString caption READ caption NOTIFY captionChanged)
    Q_PROPERTY(QString translated READ translated NOTIFY captionChanged)
    Q_PROPERTY(bool modelInstalled READ modelInstalled NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY changed)
    Q_PROPERTY(qint64 downloadBytes READ downloadBytes CONSTANT)
    // PushFPS (ui/PushOcr.h, reading the text on the picture): one text translated with the engine
    // chosen here, apart from the captions and whether or not live translation is on. onceReady:
    // 0 the engine can be used, 1 the local model is not downloaded, 2 the API is not set up,
    // 3 MyMemory has no source language, 4 the local model is being downloaded. onceOnline: the
    // text leaves this PC with the engine chosen now. onceCut: the text was longer than one request
    // takes and only its beginning was translated.
    Q_PROPERTY(QString onceText READ onceText NOTIFY onceChanged)
    Q_PROPERTY(QString onceStatus READ onceStatus NOTIFY onceChanged)
    Q_PROPERTY(bool onceBusy READ onceBusy NOTIFY onceChanged)
    Q_PROPERTY(bool onceCut READ onceCut NOTIFY onceChanged)
    Q_PROPERTY(int onceReady READ onceReady NOTIFY changed)
    Q_PROPERTY(bool onceOnline READ onceOnline NOTIFY changed)
public:
    enum Provider { Local, OpenAi, MyMemory };
    explicit LiveTranslation(const QString& directory, QObject* parent = nullptr);
    ~LiveTranslation() override;
    static bool supported();
    bool enabled() const { return enabled_; }
    int provider() const { return provider_; }
    QString sourceLanguage() const { return sourceLanguage_; }
    QString targetLanguage() const { return targetLanguage_; }
    bool showOriginal() const { return showOriginal_; }
    QString apiBase() const { return apiBase_; }
    QString apiModel() const { return apiModel_; }
    QString status() const;
    QString caption() const { return caption_; }
    QString translated() const { return translated_; }
    bool modelInstalled() const { return assets_.installed(); }
    bool downloading() const { return assets_.downloading(); }
    double downloadProgress() const { return double(assets_.received()) / double(assets_.totalBytes()); }
    qint64 downloadBytes() const { return assets_.totalBytes(); }

    void setEnabled(bool value);
    void setProvider(int value);
    void setSourceLanguage(QString value);
    void setTargetLanguage(QString value);
    void setShowOriginal(bool value);
    void setApiBase(QString value);
    void setApiModel(QString value);
    Q_INVOKABLE void setApiKey(const QString& value);
    Q_INVOKABLE void openWindowsCaptions();
    Q_INVOKABLE void downloadModel();
    Q_INVOKABLE void cancelModelDownload();
    QString onceText() const { return onceText_; }
    QString onceStatus() const { return onceStatus_; }
    bool onceBusy() const { return onceRequest_ != nullptr || !oncePending_.isEmpty(); }
    bool onceCut() const { return onceCut_; }
    int onceReady() const;
    bool onceOnline() const;
    Q_INVOKABLE bool translateOnce(const QString& text);
    Q_INVOKABLE void clearOnce();
    Q_INVOKABLE void onceDownloadModel();
    void setPlayback(quint64 session, bool active, bool discontinuity);
signals:
    void changed();
    void captionChanged();
    void onceChanged();
private:
    void setStatus(QString status);
    void tick();
    void readCaptions();
    void acceptCaption(const QString& text);
    void resetCaption();
    void cancelRequest();
    void startLocal();
    void stopLocal();
    void requestTranslation();
    void receiveTranslation(QNetworkReply* reply, const QString& text);

    TranslationAssets assets_;
    QSettings settings_;
    QNetworkAccessManager remoteNetwork_, localNetwork_;
    QProcess reader_, server_;
    QTimer timer_;
    QElapsedTimer clock_;
    QNetworkReply* request_ = nullptr;
    void* serverJob_ = nullptr;
    bool enabled_ = false, active_ = false, captionsOpen_ = false, serverFailed_ = false, readerStarted_ = false;
    bool showOriginal_ = true;
    int provider_ = Local;
    quint16 serverPort_ = 0;
    quint64 session_ = 0;
    qint64 newTextAt_ = 0, requestedAt_ = 0, serverStartedAt_ = 0, lastCaptionReadAt_ = 0;
    QByteArray captionBuffer_, serverKey_;
    QString raw_, caption_, translated_, requestedText_, status_;
    QString sourceLanguage_, targetLanguage_, apiBase_, apiModel_;
    QByteArray apiKey_;
    // PushFPS: translateOnce. While `onceHoldUntil_` is set the local model is kept running for it,
    // also with live translation off; with it at 0 every line of the live path is what it was.
    bool onceHeld() const { return onceHoldUntil_ > 0; }
    void onceTick();
    void onceSend();
    void onceFail(const QString& why);
    void onceDrop();
    QNetworkReply* onceRequest_ = nullptr;
    QTimer onceTimer_;
    QString onceText_, onceStatus_, oncePending_;
    qint64 onceHoldUntil_ = 0, onceRetryAt_ = 0, onceAskedAt_ = 0;
    bool onceCut_ = false;
    int onceStarts_ = 0;
};
}
