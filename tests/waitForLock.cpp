// This program exits when the wayland session gets locked, or 10 seconds have passed.
// In case it is already locked, it shall return immediatly.
// It uses hyprland-lock-notify to accomplish that.
#include "hyprland-lock-notify-v1.hpp"
#include "wayland.hpp"

#include <fcntl.h>
#include <print>
#include <hyprutils/memory/SharedPtr.hpp>
#include <thread>

using namespace Hyprutils::Memory;

#define SP CSharedPointer

struct SSessionLockState {
    SP<CCHyprlandLockNotifierV1>     m_lockNotifier     = nullptr;
    SP<CCHyprlandLockNotificationV1> m_lockNotification = nullptr;
    bool                             m_didLock          = false;
};

int main(int argc, char** argv) {
    auto wlDisplay = wl_display_connect(nullptr);
    if (!wlDisplay) {
        std::println(stderr, "Failed to connect to Wayland display");
        return -1;
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
        return -1;
    }

    state->m_lockNotification = makeShared<CCHyprlandLockNotificationV1>(state->m_lockNotifier->sendGetLockNotification());
    state->m_lockNotification->setLocked([state](auto) { state->m_didLock = true; });

    wl_display_flush(wlDisplay);

    const auto STARTTP = std::chrono::system_clock::now();
    while (!state->m_didLock) {
        if (wl_display_prepare_read(wlDisplay) == 0) {
            wl_display_read_events(wlDisplay);
            wl_display_dispatch_pending(wlDisplay);
        } else {
            wl_display_dispatch(wlDisplay);
        }

        if (std::chrono::system_clock::now() - STARTTP > std::chrono::seconds(10)) {
            std::print(stderr, "Timeout waiting for the lock event\n");
            return -1;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
