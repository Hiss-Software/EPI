#include "fetch.h"
#include <curl/curl.h>
#include <filesystem>
#include <fstream>

static size_t writeFileCb(char* ptr, size_t size, size_t nmemb, void* user) {
    auto* out = static_cast<std::ofstream*>(user);
    out->write(ptr, static_cast<std::streamsize>(size * nmemb));
    return out->good() ? size * nmemb : 0;
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

bool fetchUrlToFile(const std::string& url, const std::string& destPath,
                    std::string& err) {
    std::ofstream out(destPath, std::ios::binary | std::ios::trunc);
    if (!out) { err = "cannot open " + destPath; return false; }

    CURL* c = curl_easy_init();
    if (!c) { err = "curl init failed"; return false; }

    char errbuf[CURL_ERROR_SIZE];
    errbuf[0] = 0;
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeFileCb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "epi/0.4");

    const char* ca = caBundlePath();
    if (ca) curl_easy_setopt(c, CURLOPT_CAINFO, ca);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(c);
    out.close();

    if (rc != CURLE_OK) {
        err = errbuf[0] ? std::string(errbuf) : curl_easy_strerror(rc);
        return false;
    }
    if (code >= 400) { err = "HTTP " + std::to_string(code); return false; }
    return true;
}