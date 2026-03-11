#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>
#include <filesystem>
#include <format>

namespace checkdown {

enum class LogLevel { Debug = 0, Info, Warn, Error };

class Logger {
public:
    static Logger& instance();

    /// Call once from the main thread before any downloads start.
    void init(const std::filesystem::path& logPath,
              LogLevel minLevel = LogLevel::Debug);

    /// Low-level: write a pre-formatted message.
    void log(LogLevel level, const char* srcFile, int line, std::string_view msg);

    /// High-level: format then log.
    template<typename... Args>
    void logf(LogLevel level, const char* srcFile, int line,
               std::format_string<Args...> fmt, Args&&... args)
    {
        if (level < m_minLevel) return;
        log(level, srcFile, line, std::format(fmt, std::forward<Args>(args)...));
    }

    [[nodiscard]] std::filesystem::path logPath() const;

private:
    Logger() = default;
    void rotateIfNeeded();

    mutable std::mutex    m_mutex;
    std::ofstream         m_file;
    std::filesystem::path m_path;
    LogLevel              m_minLevel = LogLevel::Debug;

    static constexpr std::uintmax_t kMaxBytes = 10ULL * 1024 * 1024; // 10 MB
};

} // namespace checkdown

// ---- Convenience macros ----
// Strip the directory part of __FILE__ at compile time.
namespace checkdown::detail {
    consteval const char* basename(const char* p) {
        const char* last = p;
        for (const char* c = p; *c; ++c)
            if (*c == '/' || *c == '\\') last = c + 1;
        return last;
    }
} // namespace checkdown::detail

#define LOG_DEBUG(...) checkdown::Logger::instance().logf( \
    checkdown::LogLevel::Debug, checkdown::detail::basename(__FILE__), __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  checkdown::Logger::instance().logf( \
    checkdown::LogLevel::Info,  checkdown::detail::basename(__FILE__), __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  checkdown::Logger::instance().logf( \
    checkdown::LogLevel::Warn,  checkdown::detail::basename(__FILE__), __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) checkdown::Logger::instance().logf( \
    checkdown::LogLevel::Error, checkdown::detail::basename(__FILE__), __LINE__, __VA_ARGS__)
