#include "convert.h"
#include "util.h"
#include <fstream>
#include <regex>
#include <sstream>
#include <set>
#include <filesystem>
#include "nlohmann/json.hpp"

namespace fs = std::filesystem;

static std::string guessArchive(const std::string& githubUrl,
                                const std::string& version) {
    std::string base = githubUrl;
    if (!base.empty() && base.back() == '/') base.pop_back();
    if (base.find("github.com/") == std::string::npos) return "";
    if (base.find("/blob/") != std::string::npos) return "";
    if (base.find("/tree/") != std::string::npos) return "";
    if (base.find("/raw/")  != std::string::npos) return "";

    std::regex tagRe(R"re(^v?\d+\.\d+)re");
    if (std::regex_search(version, tagRe))
        return base + "/archive/refs/tags/" + version + ".zip";
    return base + "/archive/refs/heads/master.zip";
}

std::vector<Pkg> parseAwesomeCpp(const std::string& path) {
    std::vector<Pkg> out;
    std::ifstream in(path);
    if (!in) return out;

    std::regex lineRe(R"re(^\s*[-*]\s*\[([^\]]+)\]\((https?://[^\)]+)\)(.*)$)re");
    std::regex licRe(R"re(\[([A-Za-z0-9\.\-/]+)\])re");

    static const std::set<std::string> blacklist = {
        "C", "c", "cpp", "Cpp", "awesome-cpp", "awesome-c",
        "cpp-reference", "cppreference", "cheatsheets"
    };

    std::string line;
    while (std::getline(in, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, lineRe)) continue;
        std::string url  = m[2];
        std::string tail = m[3];
        if (url.find("github.com/") == std::string::npos) continue;

        std::string p = url;
        size_t gh = p.find("github.com/");
        if (gh == std::string::npos) continue;
        p = p.substr(gh + 11);
        size_t q = p.find_first_of("?#");
        if (q != std::string::npos) p = p.substr(0, q);
        if (!p.empty() && p.back() == '/') p.pop_back();
        if (p.size() > 4 && p.substr(p.size() - 4) == ".git")
            p = p.substr(0, p.size() - 4);

        size_t slash = p.find('/');
        if (slash == std::string::npos) continue;
        std::string owner = p.substr(0, slash);
        std::string repo  = p.substr(slash + 1);
        slash = repo.find('/');
        if (slash != std::string::npos) repo = repo.substr(0, slash);
        if (repo.empty() || owner.empty()) continue;
        if (repo == "archive" || repo == "raw" || repo == "blob") continue;
        if (repo.size() < 2) continue;
        if (blacklist.count(repo)) continue;

        std::string license;
        std::smatch lm;
        if (std::regex_search(tail, lm, licRe)) license = lm[1];

        Pkg pk;
        pk.name = repo;
        pk.archive = "https://github.com/" + owner + "/" + repo +
                     "/archive/refs/heads/master.zip";
        pk.license = license;
        out.push_back(pk);
    }
    return out;
}

static void collectPort(const nlohmann::json& e, std::vector<Pkg>& out) {
    if (!e.is_object()) return;
    auto pick = [&](const char* key) -> std::string {
        if (e.contains(key) && e[key].is_string()) return e[key].get<std::string>();
        if (e.contains("manifest") && e["manifest"].is_object() &&
            e["manifest"].contains(key) && e["manifest"][key].is_string())
            return e["manifest"][key].get<std::string>();
        return "";
    };
    Pkg p;
    p.name    = pick("name");
    p.version = pick("version");
    std::string home = pick("homepage");
    p.license = pick("license");
    if (p.name.empty() || home.empty()) return;
    p.archive = guessArchive(home, p.version);
    if (p.archive.empty()) return;
    out.push_back(p);
}

std::vector<Pkg> parseVcpkgPorts(const std::string& path) {
    std::vector<Pkg> out;
    std::ifstream in(path);
    if (!in) return out;
    nlohmann::json j;
    try { in >> j; } catch (...) { return out; }

    if (j.is_object() && j.contains("ports") && j["ports"].is_array()) {
        for (auto& e : j["ports"]) collectPort(e, out);
    } else if (j.is_array()) {
        for (auto& e : j) collectPort(e, out);
    } else if (j.is_object()) {
        for (auto& [key, val] : j.items()) collectPort(val, out);
    }
    return out;
}

// ------------------------------------------------------------
// xmake-repo: packages/*/*/xmake.lua
//   package("zlib")
//   add_urls("https://github.com/.../$(version).zip", ...)
//   add_versions("1.2.11", "sha256")
// ------------------------------------------------------------
std::vector<Pkg> parseXmakeRepo(const std::string& rootPath) {
    std::vector<Pkg> out;
    std::regex pkgRe(R"re(package\s*\(\s*"([^"]+)"\s*\))re");
    std::regex urlRe(R"re(add_urls\s*\(\s*"([^"]+)")re");
    std::regex verRe(R"re(add_versions\s*\(\s*"([^"]+)"\s*,\s*"([0-9a-fA-F]{64})"\s*\))re");

    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(rootPath, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() != "xmake.lua") continue;
        std::ifstream in(entry.path());
        if (!in) continue;
        std::string text((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());

        std::smatch pm;
        if (!std::regex_search(text, pm, pkgRe)) continue;
        std::string name = pm[1];

        std::smatch um;
        if (!std::regex_search(text, um, urlRe)) continue;
        std::string urlTemplate = um[1];

        auto vbegin = std::sregex_iterator(text.begin(), text.end(), verRe);
        auto vend   = std::sregex_iterator();
        for (auto it = vbegin; it != vend; ++it) {
            std::string ver = (*it)[1];
            std::string sha = (*it)[2];
            std::string url = urlTemplate;
            // replace $(version) with actual version
            size_t pos;
            while ((pos = url.find("$(version)")) != std::string::npos)
                url.replace(pos, 10, ver);
            if (url.find("github.com/") == std::string::npos) continue;
            Pkg p;
            p.name = name;
            p.version = ver;
            p.archive = url;
            p.sha256 = sha;
            out.push_back(p);
        }
    }
    return out;
}

// ------------------------------------------------------------
// Conan Center Index: recipes/*/config.yml + recipes/*/all/conandata.yml
// config.yml:   versions:\n  "1.2.3":\n    folder: "all"
// conandata.yml: sources:\n  "1.2.3":\n    url: "..."\n    sha256: "..."
// ------------------------------------------------------------
std::vector<Pkg> parseConanCenter(const std::string& rootPath) {
    std::vector<Pkg> out;
    std::regex verRe(R"re(^\s*"([^"]+)"\s*:\s*$)re");
    std::regex urlRe(R"re(url\s*:\s*"([^"]+)")re");
    std::regex shaRe(R"re(sha256\s*:\s*"([0-9a-fA-F]{64})")re");

    std::error_code ec;
    for (auto& entry : fs::recursive_directory_iterator(rootPath, ec)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() != "conandata.yml") continue;

        std::string libName = entry.path().parent_path().parent_path().filename().string();
        if (libName.empty() || libName == "recipes") continue;

        std::ifstream in(entry.path());
        if (!in) continue;
        std::string text((std::istreambuf_iterator<char>(in)),
                          std::istreambuf_iterator<char>());

        // Split into version blocks: "1.2.3":\n    url: ...\n    sha256: ...
        std::regex blockRe(R"re("([^"]+)"\s*:\s*\n\s*(?:-\s*)?url\s*:\s*"([^"]+)"(?:\s*\n\s*sha256\s*:\s*"([0-9a-fA-F]{64})")?)re");
        auto begin = std::sregex_iterator(text.begin(), text.end(), blockRe);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string ver = (*it)[1];
            std::string url = (*it)[2];
            std::string sha = (*it)[3];
            if (url.find("github.com/") == std::string::npos) continue;
            Pkg p;
            p.name = libName;
            p.version = ver;
            p.archive = url;
            p.sha256 = sha;
            out.push_back(p);
        }
    }
    return out;
}

// ------------------------------------------------------------
// Hunter: cmake/configs/default.cmake
// Format: hunter_config(GTest VERSION 1.14.0 ...)
// ------------------------------------------------------------
std::vector<Pkg> parseHunter(const std::string& path) {
    std::vector<Pkg> out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line;
    std::regex cfgRe(R"re(hunter_config\s*\(\s*([A-Za-z0-9_]+)\s+VERSION\s+([0-9][0-9A-Za-z\.\-]*))re");
    while (std::getline(in, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, cfgRe)) continue;
        Pkg p;
        p.name = m[1];
        p.version = m[2];
        p.archive = ""; // Hunter uses its own URLs; skip
        out.push_back(p);
    }
    return out;
}

void mergeInto(std::vector<Pkg>& dst, const std::vector<Pkg>& src) {
    for (const auto& p : src) {
        bool exists = false;
        for (const auto& d : dst)
            if (d.name == p.name) { exists = true; break; }
        if (!exists) dst.push_back(p);
    }
}