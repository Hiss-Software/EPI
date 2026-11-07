#pragma once
#include <string>
#include <vector>

struct Pkg {
    std::string name;
    std::string version;
    std::string archive;
    std::string include;
    std::string lib;
    std::string license;
    std::string sha256;
    std::string depends;   // "name1:>=1.0,name2"
};

std::vector<Pkg> loadRegistry(const std::string& path);
bool saveRegistry(const std::string& path, const std::vector<Pkg>& pkgs);
std::string suggestPkg(const std::vector<Pkg>& pkgs, const std::string& query);