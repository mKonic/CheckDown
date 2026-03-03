#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>

namespace checkdown {

/// Lightweight HTTP server on 127.0.0.1:18693 for Chrome extension communication.
/// Supports: GET /api/ping, POST /api/download, GET /api/downloads, OPTIONS *.
class LocalServer : public QObject {
    Q_OBJECT
public:
    static constexpr quint16 kDefaultPort = 18693;

    explicit LocalServer(QObject* parent = nullptr);
    ~LocalServer() override;

    bool start(quint16 port = kDefaultPort);
    void stop();
    bool isRunning() const;

signals:
    /// Emitted when the Chrome extension sends a download request.
    void downloadRequested(const QString& url, const QString& fileName,
                           int64_t fileSize, int segments);

    /// Emitted when the extension requests the current download list.
    /// The slot should call sendDownloadList() with the JSON payload.
    void downloadListRequested();

public slots:
    /// Provide the download list JSON to a pending /api/downloads request.
    void setDownloadListJson(const QByteArray& json);

private slots:
    void onNewConnection();

private:
    void handleRequest(QTcpSocket* socket);
    void sendResponse(QTcpSocket* socket, int statusCode,
                      const QByteArray& contentType, const QByteArray& body);
    void sendCorsHeaders(QTcpSocket* socket, int statusCode);

    QTcpServer* m_server = nullptr;
};

} // namespace checkdown
