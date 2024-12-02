#pragma once

#include <memory>
#include <optional>
#include <string>

class CIAuth {
    public:
        virtual void start() = 0;
        virtual bool auth() = 0;
        virtual bool isAuthenticated() = 0;
        virtual void waitForInput() = 0;
        virtual void submitInput(std::string input) = 0;
        virtual std::optional<std::string> getLastFailText() = 0;
        virtual std::optional<std::string> getLastPrompt() = 0;
        virtual bool checkWaiting() = 0;
        virtual void terminate() = 0;

        CIAuth() = default;

        // Should only be set via the main thread
        bool m_bDisplayFailText = false;
};

inline std::unique_ptr<CIAuth> g_pAuth;
