#pragma once
#include "registry.h"
#include <string>
#include <vector>

// Parse awesome-cpp style markdown into packages.
// Lines like: "- [Name](https://github.com/o/r) - desc [MIT]"
std::vector<Pkg> parseAwesomeCpp(const std::string& path);

// Parse vcpkg-ports JSON (array of objects with name/version/homepage/license).
std::vector<Pkg> parseVcpkgPorts(const std::string& path);

// Parse xmake-repo tree: packages/*/*/xmake.lua with add_urls / add_versions.
std::vector<Pkg> parseXmakeRepo(const std::string& rootPath);

// Parse Conan Center Index tree: recipes/*/all/conandata.yml.
std::vector<Pkg> parseConanCenter(const std::string& rootPath);

// Parse Hunter cmake/configs/default.cmake (name + version, no URL).
std::vector<Pkg> parseHunter(const std::string& path);

// Merge new packages into existing (skip duplicates by name).
void mergeInto(std::vector<Pkg>& dst, const std::vector<Pkg>& src);