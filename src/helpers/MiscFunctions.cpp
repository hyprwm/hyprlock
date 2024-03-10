#include <filesystem>
#include "MiscFunctions.hpp"

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