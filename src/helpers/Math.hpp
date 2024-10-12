#pragma once

#include <wayland-client.h>

#include <hyprutils/math/Box.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/math/Mat3x3.hpp>

using namespace Hyprutils::Math;

eTransform          wlTransformToHyprutils(wl_output_transform t);
wl_output_transform invertTransform(wl_output_transform tr);
