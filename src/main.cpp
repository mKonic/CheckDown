#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>
#include <cstdint>
#ifdef _WIN32
#  include <windows.h>
#  include <io.h>
#  include <fcntl.h>
#endif
#include "ui/MainWindow.h"
#include "core/HttpClient.h"
#include "core/Types.h"
#include "core/Version.h"

Q_DECLARE_METATYPE(checkdown::TaskProgress)

// ---------------------------------------------------------------------------
// Detect whether Chrome spawned us as a Native Messaging Host.
// Chrome connects a pipe to our stdin — that's the only reliable signal.
// We do NOT rely on a command-line flag because Chrome passes none.
// ---------------------------------------------------------------------------
static bool isNativeMessagingMode() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == NULL) return false;
    return GetFileType(h) == FILE_TYPE_PIPE;
#else
    struct stat st;
    return fstat(STDIN_FILENO, &st) == 0 && S_ISFIFO(st.st_mode);
#endif
}

// ---------------------------------------------------------------------------
// NMH bridge: relay Chrome ↔ the already-running app's named pipe.
// Never starts the app itself — if the pipe is absent we tell Chrome
// "not running" and exit cleanly.
// ---------------------------------------------------------------------------
static bool readStdin(void* buf, uint32_t len) {
    return fread(buf, 1, len, stdin) == static_cast<size_t>(len);
}
static bool writeStdout(const void* buf, uint32_t len) {
    return fwrite(buf, 1, len, stdout) == static_cast<size_t>(len)
        && fflush(stdout) == 0;
}

static QByteArray readNmhMessage() {
    uint32_t len = 0;
    if (!readStdin(&len, 4) || len == 0 || len > 1024 * 1024) return {};
    QByteArray data(static_cast<int>(len), '\0');
    if (!readStdin(data.data(), len)) return {};
    return data;
}
static bool writeNmhMessage(const QByteArray& json) {
    uint32_t len = static_cast<uint32_t>(json.size());
    return writeStdout(&len, 4) && writeStdout(json.constData(), len);
}

static int runNativeMessagingBridge() {
#ifdef _WIN32
    _setmode(_fileno(stdin),  _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    // Read the one message Chrome sends via sendNativeMessage
    QByteArray msg = readNmhMessage();
    if (msg.isEmpty()) return 0;

    // Parse to extract the correlation id for the response
    QJsonParseError parseErr;
    QJsonObject req = QJsonDocument::fromJson(msg, &parseErr).object();
    QJsonValue  id  = req.value("id");

    // Try to reach the already-running UI app via named pipe.
    // Never start the app ourselves — if the pipe is absent we say so and exit.
    QLocalSocket pipe;
    pipe.connectToServer("CheckDown");
    if (!pipe.waitForConnected(2000)) {
        QJsonObject resp;
        resp["id"]        = id;
        resp["connected"] = false;
        resp["error"]     = "CheckDown is not running. Please launch CheckDown first.";
        writeNmhMessage(QJsonDocument(resp).toJson(QJsonDocument::Compact));
        return 0;
    }

    // Forward the message to the UI app (framed)
    uint32_t fwdLen = static_cast<uint32_t>(msg.size());
    pipe.write(reinterpret_cast<const char*>(&fwdLen), 4);
    pipe.write(msg);
    pipe.flush();

    // Wait for the one response
    QByteArray pipeBuf;
    QByteArray resp;
    while (resp.isEmpty()) {
        if (!pipe.waitForReadyRead(5000)) break;
        pipeBuf += pipe.readAll();
        // Try to extract a framed response
        if (pipeBuf.size() >= 4) {
            uint32_t rlen = 0;
            memcpy(&rlen, pipeBuf.constData(), 4);
            if (pipeBuf.size() >= static_cast<int>(4 + rlen)) {
                resp = pipeBuf.mid(4, static_cast<int>(rlen));
            }
        }
    }

    if (!resp.isEmpty()) {
        writeNmhMessage(resp);
    } else {
        QJsonObject errResp;
        errResp["id"]    = id;
        errResp["error"] = "No response from CheckDown";
        writeNmhMessage(QJsonDocument(errResp).toJson(QJsonDocument::Compact));
    }

    pipe.disconnectFromServer();
    return 0;
}

// ---------------------------------------------------------------------------
// Normal UI entry point
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    // Must check stdin BEFORE creating QApplication (which may touch stdio)
    if (isNativeMessagingMode()) {
        QCoreApplication coreApp(argc, argv);
        return runNativeMessagingBridge();
    }

    checkdown::HttpClient::globalInit();

    QApplication app(argc, argv);
    app.setApplicationName("CheckDown");
    app.setOrganizationName("CheckDown");
    app.setApplicationVersion(checkdown::kAppVersion);
    app.setQuitOnLastWindowClosed(false);

    QIcon appIcon;
    appIcon.addFile(":/checkdown_16.png",  QSize(16, 16));
    appIcon.addFile(":/checkdown_32.png",  QSize(32, 32));
    appIcon.addFile(":/checkdown_48.png",  QSize(48, 48));
    appIcon.addFile(":/checkdown_128.png", QSize(128, 128));
    app.setWindowIcon(appIcon);

    qRegisterMetaType<checkdown::TaskProgress>("checkdown::TaskProgress");

    checkdown::MainWindow window;

    bool startMinimized = false;
    for (int i = 1; i < argc; ++i) {
        if (QString(argv[i]) == "--minimized") {
            startMinimized = true;
            break;
        }
    }
    if (!startMinimized) window.show();

    int ret = app.exec();
    checkdown::HttpClient::globalCleanup();
    return ret;
}
