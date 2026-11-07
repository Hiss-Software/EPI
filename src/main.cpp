#include "version.h"
#include "color.h"
#include "util.h"
#include "registry.h"
#include "commands.h"
#include "path_mgmt.h"
#include "fetch.h"
#include "convert.h"
#include "checksum.h"
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <curl/curl.h>

static const char* REGISTRY_NAME    = "registry.epi";
static const char* INSTALL_DIR_NAME = "third_party";

static bool takeGlobalFlag(const std::string& a) {
    if (a == "--quiet" || a == "-q")   { g_verbosity = Verbosity::Quiet;   return true; }
    if (a == "--verbose" || a == "-v") { g_verbosity = Verbosity::Verbose; return true; }
    if (a == "--no-color")             { g_color = false;                  return true; }
    return false;
}

static std::vector<Pkg> parseBySource(const std::string& src) {
    if (src.find("awesome-cpp") != std::string::npos ||
        src.find("README") != std::string::npos)
        return parseAwesomeCpp(src);
    if (src.find("xmake-repo") != std::string::npos)
        return parseXmakeRepo(src);
    if (src.find("conan-center") != std::string::npos)
        return parseConanCenter(src);
    if (src.find("hunter") != std::string::npos)
        return parseHunter(src);
    return parseVcpkgPorts(src);
}

int main(int argc, char** argv) {
    initColor();
    curl_global_init(CURL_GLOBAL_DEFAULT);

    std::string dir = exeDir();
    if (!dir.empty()) {
        REGISTRY    = (std::filesystem::path(dir) / REGISTRY_NAME).string();
        INSTALL_DIR = (std::filesystem::path(dir) / INSTALL_DIR_NAME).string();
    } else {
        REGISTRY    = REGISTRY_NAME;
        INSTALL_DIR = INSTALL_DIR_NAME;
    }

    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (!takeGlobalFlag(a)) args.push_back(a);
    }

    std::vector<Pkg> pkgs = loadRegistry(REGISTRY);

    if (args.empty()) { printHelp(); curl_global_cleanup(); return 0; }

    std::string cmd = args[0];
    int rc = 0;

    if (cmd == "help" || cmd == "--help" || cmd == "-h") {
        printHelp();
    }
    else if (cmd == "list" || cmd == "ls") {
        cmdList(pkgs);
    }
    else if (cmd == "search" && args.size() >= 2) {
        bool searchLicense = false;
        std::string q;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--license") searchLicense = true;
            else q = args[i];
        }
        if (q.empty()) { msgErr("Usage: epi search <query> [--license]\n"); curl_global_cleanup(); return 1; }
        rc = cmdSearch(pkgs, q, searchLicense);
    }
    else if (cmd == "info" && args.size() == 2) {
        rc = cmdInfo(pkgs, args[1]);
    }
    else if (cmd == "install" && args.size() == 1) {
        rc = cmdInstallInteractive(pkgs);
    }
    else if (cmd == "install" && args.size() == 2) {
        rc = cmdInstall(pkgs, args[1]);
    }
    else if (cmd == "update" && args.size() >= 2) {
        std::string ver, arch;
        for (size_t i = 2; i + 1 < args.size(); i += 2) {
            std::string a = args[i];
            if (a == "--version" || a == "--archive") {
                std::string* dst = (a == "--version") ? &ver : &arch;
                *dst = args[i + 1];
            } else { msgErr("Unknown flag: " + a + "\n"); curl_global_cleanup(); return 1; }
        }
        rc = cmdUpdate(pkgs, args[1], ver, arch);
    }
    else if (cmd == "add" && args.size() >= 2) {
        std::map<std::string, std::string> opts;
        std::string url;
        bool interactive = true;
        for (size_t i = 1; i < args.size(); ++i) {
            std::string a = args[i];
            if (a == "--name" || a == "--version" || a == "--archive" ||
                a == "--include" || a == "--lib" || a == "--license" ||
                a == "--sha256" || a == "--depends") {
                if (i + 1 >= args.size()) { msgErr("Missing value for " + a + "\n"); curl_global_cleanup(); return 1; }
                opts[a] = args[++i];
                interactive = false;
            } else url = a;
        }
        if (url.empty()) { msgErr("Usage: epi add <url> [--name N --version V ...]\n"); curl_global_cleanup(); return 1; }
        rc = cmdAdd(pkgs, url, opts, interactive);
    }
    else if (cmd == "remove" && (args.size() == 2 || (args.size() == 3 && args[2] == "--purge"))) {
        rc = cmdRemove(pkgs, args[1], args.size() == 3);
    }
    else if (cmd == "path" && args.size() == 2) {
        if (args[1] == "on")       rc = cmdPathOn();
        else if (args[1] == "off") rc = cmdPathOff();
        else { msgErr("Usage: epi path on|off\n"); curl_global_cleanup(); return 1; }
    }
    else if (cmd == "checksum" && args.size() >= 2) {
        bool all = false;
        std::string name;
        for (size_t i = 1; i < args.size(); ++i) {
            if (args[i] == "--all") all = true;
            else name = args[i];
        }
        if (!all && name.empty()) {
            msgErr("Usage: epi checksum <name> | epi checksum --all\n");
            curl_global_cleanup(); return 1;
        }
        rc = cmdChecksum(pkgs, name, all);
    }
    else if (cmd == "fetch" && args.size() >= 2) {
        std::string url = args[1];
        std::string outPath = "registry.new.epi";
        bool merge = false;
        for (size_t i = 2; i < args.size(); ++i) {
            if (args[i] == "-o" && i + 1 < args.size()) outPath = args[++i];
            else if (args[i] == "--merge") merge = true;
        }
        std::string tmp = outPath + ".download";
        std::string err;
        if (!fetchUrlToFile(url, tmp, err)) {
            msgErr("Error: fetch failed (" + err + ")\n");
            curl_global_cleanup(); return 1;
        }
        std::vector<Pkg> parsed = parseBySource(url);
        std::filesystem::remove(tmp);
        if (parsed.empty()) {
            msgErr("Error: no packages parsed from " + url + "\n");
            curl_global_cleanup(); return 1;
        }
        if (merge) {
            auto existing = loadRegistry(REGISTRY);
            mergeInto(existing, parsed);
            parsed = existing;
        }
        if (!saveRegistry(outPath, parsed)) {
            msgErr("Error: cannot write " + outPath + "\n");
            curl_global_cleanup(); return 1;
        }
        msgOk("Wrote " + std::to_string(parsed.size()) + " packages to " + outPath + "\n");
    }
    else if (cmd == "sync" && args.size() >= 2) {
        std::string src = args[1];
        std::vector<Pkg> parsed = parseBySource(src);
        if (parsed.empty()) {
            msgErr("Error: no packages parsed from " + src + "\n");
            curl_global_cleanup(); return 1;
        }
        auto existing = loadRegistry(REGISTRY);
        mergeInto(existing, parsed);
        if (!saveRegistry(REGISTRY, existing)) {
            msgErr("Error: cannot write " + REGISTRY + "\n");
            curl_global_cleanup(); return 1;
        }
        msgOk("Registry now has " + std::to_string(existing.size()) + " packages\n");
    }
    else if (cmd == "version" || cmd == "--version") {
        std::cout << "C++ EPI v" << EPI_VERSION << "\n";
    }
    else {
        msgErr("Unknown command or missing arguments.\n\n");
        printHelp();
        curl_global_cleanup();
        return 1;
    }

    curl_global_cleanup();
    return rc;
}