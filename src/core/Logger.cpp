#include "Logger.h"

#include <chrono>
#include <ctime>
#include <cstdio>

namespace checkdown {

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

void Logger::init(const std::filesystem::path& logPath, LogLevel minLevel) {
    std::lock_guard lk(m_mutex);
    m_path     = logPath;
    m_minLevel = minLevel;

    std::error_code ec;
    std::filesystem::create_directories(m_path.parent_path(), ec);

    m_file.open(m_path, std::ios::app);
    if (m_file.is_open()) {
        m_file << "\n[=== CheckDown session started ===]\n";
        m_file.flush();
    }
}

std::filesystem::path Logger::logPath() const {
    std::lock_guard lk(m_mutex);
    return m_path;
}

void Logger::rotateIfNeeded() {
    if (m_path.empty() || !m_file.is_open()) return;
    std::error_code ec;
    auto size = std::filesystem::file_size(m_path, ec);
    if (!ec && size > kMaxBytes) {
        m_file.close();
        auto oldPath = m_path;
        oldPath.replace_extension(".old.log");
        std::filesystem::rename(m_path, oldPath, ec);
        m_file.open(m_path, std::ios::trunc);
    }
}

static const char* levelStr(LogLevel l) noexcept {
    switch (l) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

void Logger::log(LogLevel level, const char* srcFile, int line, std::string_view msg) {
    if (level < m_minLevel) return;

    // Build timestamp
    auto now  = std::chrono::system_clock::now();
    auto t    = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) % 1000;

    struct tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char timeBuf[24];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", &tmBuf);

    // srcFile already has directory stripped by the consteval basename()
    std::string entry = std::format("[{}.{:03d}] [{}] {}:{} {}\n",
        timeBuf, ms.count(), levelStr(level), srcFile, line, msg);

    std::lock_guard lk(m_mutex);

    if (!m_file.is_open()) {
        std::fputs(entry.c_str(), stderr);
        return;
    }

    rotateIfNeeded();
    m_file << entry;
    m_file.flush();
}

} // namespace checkdown
