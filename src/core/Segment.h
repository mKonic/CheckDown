#pragma once

#include "Types.h"
#include "HttpClient.h"

#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>

namespace checkdown {

struct SegmentProgress {
    int          segmentId       = 0;
    int64_t      downloadedBytes = 0;
    SegmentState state           = SegmentState::Pending;
    std::string  errorMessage;
};

using SegmentProgressCallback = std::function<void(const SegmentProgress&)>;

class Segment {
public:
    Segment(SegmentInfo info, std::string url, SegmentProgressCallback callback);
    ~Segment();

    Segment(const Segment&)            = delete;
    Segment& operator=(const Segment&) = delete;

    void start();
    void requestPause();
    void join();

    [[nodiscard]] SegmentInfo info() const;

private:
    void run(std::stop_token stopToken);

    SegmentInfo             m_info;
    std::string             m_url;
    SegmentProgressCallback m_callback;
    HttpClient              m_http;
    std::jthread            m_thread;
    mutable std::mutex      m_mutex;

    // Throttle progress callbacks during download
    std::chrono::steady_clock::time_point m_lastCallbackTime{};
};

} // namespace checkdown
