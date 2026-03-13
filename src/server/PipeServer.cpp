#include "PipeServer.h"
#include "../core/Logger.h"
#include "../core/Version.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace checkdown {

PipeServer::PipeServer(QObject* parent) : QObject(parent) {
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection,
            this, &PipeServer::onNewConnection);
}

PipeServer::~PipeServer() { stop(); }

bool PipeServer::start() {
    if (m_server->isListening()) return true;

    // Remove any stale socket from a previous crash
    QLocalServer::removeServer(kPipeName);

    if (!m_server->listen(kPipeName)) {
        LOG_ERROR("PipeServer: failed to listen — {}", m_server->errorString().toStdString());
        return false;
    }
    LOG_INFO("PipeServer: listening on pipe '{}'", kPipeName);
    return true;
}

void PipeServer::stop() {
    if (m_server->isListening())
        m_server->close();
}

bool PipeServer::isRunning() const {
    return m_server->isListening();
}

void PipeServer::setDownloadListProvider(DownloadListProvider provider) {
    m_downloadListProvider = std::move(provider);
}

// ---------------------------------------------------------------------------
// Connection handling
// ---------------------------------------------------------------------------
void PipeServer::onNewConnection() {
    while (auto* sock = m_server->nextPendingConnection()) {
        LOG_DEBUG("PipeServer: NMH bridge connected");

        connect(sock, &QLocalSocket::readyRead, this, [this, sock]() {
            onReadyRead(sock);
        });
        connect(sock, &QLocalSocket::disconnected, this, [this, sock]() {
            LOG_DEBUG("PipeServer: NMH bridge disconnected");
            m_bufs.remove(sock);
            sock->deleteLater();
        });
    }
}

void PipeServer::onReadyRead(QLocalSocket* sock) {
    m_bufs[sock] += sock->readAll();
    tryProcessFrames(sock);
}

// ---------------------------------------------------------------------------
// Framing: [uint32 LE length][JSON bytes]
// ---------------------------------------------------------------------------
void PipeServer::tryProcessFrames(QLocalSocket* sock) {
    auto& buf = m_bufs[sock];

    static constexpr quint32 kMaxMessageSize = 10 * 1024 * 1024; // 10 MB sanity cap

    while (buf.size() >= 4) {
        quint32 len = 0;
        memcpy(&len, buf.constData(), 4);

        if (len > kMaxMessageSize) {
            LOG_WARN("PipeServer: message too large ({} bytes), dropping connection", len);
            sock->disconnectFromServer();
            m_bufs.remove(sock);
            return;
        }

        if (buf.size() < static_cast<int>(4 + len))
            break; // wait for more data

        QByteArray json = buf.mid(4, static_cast<int>(len));
        buf.remove(0, static_cast<int>(4 + len));

        handleMessage(sock, json);
    }
}

void PipeServer::sendMessage(QLocalSocket* sock, const QByteArray& json) {
    quint32 len = static_cast<quint32>(json.size());
    sock->write(reinterpret_cast<const char*>(&len), 4);
    sock->write(json);
    sock->flush();
}

// ---------------------------------------------------------------------------
// Message dispatch
// ---------------------------------------------------------------------------
void PipeServer::handleMessage(QLocalSocket* sock, const QByteArray& json) {
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        LOG_WARN("PipeServer: invalid JSON from bridge: {}", err.errorString().toStdString());
        return;
    }

    QJsonObject msg = doc.object();
    QString type = msg["type"].toString();
    QJsonValue id = msg["id"];   // echo back for correlation

    LOG_DEBUG("PipeServer: message type='{}'", type.toStdString());

    if (type == "ping") {
        QJsonObject resp;
        resp["id"]        = id;
        resp["connected"] = true;
        resp["app"]       = "CheckDown";
        resp["version"]   = kAppVersion;
        sendMessage(sock, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    if (type == "addUrl") {
        QString url      = msg["url"].toString();
        QString fileName = msg["fileName"].toString();
        int64_t fileSize = static_cast<int64_t>(msg["fileSize"].toDouble(-1));
        int segments     = msg["segments"].toInt(8);

        // Convert cookies JSON array → Netscape TSV lines so curl can do
        // proper domain/path matching (prevents sending cookies to CDN hosts).
        QString cookies;
        if (msg["cookies"].isArray()) {
            QStringList lines;
            for (const auto& c : msg["cookies"].toArray()) {
                if (!c.isObject()) continue;
                auto co         = c.toObject();
                QString domain  = co["domain"].toString();
                bool subdomains = domain.startsWith(".");
                QString path    = co["path"].toString();
                if (path.isEmpty()) path = "/";
                QString secure  = co["secure"].toBool() ? "TRUE" : "FALSE";
                QString expiry  = QString::number(static_cast<qint64>(co["expirationDate"].toDouble(0)));
                QString name    = co["name"].toString();
                QString value   = co["value"].toString();
                if (!name.isEmpty())
                    lines << QString("%1\t%2\t%3\t%4\t%5\t%6\t%7")
                                  .arg(domain)
                                  .arg(subdomains ? "TRUE" : "FALSE")
                                  .arg(path).arg(secure).arg(expiry)
                                  .arg(name).arg(value);
            }
            cookies = lines.join("\n");
        }

        if (!url.isEmpty()) {
            LOG_INFO("PipeServer: addUrl — url={}", url.toStdString());
            emit downloadRequested(url, fileName, fileSize, segments, cookies);
        }

        QJsonObject resp;
        resp["id"] = id;
        resp["ok"] = !url.isEmpty();
        sendMessage(sock, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    if (type == "ytdlp") {
        QString url       = msg["url"].toString();
        bool isPlaylist   = msg["isPlaylist"].toBool(false);

        // Convert cookies JSON array → Netscape TSV lines for yt-dlp --cookies file
        QStringList cookieLines;
        if (msg["cookies"].isArray()) {
            for (const auto& c : msg["cookies"].toArray()) {
                if (!c.isObject()) continue;
                auto co         = c.toObject();
                QString domain  = co["domain"].toString();
                bool subdomains = domain.startsWith(".");
                QString path    = co["path"].toString();
                if (path.isEmpty()) path = "/";
                QString secure  = co["secure"].toBool() ? "TRUE" : "FALSE";
                QString expiry  = QString::number(static_cast<qint64>(co["expirationDate"].toDouble(0)));
                QString name    = co["name"].toString();
                QString value   = co["value"].toString();
                if (!name.isEmpty())
                    cookieLines << QString("%1\t%2\t%3\t%4\t%5\t%6\t%7")
                                       .arg(domain)
                                       .arg(subdomains ? "TRUE" : "FALSE")
                                       .arg(path).arg(secure).arg(expiry)
                                       .arg(name).arg(value);
            }
        }

        if (!url.isEmpty()) {
            LOG_INFO("PipeServer: ytdlp — url={} playlist={}", url.toStdString(), isPlaylist);
            emit ytdlpRequested(url, isPlaylist, cookieLines);
        }

        QJsonObject resp;
        resp["id"] = id;
        resp["ok"] = !url.isEmpty();
        sendMessage(sock, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    if (type == "getDownloads") {
        QByteArray listJson;
        if (m_downloadListProvider) {
            listJson = m_downloadListProvider();
        } else {
            QJsonObject empty;
            empty["downloads"] = QJsonArray();
            listJson = QJsonDocument(empty).toJson(QJsonDocument::Compact);
        }

        // Inject the correlation id into the response object
        QJsonObject resp = QJsonDocument::fromJson(listJson).object();
        resp["id"] = id;
        sendMessage(sock, QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // Unknown message — return error
    QJsonObject resp;
    resp["id"]    = id;
    resp["error"] = QString("Unknown message type: %1").arg(type);
    sendMessage(sock, QJsonDocument(resp).toJson(QJsonDocument::Compact));
}

} // namespace checkdown
