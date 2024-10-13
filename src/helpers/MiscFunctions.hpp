#pragma once

#include <string>
#include <hyprlang.hpp>
#include <hyprutils/math/Vector2D.hpp>

std::string absolutePath(const std::string&, const std::string&);

//
inline Hyprutils::Math::Vector2D Vector2DFromHyprlang(const Hyprlang::VEC2& vec) {
    return Hyprutils::Math::Vector2D{vec.x, vec.y};
};
