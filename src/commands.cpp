#include "commands.h"
#include "version.h"
#include "color.h"
#include "util.h"
#include "download.h"
#include "archive.h"
#include "sha256.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <set>
#include <sstream>

std::string REGISTRY;
std::string INSTALL_DIR;

namespace {
std::set<std::string> g_installing;
}

bool isInstalled(const std::string& name) {
    std::error_code ec;
    return std::filesystem::exists(
        std::filesystem::path(INSTALL_DIR) / (name + ".epi-manifest"), ec);
}

void notFoundPkg(const std::vector<Pkg>& pkgs, const std::string& name,
                 const std::string& hint) {
    msgErr("Package '" + name + "' not found");
    if (!hint.empty()) std::cerr << " " << hint;
    std::cerr << ".\n";
    std::string s = suggestPkg(pkgs, name);
    if (!s.empty()) {
        msgInfo("Did you mean '");
        setColor(Color::Cyan); std::cout << s; setColor(Color::Reset);
        msgInfo("'?\n");
    }
}

void printHelp() {
    setColor(Color::Bold);
    std::cout << "C++ EPI - Easy Package Installer v" << EPI_VERSION << "\n";
    setColor(Color::Reset);
    std::cout
        << "\n"
        << "Commands:\n"
        << "  epi list              show all packages in registry\n"
        << "  epi search <query>    search package by name (--license also matches license)\n"
        << "  epi info <name>       show package details and install status\n"
        << "  epi install [name]    download and unpack package (no name: picker)\n"
        << "  epi update <name> [--version V] [--archive U]\n"
        << "  epi remove <name> [--purge]\n"
        << "  epi add <url> [--name N --version V --archive U --include D --lib L --license Z --sha256 H --depends D]\n"
        << "  epi checksum <name>|--all    fill sha256\n"
        << "  epi path on|off       add/remove epi folder to/from user PATH\n"
        << "  epi help              show this help\n"
        << "\n"
        << "Global flags:\n"
        << "  --quiet, -q           suppress informational output\n"
        << "  --verbose, -v         extra diagnostics\n"
        << "  --no-color            disable colored output\n"
        << "\n"
        << "Registry: " << REGISTRY << "\n"
        << "Install dir: " << INSTALL_DIR << "\n";
}

void cmdList(const std::vector<Pkg>& pkgs) {
    if (pkgs.empty()) { msgInfo("Registry is empty. Add packages: epi add <url>\n"); return; }
    setColor(Color::Bold);
    std::cout << "Packages in registry: " << pkgs.size() << "\n\n";
    setColor(Color::Reset);
    size_t wn = 0, wv = 0;
    for (auto& p : pkgs) {
        wn = std::max(wn, p.name.size());
        wv = std::max(wv, p.version.size());
    }
    for (auto& p : pkgs) {
        bool inst = isInstalled(p.name);
        std::cout << "  ";
        if (inst) { setColor(Color::Green); std::cout << "* "; setColor(Color::Reset); }
        else      { std::cout << "  "; }
        std::cout << p.name;
        for (size_t i = p.name.size(); i < wn; ++i) std::cout << ' ';
        std::cout << "  " << p.version;
        for (size_t i = p.version.size(); i < wv; ++i) std::cout << ' ';
        std::cout << "  [" << (p.license.empty() ? "?" : p.license) << "]  "
                  << p.archive << "\n";
    }
    msgVerbose("\n(* = installed)\n");
}

int cmdSearch(const std::vector<Pkg>& pkgs, const std::string& q, bool searchLicense) {
    bool found = false;
    for (auto& p : pkgs) {
        bool hit = p.name.find(q) != std::string::npos;
        if (searchLicense) hit = hit || p.license.find(q) != std::string::npos;
        if (hit) {
            std::cout << "  ";
            if (isInstalled(p.name)) { setColor(Color::Green); std::cout << "* "; setColor(Color::Reset); }
            else std::cout << "  ";
            std::cout << p.name << "  " << p.version << "  [" << p.license << "]\n";
            found = true;
        }
    }
    if (!found) {
        msgInfo("Nothing found for '" + q + "'.\n");
        std::string s = suggestPkg(pkgs, q);
        if (!s.empty()) {
            msgInfo("Did you mean '");
            setColor(Color::Cyan); std::cout << s; setColor(Color::Reset);
            msgInfo("'?\n");
        }
    }
    return 0;
}

int cmdInfo(const std::vector<Pkg>& pkgs, const std::string& name) {
    for (auto& p : pkgs) {
        if (p.name != name) continue;
        setColor(Color::Bold);
        std::cout << "Name:     "; setColor(Color::Cyan);
        std::cout << p.name << "\n"; setColor(Color::Reset);
        std::cout << "Version:  " << p.version << "\n"
                  << "Archive:  " << p.archive << "\n"
                  << "Includes: " << p.include << "\n"
                  << "Libs:     " << (p.lib.empty() ? "(header-only)" : p.lib) << "\n"
                  << "License:  " << p.license << "\n"
                  << "SHA256:   " << (p.sha256.empty() ? "(none)" : p.sha256) << "\n"
                  << "Depends:  " << (p.depends.empty() ? "(none)" : p.depends) << "\n";
        std::filesystem::path mf = std::filesystem::path(INSTALL_DIR) / (name + ".epi-manifest");
        std::error_code ec;
        if (std::filesystem::exists(mf, ec)) {
            setColor(Color::Green);
            std::cout << "Status:   installed\n";
            setColor(Color::Reset);
        } else {
            setColor(Color::Yellow);
            std::cout << "Status:   not installed\n";
            setColor(Color::Reset);
        }
        return 0;
    }
    notFoundPkg(pkgs, name, "");
    return 1;
}

static bool safeRemoveInsideInstallDir(const std::string& entry, bool verbose) {
    std::filesystem::path rel = entry;
    std::filesystem::path safe;
    for (auto& part : rel) {
        if (part == ".." || part == "/" || part == "\\") continue;
        safe /= part;
    }
    if (safe.empty()) return true;
    std::filesystem::path target = std::filesystem::path(INSTALL_DIR) / safe;
    std::error_code ec;
    auto canonBase = std::filesystem::weakly_canonical(INSTALL_DIR, ec);
    auto canonTgt  = std::filesystem::weakly_canonical(target, ec);
    if (!ec) {
        auto baseStr = canonBase.string();
        auto tgtStr  = canonTgt.string();
        if (tgtStr.compare(0, baseStr.size(), baseStr) != 0) {
            msgWarn("Warning: refusing to delete outside install dir: " + tgtStr + "\n");
            return false;
        }
    }
    std::filesystem::remove_all(target, ec);
    if (ec) {
        msgWarn("Warning: cannot delete " + target.string() + "\n");
        return false;
    }
    if (verbose) msgVerbose("Deleted: " + target.string() + "\n");
    return true;
}

static int installOne(const std::vector<Pkg>& pkgs, const Pkg& p) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(INSTALL_DIR), ec);

    std::filesystem::path partPath =
        std::filesystem::path(INSTALL_DIR) / (p.name + "_" + p.version + ".zip.part");
    std::filesystem::path zipPath =
        std::filesystem::path(INSTALL_DIR) / (p.name + "_" + p.version + ".zip");

    msgInfo("Downloading " + p.name + " " + p.version + "...\n");
    std::string dlErr;
    if (!downloadFile(p.archive, partPath.string(), dlErr, 5)) {
        msgErr("Error: download failed (" + dlErr + ") after 5 attempts.\n");
        return 1;
    }
    std::error_code rnec;
    std::filesystem::rename(partPath, zipPath, rnec);
    if (rnec) {
        msgErr("Error: cannot finalize download (" + rnec.message() + ").\n");
        return 1;
    }

    if (!p.sha256.empty()) {
        std::string actual = sha256File(zipPath.string());
        if (actual.empty()) {
            msgErr("Error: cannot compute sha256.\n");
            std::filesystem::remove(zipPath, ec);
            return 1;
        }
        std::string expected = p.sha256;
        std::transform(expected.begin(), expected.end(), expected.begin(), ::tolower);
        if (actual != expected) {
            msgErr("Error: sha256 mismatch.\n");
            msgInfo("  expected: " + expected + "\n");
            msgInfo("  actual:   " + actual + "\n");
            std::filesystem::remove(zipPath, ec);
            return 1;
        }
        msgVerbose("sha256 OK: " + actual + "\n");
    } else {
        msgWarn("Warning: package has no sha256; integrity not verified.\n");
    }

    msgInfo("Unpacking to " + INSTALL_DIR + "/...\n");
    std::vector<std::string> files;
    std::string unpackErr;
    if (!extractArchive(zipPath.string(), INSTALL_DIR, files, unpackErr)) {
        msgErr("Error: unpack failed (" + unpackErr + ").\n");
        std::filesystem::remove(zipPath, ec);
        return 1;
    }

    {
        std::string manifestPath =
            (std::filesystem::path(INSTALL_DIR) / (p.name + ".epi-manifest")).string();
        std::ofstream mf(manifestPath);
        if (mf) for (auto& f : files) mf << f << '\n';
    }

    std::filesystem::remove(zipPath, ec);

    msgOk("Installed: " + p.name + " " + p.version + "\n");
    if (!p.lib.empty()) msgInfo("Link with: " + p.lib + "\n");
    else msgInfo("Header-only: add " + INSTALL_DIR + " to your include path.\n");
    return 0;
}

int cmdInstall(const std::vector<Pkg>& pkgs, const std::string& name) {
    if (g_installing.count(name)) {
        msgVerbose("dependency cycle: " + name + " already in progress\n");
        return 0;
    }
    const Pkg* p = nullptr;
    for (auto& q : pkgs) if (q.name == name) { p = &q; break; }
    if (!p) { notFoundPkg(pkgs, name, "Try: epi search " + name); return 1; }

    if (!p->depends.empty()) {
        g_installing.insert(name);
        std::stringstream ss(p->depends);
        std::string dep;
        while (std::getline(ss, dep, ',')) {
            // trim
            while (!dep.empty() && (dep.front() == ' ' || dep.front() == '\t')) dep.erase(dep.begin());
            while (!dep.empty() && (dep.back() == ' ' || dep.back() == '\t')) dep.pop_back();
            size_t colon = dep.find(':');
            if (colon != std::string::npos) dep = dep.substr(0, colon);
            if (dep.empty() || dep == name) continue;
            if (isInstalled(dep)) { msgVerbose("dependency already installed: " + dep + "\n"); continue; }
            msgInfo("  dependency: " + dep + "\n");
            if (cmdInstall(pkgs, dep) != 0) {
                g_installing.erase(name);
                msgErr("Error: dependency '" + dep + "' failed\n");
                return 1;
            }
        }
        g_installing.erase(name);
    }

    return installOne(pkgs, *p);
}

int cmdInstallInteractive(const std::vector<Pkg>& pkgs) {
    if (pkgs.empty()) { msgInfo("Registry is empty. Add packages: epi add <url>\n"); return 0; }
    std::cout << "Select package to install (empty to cancel):\n";
    size_t wn = 0;
    for (auto& p : pkgs) wn = std::max(wn, p.name.size());
    for (size_t i = 0; i < pkgs.size(); ++i) {
        std::cout << "  " << (i + 1) << ") ";
        if (isInstalled(pkgs[i].name)) { setColor(Color::Green); std::cout << "* "; setColor(Color::Reset); }
        else std::cout << "  ";
        std::cout << pkgs[i].name;
        for (size_t k = pkgs[i].name.size(); k < wn; ++k) std::cout << ' ';
        std::cout << "  " << pkgs[i].version << "\n";
    }
    std::cout << "> " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line) || line.empty()) { msgInfo("Cancelled.\n"); return 0; }
    try {
        size_t idx = static_cast<size_t>(std::stoul(line));
        if (idx < 1 || idx > pkgs.size()) { msgErr("Error: number out of range.\n"); return 1; }
        return cmdInstall(pkgs, pkgs[idx - 1].name);
    } catch (...) {
        msgErr("Error: not a number.\n");
        return 1;
    }
}

int cmdAdd(std::vector<Pkg>& pkgs,
           const std::string& url,
           const std::map<std::string, std::string>& opts,
           bool interactive) {
    Pkg p;
    size_t slash = url.find_last_of('/');
    p.name = (slash == std::string::npos || slash + 1 >= url.size()) ? url : url.substr(slash + 1);

    auto it = opts.find("--name");
    if (it != opts.end() && !it->second.empty()) p.name = it->second;

    if (interactive) {
        std::cout << "Name [" << p.name << "]: ";
        std::string tmp;
        std::getline(std::cin, tmp);
        if (!tmp.empty()) p.name = tmp;
        std::cout << "Version: ";        std::getline(std::cin, p.version);
        std::cout << "Direct archive URL: "; std::getline(std::cin, p.archive);
        std::cout << "Include dir inside archive: "; std::getline(std::cin, p.include);
        std::cout << "Libs to link (Enter for header-only): "; std::getline(std::cin, p.lib);
        std::cout << "License: ";        std::getline(std::cin, p.license);
        std::cout << "SHA256 (Enter to skip): "; std::getline(std::cin, p.sha256);
        std::cout << "Depends (comma-separated, Enter to skip): "; std::getline(std::cin, p.depends);
    } else {
        auto get = [&](const char* key) {
            auto o = opts.find(key);
            return o == opts.end() ? std::string() : o->second;
        };
        p.version = get("--version");
        p.archive = get("--archive");
        if (p.archive.empty()) p.archive = url;
        p.include = get("--include");
        p.lib     = get("--lib");
        p.license = get("--license");
        p.sha256  = get("--sha256");
        p.depends = get("--depends");
    }

    for (auto& q : pkgs)
        if (q.name == p.name) { msgWarn("Package '" + p.name + "' already exists.\n"); return 1; }

    pkgs.push_back(p);
    if (!saveRegistry(REGISTRY, pkgs)) {
        msgErr("Error: cannot write registry file: " + REGISTRY + "\n");
        return 1;
    }
    msgOk("Added: " + p.name + " -> " + REGISTRY + "\n");
    return 0;
}

int cmdRemove(std::vector<Pkg>& pkgs, const std::string& name, bool purge) {
    std::vector<Pkg> kept;
    bool removed = false;
    for (auto& p : pkgs) {
        if (p.name == name) { removed = true; continue; }
        kept.push_back(p);
    }
    if (!removed) { notFoundPkg(pkgs, name, ""); return 1; }
    if (!saveRegistry(REGISTRY, kept)) {
        msgErr("Error: cannot write registry file: " + REGISTRY + "\n");
        return 1;
    }
    if (purge) {
        std::ifstream mf(std::filesystem::path(INSTALL_DIR) / (name + ".epi-manifest"));
        if (!mf) {
            msgOk("Removed from registry: " + name + "\n");
            msgInfo("No install manifest found; installed files (if any) were not deleted.\n");
            return 0;
        }
        std::string entry;
        while (std::getline(mf, entry)) {
            if (!entry.empty() && entry.back() == '\r') entry.pop_back();
            if (entry.empty()) continue;
            safeRemoveInsideInstallDir(entry, true);
        }
        std::error_code ec;
        std::filesystem::remove(std::filesystem::path(INSTALL_DIR) / (name + ".epi-manifest"), ec);
        msgOk("Purged: " + name + "\n");
    } else {
        msgOk("Removed from registry: " + name + "\n");
        msgInfo("(installed files in " + INSTALL_DIR + "/ are kept; use --purge to delete)\n");
    }
    return 0;
}

int cmdUpdate(std::vector<Pkg>& pkgs, const std::string& name,
              const std::string& newVersion, const std::string& newArchive) {
    Pkg* p = nullptr;
    for (auto& q : pkgs) if (q.name == name) { p = &q; break; }
    if (!p) { notFoundPkg(pkgs, name, ""); return 1; }
    if (!newVersion.empty()) p->version = newVersion;
    if (!newArchive.empty()) p->archive = newArchive;
    if (!saveRegistry(REGISTRY, pkgs)) {
        msgErr("Error: cannot write registry file: " + REGISTRY + "\n");
        return 1;
    }
    msgInfo("Updated registry entry: " + p->name + " -> " + p->version + "\n");
    msgInfo("Reinstalling...\n");
    return cmdInstall(pkgs, name);
}