#include "DownloadTask.h"
#include "Logger.h"

#include <fstream>
#include <format>
#include <algorithm>
#include <numeric>
#include <filesystem>
#include <string_view>
#include <unordered_set>

namespace checkdown {

// ---------------------------------------------------------------------------
// File categorization — returns the subdirectory name for a given filename
// ---------------------------------------------------------------------------
static std::string_view categorizeFile(const std::filesystem::path& name) {
    auto ext = name.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const std::unordered_set<std::string> videos {
        ".mp4",".mkv",".avi",".mov",".wmv",".flv",".webm",".m4v",
        ".ts",".3gp",".ogv",".vob",".rm",".rmvb",".divx"
    };
    static const std::unordered_set<std::string> audio {
        ".mp3",".flac",".wav",".aac",".ogg",".wma",".opus",
        ".m4a",".aiff",".alac",".ape",".wv"
    };
    static const std::unordered_set<std::string> images {
        ".jpg",".jpeg",".png",".gif",".bmp",".svg",
        ".webp",".ico",".tiff",".tif",".heic",".avif"
    };
    static const std::unordered_set<std::string> documents {
        ".pdf",".doc",".docx",".xls",".xlsx",".ppt",".pptx",
        ".txt",".rtf",".epub",".odt",".ods",".odp",".csv",".md"
    };
    static const std::unordered_set<std::string> archives {
        ".zip",".rar",".7z",".tar",".gz",".bz2",".xz",
        ".zst",".lzma",".lz4",".cab",".iso",".img"
    };
    static const std::unordered_set<std::string> programs {
        ".exe",".msi",".dmg",".deb",".rpm",".appimage",
        ".pkg",".apk",".ipa",".appx",".msix"
    };

    if (videos.count(ext))    return "Videos";
    if (audio.count(ext))     return "Audio";
    if (images.count(ext))    return "Images";
    if (documents.count(ext)) return "Documents";
    if (archives.count(ext))  return "Archives";
    if (programs.count(ext))  return "Programs";
    return "Other";
}

// ---------------------------------------------------------------------------
// Unique filename: file.zip → file (1).zip → file (2).zip ...
// ---------------------------------------------------------------------------
static std::filesystem::path makeUniquePath(const std::filesystem::path& desired) {
    if (!std::filesystem::exists(desired))
        return desired;

    auto dir  = desired.parent_path();
    auto stem = desired.stem().string();
    auto ext  = desired.extension().string();

    for (int i = 1; i < 10000; ++i) {
        auto candidate = dir / std::format("{} ({}){}", stem, i, ext);
        if (!std::filesystem::exists(candidate))
            return candidate;
    }
    return dir / std::format("{} ({}){}", stem, 99999, ext);
}

DownloadTask::DownloadTask(DownloadInfo info, TaskProgressCallback callback)
    : m_info(std::move(info))
    , m_callback(std::move(callback))
{
}

DownloadTask::~DownloadTask() {
    // jthread destructors will request_stop + join automatically
    m_segments.clear();
}

// ---------------------------------------------------------------------------
// prepare — HEAD probe
// ---------------------------------------------------------------------------
std::expected<void, std::string> DownloadTask::prepare() {
    LOG_INFO("Task {}: preparing — url={}", m_info.id, m_info.url);
    HttpClient http;
    if (!m_info.cookies.empty())
        http.setCookies(m_info.cookies);
    auto result = http.head(m_info.url);
    if (!result.has_value()) {
        std::lock_guard lk(m_mutex);
        m_info.state = DownloadState::Failed;
        m_info.errorMessage = result.error().message;
        LOG_ERROR("Task {}: HEAD failed — {}", m_info.id, result.error().message);
        return std::unexpected(result.error().message);
    }

    std::lock_guard lk(m_mutex);
    auto& head = result.value();

    m_info.totalSize      = head.contentLength;
    m_info.rangeSupported = head.acceptsRanges;

    if (!head.effectiveUrl.empty())
        m_info.url = head.effectiveUrl;

    if (m_info.fileName.empty()) {
        if (!head.fileName.empty()) {
            // Sanitize: take only the filename component to prevent path traversal
            auto fn = std::filesystem::path(head.fileName).filename().string();
            m_info.fileName = (fn.empty() || fn == "." || fn == "..") ? "download" : fn;
        } else {
            // Extract from URL
            auto path = std::filesystem::path(m_info.url).filename().string();
            // Strip query string
            auto qpos = path.find('?');
            if (qpos != std::string::npos) path = path.substr(0, qpos);
            m_info.fileName = path.empty() ? "download" : path;
        }
    }

    // Update save path with filename
    if (m_info.savePath.empty())
        m_info.savePath = std::filesystem::current_path() / m_info.fileName;
    else if (std::filesystem::is_directory(m_info.savePath))
        m_info.savePath /= m_info.fileName;

    // Insert category subdirectory (Videos, Audio, Images, Documents, Archives, Programs, Other)
    {
        auto parent   = m_info.savePath.parent_path();
        auto category = categorizeFile(m_info.savePath.filename());
        auto catDir   = parent / category;
        std::error_code ec;
        std::filesystem::create_directories(catDir, ec);
        if (!ec)
            m_info.savePath = catDir / m_info.savePath.filename();
        else
            LOG_WARN("Task {}: could not create category dir '{}': {}",
                     m_info.id, catDir.string(), ec.message());
    }

    m_info.savePath = makeUniquePath(m_info.savePath);
    m_info.fileName = m_info.savePath.filename().string();

    planSegments();
    LOG_INFO("Task {}: prepared — file='{}' size={} segments={} ranges={}",
             m_info.id, m_info.fileName, m_info.totalSize,
             m_info.segmentCount, m_info.rangeSupported);
    return {};
}

// ---------------------------------------------------------------------------
// planSegments — split byte ranges
// ---------------------------------------------------------------------------
void DownloadTask::planSegments() {
    m_info.segments.clear();

    if (m_info.segmentCount <= 0)
        m_info.segmentCount = kDefaultSegmentCount;

    if (!m_info.rangeSupported || m_info.totalSize <= 0) {
        // Single segment, full download
        if (m_info.segmentCount > 1)
            LOG_INFO("Task {}: server doesn't support ranges or size unknown — using 1 segment",
                     m_info.id);
        m_info.segmentCount = 1;
        SegmentInfo seg;
        seg.id        = 0;
        seg.startByte = 0;
        seg.endByte   = (m_info.totalSize > 0) ? m_info.totalSize - 1 : -1;
        seg.state     = SegmentState::Pending;
        seg.tempFilePath = m_info.savePath.string() + ".part0";
        m_info.segments.push_back(seg);
        return;
    }

    int64_t segSize = m_info.totalSize / m_info.segmentCount;
    if (segSize < static_cast<int64_t>(kMinSegmentSize)) {
        m_info.segmentCount = std::max(1, static_cast<int>(m_info.totalSize / kMinSegmentSize));
        segSize = m_info.totalSize / m_info.segmentCount;
    }

    for (int i = 0; i < m_info.segmentCount; ++i) {
        SegmentInfo seg;
        seg.id        = i;
        seg.startByte = static_cast<int64_t>(i) * segSize;
        seg.endByte   = (i == m_info.segmentCount - 1)
                            ? m_info.totalSize - 1
                            : (static_cast<int64_t>(i + 1) * segSize - 1);
        seg.state     = SegmentState::Pending;
        seg.tempFilePath = m_info.savePath.string() + ".part" + std::to_string(i);
        m_info.segments.push_back(seg);
    }
}

// ---------------------------------------------------------------------------
// start — spawn segment workers
// ---------------------------------------------------------------------------
void DownloadTask::start() {
    LOG_INFO("Task {}: starting download — file='{}'", m_info.id, m_info.fileName);
    {
        std::lock_guard lk(m_mutex);
        m_info.state = DownloadState::Downloading;
        m_lastSpeedTime  = std::chrono::steady_clock::now();
        m_lastSpeedBytes = 0;

        // Compute already downloaded bytes
        for (auto& si : m_info.segments) {
            m_lastSpeedBytes += si.downloadedBytes;
        }

        // Clear old segment objects (they may be from a previous run)
        m_segments.clear();
        m_segments.reserve(m_info.segments.size());

        for (auto& si : m_info.segments) {
            if (si.state == SegmentState::Completed) {
                m_segments.push_back(nullptr); // placeholder
                continue;
            }

            si.state = SegmentState::Pending;
            auto seg = std::make_unique<Segment>(
                si, m_info.url,
                [this](const SegmentProgress& p) { onSegmentProgress(p); },
                m_info.cookies
            );
            // Don't start yet — collect first, start after releasing lock
            m_segments.push_back(std::move(seg));
        }
    }
    // Now start all segments outside the lock so callbacks don't deadlock
    for (auto& seg : m_segments) {
        if (seg) seg->start();
    }

    emitProgress();
}

// ---------------------------------------------------------------------------
// pause
// ---------------------------------------------------------------------------
void DownloadTask::pause() {
    // Request stop on all active segments (non-blocking)
    for (auto& seg : m_segments) {
        if (seg) seg->requestPause();
    }

    // Wait for threads to finish
    for (auto& seg : m_segments) {
        if (seg) seg->join();
    }

    {
        std::lock_guard lk(m_mutex);

        // Update segment infos from the actual segment objects
        for (size_t i = 0; i < m_segments.size(); ++i) {
            if (m_segments[i]) {
                auto si = m_segments[i]->info();
                m_info.segments[i].downloadedBytes = si.downloadedBytes;
                if (si.state == SegmentState::Downloading || si.state == SegmentState::Paused)
                    m_info.segments[i].state = SegmentState::Paused;
                else
                    m_info.segments[i].state = si.state;
            }
        }

        m_segments.clear();
        m_info.state   = DownloadState::Paused;
        m_currentSpeed = 0.0;
    }

    LOG_INFO("Task {}: paused", m_info.id);
    emitProgress();
}

// ---------------------------------------------------------------------------
// cancel
// ---------------------------------------------------------------------------
void DownloadTask::cancel() {
    for (auto& seg : m_segments) {
        if (seg) seg->requestPause();
    }
    for (auto& seg : m_segments) {
        if (seg) seg->join();
    }
    m_segments.clear();

    {
        std::lock_guard lk(m_mutex);
        m_info.state = DownloadState::Cancelled;

        // Remove temp files
        for (auto& si : m_info.segments) {
            std::error_code ec;
            std::filesystem::remove(si.tempFilePath, ec);
        }
    }

    LOG_INFO("Task {}: cancelled", m_info.id);
    emitProgress();
}

// ---------------------------------------------------------------------------
// info snapshot
// ---------------------------------------------------------------------------
DownloadInfo DownloadTask::info() const {
    std::lock_guard lk(m_mutex);
    return m_info;
}

void DownloadTask::restoreInfo(DownloadInfo saved) {
    std::lock_guard lk(m_mutex);
    m_info = std::move(saved);
}

// ---------------------------------------------------------------------------
// Segment progress aggregation
// ---------------------------------------------------------------------------
void DownloadTask::onSegmentProgress(const SegmentProgress& progress) {
    bool allDone = false;
    bool anyFailed = false;

    {
        std::lock_guard lk(m_mutex);

        // Find the matching segment info
        for (auto& si : m_info.segments) {
            if (si.id == progress.segmentId) {
                si.downloadedBytes = progress.downloadedBytes;
                si.state           = progress.state;
                break;
            }
        }

        // Check aggregate state
        allDone = std::all_of(m_info.segments.begin(), m_info.segments.end(),
            [](const SegmentInfo& s) { return s.state == SegmentState::Completed; });

        anyFailed = std::any_of(m_info.segments.begin(), m_info.segments.end(),
            [](const SegmentInfo& s) { return s.state == SegmentState::Failed; });
    }

    if (allDone) {
        mergeSegments();
    } else if (anyFailed) {
        std::lock_guard lk(m_mutex);
        // If all non-failed are completed or paused, mark the whole task as failed
        bool othersSettled = std::all_of(m_info.segments.begin(), m_info.segments.end(),
            [](const SegmentInfo& s) {
                return s.state == SegmentState::Completed ||
                       s.state == SegmentState::Failed ||
                       s.state == SegmentState::Paused;
            });
        if (othersSettled) {
            m_info.state = DownloadState::Failed;
            m_info.errorMessage = progress.errorMessage;
        }
    }

    emitProgress();
}

// ---------------------------------------------------------------------------
// Merge
// ---------------------------------------------------------------------------
void DownloadTask::mergeSegments() {
    std::lock_guard lk(m_mutex);

    LOG_INFO("Task {}: merging {} segments into '{}'",
             m_info.id, m_info.segments.size(), m_info.savePath.string());

    std::ofstream out(m_info.savePath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        m_info.state = DownloadState::Failed;
        m_info.errorMessage = std::format("Cannot create output file: {}",
                                           m_info.savePath.string());
        LOG_ERROR("Task {}: {}", m_info.id, m_info.errorMessage);
        return;
    }

    std::vector<char> buf(kMergeBufferSize);

    for (auto& si : m_info.segments) {
        std::ifstream in(si.tempFilePath, std::ios::binary);
        if (!in.is_open()) {
            m_info.state = DownloadState::Failed;
            m_info.errorMessage = std::format("Missing segment file: {}", si.tempFilePath);
            LOG_ERROR("Task {}: {}", m_info.id, m_info.errorMessage);
            return;
        }

        while (in.read(buf.data(), static_cast<std::streamsize>(buf.size())) || in.gcount() > 0) {
            out.write(buf.data(), in.gcount());
            if (!out.good()) {
                m_info.state = DownloadState::Failed;
                m_info.errorMessage = std::format("Write error merging into '{}'",
                                                   m_info.savePath.string());
                LOG_ERROR("Task {}: {}", m_info.id, m_info.errorMessage);
                return;
            }
        }
    }

    out.close();

    // Clean up temp files
    for (auto& si : m_info.segments) {
        std::error_code ec;
        std::filesystem::remove(si.tempFilePath, ec);
    }

    m_info.state = DownloadState::Completed;
    LOG_INFO("Task {}: completed — saved to '{}'", m_info.id, m_info.savePath.string());
}

// ---------------------------------------------------------------------------
// Emit progress to callback
// ---------------------------------------------------------------------------
void DownloadTask::emitProgress() {
    TaskProgress tp;
    {
        std::lock_guard lk(m_mutex);
        tp.taskId     = m_info.id;
        tp.totalBytes = m_info.totalSize;
        tp.state      = m_info.state;

        int64_t total = 0;
        for (auto& si : m_info.segments)
            total += si.downloadedBytes;
        tp.downloadedBytes = total;

        // Speed calculation
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(now - m_lastSpeedTime).count();
        if (elapsed >= 0.25) {
            int64_t delta    = total - m_lastSpeedBytes;
            m_currentSpeed   = (elapsed > 1e-9) ? (static_cast<double>(delta) / elapsed) : 0.0;
            m_lastSpeedTime  = now;
            m_lastSpeedBytes = total;
        }
        tp.speedBytesPerSec = m_currentSpeed;

        // ETA
        if (m_currentSpeed > 0.0 && tp.totalBytes > 0 && tp.downloadedBytes < tp.totalBytes) {
            tp.etaSeconds = static_cast<double>(tp.totalBytes - tp.downloadedBytes) / m_currentSpeed;
        }
    }

    if (m_callback)
        m_callback(tp);
}

} // namespace checkdown
