#pragma once

#include <string>
#include <unordered_map>

std::unordered_map<std::string, std::string> hyprpaperRequest();
std::string                                  hyprpaperGetResourceId(const std::unordered_map<std::string, std::string>& map, const std::string& port, const std::string& desc);
