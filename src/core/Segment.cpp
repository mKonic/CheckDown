#include "Segment.h"
#include "Logger.h"
#include <chrono>
#include <format>

namespace checkdown {

Segment::Segment(SegmentInfo info, std::string url, SegmentProgressCallback callback,
                 std::string cookies)
    : m_info(std::move(info))
    , m_url(std::move(url))
    , m_cookies(std::move(cookies))
    , m_callback(std::move(callback))
{
    if (!m_cookies.empty())
        m_http.setCookies(m_cookies);
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
    static constexpr int kMaxRetries = 2;

    auto doDownload = [&]() {
        int64_t existingBytes = 0;
        {
            std::lock_guard lk(m_mutex);
            existingBytes = m_info.downloadedBytes;
        }
        LOG_DEBUG("Segment {}: starting bytes={}-{} existing={}",
                  m_info.id, m_info.startByte, m_info.endByte, existingBytes);

        return m_http.downloadRange(
            m_url,
            m_info.startByte,
            m_info.endByte,
            m_info.tempFilePath,
            existingBytes,
            stopToken,
            [this](int64_t downloaded, int64_t /*total*/) {
                bool shouldCallback = false;
                {
                    std::lock_guard lk(m_mutex);
                    m_info.downloadedBytes = downloaded;

                    // Throttle progress callbacks to ~4 per second
                    auto now = std::chrono::steady_clock::now();
                    if (now - m_lastCallbackTime >= std::chrono::milliseconds(250)) {
                        m_lastCallbackTime = now;
                        shouldCallback = true;
                    }
                }

                if (shouldCallback && m_callback) {
                    m_callback(SegmentProgress{
                        m_info.id, downloaded, SegmentState::Downloading, {}
                    });
                }
            }
        );
    };

    std::expected<void, HttpError> result;
    for (int attempt = 0; attempt <= kMaxRetries; ++attempt) {
        result = doDownload();

        if (result.has_value())
            break; // success

        auto& err = result.error();
        // Don't retry user-initiated pauses
        if (err.curlCode == 42 /* CURLE_ABORTED_BY_CALLBACK */)
            break;
        // Don't retry HTTP 4xx errors (client errors, not transient)
        if (err.curlCode >= 400 && err.curlCode < 500)
            break;

        if (attempt < kMaxRetries) {
            LOG_WARN("Segment {}: attempt {} failed ({}), retrying...",
                     m_info.id, attempt + 1, err.message);
            // Brief delay before retry (1s, 2s)
            std::this_thread::sleep_for(std::chrono::seconds(attempt + 1));
            if (stopToken.stop_requested())
                break;
        }
    }

    std::lock_guard lk(m_mutex);

    if (result.has_value()) {
        m_info.state = SegmentState::Completed;
        // For range downloads, set exact byte count.
        // For unknown-size (endByte==-1), keep the count tracked by the progress callback.
        if (m_info.endByte >= 0) {
            m_info.downloadedBytes = m_info.endByte - m_info.startByte + 1;
        }
        LOG_DEBUG("Segment {}: completed ({} bytes)", m_info.id, m_info.downloadedBytes);
        if (m_callback) {
            m_callback(SegmentProgress{
                m_info.id, m_info.downloadedBytes, SegmentState::Completed, {}
            });
        }
    } else {
        auto& err = result.error();
        if (err.curlCode == 42 /* CURLE_ABORTED_BY_CALLBACK */) {
            m_info.state = SegmentState::Paused;
            LOG_DEBUG("Segment {}: paused at {} bytes", m_info.id, m_info.downloadedBytes);
            if (m_callback) {
                m_callback(SegmentProgress{
                    m_info.id, m_info.downloadedBytes, SegmentState::Paused, {}
                });
            }
        } else {
            m_info.state = SegmentState::Failed;
            LOG_ERROR("Segment {}: failed — {}", m_info.id, err.message);
            if (m_callback) {
                m_callback(SegmentProgress{
                    m_info.id, m_info.downloadedBytes, SegmentState::Failed, err.message
                });
            }
        }
    }
}

} // namespace checkdown
