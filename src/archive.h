#pragma once
#include <string>
#include <vector>

// Detect archive type by magic bytes and extract into destDir.
// Fills `files` with entry names for manifest. Returns false on error.
bool extractArchive(const std::string& archivePath,
                    const std::string& destDir,
                    std::vector<std::string>& files,
                    std::string& err);
