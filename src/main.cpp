
#include "config/ConfigManager.hpp"
#include "core/hyprlock.hpp"

void help() {
    std::cout << "Usage: hyprlock [options]\n\n"
                 "Options:\n"
                 "  -v, --verbose            - Enable verbose logging\n"
                 "  -q, --quiet              - Disable logging\n"
                 "  --display (display)      - Specify the Wayland display to connect to\n"
                 "  --immediate              - Lock immediately, ignoring any configured grace period\n"
                 "  -h, --help               - Show this help message\n";
}
int main(int argc, char** argv, char** envp) {
    std::string wlDisplay;
    bool immediate = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--verbose" || arg == "-v")
            Debug::verbose = true;

        else if (arg == "--quiet" || arg == "-q")
            Debug::quiet = true;

        else if (arg == "--display" && i + 1 < argc) {
            wlDisplay = argv[i + 1];
            i++;
        }
        else if (arg == "--immediate") {
            immediate = true;
        } else if (arg == "--help" || arg == "-h") {
            help();
            return 0;
        }
    }

    try {
        g_pConfigManager = std::make_unique<CConfigManager>();
        g_pConfigManager->init();
    } catch (const char* err) {
        Debug::log(CRIT, "ConfigManager threw: {}", err);
        std::string strerr = err;
        if (strerr.contains("File does not exist"))
            Debug::log(NONE, "           Make sure you have a config.");
        return 1;
    }

    g_pHyprlock = std::make_unique<CHyprlock>(wlDisplay, immediate);
    g_pHyprlock->run();

    return 0;
}
