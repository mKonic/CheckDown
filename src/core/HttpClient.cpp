#include "HttpClient.h"

#include <curl/curl.h>
#include <fstream>
#include <format>
#include <mutex>
#include <algorithm>

namespace checkdown {

// ---------------------------------------------------------------------------
// Global init (call once from main)
// ---------------------------------------------------------------------------
static std::once_flag g_curlInitFlag;

void HttpClient::globalInit() {
    std::call_once(g_curlInitFlag, [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    });
}

void HttpClient::globalCleanup() {
    curl_global_cleanup();
}

// ---------------------------------------------------------------------------
// PIMPL
// ---------------------------------------------------------------------------
struct HttpClient::Impl {
    CURL* curl = nullptr;

    Impl() {
        curl = curl_easy_init();
    }
    ~Impl() {
        if (curl) curl_easy_cleanup(curl);
    }

    void reset() {
        if (curl) curl_easy_reset(curl);
    }

    void setCommonOpts(const std::string& url) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "CheckDown/1.0");
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
        // Use native Windows CA store
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    }
};

// ---------------------------------------------------------------------------
// Ctor / dtor / move
// ---------------------------------------------------------------------------
HttpClient::HttpClient()  : m_impl(std::make_unique<Impl>()) {}
HttpClient::~HttpClient() = default;

HttpClient::HttpClient(HttpClient&&) noexcept            = default;
HttpClient& HttpClient::operator=(HttpClient&&) noexcept = default;

// ---------------------------------------------------------------------------
// HEAD
// ---------------------------------------------------------------------------
struct HeaderCtx {
    bool        acceptsRanges = false;
    std::string contentDisposition;
};

static size_t headHeaderCb(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* ctx = static_cast<HeaderCtx*>(userdata);
    std::string line(buffer, size * nitems);

    // case-insensitive check
    auto lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower.starts_with("accept-ranges:")) {
        if (lower.find("bytes") != std::string::npos)
            ctx->acceptsRanges = true;
    }
    if (lower.starts_with("content-disposition:")) {
        ctx->contentDisposition = line.substr(line.find(':') + 1);
        // trim leading whitespace
        auto pos = ctx->contentDisposition.find_first_not_of(" \t");
        if (pos != std::string::npos)
            ctx->contentDisposition = ctx->contentDisposition.substr(pos);
        // trim trailing \r\n
        while (!ctx->contentDisposition.empty() &&
               (ctx->contentDisposition.back() == '\r' || ctx->contentDisposition.back() == '\n'))
            ctx->contentDisposition.pop_back();
    }
    return size * nitems;
}

static std::string extractFileNameFromDisposition(const std::string& cd) {
    // Try filename*=UTF-8''name first, then filename="name", then filename=name
    // Simple manual parsing to avoid regex complications
    auto lower = cd;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    // filename*=UTF-8''...
    auto pos = lower.find("filename*=utf-8''");
    if (pos != std::string::npos) {
        auto start = pos + 17; // length of "filename*=utf-8''"
        auto end = cd.find_first_of(";\r\n", start);
        if (end == std::string::npos) end = cd.size();
        return cd.substr(start, end - start);
    }

    // filename="..."
    pos = lower.find("filename=");
    if (pos != std::string::npos) {
        auto start = pos + 9;
        if (start < cd.size() && cd[start] == '"') {
            ++start;
            auto end = cd.find('"', start);
            if (end != std::string::npos)
                return cd.substr(start, end - start);
        }
        // filename=name (no quotes)
        auto end = cd.find_first_of("; \t\r\n", start);
        if (end == std::string::npos) end = cd.size();
        return cd.substr(start, end - start);
    }

    return {};
}

std::expected<HeadResult, HttpError> HttpClient::head(const std::string& url) {
    if (!m_impl->curl)
        return std::unexpected(HttpError{0, "CURL handle is null"});

    m_impl->reset();
    m_impl->setCommonOpts(url);
    curl_easy_setopt(m_impl->curl, CURLOPT_NOBODY, 1L);

    HeaderCtx hctx;
    curl_easy_setopt(m_impl->curl, CURLOPT_HEADERFUNCTION, headHeaderCb);
    curl_easy_setopt(m_impl->curl, CURLOPT_HEADERDATA, &hctx);

    CURLcode res = curl_easy_perform(m_impl->curl);
    if (res != CURLE_OK) {
        return std::unexpected(HttpError{
            static_cast<int>(res),
            std::format("HEAD failed: {}", curl_easy_strerror(res))
        });
    }

    long httpCode = 0;
    curl_easy_getinfo(m_impl->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode >= 400) {
        return std::unexpected(HttpError{
            static_cast<int>(httpCode),
            std::format("HTTP {}", httpCode)
        });
    }

    HeadResult result;
    curl_off_t cl = -1;
    curl_easy_getinfo(m_impl->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
    result.contentLength = static_cast<int64_t>(cl);
    result.acceptsRanges = hctx.acceptsRanges;

    char* effUrl = nullptr;
    curl_easy_getinfo(m_impl->curl, CURLINFO_EFFECTIVE_URL, &effUrl);
    if (effUrl) result.effectiveUrl = effUrl;

    if (!hctx.contentDisposition.empty())
        result.fileName = extractFileNameFromDisposition(hctx.contentDisposition);

    return result;
}

// ---------------------------------------------------------------------------
// Download Range
// ---------------------------------------------------------------------------
struct DownloadCtx {
    std::ofstream*   file        = nullptr;
    std::stop_token* stopToken   = nullptr;
    ProgressCallback onProgress;
    int64_t          written     = 0;
    int64_t          totalExpected = -1;
};

static size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<DownloadCtx*>(userdata);
    size_t bytes = size * nmemb;
    ctx->file->write(ptr, static_cast<std::streamsize>(bytes));
    if (!ctx->file->good()) return 0;
    ctx->written += static_cast<int64_t>(bytes);
    return bytes;
}

static int progressCb(void* clientp, curl_off_t /*dltotal*/, curl_off_t /*dlnow*/,
                       curl_off_t /*ultotal*/, curl_off_t /*ulnow*/) {
    auto* ctx = static_cast<DownloadCtx*>(clientp);
    if (ctx->stopToken && ctx->stopToken->stop_requested())
        return 1; // non-zero aborts the transfer

    if (ctx->onProgress)
        ctx->onProgress(ctx->written, ctx->totalExpected);

    return 0;
}

std::expected<void, HttpError> HttpClient::downloadRange(
    const std::string&           url,
    int64_t                      startByte,
    int64_t                      endByte,
    const std::filesystem::path& outputPath,
    int64_t                      existingBytes,
    std::stop_token              stopToken,
    ProgressCallback             onProgress)
{
    if (!m_impl->curl)
        return std::unexpected(HttpError{0, "CURL handle is null"});

    m_impl->reset();
    m_impl->setCommonOpts(url);

    // Open file in append or truncate mode
    auto mode = std::ios::binary;
    if (existingBytes > 0)
        mode |= std::ios::app;
    else
        mode |= std::ios::trunc;

    std::ofstream file(outputPath, mode);
    if (!file.is_open()) {
        return std::unexpected(HttpError{
            0, std::format("Cannot open file: {}", outputPath.string())
        });
    }

    // Byte range
    int64_t actualStart = startByte + existingBytes;
    if (endByte >= 0) {
        auto range = std::format("{}-{}", actualStart, endByte);
        curl_easy_setopt(m_impl->curl, CURLOPT_RANGE, range.c_str());
    }

    int64_t totalExpected = (endByte >= 0) ? (endByte - startByte + 1) : -1;

    DownloadCtx ctx;
    ctx.file          = &file;
    ctx.stopToken     = &stopToken;
    ctx.onProgress    = std::move(onProgress);
    ctx.written       = existingBytes;
    ctx.totalExpected = totalExpected;

    curl_easy_setopt(m_impl->curl, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(m_impl->curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(m_impl->curl, CURLOPT_XFERINFOFUNCTION, progressCb);
    curl_easy_setopt(m_impl->curl, CURLOPT_XFERINFODATA, &ctx);
    curl_easy_setopt(m_impl->curl, CURLOPT_NOPROGRESS, 0L);

    // Low speed limit: abort if below 1 byte/sec for 60 seconds
    curl_easy_setopt(m_impl->curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(m_impl->curl, CURLOPT_LOW_SPEED_TIME, 60L);

    CURLcode res = curl_easy_perform(m_impl->curl);
    file.close();

    if (res == CURLE_ABORTED_BY_CALLBACK) {
        // Graceful pause via stop_token
        return std::unexpected(HttpError{
            static_cast<int>(res), "Aborted"
        });
    }

    if (res != CURLE_OK) {
        return std::unexpected(HttpError{
            static_cast<int>(res),
            std::format("Download failed: {}", curl_easy_strerror(res))
        });
    }

    // Verify we got 206 when we asked for a range
    long httpCode = 0;
    curl_easy_getinfo(m_impl->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (endByte >= 0 && httpCode == 200 && existingBytes > 0) {
        // Server ignored Range, we got full content on an append — bad state
        return std::unexpected(HttpError{
            static_cast<int>(httpCode),
            "Server does not support resume (returned 200 instead of 206)"
        });
    }
    if (httpCode >= 400) {
        return std::unexpected(HttpError{
            static_cast<int>(httpCode),
            std::format("HTTP {}", httpCode)
        });
    }

    // Final progress callback
    if (ctx.onProgress)
        ctx.onProgress(ctx.written, totalExpected);

    return {};
}

} // namespace checkdown
