#pragma once

#include "Types.h"
#include "DownloadTask.h"

#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <filesystem>

namespace checkdown {

using ManagerProgressCallback = std::function<void(const TaskProgress&)>;

class DownloadManager {
public:
    explicit DownloadManager(std::filesystem::path stateDir,
                             ManagerProgressCallback callback);
    ~DownloadManager();

    DownloadManager(const DownloadManager&)            = delete;
    DownloadManager& operator=(const DownloadManager&) = delete;

    /// Add a new download, returns the task ID.
    int addDownload(const std::string& url,
                    const std::filesystem::path& savePath,
                    const std::string& fileName = {},
                    int segmentCount = kDefaultSegmentCount);

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
    void onTaskProgress(const TaskProgress& progress);
    DownloadTask* findTask(int taskId);

    std::filesystem::path                          m_stateDir;
    ManagerProgressCallback                        m_callback;
    std::vector<std::unique_ptr<DownloadTask>>     m_tasks;
    int                                            m_maxConcurrent = kDefaultMaxConcurrent;
    int                                            m_nextId        = 1;
    mutable std::mutex                             m_mutex;
};

} // namespace checkdown
