#pragma once

#include <string>
#include <hyprlang.hpp>
#include <hyprutils/math/Vector2D.hpp>

std::string absolutePath(const std::string&, const std::string&);
int64_t     configStringToInt(const std::string& VALUE);
int         createPoolFile(size_t size, std::string& name);
class SLoginSessionConfig;
std::vector<SLoginSessionConfig> gatherSessions(std::vector<std::string> searchPaths);
