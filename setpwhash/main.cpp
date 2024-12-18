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
#include <vector>

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

void readPW(std::string& pw) {
    setStdinEcho(false);
    std::string input;
    std::getline(std::cin, pw);
    setStdinEcho(true);
}

enum HashCost { INTERACTIVE, MODERATE, SENSITIVE };

unsigned int getOpsLimit(HashCost cost) {
    switch (cost) {
        case HashCost::INTERACTIVE:
            return crypto_pwhash_OPSLIMIT_INTERACTIVE;
        case HashCost::MODERATE:
            return crypto_pwhash_OPSLIMIT_MODERATE;
        case HashCost::SENSITIVE:
            return crypto_pwhash_OPSLIMIT_SENSITIVE;
        default:
            return crypto_pwhash_OPSLIMIT_MODERATE;
    }
    std::unreachable();
}

unsigned int getMemLimit(HashCost cost) {
    switch (cost) {
        case HashCost::INTERACTIVE:
            return crypto_pwhash_MEMLIMIT_INTERACTIVE;
        case HashCost::MODERATE:
            return crypto_pwhash_MEMLIMIT_MODERATE;
        case HashCost::SENSITIVE:
            return crypto_pwhash_MEMLIMIT_SENSITIVE;
        default:
            return crypto_pwhash_MEMLIMIT_MODERATE;
    }
    std::unreachable();
}

void help() {
    std::cout << "Usage: hyprlock-setpwhash [hashing cost]" << std::endl;
    std::cout << "Set the password hash for hyprlock" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  -h, --help     Show this help message and exit" << std::endl;
    std::cout << "  [hashing cost] How computationally expensive should the hashing be?" << std::endl;
    std::cout << "                 interactive - fast checking, least security" << std::endl;
    std::cout << "                 moderate (default) - moderate checking speed, moderate security" << std::endl;
    std::cout << "                 sensitive - slow checking speed, decent security" << std::endl;
}


int  main(int argc, char** argv, char** envp) {
    std::vector<std::string> args(argv, argv + argc);

    RASSERT(sodium_init() >= 0, "Failed to initialize libsodium");

    auto hashCost = HashCost::MODERATE;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            help();
            return 0;
        }

        if (arg == "interactive")
            hashCost = HashCost::INTERACTIVE;
        else if (arg == "moderate")
            hashCost = HashCost::MODERATE;
        else if (arg == "sensitive")
            hashCost = HashCost::SENSITIVE;
        else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            help();
            return 1;
        }
    }

    static const auto [SECRETSCONF, DOTDIR] = Hyprutils::Path::findConfig("hyprlock_pwhash");
    if (SECRETSCONF.has_value()) {
        // check permissions
        std::cout << SECRETSCONF.value() << " already exists" << std::endl;
        std::cout << "Do you want to overwrite it? [y/N] ";
        char C;
        std::cin >> C;
        std::cin.ignore(10, '\n');

        if (C != 'y' && C != 'Y') {
            std::cout << "Keeping existing secrets!" << std::endl;

            const auto PERMS = std::filesystem::status(SECRETSCONF.value()).permissions();
            if ((PERMS & perms::group_read) != perms::none || (PERMS & perms::group_write) != perms::none || (PERMS & perms::others_read) != perms::none ||
                (PERMS & perms::others_write) != perms::none) {
                std::cout << "Setting permissions of " << SECRETSCONF.value() << " to -rw-------" << std::endl;
                // set perms to -rw-------
                std::filesystem::permissions(SECRETSCONF.value(), perms::owner_read | perms::owner_write);
            }
            return 0;
        }
    }

    RASSERT(DOTDIR.has_value(), "Failed to find config directory!");
    const auto       DEST = DOTDIR.value() + "/hypr/hyprlock_pwhash.conf";

    std::string pw;
    std::cout << "New password: ";

    readPW(pw);

    std::cout << "\r";

    std::string pw2;
    std::cout << "Repeat password: ";

    readPW(pw2);

    if (pw != pw2) {
        std::cout << "Passwords do not match" << std::endl;
        return 1;
    }

    char hash[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hash, pw.c_str(), pw.size(), getOpsLimit(hashCost), getMemLimit(hashCost)) != 0) {
        Debug::log(ERR, "[Sodium] Failed to hash password");
        return 1;
    }

    std::cout << "Writing password hash to " << DEST << std::endl;

    std::ofstream out(DEST);
    out << "pw_hash = " << hash << std::endl;
    out.close();

    // set perms to -rw-------
    std::filesystem::permissions(DEST, perms::owner_read | perms::owner_write);
    return 0;
}
