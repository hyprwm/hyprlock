#pragma once
#include <format>
#include <string>
#include <print>

enum eLogLevel {
    TRACE = 0,
    INFO,
    LOG,
    WARN,
    ERR,
    CRIT,
    NONE
};

#define RASSERT(expr, reason, ...)                                                                                                                                                 \
    if (!(expr)) {                                                                                                                                                                 \
        Debug::log(CRIT, "\n==========================================================================================\nASSERTION FAILED! \n\n{}\n\nat: line {} in {}",            \
                   std::format(reason, ##__VA_ARGS__), __LINE__,                                                                                                                   \
                   ([]() constexpr -> std::string { return std::string(__FILE__).substr(std::string(__FILE__).find_last_of('/') + 1); })().c_str());                               \
        std::abort();                                                                                                                                                              \
    }

#define ASSERT(expr) RASSERT(expr, "?")

namespace Debug {
    constexpr const char* logLevelString(eLogLevel level) {
        switch (level) {
            case TRACE: return "TRACE"; break;
            case INFO: return "INFO"; break;
            case LOG: return "LOG"; break;
            case WARN: return "WARN"; break;
            case ERR: return "ERR"; break;
            case CRIT: return "CRITICAL"; break;
            default: return "??";
        }
    }
    inline bool quiet   = false;
    inline bool verbose = false;

    template <typename... Args>
    void log(eLogLevel level, const std::string& fmt, Args&&... args) {

        if (!verbose && level == TRACE)
            return;

        if (quiet)
            return;

        if (level != NONE) {
            std::println("[{}] {}", logLevelString(level), std::vformat(fmt, std::make_format_args(args...)));
        }
    }
};
