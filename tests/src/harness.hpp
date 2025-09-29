#pragma once

#include "wayland.hpp"
#include "hyprland-lock-notify-v1.hpp"

#include <cstdint>
#include <string>
#include <hyprutils/memory/SharedPtr.hpp>

#define SP Hyprutils::Memory::CSharedPointer

namespace NTestSessionLock {
    enum eTestResult : uint8_t {
        OK,
        BAD_ENVIRONMENT,
        PREMATURE_UNLOCK,
        LOCK_TIMEOUT,
        UNLOCK_TIMEOUT,
        BAD_EXITCODE,
        CRASH,
    };

    struct SSesssionLockTest {
        std::string m_clientPath = "";
        std::string m_configPath = "";

        bool        m_unlockWithUSR1 = false;

        uint32_t    m_timeoutMs = 60000; // 1 minute
    };

    struct SSessionLockState {
        SP<CCHyprlandLockNotifierV1>     m_lockNotifier     = nullptr;
        SP<CCHyprlandLockNotificationV1> m_lockNotification = nullptr;
        bool                             m_didLock          = false;
        bool                             m_didUnlock        = false;
    };

    const char* testResultString(eTestResult res);
    eTestResult run(const SSesssionLockTest& test);
};
