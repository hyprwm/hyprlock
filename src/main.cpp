
#include "config/ConfigManager.hpp"
#include "core/hyprlock.hpp"
#include <cstddef>

void help() {
    std::cout << "Usage: hyprlock [options]\n\n"
                 "Options:\n"
                 "  -v, --verbose            - Enable verbose logging\n"
                 "  -q, --quiet              - Disable logging\n"
                 "  -c FILE, --config FILE   - Specify config file to use\n"
                 "  --display (display)      - Specify the Wayland display to connect to\n"
                 "  --immediate              - Lock immediately, ignoring any configured grace period\n"
                 "  -h, --help               - Show this help message\n";
}

std::optional<std::string> parseArg(const std::vector<std::string>& args, const std::string& flag, std::size_t& i) {
    if (i + 1 < args.size()) {
        return args[++i];
    } else {
        std::cerr << "Error: Missing value for " << flag << " option.\n";
        return std::nullopt;
    }
}

int main(int argc, char** argv, char** envp) {
    std::string              configPath;
    std::string              wlDisplay;
    bool                     immediate = false;
    bool                     showHelp  = false;

    std::vector<std::string> args(argv, argv + argc);

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string arg = argv[i];

        if (arg == "--verbose" || arg == "-v")
            Debug::verbose = true;

        else if (arg == "--quiet" || arg == "-q")
            Debug::quiet = true;

        else if ((arg == "--config" || arg == "-c") && i + 1 < (std::size_t)argc) {
            if (auto value = parseArg(args, arg, i); value)
                configPath = *value;
            else
                return 1;

        } else if (arg == "--display" && i + 1 < (std::size_t)argc) {
            if (auto value = parseArg(args, arg, i); value)
                wlDisplay = *value;
            else
                return 1;

        } else if (arg == "--immediate")
            immediate = true;

        else if (arg == "--help" || arg == "-h") {
            showHelp = true;
            break;

        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            help();
            return 1;
        }
    }

    if (showHelp) {
        help();
        return 0;
    }

    try {
        g_pConfigManager = std::make_unique<CConfigManager>(configPath);
        g_pConfigManager->init();
    } catch (const std::exception& ex) {
        Debug::log(CRIT, "ConfigManager threw: {}", ex.what());
        if (std::string(ex.what()).contains("File does not exist"))
            Debug::log(NONE, "           Make sure you have a config.");

        return 1;
    }

    try {
        g_pHyprlock = std::make_unique<CHyprlock>(wlDisplay, immediate);
        g_pHyprlock->run();
    } catch (const std::exception& ex) {
        Debug::log(CRIT, "Hyprlock threw: {}", ex.what());
        return 1;
    }

    return 0;
}
