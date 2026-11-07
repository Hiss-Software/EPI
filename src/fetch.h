#pragma once
#include <string>

// Download url to destPath. Returns true on success.
bool fetchUrlToFile(const std::string& url, const std::string& destPath,
                    std::string& err);