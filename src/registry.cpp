#include "registry.h"
#include "util.h"
#include <fstream>
#include <algorithm>

std::vector<Pkg> loadRegistry(const std::string& path) {
    std::vector<Pkg> out;
    std::ifstream in(path);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f;
        size_t start = 0;
        while (true) {
            size_t bar = line.find('|', start);
            if (bar == std::string::npos) { f.push_back(line.substr(start)); break; }
            f.push_back(line.substr(start, bar - start));
            start = bar + 1;
        }
        Pkg p;
        p.name    = f.size() > 0 ? f[0] : "";
        p.version = f.size() > 1 ? f[1] : "";
        p.archive = f.size() > 2 ? f[2] : "";
        p.include = f.size() > 3 ? f[3] : "";
        p.lib     = f.size() > 4 ? f[4] : "";
        p.license = f.size() > 5 ? f[5] : "";
        p.sha256  = f.size() > 6 ? f[6] : "";
        p.depends = f.size() > 7 ? f[7] : "";
        if (!p.name.empty()) out.push_back(p);
    }
    return out;
}

bool saveRegistry(const std::string& path, const std::vector<Pkg>& pkgs) {
    std::ofstream out(path);
    if (!out) return false;
    out << "# C++ EPI registry\n";
    out << "# name|version|archive|include_dir|lib|license|sha256|depends\n";
    for (const auto& p : pkgs) {
        out << p.name << '|' << p.version << '|' << p.archive << '|'
            << p.include << '|' << p.lib << '|' << p.license << '|'
            << p.sha256 << '|' << p.depends << '\n';
    }
    out.flush();
    return out.good();
}

std::string suggestPkg(const std::vector<Pkg>& pkgs, const std::string& query) {
    size_t bestDist = std::string::npos;
    std::string best;
    size_t threshold = std::max<size_t>(2, query.size() / 3);
    for (const auto& p : pkgs) {
        size_t d = levenshtein(query, p.name);
        if (d < bestDist) { bestDist = d; best = p.name; }
    }
    if (!best.empty() && bestDist <= threshold) return best;
    return "";
}