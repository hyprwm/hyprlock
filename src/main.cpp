
#include "config/ConfigManager.hpp"
#include "core/hyprlock.hpp"
#include "helpers/Log.hpp"
#include "core/AnimationManager.hpp"
#include <cstddef>
#include <string_view>

void help() {
    std::println("Usage: hyprlock [options]\n\n"
                 "Options:\n"
                 "  -v, --verbose            - Enable verbose logging\n"
                 "  -q, --quiet              - Disable logging\n"
                 "  -c FILE, --config FILE   - Specify config file to use\n"
                 "  --display NAME           - Specify the Wayland display to connect to\n"
                 "  --grace SECONDS          - Set grace period in seconds before requiring authentication\n"
                 "  --immediate-render       - Do not wait for resources before drawing the background\n"
                 "  --no-fade-in             - Disable the fade-in animation when the lock screen appears\n"
                 "  -V, --version            - Show version information\n"
                 "  -h, --help               - Show this help message");
}

std::optional<std::string> parseArg(const std::vector<std::string>& args, const std::string& flag, std::size_t& i) {
    if (i + 1 < args.size()) {
        return args[++i];
    } else {
        std::println(stderr, "Error: Missing value for {} option.", flag);
        return std::nullopt;
    }
}

static void printVersion() {
    constexpr bool ISTAGGEDRELEASE = std::string_view(HYPRLOCK_COMMIT) == HYPRLOCK_VERSION_COMMIT;
    if (ISTAGGEDRELEASE)
        std::println("Hyprlock version v{}", HYPRLOCK_VERSION);
    else
        std::println("Hyprlock version v{} (commit {})", HYPRLOCK_VERSION, HYPRLOCK_COMMIT);
}

int main(int argc, char** argv, char** envp) {
    std::string              configPath;
    std::string              wlDisplay;
    bool                     immediateRender = false;
    bool                     noFadeIn        = false;
    int                      graceSeconds    = 0;

    std::vector<std::string> args(argv, argv + argc);

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            help();
            return 0;
        }

        if (arg == "--version" || arg == "-V") {
            printVersion();
            return 0;
        }

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

        } else if (arg == "--grace" && i + 1 < (std::size_t)argc) {
            if (auto value = parseArg(args, arg, i); value) {
                try {
                    graceSeconds = std::stoi(*value);
                    if (graceSeconds < 0) {
                        std::println(stderr, "Error: Grace period must be non-negative.");
                        return 1;
                    }
                } catch (const std::exception&) {
                    std::println(stderr, "Error: Invalid grace period value: {}", *value);
                    return 1;
                }
            } else
                return 1;

        } else if (arg == "--immediate-render")
            immediateRender = true;

        else if (arg == "--no-fade-in")
            noFadeIn = true;

        else {
            std::println(stderr, "Unknown option: {}", arg);
            help();
            return 1;
        }
    }

    printVersion();
    g_pAnimationManager = makeUnique<CHyprlockAnimationManager>();

    try {
        g_pConfigManager = makeUnique<CConfigManager>(configPath);
        g_pConfigManager->init();
    } catch (const std::exception& ex) {
        Debug::log(CRIT, "ConfigManager threw: {}", ex.what());
        if (std::string(ex.what()).contains("File does not exist"))
            Debug::log(NONE, "           Make sure you have a config.");

        return 1;
    }

    if (noFadeIn)
        g_pConfigManager->m_AnimationTree.setConfigForNode("fadeIn", false, 0.f, "default");

    try {
        g_pHyprlock = makeUnique<CHyprlock>(wlDisplay, immediateRender, graceSeconds);
        g_pHyprlock->run();
    } catch (const std::exception& ex) {
        Debug::log(CRIT, "Hyprlock threw: {}", ex.what());
        return 1;
    }

    return 0;
}
