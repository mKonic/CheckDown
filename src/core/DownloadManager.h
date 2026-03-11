#pragma once

#include "Types.h"
#include "DownloadTask.h"

#include <QStringList>
#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <filesystem>

namespace checkdown {

class YtdlpTask;

using ManagerProgressCallback = std::function<void(const TaskProgress&)>;

class DownloadManager {
public:
    explicit DownloadManager(std::filesystem::path stateDir,
                             ManagerProgressCallback callback);
    ~DownloadManager();

    DownloadManager(const DownloadManager&)            = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    /// Add a new HTTP segmented download, returns the task ID.
    int addDownload(const std::string& url,
                    const std::filesystem::path& savePath,
                    const std::string& fileName = {},
                    int segmentCount = kDefaultSegmentCount,
                    const std::string& cookies = {});

    /// Add a yt-dlp download (must be called from the main thread). Returns task ID.
    int addYtdlpDownload(const std::string& url,
                         const std::filesystem::path& savePath,
                         const std::string& label,
                         const QStringList& cookieLines,
                         bool isPlaylist);

    void startDownload(int taskId);
    void pauseDownload(int taskId);
    void cancelDownload(int taskId);
    void removeDownload(int taskId);

    /// Remove all completed/cancelled/failed entries from the list.
    void clearFinished();

    void setMaxConcurrent(int max);
    [[nodiscard]] int maxConcurrent() const;

    void saveState();
    void loadState();

    [[nodiscard]] std::vector<DownloadInfo> allDownloads() const;

private:
    void scheduleNext();
    void scheduleNextYtdlp();          // Must be called from the main (Qt) thread.
    void onTaskProgress(const TaskProgress& progress);
    void onYtdlpProgress(const TaskProgress& progress);
    DownloadTask* findTask(int taskId);
    YtdlpTask*    findYtdlpTask(int taskId);

    std::filesystem::path                          m_stateDir;
    ManagerProgressCallback                        m_callback;
    std::vector<std::unique_ptr<DownloadTask>>     m_tasks;
    std::vector<std::unique_ptr<YtdlpTask>>        m_ytdlpTasks;
    int                                            m_maxConcurrent = kDefaultMaxConcurrent;
    int                                            m_nextId        = 1;
    mutable std::mutex                             m_mutex;
};

} // namespace checkdown
