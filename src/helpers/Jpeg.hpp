#pragma once

#include <filesystem>
#include <cairo/cairo.h>

namespace JPEG {
    cairo_surface_t* createSurfaceFromJPEG(const std::filesystem::path&);
};
