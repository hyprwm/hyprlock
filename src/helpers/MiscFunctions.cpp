#include <filesystem>
#include "MiscFunctions.hpp"

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
        return std::filesystem::weakly_canonical(currentDir / path);
    } else {
        return std::filesystem::weakly_canonical(path);
    }
}