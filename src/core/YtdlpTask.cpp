#include "YtdlpTask.h"
#include "Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

namespace checkdown {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static double parseSizeToBytes(const QString& num, const QString& unit) {
    double v = num.toDouble();
    QString u = unit.toLower();
    if (u.startsWith("k")) return v * 1024.0;
    if (u.startsWith("m")) return v * 1024.0 * 1024.0;
    if (u.startsWith("g")) return v * 1024.0 * 1024.0 * 1024.0;
    return v;
}

static double parseEta(const QString& eta) {
    QStringList parts = eta.split(":");
    if (parts.size() == 2) return parts[0].toDouble() * 60.0  + parts[1].toDouble();
    if (parts.size() == 3) return parts[0].toDouble() * 3600.0 + parts[1].toDouble() * 60.0 + parts[2].toDouble();
    return -1.0;
}

// ---------------------------------------------------------------------------
// Ctor / dtor
// ---------------------------------------------------------------------------

YtdlpTask::YtdlpTask(DownloadInfo info,
                     TaskProgressCallback callback,
                     QStringList cookieLines,
                     bool isPlaylist,
                     QObject* parent)
    : QObject(parent)
    , m_info(std::move(info))
    , m_callback(std::move(callback))
    , m_cookieLines(std::move(cookieLines))
    , m_isPlaylist(isPlaylist)
{
}

YtdlpTask::~YtdlpTask() {
    cleanupCookieFile();
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
}

// ---------------------------------------------------------------------------
// start
// ---------------------------------------------------------------------------

void YtdlpTask::start() {
    writeCookieFile();

    QStringList args;
    args << "--no-colors" << "--newline";

    // Best video up to 1080p + best audio (merges into mkv/mp4).
    // Falls back to best single-stream format if the site doesn't support
    // separate video/audio, or if the video's max quality is below 1080p.
    args << "-f" << "bestvideo[height<=1080]+bestaudio/best[height<=1080]/best";
    args << "--merge-output-format" << "mp4";

    if (!m_cookiePath.isEmpty())
        args << "--cookies" << m_cookiePath;

    args << (m_isPlaylist ? "--yes-playlist" : "--no-playlist");

    QString outDir = QString::fromStdString(m_info.savePath.string());
    QDir().mkpath(outDir);
    args << "-o" << (outDir + "/%(title)s.%(ext)s");
    args << QString::fromStdString(m_info.url);

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &YtdlpTask::onReadyReadStdout);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &YtdlpTask::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &YtdlpTask::onProcessError);

    {
        std::lock_guard lk(m_mutex);
        m_info.state = DownloadState::Downloading;
    }

    LOG_INFO("YtdlpTask {}: starting — playlist={} url={}", m_info.id, m_isPlaylist, m_info.url);
    m_process->start(ytdlpBinaryPath(), args);

    if (!m_process->waitForStarted(5000)) {
        {
            std::lock_guard lk(m_mutex);
            m_info.state        = DownloadState::Failed;
            m_info.errorMessage = "yt-dlp failed to start: "
                                + m_process->errorString().toStdString();
            LOG_ERROR("YtdlpTask {}: {}", m_info.id, m_info.errorMessage);
        }
        emitProgress();   // called OUTSIDE lock
        return;
    }

    emitProgress();
}

// ---------------------------------------------------------------------------
// cancel
// ---------------------------------------------------------------------------

void YtdlpTask::cancel() {
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(3000);
    }
    {
        std::lock_guard lk(m_mutex);
        m_info.state = DownloadState::Cancelled;
    }
    cleanupCookieFile();
    LOG_INFO("YtdlpTask {}: cancelled", m_info.id);
    emitProgress();   // called OUTSIDE lock
}

// ---------------------------------------------------------------------------
// info snapshot
// ---------------------------------------------------------------------------

DownloadInfo YtdlpTask::info() const {
    std::lock_guard lk(m_mutex);
    return m_info;
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------

void YtdlpTask::onReadyReadStdout() {
    m_lineBuf += QString::fromUtf8(m_process->readAllStandardOutput());
    int nl;
    while ((nl = m_lineBuf.indexOf('\n')) != -1) {
        QString line = m_lineBuf.left(nl).trimmed();
        m_lineBuf.remove(0, nl + 1);
        if (parseLine(line))
            emitProgress();   // called OUTSIDE any lock — parseLine returns true when state changed
    }
}

void YtdlpTask::onProcessFinished(int exitCode, QProcess::ExitStatus) {
    // Flush remaining output
    m_lineBuf += QString::fromUtf8(m_process->readAllStandardOutput());
    int nl;
    while ((nl = m_lineBuf.indexOf('\n')) != -1) {
        parseLine(m_lineBuf.left(nl).trimmed());
        m_lineBuf.remove(0, nl + 1);
    }

    cleanupCookieFile();

    QString stderrMsg;
    if (exitCode != 0)
        stderrMsg = QString::fromUtf8(m_process->readAllStandardError()).trimmed();

    {
        std::lock_guard lk(m_mutex);
        if (m_info.state != DownloadState::Cancelled) {
            if (exitCode == 0) {
                m_info.state = DownloadState::Completed;
                if (m_info.totalSize > 0)
                    m_info.downloadedBytes = m_info.totalSize;
            } else {
                m_info.state        = DownloadState::Failed;
                m_info.errorMessage = stderrMsg.isEmpty()
                    ? "yt-dlp exited with code " + std::to_string(exitCode)
                    : stderrMsg.toStdString();
            }
        }
    }

    LOG_INFO("YtdlpTask {}: finished — exitCode={}", m_info.id, exitCode);
    emitProgress();   // called OUTSIDE lock
}

void YtdlpTask::onProcessError(QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
        {
            std::lock_guard lk(m_mutex);
            m_info.state        = DownloadState::Failed;
            m_info.errorMessage = "yt-dlp not found at: " + ytdlpBinaryPath().toStdString();
            LOG_ERROR("YtdlpTask {}: {}", m_info.id, m_info.errorMessage);
        }
        emitProgress();   // called OUTSIDE lock
    }
}

// ---------------------------------------------------------------------------
// Line parser — returns true if progress state changed (caller should emit)
// ---------------------------------------------------------------------------

bool YtdlpTask::parseLine(const QString& line) {
    if (line.isEmpty()) return false;

    // [download] Downloading item 3 of 12
    static QRegularExpression reItem(R"(\[download\] Downloading item (\d+) of (\d+))");
    auto m = reItem.match(line);
    if (m.hasMatch()) {
        std::lock_guard lk(m_mutex);
        m_plDone  = m.captured(1).toInt();
        m_plTotal = m.captured(2).toInt();
        m_info.fileName = "Playlist [" + std::to_string(m_plDone)
                        + "/" + std::to_string(m_plTotal) + "]";
        return true;
    }

    // [download] Destination: /path/to/file.mp4
    static QRegularExpression reDest(R"(\[download\] Destination: (.+))");
    m = reDest.match(line);
    if (m.hasMatch()) {
        QFileInfo fi(m.captured(1).trimmed());
        std::lock_guard lk(m_mutex);
        if (!m_isPlaylist)
            m_info.fileName = fi.fileName().toStdString();
        return true;
    }

    // [download]  45.3% of  123.45MiB at  2.34MiB/s ETA 00:42
    static QRegularExpression reProgress(
        R"(\[download\]\s+([\d.]+)%\s+of\s+([\d.]+)(\w+)\s+at\s+([\d.]+)(\w+)/s\s+ETA\s+([\d:]+))");
    m = reProgress.match(line);
    if (m.hasMatch()) {
        double percent = m.captured(1).toDouble();
        double totalB  = parseSizeToBytes(m.captured(2), m.captured(3));
        double speedB  = parseSizeToBytes(m.captured(4), m.captured(5));
        double eta     = parseEta(m.captured(6));
        std::lock_guard lk(m_mutex);
        m_info.totalSize       = static_cast<int64_t>(totalB);
        m_info.downloadedBytes = static_cast<int64_t>(totalB * percent / 100.0);
        m_speedBytes           = speedB;
        m_etaSeconds           = eta;
        return true;
    }

    // [download] 100% of 123.45MiB in 00:53
    static QRegularExpression reComplete(R"(\[download\] 100% of\s+([\d.]+)(\w+) in)");
    m = reComplete.match(line);
    if (m.hasMatch()) {
        double totalB = parseSizeToBytes(m.captured(1), m.captured(2));
        std::lock_guard lk(m_mutex);
        m_info.totalSize       = static_cast<int64_t>(totalB);
        m_info.downloadedBytes = m_info.totalSize;
        m_speedBytes           = 0.0;
        m_etaSeconds           = 0.0;
        return true;
    }

    return false;
}

// ---------------------------------------------------------------------------
// emitProgress — copies state under lock, fires callback outside the lock
// ---------------------------------------------------------------------------

void YtdlpTask::emitProgress() {
    TaskProgress tp;
    {
        std::lock_guard lk(m_mutex);
        tp.taskId           = m_info.id;
        tp.totalBytes       = m_info.totalSize;
        tp.downloadedBytes  = m_info.downloadedBytes;
        tp.state            = m_info.state;
        tp.speedBytesPerSec = m_speedBytes;
        tp.etaSeconds       = m_etaSeconds;
        tp.fileName         = m_info.fileName;
        tp.errorMessage     = m_info.errorMessage;
    }
    if (m_callback) m_callback(tp);
}

// ---------------------------------------------------------------------------
// Cookie file
// ---------------------------------------------------------------------------

void YtdlpTask::writeCookieFile() {
    if (m_cookieLines.isEmpty()) return;

    QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    m_cookiePath = tmpDir + "/checkdown_cookies_" + QString::number(m_info.id) + ".txt";

    QFile f(m_cookiePath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        LOG_WARN("YtdlpTask {}: could not write cookie file", m_info.id);
        m_cookiePath.clear();
        return;
    }
    f.write("# Netscape HTTP Cookie File\n");
    for (const auto& line : m_cookieLines)
        f.write((line + "\n").toUtf8());
    f.close();
}

void YtdlpTask::cleanupCookieFile() {
    if (!m_cookiePath.isEmpty()) {
        QFile::remove(m_cookiePath);
        m_cookiePath.clear();
    }
}

// ---------------------------------------------------------------------------
// Binary path
// ---------------------------------------------------------------------------

QString YtdlpTask::ytdlpBinaryPath() const {
    return QDir(QCoreApplication::applicationDirPath())
               .filePath("vendor/yt-dlp/yt-dlp.exe");
}

} // namespace checkdown
