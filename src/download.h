#pragma once
#include <string>

bool downloadFile(const std::string& url,
                  const std::string& partPath,
                  std::string& err,
                  int maxAttempts);
