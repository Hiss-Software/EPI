#include "path_mgmt.h"
#include "color.h"
#include "util.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif

#ifdef _WIN32

static void broadcastEnvironmentChange() {
    DWORD_PTR dummy;
    SendMessageTimeoutA(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                        (LPARAM)"Environment", SMTO_ABORTIFHUNG, 5000, &dummy);
}

static bool getUserPath(std::string& out) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0,
                      KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, size = 0;
    if (RegQueryValueExA(key, "Path", nullptr, &type, nullptr, &size)
            != ERROR_SUCCESS || size == 0) {
        RegCloseKey(key); out = ""; return true;
    }
    out.assign(size, '\0');
    LSTATUS rc = RegQueryValueExA(key, "Path", nullptr, &type,
                                  (BYTE*)out.data(), &size);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return false;
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return true;
}

static bool setUserPath(const std::string& value) {
    HKEY key;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Environment", 0,
                      KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
    LSTATUS rc = RegSetValueExA(key, "Path", 0, REG_EXPAND_SZ,
                                (const BYTE*)value.c_str(),
                                (DWORD)(value.size() + 1));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) return false;
    broadcastEnvironmentChange();
    return true;
}

static std::vector<std::string> splitPath(const std::string& cur) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= cur.size()) {
        size_t semi = cur.find(';', start);
        if (semi == std::string::npos) {
            if (start < cur.size()) parts.push_back(cur.substr(start));
            break;
        }
        parts.push_back(cur.substr(start, semi - start));
        start = semi + 1;
    }
    return parts;
}

int cmdPathOn() {
    std::string dir = exeDir();
    if (dir.empty()) { msgErr("Error: cannot resolve epi location.\n"); return 1; }
    std::string cur;
    if (!getUserPath(cur)) { msgErr("Error: cannot read user PATH.\n"); return 1; }
    for (auto& part : splitPath(cur))
        if (!_stricmp(part.c_str(), dir.c_str())) {
            msgInfo("Already in PATH: " + dir + "\n"); return 0;
        }
    std::string newVal = cur.empty() ? dir : (cur + ";" + dir);
    if (setUserPath(newVal)) {
        msgOk("Added to user PATH: " + dir + "\n");
        msgInfo("Open a NEW terminal window for epi to be found.\n");
        return 0;
    }
    msgErr("Error: cannot write user PATH.\n");
    return 1;
}

int cmdPathOff() {
    std::string dir = exeDir();
    if (dir.empty()) { msgErr("Error: cannot resolve epi location.\n"); return 1; }
    std::string cur;
    if (!getUserPath(cur)) { msgErr("Error: cannot read user PATH.\n"); return 1; }
    std::string newVal;
    for (auto& part : splitPath(cur)) {
        if (part.empty() || !_stricmp(part.c_str(), dir.c_str())) continue;
        if (!newVal.empty()) newVal += ";";
        newVal += part;
    }
    if (setUserPath(newVal)) { msgOk("Removed from user PATH: " + dir + "\n"); return 0; }
    msgErr("Error: cannot write user PATH.\n");
    return 1;
}

#else  // POSIX

static const char* EPI_PATH_TAG = "# added by epi";

static std::string rcFilePath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return "";
    // prefer ~/.profile, fallback ~/.bashrc
    std::string profile = (std::filesystem::path(home) / ".profile").string();
    std::error_code ec;
    if (std::filesystem::exists(profile, ec)) return profile;
    return (std::filesystem::path(home) / ".bashrc").string();
}

int cmdPathOn() {
    std::string dir = exeDir();
    std::string rc = rcFilePath();
    if (dir.empty() || rc.empty()) { msgErr("Error: cannot resolve epi location or $HOME.\n"); return 1; }
    {
        std::ifstream in(rc);
        std::string line;
        while (std::getline(in, line))
            if (line.find(dir) != std::string::npos &&
                line.find(EPI_PATH_TAG) != std::string::npos) {
                msgInfo("Already in PATH: " + dir + "\n"); return 0;
            }
    }
    std::ofstream out(rc, std::ios::app);
    if (!out) { msgErr("Error: cannot write " + rc + "\n"); return 1; }
    out << "\n" << EPI_PATH_TAG << "\n"
        << "export PATH=\"$PATH:" << dir << "\"\n";
    out.close();
    msgOk("Added to PATH via " + rc + ": " + dir + "\n");
    msgInfo("Run 'source " + rc + "' or open a NEW terminal.\n");
    return 0;
}

int cmdPathOff() {
    std::string dir = exeDir();
    std::string rc = rcFilePath();
    if (dir.empty() || rc.empty()) { msgErr("Error: cannot resolve epi location or $HOME.\n"); return 1; }
    std::ifstream in(rc);
    if (!in) { msgInfo("Nothing to remove: " + rc + " not found.\n"); return 0; }
    std::vector<std::string> lines;
    std::string line;
    bool found = false;
    while (std::getline(in, line)) {
        if (line.find(EPI_PATH_TAG) != std::string::npos ||
            (line.find("export PATH=") != std::string::npos &&
             line.find(dir) != std::string::npos)) {
            found = true; continue;
        }
        lines.push_back(line);
    }
    in.close();
    if (!found) { msgInfo("Not in PATH: " + dir + "\n"); return 0; }
    std::ofstream out(rc, std::ios::trunc);
    if (!out) { msgErr("Error: cannot write " + rc + "\n"); return 1; }
    for (auto& l : lines) out << l << "\n";
    msgOk("Removed from PATH: " + dir + "\n");
    return 0;
}

#endif
