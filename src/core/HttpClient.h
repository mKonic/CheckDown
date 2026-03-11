#pragma once

#include <string>
#include <cstdint>
#include <functional>
#include <expected>
#include <filesystem>
#include <stop_token>
#include <memory>

namespace checkdown {

struct HeadResult {
    int64_t     contentLength = -1;
    bool        acceptsRanges = false;
    std::string effectiveUrl;
    std::string fileName;          // from Content-Disposition
};

struct HttpError {
    int         curlCode = 0;
    std::string message;
};

using ProgressCallback = std::function<void(int64_t downloadedBytes, int64_t totalBytes)>;

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpClient(const HttpClient&)            = delete;
    HttpClient& operator=(const HttpClient&) = delete;
    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    /// Send HEAD request; follows redirects. Thread-safe per-instance.
    [[nodiscard]]
    std::expected<HeadResult, HttpError> head(const std::string& url);

    /// Download [startByte, endByte] to outputPath (append if existingBytes > 0).
    /// Pass endByte == -1 for full file (no Range header).
    [[nodiscard]]
    std::expected<void, HttpError> downloadRange(
        const std::string&          url,
        int64_t                     startByte,
        int64_t                     endByte,
        const std::filesystem::path& outputPath,
        int64_t                     existingBytes,
        std::stop_token             stopToken,
        ProgressCallback            onProgress = {});

    /// Set cookies to be sent with all subsequent requests.
    /// Format: "name1=value1; name2=value2" (libcurl CURLOPT_COOKIE).
    void setCookies(std::string cookies);

    /// Call once from main() before any threads.
    static void globalInit();
    static void globalCleanup();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace checkdown
