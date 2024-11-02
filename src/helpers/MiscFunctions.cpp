#include <filesystem>
#include "MiscFunctions.hpp"
#include "../helpers/Log.hpp"
#include <hyprutils/string/String.hpp>

using namespace Hyprutils::String;

std::string absolutePath(const std::string& rawpath, const std::string& currentDir) {
    std::filesystem::path path(rawpath);

    // Handling where rawpath starts with '~'
    if (!rawpath.empty() && rawpath[0] == '~') {
        static const char* const ENVHOME = getenv("HOME");
        return std::filesystem::path(ENVHOME) / path.relative_path().string().substr(2);
    }
    // Handling e.g. ./, ../
    else if (path.is_relative()) {
        return std::filesystem::weakly_canonical(std::filesystem::path(currentDir) / path);
    } else {
        return std::filesystem::weakly_canonical(path);
    }
}

int64_t configStringToInt(const std::string& VALUE) {
    if (VALUE.starts_with("0x")) {
        // Values with 0x are hex
        const auto VALUEWITHOUTHEX = VALUE.substr(2);
        return stol(VALUEWITHOUTHEX, nullptr, 16);
    } else if (VALUE.starts_with("rgba(") && VALUE.ends_with(')')) {
        const auto VALUEWITHOUTFUNC = VALUE.substr(5, VALUE.length() - 6);

        if (trim(VALUEWITHOUTFUNC).length() != 8) {
            Debug::log(WARN, "invalid length {} for rgba", VALUEWITHOUTFUNC.length());
            throw std::invalid_argument("rgba() expects length of 8 characters (4 bytes)");
        }

        const auto RGBA = std::stol(VALUEWITHOUTFUNC, nullptr, 16);

        // now we need to RGBA -> ARGB. The config holds ARGB only.
        return (RGBA >> 8) + 0x1000000 * (RGBA & 0xFF);
    } else if (VALUE.starts_with("rgb(") && VALUE.ends_with(')')) {
        const auto VALUEWITHOUTFUNC = VALUE.substr(4, VALUE.length() - 5);

        if (trim(VALUEWITHOUTFUNC).length() != 6) {
            Debug::log(WARN, "invalid length {} for rgb", VALUEWITHOUTFUNC.length());
            throw std::invalid_argument("rgb() expects length of 6 characters (3 bytes)");
        }

        const auto RGB = std::stol(VALUEWITHOUTFUNC, nullptr, 16);

        return RGB + 0xFF000000; // 0xFF for opaque
    } else if (VALUE.starts_with("true") || VALUE.starts_with("on") || VALUE.starts_with("yes")) {
        return 1;
    } else if (VALUE.starts_with("false") || VALUE.starts_with("off") || VALUE.starts_with("no")) {
        return 0;
    }

    if (VALUE.empty() || !isNumber(VALUE))
        return 0;

    return std::stoll(VALUE);
}