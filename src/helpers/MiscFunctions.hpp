#pragma once

#include <string>
#include <optional>

std::optional<std::string> getUserName();
std::optional<std::string> absolutePath(const std::string&, const std::string&);
