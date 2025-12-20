#include "../src/helpers/Log.hpp"

#include <filesystem>
#include <hyprutils/path/Path.hpp>
#include <iostream>
#include <fstream>
#include <sodium.h>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <print>

using std::filesystem::perms;

static void setStdinEcho(bool enable = true) {
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if (!enable)
        tty.c_lflag &= ~ECHO;
    else
        tty.c_lflag |= ECHO;
    RASSERT(tcsetattr(STDIN_FILENO, TCSANOW, &tty) == 0, "Failed to set terminal attributes");
}

// returns the first none-whitespace char
static int getChoice() {
    std::string input;
    std::getline(std::cin, input);
    const auto p = input.find_first_not_of(" \n");
    return (p == std::string::npos) ? 0 : input[p];
}

constexpr auto      CHOOSELIMITSPROMPT = R"#(
Choose how hard it will be to brute force your password.
This also defines how long it will take to check the password.
1 - interactive (least security, pretty fast checking)
2 - moderate (medium security, takes below a second on most machines)
3 - sensitive (decent security, takes around 2-4 seconds on most machines)
Type 1, 2 or 3, or Enter for default (2): )#";

static unsigned int getOpsLimit(int choice) {
    switch (choice) {
        case '1': return crypto_pwhash_OPSLIMIT_INTERACTIVE;
        case '2': return crypto_pwhash_OPSLIMIT_MODERATE;
        case '3': return crypto_pwhash_OPSLIMIT_SENSITIVE;
        default: return crypto_pwhash_OPSLIMIT_MODERATE;
    }
    std::unreachable();
}

static unsigned int getMemLimit(int choice) {
    switch (choice) {
        case '1': return crypto_pwhash_MEMLIMIT_INTERACTIVE;
        case '2': return crypto_pwhash_MEMLIMIT_MODERATE;
        case '3': return crypto_pwhash_MEMLIMIT_SENSITIVE;
        default: return crypto_pwhash_MEMLIMIT_MODERATE;
    }
    std::unreachable();
}

static void help() {
    std::println("Usage: hyprlock-setpwhash [options]\n\n"
                 "Options:\n"
                 "  -c FILE, --config FILE   - Specify config file to use\n"
                 "  -h, --help               - Show this help message\n\n"
                 "Interactive utility to set the password hash for hyprlock");
}

static std::optional<std::string> parseArg(const std::vector<std::string>& args, const std::string& flag, std::size_t& i) {
    if (i + 1 < args.size()) {
        return args[++i];
    } else {
        std::println(stderr, "Error: Missing value for {} option.", flag);
        return std::nullopt;
    }
}

int main(int argc, char** argv, char** envp) {
    std::string              configPath;
    std::vector<std::string> args(argv, argv + argc);

    RASSERT(sodium_init() >= 0, "Failed to initialize libsodium");

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            help();
            return 0;
        } else if ((arg == "--config" || arg == "-c") && i + 1 < (std::size_t)argc) {
            if (auto value = parseArg(args, arg, i); value)
                configPath = *value;
            else
                return 1;

        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            help();
            return 1;
        }
    }

    std::string DEST;
    const auto [SECRETSCONF, DOTDIR] = Hyprutils::Path::findConfig("hyprlock_sodium");

    if (!configPath.empty())
        DEST = configPath;

    else if (SECRETSCONF.has_value())
        DEST = SECRETSCONF.value();

    else {
        RASSERT(DOTDIR.has_value(), "Failed to find config directory!");
        DEST = DOTDIR.value() + "/hypr/hyprlock_sodium.conf";
    }

    if (std::filesystem::exists(DEST)) {
        // check permissions
        std::println("{} already exists.", DEST);
        std::print("Do you want to overwrite it? [y/N] ");
        const auto CHOICE = getChoice();

        if (CHOICE != 'y' && CHOICE != 'Y') {
            std::println("Keeping existing secrets!");

            const auto PERMS = std::filesystem::status(DEST).permissions();
            if ((PERMS & perms::group_read) != perms::none || (PERMS & perms::group_write) != perms::none || (PERMS & perms::others_read) != perms::none ||
                (PERMS & perms::others_write) != perms::none) {
                std::println("Setting permissions of {} to -rw-------", DEST);

                // set perms to -rw-------
                std::filesystem::permissions(DEST, perms::owner_read | perms::owner_write);
            }
            return 0;
        }
    }

    std::println("Note: We are going to write a password hash to {}\n"
                 "      If you choose a weak password and this hash gets leaked,\n"
                 "      someone might be able to guess your password using a password list or brute force.\n"
                 "      So best to keep it safe and (or) choose a good password.",
                 DEST);

    std::print(CHOOSELIMITSPROMPT);
    const auto CHOICE = getChoice();

    setStdinEcho(false);
    std::string pw = "";
    while (true) {
        std::print("New password: ");
        std::getline(std::cin, pw);

        if (pw.empty()) {
            std::println("\rEmpty password");
            continue;
        }

        if (pw.size() < 4) {
            std::println("\rPassword too short");
            continue;
        }

        std::string pw2 = "";
        std::print("\rRepeat password: ");
        std::getline(std::cin, pw2);

        if (pw != pw2) {
            std::println("\rPasswords do not match");
            continue;
        }

        break;
    }
    setStdinEcho(true);

    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hash, pw.c_str(), pw.size(), getOpsLimit(CHOICE), getMemLimit(CHOICE)) != 0) {
        std::println("[Sodium] Failed to hash password");
        return 1;
    }

    {
        std::ofstream out(DEST);
        // set perms to -rw-------
        std::filesystem::permissions(DEST, perms::owner_read | perms::owner_write);

        out << "hash = " << hash << std::endl;
    }

    std::println("\nDone!");
    return 0;
}
