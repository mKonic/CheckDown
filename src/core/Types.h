#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <filesystem>

namespace checkdown {

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
inline constexpr int         kDefaultSegmentCount  = 8;
inline constexpr int         kDefaultMaxConcurrent = 3;
inline constexpr std::size_t kMinSegmentSize        = 256 * 1024;        // 256 KB
inline constexpr std::size_t kMergeBufferSize       = 64 * 1024;         // 64 KB
inline constexpr auto        kProgressInterval      = std::chrono::milliseconds(250);
inline constexpr const char* kStateFileName          = "checkdown_state.json";

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
enum class SegmentState : uint8_t {
    Pending,
    Downloading,
    Paused,
    Completed,
    Failed
};

enum class DownloadState : uint8_t {
    Queued,
    Downloading,
    Paused,
    Completed,
    Failed,
    Cancelled
};

// ---------------------------------------------------------------------------
// Data structs (plain data, easily serialisable)
// ---------------------------------------------------------------------------
struct SegmentInfo {
    int          id              = 0;
    int64_t      startByte       = 0;
    int64_t      endByte         = -1;     // inclusive
    int64_t      downloadedBytes = 0;
    SegmentState state           = SegmentState::Pending;
    std::string  tempFilePath;
};

struct DownloadInfo {
    int                                     id             = 0;
    std::string                             url;
    std::string                             fileName;
    std::filesystem::path                   savePath;      // final output
    int64_t                                 totalSize      = -1;
    bool                                    rangeSupported = false;
    DownloadState                           state          = DownloadState::Queued;
    int                                     segmentCount   = kDefaultSegmentCount;
    std::vector<SegmentInfo>                segments;
    int64_t                                 downloadedBytes = 0;   // yt-dlp tasks; regular tasks use segments sum
    bool                                    isYtdlp         = false;
    std::chrono::system_clock::time_point   addedTime{};
    std::string                             errorMessage;
    std::string                             cookies;       // libcurl CURLOPT_COOKIE format: "name=val; name2=val2"
};

struct TaskProgress {
    int            taskId           = 0;
    int64_t        downloadedBytes  = 0;
    int64_t        totalBytes       = -1;
    DownloadState  state            = DownloadState::Queued;
    double         speedBytesPerSec = 0.0;
    double         etaSeconds       = -1.0;   // estimated seconds remaining, -1 if unknown
    std::string    fileName;                   // non-empty when filename changes (e.g. yt-dlp)
    std::string    errorMessage;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
inline const char* toString(DownloadState s) {
    switch (s) {
        case DownloadState::Queued:      return "Queued";
        case DownloadState::Downloading: return "Downloading";
        case DownloadState::Paused:      return "Paused";
        case DownloadState::Completed:   return "Completed";
        case DownloadState::Failed:      return "Failed";
        case DownloadState::Cancelled:   return "Cancelled";
    }
    return "Unknown";
}

inline const char* toString(SegmentState s) {
    switch (s) {
        case SegmentState::Pending:     return "Pending";
        case SegmentState::Downloading: return "Downloading";
        case SegmentState::Paused:      return "Paused";
        case SegmentState::Completed:   return "Completed";
        case SegmentState::Failed:      return "Failed";
    }
    return "Unknown";
}

} // namespace checkdown
