
#include "config/ConfigManager.hpp"
#include "core/hyprlock.hpp"
#include "src/helpers/Log.hpp"
#include <cstddef>
#include <iostream>
#include <string_view>
#include <charconv>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

void help() {
    std::cout << "Usage: hyprlock [options]\n\n"
                 "Options:\n"
                 "  -v, --verbose            - Enable verbose logging\n"
                 "  -q, --quiet              - Disable logging\n"
                 "  -c FILE, --config FILE   - Specify config file to use\n"
                 "  --display NAME           - Specify the Wayland display to connect to\n"
                 "  --immediate              - Lock immediately, ignoring any configured grace period\n"
                 "  --immediate-render       - Do not wait for resources before drawing the background\n"
                 "  --no-fade-in             - Disable the fade-in animation when the lock screen appears\n"
                 "  -R FD, --ready-fd FD     - Write a single newline character to this file descriptor after locking the screen\n"
                 "  -D, --daemonize          - Run hyprlock in the background. This command will return after the screen has been locked\n"
                 "  -V, --version            - Show version information\n"
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
    bool                     immediate       = false;
    bool                     immediateRender = false;
    bool                     noFadeIn        = false;
    bool                     daemonize       = false;
    int                      notifyFd        = -1;

    std::vector<std::string> args(argv, argv + argc);

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            help();
            return 0;
        }

        if (arg == "--version" || arg == "-V") {
            constexpr bool ISTAGGEDRELEASE = std::string_view(HYPRLOCK_COMMIT) == HYPRLOCK_VERSION_COMMIT;

            std::cout << "Hyprlock version v" << HYPRLOCK_VERSION;
            if (!ISTAGGEDRELEASE)
                std::cout << " (commit " << HYPRLOCK_COMMIT << ")";
            std::cout << std::endl;
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

        } else if (arg == "--immediate")
            immediate = true;

        else if (arg == "--immediate-render")
            immediateRender = true;

        else if (arg == "--no-fade-in")
            noFadeIn = true;

        else if (arg == "--ready-fd" || arg == "-R") {
            if (auto value = parseArg(args, arg, i); value) {
                auto [ptr, ec] = std::from_chars(value->data(), value->data() + value->size(), notifyFd);
                if (ptr != value->data() + value->size() || notifyFd < 0)
                    return 1;
            } else
                return 1;
        }

        else if (arg == "--daemonize" || arg == "-D")
            daemonize = true;

        else {
            std::cerr << "Unknown option: " << arg << "\n";
            help();
            return 1;
        }
    }

    if (daemonize) {
        int pipe_fds[2];
        if (pipe(pipe_fds) != 0) {
            Debug::log(CRIT, "Cannot open pipe to child process\n");
            return 1;
        }

        pid_t pid = fork();
        if (pid < 0) {
            Debug::log(CRIT, "Cannot create child process\n");
            return 1;
        }

        if (pid > 0) {
            close(pipe_fds[1]);

            char tmp;
            ssize_t n_read;
            do {
                // if the child exits without writing, read() will return zero
                n_read = read(pipe_fds[0], &tmp, 1);
            } while (n_read == -1 && errno == EINTR);

            if (n_read == 1) return 0; // screen is now locked
            return 1; // child failed to lock the screen
        }

        int flags = fcntl(pipe_fds[1], F_GETFD);
        fcntl(pipe_fds[1], F_SETFD, flags | FD_CLOEXEC);
        close(pipe_fds[0]);
        notifyFd = pipe_fds[1];
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
        g_pHyprlock = std::make_unique<CHyprlock>(wlDisplay, immediate, immediateRender, noFadeIn, notifyFd);
        g_pHyprlock->run();
    } catch (const std::exception& ex) {
        Debug::log(CRIT, "Hyprlock threw: {}", ex.what());
        return 1;
    }

    return 0;
}
