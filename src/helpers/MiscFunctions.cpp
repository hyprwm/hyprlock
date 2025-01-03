#include <filesystem>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <fcntl.h>
#include "MiscFunctions.hpp"
#include "Log.hpp"
#include "../defines.hpp"
#include <hyprutils/string/String.hpp>
#include <hyprutils/os/Process.hpp>
#include <unistd.h>

using namespace Hyprutils::String;
using namespace Hyprutils::OS;

std::string absolutePath(const std::string& rawpath, const std::string& currentDir) {
    std::filesystem::path path(rawpath);

    // Handling where rawpath starts with '~'
    if (!rawpath.empty() && rawpath[0] == '~') {
        static const char* const ENVHOME = getenv("HOME");
        path                             = std::filesystem::path(ENVHOME) / path.relative_path().string().substr(2);
    }

    // Handling e.g. ./, ../
    if (path.is_relative()) {
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

            return (a * (Hyprlang::INT)0x1000000) + (r * (Hyprlang::INT)0x10000) + (g * (Hyprlang::INT)0x100) + b;
        } else if (VALUEWITHOUTFUNC.length() == 8) {
            const auto RGBA = parseHex(VALUEWITHOUTFUNC);
            // now we need to RGBA -> ARGB. The config holds ARGB only.
            return (RGBA >> 8) + (0x1000000 * (RGBA & 0xFF));
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

            return (Hyprlang::INT)0xFF000000 + (r * (Hyprlang::INT)0x10000) + (g * (Hyprlang::INT)0x100) + b;
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

int createPoolFile(size_t size, std::string& name) {
    const auto XDGRUNTIMEDIR = getenv("XDG_RUNTIME_DIR");
    if (!XDGRUNTIMEDIR) {
        Debug::log(CRIT, "XDG_RUNTIME_DIR not set!");
        return -1;
    }

    name = std::string(XDGRUNTIMEDIR) + "/.hyprlock_sc_XXXXXX";

    const auto FD = mkstemp((char*)name.c_str());
    if (FD < 0) {
        Debug::log(CRIT, "createPoolFile: fd < 0");
        return -1;
    }
    // set cloexec
    long flags = fcntl(FD, F_GETFD);
    if (flags == -1) {
        close(FD);
        return -1;
    }

    if (fcntl(FD, F_SETFD, flags | FD_CLOEXEC) == -1) {
        close(FD);
        Debug::log(CRIT, "createPoolFile: fcntl < 0");
        return -1;
    }

    if (ftruncate(FD, size) < 0) {
        close(FD);
        Debug::log(CRIT, "createPoolFile: ftruncate < 0");
        return -1;
    }

    return FD;
}

std::string spawnSync(const std::string& cmd) {
    CProcess proc("/bin/sh", {"-c", cmd});
    if (!proc.runSync()) {
        Debug::log(ERR, "Failed to run \"{}\"", cmd);
        return "";
    }

    if (!proc.stdErr().empty())
        Debug::log(ERR, "Shell command \"{}\" STDERR:\n{}", cmd, proc.stdErr());

    return proc.stdOut();
}

void spawnAsync(const std::string& cmd) {
    CProcess proc("/bin/sh", {"-c", cmd});
    if (!proc.runAsync())
        Debug::log(ERR, "Failed to start \"{}\"", cmd);
}

std::vector<SLoginSessionConfig> gatherSessions(std::vector<std::string> searchPaths) {
    std::vector<SLoginSessionConfig> sessions;
    searchPaths.insert(searchPaths.end(),
                       {
                           "/usr/local/share/wayland-sessions",
                           "/usr/share/wayland-sessions",
                       });

    for (const auto& DIR : searchPaths) {
        if (!std::filesystem::exists(DIR))
            continue;

        for (const auto& dirEntry : std::filesystem::recursive_directory_iterator{DIR}) {
            if (!dirEntry.is_regular_file() || dirEntry.path().extension() != ".desktop")
                continue;

            SLoginSessionConfig session;

            // read line for line and parse the desktop file naivly
            std::ifstream fHandle(dirEntry.path().c_str());
            std::string   line;
            while (std::getline(fHandle, line)) {
                if (line.empty())
                    continue;

                if (line.find("Name=") != std::string::npos)
                    session.name = line.substr(5);
                else if (line.find("Exec=") != std::string::npos)
                    session.exec = line.substr(5);
            }

            if (!session.name.empty() && !session.exec.empty()) {
                sessions.emplace_back(session);
                Debug::log(LOG, "Registered session from {}: {}", dirEntry.path().c_str(), session.name.c_str());
            } else
                Debug::log(LOG, "Failed to parse session file: {}", dirEntry.path().c_str());
        }
    }

    return sessions;
}
