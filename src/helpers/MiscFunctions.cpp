#include <filesystem>
#include <algorithm>
#include <cmath>
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
    auto parseHex = [](const std::string& value) -> int64_t {
        try {
            size_t position;
            auto   result = stoll(value, &position, 16);
            if (position == value.size())
                return result;
        } catch (const std::exception&) {}
        throw std::invalid_argument("invalid hex " + value);
    };
    if (VALUE.starts_with("0x")) {
        // Values with 0x are hex
        return parseHex(VALUE);
    } else if (VALUE.starts_with("rgba(") && VALUE.ends_with(')')) {
        const auto VALUEWITHOUTFUNC = trim(VALUE.substr(5, VALUE.length() - 6));

        // try doing it the comma way first
        if (std::count(VALUEWITHOUTFUNC.begin(), VALUEWITHOUTFUNC.end(), ',') == 3) {
            // cool
            std::string rolling = VALUEWITHOUTFUNC;
            auto        r       = configStringToInt(trim(rolling.substr(0, rolling.find(','))));
            rolling             = rolling.substr(rolling.find(',') + 1);
            auto g              = configStringToInt(trim(rolling.substr(0, rolling.find(','))));
            rolling             = rolling.substr(rolling.find(',') + 1);
            auto b              = configStringToInt(trim(rolling.substr(0, rolling.find(','))));
            rolling             = rolling.substr(rolling.find(',') + 1);
            uint8_t a           = 0;
            try {
                a = std::round(std::stof(trim(rolling.substr(0, rolling.find(',')))) * 255.f);
            } catch (std::exception& e) { throw std::invalid_argument("failed parsing " + VALUEWITHOUTFUNC); }

            return a * (Hyprlang::INT)0x1000000 + r * (Hyprlang::INT)0x10000 + g * (Hyprlang::INT)0x100 + b;
        } else if (VALUEWITHOUTFUNC.length() == 8) {
            const auto RGBA = parseHex(VALUEWITHOUTFUNC);
            // now we need to RGBA -> ARGB. The config holds ARGB only.
            return (RGBA >> 8) + 0x1000000 * (RGBA & 0xFF);
        }

        throw std::invalid_argument("rgba() expects length of 8 characters (4 bytes) or 4 comma separated values");

    } else if (VALUE.starts_with("rgb(") && VALUE.ends_with(')')) {
        const auto VALUEWITHOUTFUNC = trim(VALUE.substr(4, VALUE.length() - 5));

        // try doing it the comma way first
        if (std::count(VALUEWITHOUTFUNC.begin(), VALUEWITHOUTFUNC.end(), ',') == 2) {
            // cool
            std::string rolling = VALUEWITHOUTFUNC;
            auto        r       = configStringToInt(trim(rolling.substr(0, rolling.find(','))));
            rolling             = rolling.substr(rolling.find(',') + 1);
            auto g              = configStringToInt(trim(rolling.substr(0, rolling.find(','))));
            rolling             = rolling.substr(rolling.find(',') + 1);
            auto b              = configStringToInt(trim(rolling.substr(0, rolling.find(','))));

            return (Hyprlang::INT)0xFF000000 + r * (Hyprlang::INT)0x10000 + g * (Hyprlang::INT)0x100 + b;
        } else if (VALUEWITHOUTFUNC.length() == 6) {
            return parseHex(VALUEWITHOUTFUNC) + 0xFF000000;
        }

        throw std::invalid_argument("rgb() expects length of 6 characters (3 bytes) or 3 comma separated values");
    } else if (VALUE.starts_with("true") || VALUE.starts_with("on") || VALUE.starts_with("yes")) {
        return 1;
    } else if (VALUE.starts_with("false") || VALUE.starts_with("off") || VALUE.starts_with("no")) {
        return 0;
    }

    if (VALUE.empty() || !isNumber(VALUE, false))
        throw std::invalid_argument("cannot parse \"" + VALUE + "\" as an int.");

    try {
        const auto RES = std::stoll(VALUE);
        return RES;
    } catch (std::exception& e) { throw std::invalid_argument(std::string{"stoll threw: "} + e.what()); }

    return 0;
}