#include "../src/helpers/Log.hpp"

#include <filesystem>
#include <hyprutils/path/Path.hpp>
#include <iostream>
#include <fstream>
#include <istream>
#include <sodium.h>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <print>

using std::filesystem::perms;

void setStdinEcho(bool enable = true) {
    struct termios tty;
    tcgetattr(STDIN_FILENO, &tty);
    if (!enable)
        tty.c_lflag &= ~ECHO;
    else
        tty.c_lflag |= ECHO;
    RASSERT(tcsetattr(STDIN_FILENO, TCSANOW, &tty) == 0, "Failed to set terminal attributes");
}

// returns the first none-whitespace char
int getChoice() {
    std::string input;
    std::getline(std::cin, input);
    const auto p = input.find_first_not_of(" \n");
    return (p == std::string::npos) ? 0 : input[p];
}

constexpr auto CHOOSELIMITSPROMPT = R"#(
Choose how hard it will be to brute force your password.
This also defines how long it will take to check the password.
1 - interactive (least security, pretty fast checking)
2 - moderate (medium security, takes below a second on most machines)
3 - sensitive (decent security, takes around 2-4 seconds on most machines)

Type 1, 2 or 3, or Enter for default (2): )#";

unsigned int   getOpsLimit(int choice) {
    switch (choice) {
        case '1': return crypto_pwhash_OPSLIMIT_INTERACTIVE;
        case '2': return crypto_pwhash_OPSLIMIT_MODERATE;
        case '3': return crypto_pwhash_OPSLIMIT_SENSITIVE;
        default: return crypto_pwhash_OPSLIMIT_MODERATE;
    }
    std::unreachable();
}

unsigned int getMemLimit(int choice) {
    switch (choice) {
        case '1': return crypto_pwhash_MEMLIMIT_INTERACTIVE;
        case '2': return crypto_pwhash_MEMLIMIT_MODERATE;
        case '3': return crypto_pwhash_MEMLIMIT_SENSITIVE;
        default: return crypto_pwhash_MEMLIMIT_MODERATE;
    }
    std::unreachable();
}

void help() {
    std::println("Usage: hyprlock-setpwhash\n"
                 "Interactive utility to set the password hash for hyprlock");
}

int main(int argc, char** argv, char** envp) {
    std::vector<std::string> args(argv, argv + argc);

    RASSERT(sodium_init() >= 0, "Failed to initialize libsodium");

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            help();
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            help();
            return 1;
        }
    }

    const auto [SECRETSCONF, DOTDIR] = Hyprutils::Path::findConfig("hyprlock_pwhash");
    if (SECRETSCONF.has_value()) {
        // check permissions
        std::println("{} already exists.", SECRETSCONF.value());
        std::print("Do you want to overwrite it? [y/N] ");
        const auto CHOICE = getChoice();

        if (CHOICE != 'y' && CHOICE != 'Y') {
            std::println("Keeping existing secrets!");

            const auto PERMS = std::filesystem::status(SECRETSCONF.value()).permissions();
            if ((PERMS & perms::group_read) != perms::none || (PERMS & perms::group_write) != perms::none || (PERMS & perms::others_read) != perms::none ||
                (PERMS & perms::others_write) != perms::none) {
                std::println("Setting permissions of {} to -rw-------", SECRETSCONF.value());

                // set perms to -rw-------
                std::filesystem::permissions(SECRETSCONF.value(), perms::owner_read | perms::owner_write);
            }
            return 0;
        }
    }

    RASSERT(DOTDIR.has_value(), "Failed to find config directory!");
    const auto DEST = DOTDIR.value() + "/hypr/hyprlock_pwhash.conf";

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
        std::print("\r");

        if (pw.empty()) {
            std::println("Empty password");
            continue;
        }

        if (pw.size() < 4) {
            std::println("Less than 4 characters? Nope.");
            continue;
        }

        std::string pw2 = "";
        std::print("Repeat password: ");
        std::getline(std::cin, pw2);
        std::print("\r");

        if (pw != pw2) {
            std::println("Ups, passwords do not match");
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
        out << "pw_hash = " << hash << std::endl;
    }

    // set perms to -rw-------
    std::filesystem::permissions(DEST, perms::owner_read | perms::owner_write);

    std::println("Done!");
    return 0;
}
