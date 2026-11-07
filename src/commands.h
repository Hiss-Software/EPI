#pragma once
#include "registry.h"
#include <string>
#include <vector>
#include <map>

extern std::string REGISTRY;
extern std::string INSTALL_DIR;

void printHelp();
void cmdList(const std::vector<Pkg>& pkgs);
int  cmdSearch(const std::vector<Pkg>& pkgs, const std::string& q, bool searchLicense);
int  cmdInfo(const std::vector<Pkg>& pkgs, const std::string& name);
int  cmdInstall(const std::vector<Pkg>& pkgs, const std::string& name);
int  cmdInstallInteractive(const std::vector<Pkg>& pkgs);
int  cmdAdd(std::vector<Pkg>& pkgs, const std::string& url,
            const std::map<std::string, std::string>& opts, bool interactive);
int  cmdRemove(std::vector<Pkg>& pkgs, const std::string& name, bool purge);
int  cmdUpdate(std::vector<Pkg>& pkgs, const std::string& name,
               const std::string& newVersion, const std::string& newArchive);

bool isInstalled(const std::string& name);
void notFoundPkg(const std::vector<Pkg>& pkgs, const std::string& name,
                 const std::string& hint);