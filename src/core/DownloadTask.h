#pragma once

#include "Types.h"
#include "Segment.h"

#include <vector>
#include <memory>
#include <mutex>
#include <functional>
#include <expected>
#include <chrono>

namespace checkdown {

using TaskProgressCallback = std::function<void(const TaskProgress&)>;

class DownloadTask {
public:
    DownloadTask(DownloadInfo info, TaskProgressCallback callback);
    ~DownloadTask();

    DownloadTask(const DownloadTask&)            = delete;
    DownloadTask& operator=(const DownloadTask&) = delete;

    /// HEAD probe + compute segment plan. Safe to call from any thread.
    [[nodiscard]]
    std::expected<void, std::string> prepare();

    /// Start or resume downloading.
    void start();

    /// Request all segments to pause (non-blocking).
    void pause();

    /// Cancel and remove temp files.
    void cancel();

    /// Thread-safe snapshot.
    [[nodiscard]] DownloadInfo info() const;

    /// Update info from a previously saved state (for restore).
    void restoreInfo(DownloadInfo saved);

private:
    void planSegments();
    void onSegmentProgress(const SegmentProgress& progress);
    void mergeSegments();
    void emitProgress();

    DownloadInfo                              m_info;
    TaskProgressCallback                      m_callback;
    std::vector<std::unique_ptr<Segment>>     m_segments;
    mutable std::mutex                        m_mutex;

    // Speed calculation
    std::chrono::steady_clock::time_point     m_lastSpeedTime;
    int64_t                                   m_lastSpeedBytes = 0;
    double                                    m_currentSpeed   = 0.0;
};

} // namespace checkdown
