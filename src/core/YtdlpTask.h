#pragma once

#include "Types.h"

#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTemporaryFile>
#include <functional>
#include <mutex>
#include <memory>

namespace checkdown {

using TaskProgressCallback = std::function<void(const TaskProgress&)>;

/// Wraps a yt-dlp.exe QProcess, parses its stdout progress lines, and emits
/// TaskProgress callbacks that map directly onto the existing download table.
/// Must be created and used on the Qt main thread.
class YtdlpTask : public QObject {
    Q_OBJECT
public:
    YtdlpTask(DownloadInfo       info,
              TaskProgressCallback callback,
              QStringList        cookieLines,   // Netscape-format cookie file lines
              bool               isPlaylist,
              QObject*           parent = nullptr);

    ~YtdlpTask() override;

    void start();
    void cancel();

    [[nodiscard]] DownloadInfo info() const;

private slots:
    void onReadyReadStdout();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    bool parseLine(const QString& line);   // returns true if state changed
    void emitProgress();
    void writeCookieFile();
    void cleanupCookieFile();
    [[nodiscard]] QString ytdlpBinaryPath() const;

    DownloadInfo          m_info;
    TaskProgressCallback  m_callback;
    QStringList           m_cookieLines;
    bool                  m_isPlaylist;
    QProcess*             m_process    = nullptr;
    QString               m_cookiePath;     // temp file path, empty if no cookies
    QString               m_lineBuf;        // incomplete line buffer

    // Parsed state from yt-dlp stdout
    double  m_speedBytes  = 0.0;
    double  m_etaSeconds  = -1.0;
    int     m_plDone      = 0;
    int     m_plTotal     = 0;

    mutable std::mutex m_mutex;
};

} // namespace checkdown
