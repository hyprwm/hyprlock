
#include "config/ConfigManager.hpp"
#include "core/hyprlock.hpp"
#include "helpers/Log.hpp"
#include "core/AnimationManager.hpp"

#include <string_view>

#include <hyprutils/cli/ArgumentParser.hpp>

static void printVersion() {
    constexpr bool ISTAGGEDRELEASE = std::string_view(HYPRLOCK_COMMIT) == HYPRLOCK_VERSION_COMMIT;
    if (ISTAGGEDRELEASE)
        std::println("Hyprlock version v{}", HYPRLOCK_VERSION);
    else
        std::println("Hyprlock version v{} (commit {})", HYPRLOCK_VERSION, HYPRLOCK_COMMIT);
}

int main(int argc, char** argv, char** envp) {
    std::vector<const char*>        args(argv, argv + argc);

    Hyprutils::CLI::CArgumentParser argParser(args);

    ASSERT(argParser.registerBoolOption("help", "h", "Show this help message").has_value());
    ASSERT(argParser.registerBoolOption("version", "V", "Print hyprlock version, then exit").has_value());
    ASSERT(argParser.registerBoolOption("verbose", "v", "Enable verbose logging").has_value());
    ASSERT(argParser.registerBoolOption("quiet", "q", "Disable logging").has_value());
    ASSERT(argParser.registerStringOption("config", "c", "Specify config file to use").has_value());
    ASSERT(argParser.registerIntOption("grace", "g", "Seconds before authentication is required").has_value());
    ASSERT(argParser.registerBoolOption("immediate-render", "", "Draw background immediately (Don't wait for resources)").has_value());
    ASSERT(argParser.registerBoolOption("no-fade-in", "", "Disable the fade-in animation").has_value());
    ASSERT(argParser.registerStringOption("display", "", "Specify the Wayland display to connect to").has_value());
    ASSERT(argParser.registerIntOption("immediate", "", "[Deprecated] (Use \"--grace 0\" instead)").has_value());

    auto options = argParser.parse();

    if (!options.has_value()) {
        Log::logger->log(Log::ERR, "Invalid argument: {}", options.error());
        return 1;
    }

    if (argParser.getBool("help")) {
        std::print("{}", argParser.getDescription("Hyprlock CLI Arguments", 87));
        return 0;
    }

    if (argParser.getBool("version")) {
        printVersion();
        return 0;
    }

    if (argParser.getBool("verbose"))
        Log::logger->setVerbose();

    bool quiet = argParser.getBool("quiet").value_or(false);
    if (quiet)
        Log::logger->setQuiet();

    int graceSeconds = argParser.getInt("grace").value_or(0);
    if (argParser.getBool("immediate")) {
        graceSeconds = 0;
        Log::logger->log(Log::WARN, R"("--immediate" is deprecated. Use the "--grace" option instead.)");
    }

    bool immediateRender = argParser.getBool("immediate-render").value_or(false);
    bool noFadeIn        = argParser.getBool("no-fade-in").value_or(false);

    if (!quiet)
        printVersion();

    g_pAnimationManager = makeUnique<CHyprlockAnimationManager>();

    auto configPath = CConfigManager::resolveConfigPath(argParser.getString("config"));
    if (!configPath.has_value()) {
        Log::logger->log(Log::CRIT, " Config path error: {}", configPath.error());
        return 1;
    }

    try {
        g_pConfigManager = makeUnique<CConfigManager>(configPath.value().c_str());
        g_pConfigManager->init();
    } catch (const std::exception& ex) {
        Log::logger->log(Log::CRIT, "Config threw: {}", ex.what());
        return 1;
    }

    if (noFadeIn)
        g_pConfigManager->m_AnimationTree.setConfigForNode("fadeIn", false, 0.f, "default");

    try {
        g_pHyprlock = makeUnique<CHyprlock>(argParser.getString("display").value_or(""), immediateRender, graceSeconds);
        g_pHyprlock->run();
    } catch (const std::exception& ex) {
        Log::logger->log(Log::CRIT, "Hyprlock threw: {}", ex.what());
        return 1;
    }

    return 0;
}
