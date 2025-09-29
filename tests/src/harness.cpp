#include "harness.hpp"

#include <chrono>
#include <fstream>
#include <print>
#include <hyprutils/os/Process.hpp>
#include <sys/wait.h>
#include <thread>

using namespace NTestSessionLock;
using namespace Hyprutils::Memory;
using namespace Hyprutils::OS;

const char* NTestSessionLock::testResultString(eTestResult res) {
    switch (res) {
        case eTestResult::OK: return "OK"; break;
        case eTestResult::BAD_ENVIRONMENT: return "BAD_ENVIRONMENT"; break;
        case eTestResult::PREMATURE_UNLOCK: return "PREMATURE_UNLOCK"; break;
        case eTestResult::LOCK_TIMEOUT: return "LOCK_TIMEOUT"; break;
        case eTestResult::UNLOCK_TIMEOUT: return "UNLOCK_TIMEOUT"; break;
        case eTestResult::BAD_EXITCODE: return "BAD_EXITCODE"; break;
        case eTestResult::CRASH: return "CRASH"; break;
        default: return "???";
    }
}

eTestResult NTestSessionLock::run(const SSesssionLockTest& test) {
    // Setup wayland stuff to recieve lock notifications via hyprland-lock-notify-v1
    auto wlDisplay = wl_display_connect(nullptr);
    if (!wlDisplay) {
        std::print(stderr, "Failed to connect to Wayland display\n");
        return eTestResult::BAD_ENVIRONMENT;
    }

    auto state = makeShared<SSessionLockState>();

    auto wlRegistry = makeShared<CCWlRegistry>((wl_proxy*)wl_display_get_registry(wlDisplay));
    wlRegistry->setGlobal([state](CCWlRegistry* r, uint32_t name, const char* interface, uint32_t version) {
        const std::string IFACE = interface;

        if (IFACE == hyprland_lock_notifier_v1_interface.name)
            state->m_lockNotifier =
                makeShared<CCHyprlandLockNotifierV1>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &hyprland_lock_notifier_v1_interface, version));
    });

    wl_display_roundtrip(wlDisplay);

    if (!state->m_lockNotifier) {
        std::print(stderr, "Failed to bind to lock notifier\n");
        return eTestResult::BAD_ENVIRONMENT;
    }

    state->m_lockNotification = makeShared<CCHyprlandLockNotificationV1>(state->m_lockNotifier->sendGetLockNotification());
    state->m_lockNotification->setLocked([state](auto) { state->m_didLock = true; });
    state->m_lockNotification->setUnlocked([state](auto) { state->m_didUnlock = true; });

    wl_display_flush(wlDisplay);

    // Start the client
    CProcess   client(test.m_clientPath, {"--config", test.m_configPath, "--verbose"});

    const auto STARTTP = std::chrono::system_clock::now();
    if (!client.runAsync()) {
        std::print(stderr, "Failed to start client process\n");
        return eTestResult::BAD_ENVIRONMENT;
    }

    while (!state->m_didLock) {
        if (wl_display_prepare_read(wlDisplay) == 0) {
            wl_display_read_events(wlDisplay);
            wl_display_dispatch_pending(wlDisplay);
        } else {
            wl_display_dispatch(wlDisplay);
        }

        if (std::chrono::system_clock::now() - STARTTP > std::chrono::milliseconds(test.m_timeoutMs)) {
            std::print(stderr, "Timeout waiting for the lock event\n");
            return eTestResult::LOCK_TIMEOUT;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Let it fade in
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    if (state->m_didUnlock) {
        std::print(stderr, "Client unlocked before it was expected to\n");
        return eTestResult::PREMATURE_UNLOCK;
    }

    if (test.m_unlockWithUSR1) {
        CProcess unlockClient("/bin/sh", {"-c", "kill -USR1 " + std::to_string(client.pid())});
        if (!unlockClient.runSync()) {
            std::print(stderr, "Failed to unlock client process\n");
            return eTestResult::BAD_ENVIRONMENT;
        }
    } else {
        // This is used by the qemu driver to send the unlock password
        std::ofstream lockedNotifyFile("/tmp/.session-locked");
        if (lockedNotifyFile.is_open())
            lockedNotifyFile << "locked!";
        lockedNotifyFile.close();
    }

    while (!state->m_didUnlock) {
        if (wl_display_prepare_read(wlDisplay) == 0) {
            wl_display_read_events(wlDisplay);
            wl_display_dispatch_pending(wlDisplay);
        } else {
            wl_display_dispatch(wlDisplay);
        }

        if (std::chrono::system_clock::now() - STARTTP > std::chrono::milliseconds(test.m_timeoutMs)) {
            std::print(stderr, "Timeout waiting for the unlock event\n");
            return eTestResult::UNLOCK_TIMEOUT;
        }
    }

    int status = -1;
    waitpid(client.pid(), &status, 0);

    if (status != 0)
        return eTestResult::BAD_EXITCODE;

    return eTestResult::OK;
}
