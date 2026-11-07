#include "color.h"
#include <cstdlib>
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <unistd.h>
#endif

Verbosity g_verbosity = Verbosity::Normal;
bool g_color = false;

void initColor() {
    if (std::getenv("NO_COLOR")) { g_color = false; return; }
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        g_color = true;
    } else g_color = false;
#else
    g_color = isatty(fileno(stdout)) != 0;
#endif
}

void setColor(Color c) {
    if (!g_color) return;
    const char* code = "";
    switch (c) {
        case Color::Reset:  code = "\x1b[0m";  break;
        case Color::Red:    code = "\x1b[31m"; break;
        case Color::Green:  code = "\x1b[32m"; break;
        case Color::Yellow: code = "\x1b[33m"; break;
        case Color::Cyan:   code = "\x1b[36m"; break;
        case Color::Bold:   code = "\x1b[1m";  break;
        case Color::Dim:    code = "\x1b[2m";  break;
    }
    std::cout << code;
}

void msgOk(const std::string& s) {
    if (g_verbosity == Verbosity::Quiet) return;
    setColor(Color::Green); std::cout << s; setColor(Color::Reset);
}
void msgErr(const std::string& s) {
    setColor(Color::Red); std::cerr << s; setColor(Color::Reset);
}
void msgWarn(const std::string& s) {
    if (g_verbosity == Verbosity::Quiet) return;
    setColor(Color::Yellow); std::cout << s; setColor(Color::Reset);
}
void msgInfo(const std::string& s) {
    if (g_verbosity == Verbosity::Quiet) return;
    std::cout << s;
}
void msgVerbose(const std::string& s) {
    if (g_verbosity != Verbosity::Verbose) return;
    setColor(Color::Dim); std::cout << s; setColor(Color::Reset);
}
