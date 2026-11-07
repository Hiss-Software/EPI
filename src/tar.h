#pragma once
#include <string>
#include <vector>

// Extract tar / tar.gz / tar.xz-free archives into destDir.
// Returns true on success; fills `files` with entry names.
bool extractTar(const std::string& archivePath,
                const std::string& destDir,
                std::vector<std::string>& files,
                std::string& err);
