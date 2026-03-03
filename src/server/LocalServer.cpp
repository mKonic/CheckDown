#include "LocalServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace checkdown {

LocalServer::LocalServer(QObject* parent)
    : QObject(parent)
{
    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection,
            this, &LocalServer::onNewConnection);
}

LocalServer::~LocalServer() {
    stop();
}

bool LocalServer::start(quint16 port) {
    if (m_server->isListening())
        return true;

    // Bind only to localhost for security
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        return false;
    }
    return true;
}

void LocalServer::stop() {
    if (m_server->isListening())
        m_server->close();
}

bool LocalServer::isRunning() const {
    return m_server->isListening();
}

void LocalServer::onNewConnection() {
    while (auto* socket = m_server->nextPendingConnection()) {
        // Read the full HTTP request when data arrives
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            handleRequest(socket);
        });

        // Clean up on disconnect
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void LocalServer::handleRequest(QTcpSocket* socket) {
    QByteArray data = socket->readAll();
    if (data.isEmpty()) return;

    // Parse the HTTP request line
    int lineEnd = data.indexOf("\r\n");
    if (lineEnd < 0) lineEnd = data.indexOf("\n");
    if (lineEnd < 0) {
        sendResponse(socket, 400, "text/plain", "Bad Request");
        return;
    }

    QString requestLine = QString::fromUtf8(data.left(lineEnd));
    QStringList parts = requestLine.split(' ');
    if (parts.size() < 2) {
        sendResponse(socket, 400, "text/plain", "Bad Request");
        return;
    }

    QString method = parts[0].toUpper();
    QString path   = parts[1];

    // Extract body (everything after the double CRLF)
    QByteArray body;
    int bodyStart = data.indexOf("\r\n\r\n");
    if (bodyStart >= 0) {
        body = data.mid(bodyStart + 4);
    }

    // --- CORS preflight ---
    if (method == "OPTIONS") {
        sendCorsHeaders(socket, 204);
        return;
    }

    // --- GET /api/ping ---
    if (method == "GET" && path == "/api/ping") {
        QJsonObject obj;
        obj["status"] = "ok";
        obj["app"]    = "CheckDown";
        obj["version"] = "1.0.0";
        sendResponse(socket, 200, "application/json",
                     QJsonDocument(obj).toJson(QJsonDocument::Compact));
        return;
    }

    // --- POST /api/download ---
    if (method == "POST" && path == "/api/download") {
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            QJsonObject errObj;
            errObj["error"] = "Invalid JSON";
            sendResponse(socket, 400, "application/json",
                         QJsonDocument(errObj).toJson(QJsonDocument::Compact));
            return;
        }

        QJsonObject obj = doc.object();
        QString url      = obj.value("url").toString();
        QString fileName = obj.value("fileName").toString();
        int64_t fileSize = static_cast<int64_t>(obj.value("fileSize").toDouble(-1));
        int segments     = obj.value("segments").toInt(8);

        if (url.isEmpty()) {
            QJsonObject errObj;
            errObj["error"] = "Missing 'url' field";
            sendResponse(socket, 400, "application/json",
                         QJsonDocument(errObj).toJson(QJsonDocument::Compact));
            return;
        }

        emit downloadRequested(url, fileName, fileSize, segments);

        QJsonObject resp;
        resp["status"]  = "ok";
        resp["message"] = "Download queued";
        sendResponse(socket, 200, "application/json",
                     QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // --- GET /api/downloads ---
    if (method == "GET" && path == "/api/downloads") {
        // For simplicity, we return an empty list here. The MainWindow can
        // connect downloadListRequested to provide actual data, but for the
        // extension's purposes the ping + download endpoints are sufficient.
        emit downloadListRequested();

        QJsonObject resp;
        resp["status"]    = "ok";
        resp["downloads"] = QJsonArray();
        sendResponse(socket, 200, "application/json",
                     QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return;
    }

    // --- 404 ---
    QJsonObject errObj;
    errObj["error"] = "Not Found";
    sendResponse(socket, 404, "application/json",
                 QJsonDocument(errObj).toJson(QJsonDocument::Compact));
}

void LocalServer::sendResponse(QTcpSocket* socket, int statusCode,
                                const QByteArray& contentType,
                                const QByteArray& body) {
    QString statusText;
    switch (statusCode) {
        case 200: statusText = "OK"; break;
        case 204: statusText = "No Content"; break;
        case 400: statusText = "Bad Request"; break;
        case 404: statusText = "Not Found"; break;
        case 500: statusText = "Internal Server Error"; break;
        default:  statusText = "OK"; break;
    }

    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText.toUtf8() + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: Content-Type\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;

    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();
}

void LocalServer::sendCorsHeaders(QTcpSocket* socket, int statusCode) {
    sendResponse(socket, statusCode, "text/plain", "");
}

void LocalServer::setDownloadListJson(const QByteArray& /*json*/) {
    // Placeholder for future use — currently /api/downloads returns
    // immediately with an empty list.
}

} // namespace checkdown
