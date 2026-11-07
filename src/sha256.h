#pragma once
#include <string>

// Returns lowercase hex SHA-256 of the file at `path`,
// or empty string on error.
std::string sha256File(const std::string& path);
