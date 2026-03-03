#include "Segment.h"
#include <chrono>
#include <format>

namespace checkdown {

Segment::Segment(SegmentInfo info, std::string url, SegmentProgressCallback callback)
    : m_info(std::move(info))
    , m_url(std::move(url))
    , m_callback(std::move(callback))
{
}

Segment::~Segment() {
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }
}

void Segment::start() {
    {
        std::lock_guard lk(m_mutex);
        m_info.state = SegmentState::Downloading;
    }

    if (m_callback) {
        m_callback(SegmentProgress{
            m_info.id, m_info.downloadedBytes, SegmentState::Downloading, {}
        });
    }

    m_thread = std::jthread([this](std::stop_token st) { run(st); });
}

void Segment::requestPause() {
    if (m_thread.joinable())
        m_thread.request_stop();
}

void Segment::join() {
    if (m_thread.joinable())
        m_thread.join();
}

SegmentInfo Segment::info() const {
    std::lock_guard lk(m_mutex);
    return m_info;
}

void Segment::run(std::stop_token stopToken) {
    int64_t existingBytes = 0;
    {
        std::lock_guard lk(m_mutex);
        existingBytes = m_info.downloadedBytes;
    }

    auto result = m_http.downloadRange(
        m_url,
        m_info.startByte,
        m_info.endByte,
        m_info.tempFilePath,
        existingBytes,
        stopToken,
        [this](int64_t downloaded, int64_t /*total*/) {
            {
                std::lock_guard lk(m_mutex);
                m_info.downloadedBytes = downloaded;
            }

            // Throttle progress callbacks to ~4 per second
            auto now = std::chrono::steady_clock::now();
            if (now - m_lastCallbackTime >= std::chrono::milliseconds(250)) {
                m_lastCallbackTime = now;
                if (m_callback) {
                    m_callback(SegmentProgress{
                        m_info.id, downloaded, SegmentState::Downloading, {}
                    });
                }
            }
        }
    );

    std::lock_guard lk(m_mutex);

    if (result.has_value()) {
        m_info.state = SegmentState::Completed;
        // Ensure downloadedBytes reflects full range
        m_info.downloadedBytes = m_info.endByte - m_info.startByte + 1;
        if (m_callback) {
            m_callback(SegmentProgress{
                m_info.id, m_info.downloadedBytes, SegmentState::Completed, {}
            });
        }
    } else {
        auto& err = result.error();
        if (err.curlCode == 42 /* CURLE_ABORTED_BY_CALLBACK */) {
            m_info.state = SegmentState::Paused;
            if (m_callback) {
                m_callback(SegmentProgress{
                    m_info.id, m_info.downloadedBytes, SegmentState::Paused, {}
                });
            }
        } else {
            m_info.state = SegmentState::Failed;
            if (m_callback) {
                m_callback(SegmentProgress{
                    m_info.id, m_info.downloadedBytes, SegmentState::Failed, err.message
                });
            }
        }
    }
}

} // namespace checkdown
