#include "MiscFunctions.hpp"

std::string absolutePath(const std::string& rawpath, const std::string& currentPath) {
    auto value = rawpath;

    if (value[0] == '~') {
        static const char* const ENVHOME = getenv("HOME");
        value.replace(0, 1, std::string(ENVHOME));
    } else if (value[0] != '/') {
        auto currentDir = currentPath.substr(0, currentPath.find_last_of('/'));

        if (value[0] == '.') {
            if (value[1] == '.' && value[2] == '/') {
                auto parentDir = currentDir.substr(0, currentDir.find_last_of('/'));
                value.replace(0, 2 + currentPath.empty(), parentDir);
            } else if (value[1] == '/')
                value.replace(0, 1 + currentPath.empty(), currentDir);
            else
                value = currentDir + '/' + value;
        } else
            value = currentDir + '/' + value;
    }

    return value;
}