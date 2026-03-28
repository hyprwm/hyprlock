#pragma once
#include "../defines.hpp"

#include <format>
#include <hyprutils/cli/Logger.hpp>

#define RASSERT(expr, reason, ...)                                                                                                                                                 \
    if (!(expr)) {                                                                                                                                                                 \
        Log::logger->log(Log::CRIT, "\n==========================================================================================\nASSERTION FAILED! \n\n{}\n\nat: line {} in {}",            \
                   std::format(reason, ##__VA_ARGS__), __LINE__,                                                                                                                   \
                   ([]() constexpr -> std::string { return std::string(__FILE__).substr(std::string(__FILE__).find_last_of('/') + 1); })().c_str());                               \
        std::abort();                                                                                                                                                              \
    }

#define ASSERT(expr) RASSERT(expr, "?")

namespace Log {
    class CLogger {
      public:
        template <typename... Args>
        void log(Hyprutils::CLI::eLogLevel level, std::format_string<Args...> fmt, Args&&... args) {
            if (m_quiet)
                return;

            if (level == Hyprutils::CLI::LOG_TRACE && !m_verbose)
                return;

            m_logger.log(level, std::vformat(fmt.get(), std::make_format_args(args...)));
        }

        bool                    m_quiet   = false;
        bool                    m_verbose = false;

      private:
        Hyprutils::CLI::CLogger m_logger;
    };

    inline UP<CLogger> logger = makeUnique<CLogger>();

    //
    inline constexpr const Hyprutils::CLI::eLogLevel WARN  = Hyprutils::CLI::LOG_WARN;
    inline constexpr const Hyprutils::CLI::eLogLevel ERR   = Hyprutils::CLI::LOG_ERR;
    inline constexpr const Hyprutils::CLI::eLogLevel CRIT  = Hyprutils::CLI::LOG_CRIT;
    inline constexpr const Hyprutils::CLI::eLogLevel INFO  = Hyprutils::CLI::LOG_DEBUG;
    inline constexpr const Hyprutils::CLI::eLogLevel TRACE = Hyprutils::CLI::LOG_TRACE;
};
