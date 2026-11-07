#pragma once
#include <string>

enum class Verbosity { Quiet = 0, Normal = 1, Verbose = 2 };
extern Verbosity g_verbosity;
extern bool g_color;

void initColor();
enum class Color { Reset, Red, Green, Yellow, Cyan, Bold, Dim };
void setColor(Color c);

void msgOk(const std::string& s);
void msgErr(const std::string& s);
void msgWarn(const std::string& s);
void msgInfo(const std::string& s);
void msgVerbose(const std::string& s);
