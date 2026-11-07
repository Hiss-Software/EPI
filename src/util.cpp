#include "util.h"
#include <cstdio>
#include <algorithm>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

std::string exePath() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return "";
    return std::string(buf, n);
#else
    std::error_code ec;
    auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return "";
    return p.string();
#endif
}

std::string exeDir() {
    std::string p = exePath();
    size_t pos = p.find_last_of("/\\");
    return pos == std::string::npos ? "" : p.substr(0, pos);
}

std::string humanSize(long long bytes) {
    const char* units[] = {"B", "KB", "MB", "GB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 3) { v /= 1024.0; ++u; }
    char buf[64];
    std::snprintf(buf, sizeof(buf), (u == 0 ? "%.0f %s" : "%.1f %s"),
                  v, units[u]);
    return buf;
}

std::string dirnameOf(const std::string& p) {
    size_t pos = p.find_last_of("/\\");
    return pos == std::string::npos ? "" : p.substr(0, pos);
}

std::string basenameOf(const std::string& p) {
    size_t pos = p.find_last_of("/\\");
    return pos == std::string::npos ? p : p.substr(pos + 1);
}

bool fileExists(const std::string& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

long long fileSizeOf(const std::string& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) return 0;
    auto sz = std::filesystem::file_size(p, ec);
    return ec ? 0 : static_cast<long long>(sz);
}

size_t levenshtein(const std::string& a, const std::string& b) {
    const size_t n = a.size(), m = b.size();
    if (n == 0) return m;
    if (m == 0) return n;
    std::vector<size_t> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; ++j) prev[j] = j;
    for (size_t i = 1; i <= n; ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= m; ++j) {
            size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[m];
}
