#include "DownloadManager.h"
#include "YtdlpTask.h"
#include "Logger.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <format>
#include <algorithm>
#include <ranges>

namespace checkdown {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// JSON serialisation helpers
// ---------------------------------------------------------------------------
static json segmentToJson(const SegmentInfo& s) {
    return {
        {"id",              s.id},
        {"startByte",       s.startByte},
        {"endByte",         s.endByte},
        {"downloadedBytes", s.downloadedBytes},
        {"state",           toString(s.state)},
        {"tempFilePath",    s.tempFilePath},
    };
}

static SegmentInfo segmentFromJson(const json& j) {
    SegmentInfo s;
    s.id              = j.value("id", 0);
    s.startByte       = j.value("startByte", int64_t(0));
    s.endByte         = j.value("endByte", int64_t(-1));
    s.downloadedBytes = j.value("downloadedBytes", int64_t(0));
    s.tempFilePath    = j.value("tempFilePath", std::string{});

    auto st = j.value("state", std::string{"Pending"});
    if      (st == "Completed")   s.state = SegmentState::Completed;
    else if (st == "Paused")      s.state = SegmentState::Paused;
    else if (st == "Failed")      s.state = SegmentState::Failed;
    else if (st == "Downloading") s.state = SegmentState::Paused; // saved mid-download → paused
    else                          s.state = SegmentState::Pending;

    return s;
}

static json downloadToJson(const DownloadInfo& d) {
    json segs = json::array();
    for (auto& s : d.segments) segs.push_back(segmentToJson(s));

    return {
        {"id",             d.id},
        {"url",            d.url},
        {"fileName",       d.fileName},
        {"savePath",       d.savePath.string()},
        {"totalSize",      d.totalSize},
        {"rangeSupported", d.rangeSupported},
        {"state",          toString(d.state)},
        {"segmentCount",   d.segmentCount},
        {"segments",       segs},
    };
}

static DownloadInfo downloadFromJson(const json& j) {
    DownloadInfo d;
    d.id             = j.value("id", 0);
    d.url            = j.value("url", std::string{});
    d.fileName       = j.value("fileName", std::string{});
    d.savePath       = j.value("savePath", std::string{});
    d.totalSize      = j.value("totalSize", int64_t(-1));
    d.rangeSupported = j.value("rangeSupported", false);
    d.segmentCount   = j.value("segmentCount", kDefaultSegmentCount);

    auto st = j.value("state", std::string{"Queued"});
    if      (st == "Downloading") d.state = DownloadState::Paused; // was active → now paused
    else if (st == "Paused")      d.state = DownloadState::Paused;
    else if (st == "Queued")      d.state = DownloadState::Queued;
    else if (st == "Completed")   d.state = DownloadState::Completed;
    else if (st == "Failed")      d.state = DownloadState::Failed;
    else                          d.state = DownloadState::Queued;

    if (j.contains("segments") && j["segments"].is_array()) {
        for (auto& sj : j["segments"])
            d.segments.push_back(segmentFromJson(sj));
    }

    return d;
}

// ---------------------------------------------------------------------------
// Ctor / dtor
// ---------------------------------------------------------------------------
DownloadManager::DownloadManager(std::filesystem::path stateDir,
                                 ManagerProgressCallback callback)
    : m_stateDir(std::move(stateDir))
    , m_callback(std::move(callback))
{
    std::error_code ec;
    std::filesystem::create_directories(m_stateDir, ec);
}

DownloadManager::~DownloadManager() {
    // Pause/cancel everything before destruction
    for (auto& task : m_tasks) {
        if (task->info().state == DownloadState::Downloading)
            task->pause();
    }
    for (auto& task : m_ytdlpTasks) {
        if (task->info().state == DownloadState::Downloading)
            task->cancel();
    }
}

// ---------------------------------------------------------------------------
// addDownload
// ---------------------------------------------------------------------------
int DownloadManager::addDownload(const std::string& url,
                                  const std::filesystem::path& savePath,
                                  const std::string& fileName,
                                  int segmentCount,
                                  const std::string& cookies) {
    DownloadInfo info;
    {
        std::lock_guard lk(m_mutex);
        info.id           = m_nextId++;
        info.url          = url;
        info.savePath     = savePath;
        info.fileName     = fileName;
        info.segmentCount = segmentCount;
        info.cookies      = cookies;
        info.state        = DownloadState::Queued;
        info.addedTime    = std::chrono::system_clock::now();
    }

    auto task = std::make_unique<DownloadTask>(
        info,
        [this](const TaskProgress& p) { onTaskProgress(p); }
    );

    int id = info.id;
    {
        std::lock_guard lk(m_mutex);
        m_tasks.push_back(std::move(task));
    }

    LOG_INFO("DownloadManager: queued task {} — url={}", id, url);
    scheduleNext();
    saveState();
    return id;
}

int DownloadManager::addYtdlpDownload(const std::string& url,
                                       const std::filesystem::path& savePath,
                                       const std::string& label,
                                       const QStringList& cookieLines,
                                       bool isPlaylist) {
    DownloadInfo info;
    {
        std::lock_guard lk(m_mutex);
        info.id        = m_nextId++;
        info.url       = url;
        info.savePath  = savePath;
        info.fileName  = label.empty() ? "yt-dlp download" : label;
        info.state     = DownloadState::Queued;
        info.isYtdlp   = true;
        info.addedTime = std::chrono::system_clock::now();
    }

    int id = info.id;
    auto task = std::make_unique<YtdlpTask>(
        info,
        [this](const TaskProgress& p) { onYtdlpProgress(p); },
        cookieLines,
        isPlaylist
    );

    {
        std::lock_guard lk(m_mutex);
        m_ytdlpTasks.push_back(std::move(task));
    }

    LOG_INFO("DownloadManager: queued yt-dlp task {} — url={}", id, url);
    scheduleNextYtdlp();
    // Do NOT save state — yt-dlp tasks are ephemeral
    return id;
}

// ---------------------------------------------------------------------------
// Control
// ---------------------------------------------------------------------------
void DownloadManager::startDownload(int taskId) {
    DownloadTask* task = findTask(taskId);
    if (!task) return;

    auto info = task->info();
    if (info.state == DownloadState::Paused || info.state == DownloadState::Failed) {
        // If we have segment data (from a previous run), just start
        if (!info.segments.empty()) {
            task->start();
        } else {
            auto res = task->prepare();
            if (res.has_value())
                task->start();
        }
    } else if (info.state == DownloadState::Queued) {
        auto res = task->prepare();
        if (res.has_value())
            task->start();
    }

    saveState();
}

void DownloadManager::pauseDownload(int taskId) {
    DownloadTask* task = findTask(taskId);
    if (!task) return;

    auto info = task->info();
    if (info.state == DownloadState::Downloading)
        task->pause();

    saveState();
}

void DownloadManager::cancelDownload(int taskId) {
    if (auto* task = findTask(taskId)) {
        task->cancel();
        scheduleNext();
        saveState();
        return;
    }
    if (auto* task = findYtdlpTask(taskId)) {
        task->cancel();
        // scheduleNextYtdlp will be triggered via onYtdlpProgress callback
    }
}

void DownloadManager::removeDownload(int taskId) {
    // Regular task
    {
        DownloadTask* task = findTask(taskId);
        if (task) {
            if (task->info().state == DownloadState::Downloading)
                task->cancel();
            {
                std::lock_guard lk(m_mutex);
                std::erase_if(m_tasks, [taskId](const auto& t) {
                    return t->info().id == taskId;
                });
            }
            scheduleNext();   // called outside the lock to avoid recursive deadlock
            saveState();
            return;
        }
    }
    // yt-dlp task
    {
        YtdlpTask* task = findYtdlpTask(taskId);
        if (task) {
            if (task->info().state == DownloadState::Downloading)
                task->cancel();
            {
                std::lock_guard lk(m_mutex);
                std::erase_if(m_ytdlpTasks, [taskId](const auto& t) {
                    return t->info().id == taskId;
                });
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Concurrency
// ---------------------------------------------------------------------------
void DownloadManager::setMaxConcurrent(int max) {
    {
        std::lock_guard lk(m_mutex);
        m_maxConcurrent = std::max(1, max);
    }
    scheduleNext();
}

int DownloadManager::maxConcurrent() const {
    std::lock_guard lk(m_mutex);
    return m_maxConcurrent;
}

// ---------------------------------------------------------------------------
// State persistence
// ---------------------------------------------------------------------------
void DownloadManager::clearFinished() {
    {
        std::lock_guard lk(m_mutex);
        auto finished = [](const auto& t) {
            auto s = t->info().state;
            return s == DownloadState::Completed ||
                   s == DownloadState::Cancelled ||
                   s == DownloadState::Failed;
        };
        std::erase_if(m_tasks,       finished);
        std::erase_if(m_ytdlpTasks,  finished);
    }
    saveState();
}

void DownloadManager::saveState() {
    json root;
    {
        std::lock_guard lk(m_mutex);
        root["version"]       = 1;
        root["maxConcurrent"] = m_maxConcurrent;
        root["nextId"]        = m_nextId;

        json downloads = json::array();
        for (auto& task : m_tasks) {
            auto info = task->info();
            // Skip cancelled — no useful state
            if (info.state == DownloadState::Cancelled)
                continue;
            downloads.push_back(downloadToJson(info));
        }
        root["downloads"] = downloads;
    }

    auto statePath = m_stateDir / kStateFileName;
    auto tempPath  = m_stateDir / (std::string(kStateFileName) + ".tmp");

    std::ofstream out(tempPath, std::ios::trunc);
    if (out.is_open()) {
        out << root.dump(2);
        out.close();
        std::error_code ec;
        std::filesystem::rename(tempPath, statePath, ec);
    }
}

void DownloadManager::loadState() {
    auto statePath = m_stateDir / kStateFileName;
    std::ifstream in(statePath);
    if (!in.is_open()) return;

    json root;
    try {
        in >> root;
    } catch (...) {
        return; // corrupt file, ignore
    }

    std::lock_guard lk(m_mutex);
    m_maxConcurrent = root.value("maxConcurrent", kDefaultMaxConcurrent);
    m_nextId        = root.value("nextId", 1);

    if (root.contains("downloads") && root["downloads"].is_array()) {
        for (auto& dj : root["downloads"]) {
            auto info = downloadFromJson(dj);
            auto task = std::make_unique<DownloadTask>(
                info,
                [this](const TaskProgress& p) { onTaskProgress(p); }
            );
            task->restoreInfo(info);
            m_tasks.push_back(std::move(task));
        }
    }
}

// ---------------------------------------------------------------------------
// Snapshot
// ---------------------------------------------------------------------------
std::vector<DownloadInfo> DownloadManager::allDownloads() const {
    std::lock_guard lk(m_mutex);
    std::vector<DownloadInfo> result;
    result.reserve(m_tasks.size() + m_ytdlpTasks.size());
    for (auto& t : m_tasks)       result.push_back(t->info());
    for (auto& t : m_ytdlpTasks)  result.push_back(t->info());
    return result;
}

// ---------------------------------------------------------------------------
// Internal
// ---------------------------------------------------------------------------
void DownloadManager::scheduleNext() {
    std::vector<DownloadTask*> toStart;

    {
        std::lock_guard lk(m_mutex);

        int active = 0;
        for (auto& t : m_tasks)      if (t->info().state == DownloadState::Downloading) ++active;
        for (auto& t : m_ytdlpTasks) if (t->info().state == DownloadState::Downloading) ++active;

        for (auto& t : m_tasks) {
            if (active >= m_maxConcurrent) break;
            if (t->info().state == DownloadState::Queued) {
                toStart.push_back(t.get());
                ++active;
            }
        }
    }

    for (auto* task : toStart) {
        auto res = task->prepare();
        if (res.has_value())
            task->start();
    }
}

void DownloadManager::scheduleNextYtdlp() {
    // Must run on the main (Qt) thread — QProcess requires it.
    std::vector<YtdlpTask*> toStart;

    {
        std::lock_guard lk(m_mutex);

        int active = 0;
        for (auto& t : m_tasks)      if (t->info().state == DownloadState::Downloading) ++active;
        for (auto& t : m_ytdlpTasks) if (t->info().state == DownloadState::Downloading) ++active;

        for (auto& t : m_ytdlpTasks) {
            if (active >= m_maxConcurrent) break;
            if (t->info().state == DownloadState::Queued) {
                toStart.push_back(t.get());
                ++active;
            }
        }
    }

    for (auto* task : toStart)
        task->start();
}

void DownloadManager::onTaskProgress(const TaskProgress& progress) {
    // Forward to the UI callback first (this crosses the thread boundary
    // via Qt::QueuedConnection so it's safe).
    if (m_callback)
        m_callback(progress);

    // If a download finished, schedule the next one. This runs on a worker
    // thread but scheduleNext() only briefly locks m_mutex to collect tasks,
    // then starts them outside the lock.
    if (progress.state == DownloadState::Completed ||
        progress.state == DownloadState::Failed ||
        progress.state == DownloadState::Cancelled) {
        scheduleNext();
    }
}

void DownloadManager::onYtdlpProgress(const TaskProgress& progress) {
    if (m_callback)
        m_callback(progress);

    // Fired on the main thread (QProcess signals) — safe to schedule yt-dlp tasks here.
    if (progress.state == DownloadState::Completed ||
        progress.state == DownloadState::Failed    ||
        progress.state == DownloadState::Cancelled) {
        scheduleNext();
        scheduleNextYtdlp();
    }
}

DownloadTask* DownloadManager::findTask(int taskId) {
    std::lock_guard lk(m_mutex);
    for (auto& t : m_tasks) {
        if (t->info().id == taskId)
            return t.get();
    }
    return nullptr;
}

YtdlpTask* DownloadManager::findYtdlpTask(int taskId) {
    std::lock_guard lk(m_mutex);
    for (auto& t : m_ytdlpTasks) {
        if (t->info().id == taskId)
            return t.get();
    }
    return nullptr;
}

} // namespace checkdown
