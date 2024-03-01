#include <filesystem>
#include <optional>
#include <unistd.h>
#include <pwd.h>
#include "MiscFunctions.hpp"

std::optional<std::string> getUserName() {
    auto username = getlogin();
    if (!username) {
        auto pwd = getpwuid(getuid());
        username = pwd->pw_name;
    }
    if (username)
        return username;
    // in theory, this could very well happen, and no unlocking can be done
    return {};
}

std::optional<std::string> absolutePath(const std::string& rawpath, const std::string& rawcurrentpath) {
    std::filesystem::path path(rawpath);

    // Handling where rawpath starts with '~'
    if (!rawpath.empty() && rawpath[0] == '~') {
        static const char* const ENVHOME = getenv("HOME");
        return std::filesystem::path(ENVHOME) / path.relative_path().string().substr(2);
    }
    // Handling e.g. ./, ../
    else if (path.is_relative()) {
        const std::filesystem::path currentDir = std::filesystem::path(rawcurrentpath).parent_path();

        auto                        finalPath = currentDir / path;
        if (exists(finalPath))
            return std::filesystem::canonical(currentDir / path);
        return {};
    } else {
        return std::filesystem::canonical(path);
    }
}
