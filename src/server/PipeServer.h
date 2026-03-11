#pragma once

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QHash>
#include <QStringList>
#include <functional>
#include "../core/Version.h"

namespace checkdown {

/// Named-pipe server for Chrome extension communication via Native Messaging.
/// Protocol: [uint32 LE length][UTF-8 JSON] — same framing as the NMH protocol.
/// Pipe name: "CheckDown"  →  \\.\pipe\CheckDown on Windows.
///
/// The NMH bridge process (spawned by Chrome) connects here and relays
/// messages from Chrome's stdin/stdout.
class PipeServer : public QObject {
    Q_OBJECT
public:

    using DownloadListProvider = std::function<QByteArray()>;

    explicit PipeServer(QObject* parent = nullptr);
    ~PipeServer() override;

    bool start();
    void stop();
    bool isRunning() const;

    void setDownloadListProvider(DownloadListProvider provider);

signals:
    void downloadRequested(const QString& url, const QString& fileName,
                           int64_t fileSize, int segments, const QString& cookies);
    void ytdlpRequested(const QString& url, bool isPlaylist,
                        const QStringList& cookieLines);

private slots:
    void onNewConnection();

private:
    void onReadyRead(QLocalSocket* socket);
    void tryProcessFrames(QLocalSocket* socket);
    void handleMessage(QLocalSocket* socket, const QByteArray& json);
    void sendMessage(QLocalSocket* socket, const QByteArray& json);

    QLocalServer*                    m_server = nullptr;
    QHash<QLocalSocket*, QByteArray> m_bufs;
    DownloadListProvider             m_downloadListProvider;
};

} // namespace checkdown
