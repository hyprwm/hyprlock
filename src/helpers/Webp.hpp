#pragma once

#include <filesystem>
#include <cairo/cairo.h>

namespace WEBP {
    cairo_surface_t* createSurfaceFromWEBP(const std::filesystem::path&);
};
