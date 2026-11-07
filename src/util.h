#pragma once
#include <string>
#include <vector>

std::string exePath();
std::string exeDir();
std::string humanSize(long long bytes);

std::string dirnameOf(const std::string& p);
std::string basenameOf(const std::string& p);
bool fileExists(const std::string& p);
long long fileSizeOf(const std::string& p);

size_t levenshtein(const std::string& a, const std::string& b);
