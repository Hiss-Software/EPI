#include "checksum.h"
#include "color.h"
#include "download.h"
#include "sha256.h"
#include "util.h"
#include <filesystem>
#include <iostream>

extern std::string INSTALL_DIR;

static bool computeSha(const Pkg& p, std::string& sha, std::string& err) {
    std::filesystem::path tmp =
        std::filesystem::path(INSTALL_DIR) / ("_sha_" + p.name + ".part");
    std::error_code ec;
    std::filesystem::create_directories(INSTALL_DIR, ec);

    if (!downloadFile(p.archive, tmp.string(), err, 5)) {
        std::filesystem::remove(tmp, ec);
        return false;
    }
    sha = sha256File(tmp.string());
    std::filesystem::remove(tmp, ec);
    if (sha.empty()) { err = "sha256 compute failed"; return false; }
    return true;
}

static bool updatePkg(std::vector<Pkg>& pkgs, Pkg& p, const std::string& sha) {
    for (auto& q : pkgs) {
        if (q.name == p.name) { q.sha256 = sha; return true; }
    }
    return false;
}

int cmdChecksum(std::vector<Pkg>& pkgs, const std::string& name, bool all) {
    int count = 0;
    for (auto& p : pkgs) {
        if (!all && p.name != name) continue;
        if (!all && p.name == name && !p.sha256.empty()) {
            msgInfo(p.name + " already has sha256, skipping\n");
            return 0;
        }
        if (all && !p.sha256.empty()) continue;

        msgInfo("Computing " + p.name + " " + p.version + "...\n");
        std::string sha, err;
        if (!computeSha(p, sha, err)) {
            msgErr("Error: " + p.name + ": " + err + "\n");
            if (!all) return 1;
            continue;
        }
        updatePkg(pkgs, p, sha);
        msgOk("  " + p.name + " -> " + sha.substr(0, 16) + "...\n");
        ++count;

        if (all) {
            // save after every package so a long run can be resumed
            extern std::string REGISTRY;
            if (!saveRegistry(REGISTRY, pkgs)) {
                msgErr("Error: cannot write registry\n");
                return 1;
            }
        }
    }
    if (!all && count == 0) {
        msgErr("Package '" + name + "' not found\n");
        return 1;
    }
    extern std::string REGISTRY;
    if (!saveRegistry(REGISTRY, pkgs)) {
        msgErr("Error: cannot write registry\n");
        return 1;
    }
    msgOk("Updated " + std::to_string(count) + " package(s)\n");
    return 0;
}