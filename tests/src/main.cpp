#include "harness.hpp"
#include "shared.hpp"
#include "Log.hpp"

#include <filesystem>

using namespace NTestSessionLock;

static int runTest(const std::string& binaryPath, const std::string& configPath) {
    int ret = 0;
    NLog::log("Running lock test (clientPath={}, configPath={})...", binaryPath, configPath);

    SSesssionLockTest test = {
        .m_clientPath     = binaryPath,
        .m_configPath     = configPath,
        .m_unlockWithUSR1 = false,
    };

    auto testResult = run(test);
    EXPECT(testResultString(testResult), testResultString(eTestResult::OK));

    return ret;
}

static void help() {
    NLog::log("usage: lock_tester [arg [...]].\n");
    NLog::log(R"(Arguments:
    --help              -h       - Show this message again
    --config FILE       -c FILE  - Specify config file to use
    --binary FILE       -b FILE  - Specify Hyprland binary to use)");
}

int main(int argc, char** argv) {
    std::string              binaryPath = "";
    std::string              configPath = "";

    std::vector<std::string> args{argv + 1, argv + argc};

    for (auto it = args.begin(); it != args.end(); it++) {
        if (*it == "--config" || *it == "-c") {
            if (std::next(it) == args.end()) {
                help();

                return 1;
            }

            configPath = *std::next(it);

            try {
                configPath = std::filesystem::canonical(configPath);

                if (!std::filesystem::is_regular_file(configPath)) {
                    throw std::exception();
                }
            } catch (...) {
                std::println(stderr, "[ ERROR ] Config file '{}' doesn't exist!", configPath);
                help();

                return 1;
            }

            it++;

            continue;
        } else if (*it == "--binary" || *it == "-b") {
            if (std::next(it) == args.end()) {
                help();

                return 1;
            }

            binaryPath = *std::next(it);

            try {
                binaryPath = std::filesystem::canonical(binaryPath);

                if (!std::filesystem::is_regular_file(binaryPath)) {
                    throw std::exception();
                }
            } catch (...) {
                std::println(stderr, "[ ERROR ] Binary '{}' doesn't exist!", binaryPath);
                help();

                return 1;
            }

            it++;

            continue;
        } else if (*it == "--help" || *it == "-h") {
            help();

            return 0;
        } else {
            std::println(stderr, "[ ERROR ] Unknown option '{}' !", *it);
            help();

            return 1;
        }
    }

    if (binaryPath.empty() || configPath.empty()) {
        std::println(stderr, "[ ERROR ] Expected binary and config path!");
        help();

        return 1;
    }
    return runTest(binaryPath, configPath);
}
