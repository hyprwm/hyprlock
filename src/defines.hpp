#pragma once

#include <hyprutils/memory/WeakPtr.hpp>
#include <hyprutils/memory/UniquePtr.hpp>
#include <hyprutils/memory/Atomic.hpp>
#include <hyprgraphics/color/Color.hpp>

using namespace Hyprutils::Memory;
using namespace Hyprgraphics;
#define SP CSharedPointer
#define WP CWeakPointer
#define UP CUniquePointer

#define ASP CAtomicSharedPointer
#define AWP CAtomicWeakPointer

typedef int64_t    OUTPUTID;
constexpr OUTPUTID OUTPUT_INVALID = -1;

struct SLoginSessionConfig {
    std::string name = "";
    std::string exec = "";
};
