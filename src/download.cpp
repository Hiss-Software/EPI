#include "download.h"
#include "color.h"
#include "util.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <curl/curl.h>
#ifdef _WIN32
#include <windows.h>
#endif

struct DownloadCtx {
    std::ofstream* out = nullptr;
    long long total = 0;
    long long done = 0;
    long long lastPct = -1;
    bool quiet = false;
};

static void renderProgress(long long done, long long total) {
    if (total <= 0) {
        std::cout << "\rDownloading: " << humanSize(done) << "   " << std::flush;
        return;
    }
    long long pct = (done * 100) / total;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    const int width = 24;
    int filled = static_cast<int>((pct * width) / 100);
    std::string bar(width, ' ');
    for (int i = 0; i < filled; ++i) bar[i] = '=';
    if (filled < width) bar[filled] = '>';
    std::cout << "\rDownloading [" << bar << "] " << pct << "%  "
              << humanSize(done) << "/" << humanSize(total) << "   " << std::flush;
}

static int progressCb(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                      curl_off_t, curl_off_t) {
    auto* ctx = static_cast<DownloadCtx*>(clientp);
    ctx->total = static_cast<long long>(dltotal);
    ctx->done = static_cast<long long>(dlnow);
    if (ctx->quiet) return 0;
    long long pct = (ctx->total > 0) ? (ctx->done * 100) / ctx->total : -1;
    if (pct != ctx->lastPct) {
        renderProgress(ctx->done, ctx->total);
        ctx->lastPct = pct;
    }
    return 0;
}

static size_t writeCb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* ctx = static_cast<DownloadCtx*>(userdata);
    if (!ctx->out->good()) return 0;
    ctx->out->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return ctx->out->good() ? size * nmemb : 0;
}

static const char* caBundlePath() {
#ifdef _WIN32
    static std::string p;
    if (!p.empty()) return p.c_str();
    const char* candidates[] = {
        "C:/Users/toxic/MSYS2/mingw64/etc/ssl/certs/ca-bundle.crt",
        "C:/Users/toxic/MSYS2/mingw64/ssl/certs/ca-bundle.crt",
        "C:/Users/toxic/MSYS2/etc/ssl/certs/ca-bundle.crt",
        nullptr
    };
    for (int i = 0; candidates[i]; ++i) {
        std::error_code ec;
        if (std::filesystem::exists(candidates[i], ec)) {
            p = candidates[i];
            return p.c_str();
        }
    }
    return nullptr;
#else
    return nullptr;
#endif
}

bool downloadFile(const std::string& url,
                  const std::string& partPath,
                  std::string& err,
                  int maxAttempts) {
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        long long have = fileSizeOf(partPath);
        if (have > 0) {
            msgInfo("Resuming from " + humanSize(have) + " (attempt " +
                    std::to_string(attempt) + "/" +
                    std::to_string(maxAttempts) + ")...\n");
        }
        std::ofstream out(partPath, std::ios::binary |
                        (have > 0 ? std::ios::app : std::ios::trunc));
        if (!out) { err = "cannot open file for writing: " + partPath; return false; }
        DownloadCtx ctx;
        ctx.out = &out;
        ctx.quiet = (g_verbosity == Verbosity::Quiet);
        CURL* curl = curl_easy_init();
        if (!curl) { err = "curl init failed"; return false; }

        char errbuf[CURL_ERROR_SIZE];
        errbuf[0] = 0;
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progressCb);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
        if (g_verbosity == Verbosity::Verbose)
            curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
        const char* ca = caBundlePath();
        if (ca) curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
        if (have > 0)
            curl_easy_setopt(curl, CURLOPT_RESUME_FROM_LARGE,
                             static_cast<curl_off_t>(have));

        CURLcode rc = curl_easy_perform(curl);
        out.flush();
        long code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(curl);
        out.close();
        if (!ctx.quiet) std::cout << "\n";

        if (rc == CURLE_OK && code >= 200 && code < 300) return true;
        if (rc == CURLE_OK && code == 0) return true; // file://

        // Server didn't honor Range: restart from zero next attempt.
        if (rc == CURLE_RANGE_ERROR && have > 0) {
            msgWarn("Server does not support resume, restarting from zero.\n");
            std::error_code ec;
            std::filesystem::remove(partPath, ec);
            continue;
        }
        // HTTP 416 (Range Not Satisfiable): also restart from zero.
        if (code == 416 && have > 0) {
            msgWarn("Range not satisfiable, restarting from zero.\n");
            std::error_code ec;
            std::filesystem::remove(partPath, ec);
            continue;
        }

        if (rc != CURLE_OK) {
            std::cerr << "[debug] curl rc=" << (int)rc
                      << " (" << curl_easy_strerror(rc) << ") attempt "
                      << attempt << "/" << maxAttempts << "\n";
            if (errbuf[0]) std::cerr << "[debug] " << errbuf << "\n";
        }
        err = (rc != CURLE_OK)
              ? (errbuf[0] ? std::string(errbuf) : std::string(curl_easy_strerror(rc)))
              : ("HTTP " + std::to_string(code));
        if (attempt < maxAttempts)
            msgWarn("Download interrupted, retrying...\n");
    }
    return false;
}