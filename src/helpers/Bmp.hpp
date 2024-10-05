#pragma once

#include <cairo/cairo.h>
#include <filesystem>
#include <fstream>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include "string.h"
#include <vector>
#include "Log.hpp"

namespace BMP {
    cairo_surface_t* createSurfaceFromBMP(const std::filesystem::path&);
};