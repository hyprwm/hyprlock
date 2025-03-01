#pragma once

#include <hyprutils/memory/WeakPtr.hpp>
#include <hyprutils/memory/UniquePtr.hpp>
#include <hyprgraphics/color/Color.hpp>

using namespace Hyprutils::Memory;
using namespace Hyprgraphics;
#define SP CSharedPointer
#define WP CWeakPointer
#define UP CUniquePointer

typedef int64_t    OUTPUTID;
constexpr OUTPUTID OUTPUT_INVALID = -1;
