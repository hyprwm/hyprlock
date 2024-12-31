#include "hyprlock.hpp"
#include "AnimationManager.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"
#include "../renderer/Renderer.hpp"
#include "../auth/Auth.hpp"
#include "../auth/Fingerprint.hpp"
#include "Egl.hpp"
#include <sys/wait.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <xf86drm.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sdbus-c++/sdbus-c++.h>
#include <hyprutils/os/Process.hpp>

using namespace Hyprutils::OS;

CHyprlock::CHyprlock(const std::string& wlDisplay, const bool immediate, const bool immediateRender, const bool noFadeIn) {
    m_sWaylandState.display = wl_display_connect(wlDisplay.empty() ? nullptr : wlDisplay.c_str());
    if (!m_sWaylandState.display) {
        Debug::log(CRIT, "Couldn't connect to a wayland compositor");
        exit(1);
    }

    g_pEGL = std::make_unique<CEGL>(m_sWaylandState.display);

    if (!immediate) {
        const auto PGRACE = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:grace");
        m_tGraceEnds      = **PGRACE ? std::chrono::system_clock::now() + std::chrono::seconds(**PGRACE) : std::chrono::system_clock::from_time_t(0);
    } else
        m_tGraceEnds = std::chrono::system_clock::from_time_t(0);

    const auto PIMMEDIATERENDER = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:immediate_render");
    m_bImmediateRender          = immediateRender || **PIMMEDIATERENDER;

    const auto* const PNOFADEIN = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:no_fade_in");
    m_bNoFadeIn                 = noFadeIn || **PNOFADEIN;

    const auto CURRENTDESKTOP = getenv("XDG_CURRENT_DESKTOP");
    const auto SZCURRENTD     = std::string{CURRENTDESKTOP ? CURRENTDESKTOP : ""};
    m_sCurrentDesktop         = SZCURRENTD;
}

CHyprlock::~CHyprlock() {
    if (dma.gbmDevice)
        gbm_device_destroy(dma.gbmDevice);
}

static void registerSignalAction(int sig, void (*handler)(int), int sa_flags = 0) {
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = sa_flags;
    sigaction(sig, &sa, nullptr);
}

static void handleUnlockSignal(int sig) {
    if (sig == SIGUSR1) {
        Debug::log(LOG, "Unlocking with a SIGUSR1");
        g_pHyprlock->releaseSessionLock();
    }
}

static void handleForceUpdateSignal(int sig) {
    if (sig == SIGUSR2) {
        for (auto& t : g_pHyprlock->getTimers()) {
            if (t->canForceUpdate()) {
                t->call(t);
                t->cancel();
            }
        }
    }
}

static void handlePollTerminate(int sig) {
    ;
}

static void handleCriticalSignal(int sig) {
    g_pHyprlock->attemptRestoreOnDeath();

    // remove our handlers
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGSEGV, &sa, nullptr);

    abort();
}

static char* gbm_find_render_node(drmDevice* device) {
    drmDevice* devices[64];
    char*      render_node = nullptr;

    int        n = drmGetDevices2(0, devices, sizeof(devices) / sizeof(devices[0]));
    for (int i = 0; i < n; ++i) {
        drmDevice* dev = devices[i];
        if (device && !drmDevicesEqual(device, dev)) {
            continue;
        }
        if (!(dev->available_nodes & (1 << DRM_NODE_RENDER)))
            continue;

        render_node = strdup(dev->nodes[DRM_NODE_RENDER]);
        break;
    }

    drmFreeDevices(devices, n);
    return render_node;
}

gbm_device* CHyprlock::createGBMDevice(drmDevice* dev) {
    char* renderNode = gbm_find_render_node(dev);

    if (!renderNode) {
        Debug::log(ERR, "[core] Couldn't find a render node");
        return nullptr;
    }

    Debug::log(TRACE, "[core] createGBMDevice: render node {}", renderNode);

    int fd = open(renderNode, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        Debug::log(ERR, "[core] couldn't open render node");
        free(renderNode);
        return nullptr;
    }

    free(renderNode);
    return gbm_create_device(fd);
}

void CHyprlock::addDmabufListener() {
    dma.linuxDmabufFeedback->setTrancheDone([this](CCZwpLinuxDmabufFeedbackV1* r) {
        Debug::log(TRACE, "[core] dmabufFeedbackTrancheDone");

        dma.deviceUsed = false;
    });

    dma.linuxDmabufFeedback->setTrancheFormats([this](CCZwpLinuxDmabufFeedbackV1* r, wl_array* indices) {
        Debug::log(TRACE, "[core] dmabufFeedbackTrancheFormats");

        if (!dma.deviceUsed || !dma.formatTable)
            return;

        struct fm_entry {
            uint32_t format;
            uint32_t padding;
            uint64_t modifier;
        };
        // An entry in the table has to be 16 bytes long
        assert(sizeof(fm_entry) == 16);

        uint32_t  n_modifiers = dma.formatTableSize / sizeof(fm_entry);
        fm_entry* fm_entry    = (struct fm_entry*)dma.formatTable;
        uint16_t* idx;

        for (idx = (uint16_t*)indices->data; (const char*)idx < (const char*)indices->data + indices->size; idx++) {
            if (*idx >= n_modifiers)
                continue;

            Debug::log(TRACE, "GPU Reports supported format {:x} with modifier {:x}", (fm_entry + *idx)->format, (fm_entry + *idx)->modifier);

            dma.dmabufMods.push_back({(fm_entry + *idx)->format, (fm_entry + *idx)->modifier});
        }
    });

    dma.linuxDmabufFeedback->setTrancheTargetDevice([this](CCZwpLinuxDmabufFeedbackV1* r, wl_array* device_arr) {
        Debug::log(TRACE, "[core] dmabufFeedbackTrancheTargetDevice");

        dev_t device;
        assert(device_arr->size == sizeof(device));
        memcpy(&device, device_arr->data, sizeof(device));

        drmDevice* drmDev;
        if (drmGetDeviceFromDevId(device, /* flags */ 0, &drmDev) != 0)
            return;

        if (dma.gbmDevice) {
            drmDevice* drmDevRenderer = nullptr;
            drmGetDevice2(gbm_device_get_fd(dma.gbmDevice), /* flags */ 0, &drmDevRenderer);
            dma.deviceUsed = drmDevicesEqual(drmDevRenderer, drmDev);
        } else {
            dma.gbmDevice  = createGBMDevice(drmDev);
            dma.deviceUsed = dma.gbm;
        }
    });

    dma.linuxDmabufFeedback->setDone([this](CCZwpLinuxDmabufFeedbackV1* r) {
        Debug::log(TRACE, "[core] dmabufFeedbackDone");

        if (dma.formatTable)
            munmap(dma.formatTable, dma.formatTableSize);

        dma.formatTable     = nullptr;
        dma.formatTableSize = 0;
    });

    dma.linuxDmabufFeedback->setFormatTable([this](CCZwpLinuxDmabufFeedbackV1* r, int fd, uint32_t size) {
        Debug::log(TRACE, "[core] dmabufFeedbackFormatTable");

        dma.dmabufMods.clear();

        dma.formatTable = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);

        if (dma.formatTable == MAP_FAILED) {
            Debug::log(ERR, "[core] format table failed to mmap");
            dma.formatTable     = nullptr;
            dma.formatTableSize = 0;
            return;
        }

        dma.formatTableSize = size;
    });

    dma.linuxDmabufFeedback->setMainDevice([this](CCZwpLinuxDmabufFeedbackV1* r, wl_array* device_arr) {
        Debug::log(LOG, "[core] dmabufFeedbackMainDevice");

        RASSERT(!dma.gbm, "double dmabuf feedback");

        dev_t device;
        assert(device_arr->size == sizeof(device));
        memcpy(&device, device_arr->data, sizeof(device));

        drmDevice* drmDev;
        if (drmGetDeviceFromDevId(device, /* flags */ 0, &drmDev) != 0) {
            Debug::log(WARN, "[dmabuf] unable to open main device?");
            exit(1);
        }

        dma.gbmDevice = createGBMDevice(drmDev);
        drmFreeDevice(&drmDev);
    });

    dma.linuxDmabuf->setModifier([this](CCZwpLinuxDmabufV1* r, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo) {
        dma.dmabufMods.push_back({format, (((uint64_t)modifier_hi) << 32) | modifier_lo});
    });
}

void CHyprlock::run() {
    m_sWaylandState.registry = makeShared<CCWlRegistry>((wl_proxy*)wl_display_get_registry(m_sWaylandState.display));
    m_sWaylandState.registry->setGlobal([this](CCWlRegistry* r, uint32_t name, const char* interface, uint32_t version) {
        const std::string IFACE = interface;
        Debug::log(LOG, "  | got iface: {} v{}", IFACE, version);

        if (IFACE == zwp_linux_dmabuf_v1_interface.name) {
            if (version < 4) {
                Debug::log(ERR, "cannot use linux_dmabuf with ver < 4");
                return;
            }

            dma.linuxDmabuf         = makeShared<CCZwpLinuxDmabufV1>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &zwp_linux_dmabuf_v1_interface, 4));
            dma.linuxDmabufFeedback = makeShared<CCZwpLinuxDmabufFeedbackV1>(dma.linuxDmabuf->sendGetDefaultFeedback());

            addDmabufListener();
        } else if (IFACE == wl_seat_interface.name) {
            if (g_pSeatManager->registered()) {
                Debug::log(WARN, "Hyprlock does not support multi-seat configurations. Only binding to the first seat.");
                return;
            }

            g_pSeatManager->registerSeat(makeShared<CCWlSeat>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wl_seat_interface, 9)));
        } else if (IFACE == ext_session_lock_manager_v1_interface.name)
            m_sWaylandState.sessionLock =
                makeShared<CCExtSessionLockManagerV1>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &ext_session_lock_manager_v1_interface, 1));
        else if (IFACE == wl_output_interface.name)
            m_vOutputs.emplace_back(
                std::make_unique<COutput>(makeShared<CCWlOutput>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wl_output_interface, 4)), name));
        else if (IFACE == wp_cursor_shape_manager_v1_interface.name)
            g_pSeatManager->registerCursorShape(
                makeShared<CCWpCursorShapeManagerV1>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wp_cursor_shape_manager_v1_interface, 1)));
        else if (IFACE == wl_compositor_interface.name)
            m_sWaylandState.compositor = makeShared<CCWlCompositor>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wl_compositor_interface, 4));
        else if (IFACE == wp_fractional_scale_manager_v1_interface.name)
            m_sWaylandState.fractional =
                makeShared<CCWpFractionalScaleManagerV1>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wp_fractional_scale_manager_v1_interface, 1));
        else if (IFACE == wp_viewporter_interface.name)
            m_sWaylandState.viewporter = makeShared<CCWpViewporter>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wp_viewporter_interface, 1));
        else if (IFACE == zwlr_screencopy_manager_v1_interface.name)
            m_sWaylandState.screencopy =
                makeShared<CCZwlrScreencopyManagerV1>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &zwlr_screencopy_manager_v1_interface, 3));
        else
            return;

        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    });
    m_sWaylandState.registry->setGlobalRemove([this](CCWlRegistry* r, uint32_t name) {
        Debug::log(LOG, "  | removed iface {}", name);
        auto outputIt = std::find_if(m_vOutputs.begin(), m_vOutputs.end(), [name](const auto& other) { return other->name == name; });
        if (outputIt != m_vOutputs.end()) {
            g_pRenderer->removeWidgetsFor(outputIt->get()->sessionLockSurface.get());
            m_vOutputs.erase(outputIt);
        }
    });

    wl_display_roundtrip(m_sWaylandState.display);

    if (!m_sWaylandState.sessionLock) {
        Debug::log(CRIT, "Couldn't bind to ext-session-lock-v1, does your compositor support it?");
        exit(1);
    }

    // gather info about monitors
    wl_display_roundtrip(m_sWaylandState.display);

    g_pRenderer = std::make_unique<CRenderer>();
    g_pAuth     = std::make_unique<CAuth>();
    g_pAnimationManager = std::make_unique<CHyprlockAnimationManager>();
    g_pRenderer         = std::make_unique<CRenderer>();
    g_pAuth             = std::make_unique<CAuth>();
    g_pAuth->start();

    static auto* const PNOFADEOUT = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:no_fade_out");
    const bool         NOFADEOUT  = **PNOFADEOUT;

    Debug::log(LOG, "Running on {}", m_sCurrentDesktop);

    // Hyprland violates the protocol a bit to allow for this.
    if (m_sCurrentDesktop != "Hyprland") {
        while (!g_pRenderer->asyncResourceGatherer->gathered) {
            wl_display_flush(m_sWaylandState.display);
            if (wl_display_prepare_read(m_sWaylandState.display) == 0) {
                wl_display_read_events(m_sWaylandState.display);
                wl_display_dispatch_pending(m_sWaylandState.display);
            } else {
                wl_display_dispatch(m_sWaylandState.display);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    acquireSessionLock();

    // Recieved finished
    if (m_bTerminate) {
        m_sLoopState.timerEvent = true;
        m_sLoopState.timerCV.notify_all();
        g_pRenderer->asyncResourceGatherer->notify();
        g_pRenderer->asyncResourceGatherer->await();
        g_pAuth->terminate();
        exit(1);
    }

    const auto fingerprintAuth = g_pAuth->getImpl(AUTH_IMPL_FINGERPRINT);
    const auto dbusConn        = (fingerprintAuth) ? ((CFingerprint*)fingerprintAuth.get())->getConnection() : nullptr;

    registerSignalAction(SIGUSR1, handleUnlockSignal, SA_RESTART);
    registerSignalAction(SIGUSR2, handleForceUpdateSignal);
    registerSignalAction(SIGRTMIN, handlePollTerminate);
    registerSignalAction(SIGSEGV, handleCriticalSignal);
    registerSignalAction(SIGABRT, handleCriticalSignal);

    createSessionLockSurfaces();

    pollfd pollfds[2];
    pollfds[0] = {
        .fd     = wl_display_get_fd(m_sWaylandState.display),
        .events = POLLIN,
    };
    if (dbusConn) {
        pollfds[1] = {
            .fd     = dbusConn->getEventLoopPollData().fd,
            .events = POLLIN,
        };
    }
    size_t      fdcount = dbusConn ? 2 : 1;

    std::thread pollThr([this, &pollfds, fdcount]() {
        while (!m_bTerminate) {
            int ret = poll(pollfds, fdcount, 5000 /* 5 seconds, reasonable. Just in case we need to terminate and the signal fails */);

            if (ret < 0) {
                if (errno == EINTR)
                    continue;

                Debug::log(CRIT, "[core] Polling fds failed with {}", errno);
                attemptRestoreOnDeath();
                m_bTerminate = true;
                exit(1);
            }

            for (size_t i = 0; i < fdcount; ++i) {
                if (pollfds[i].revents & POLLHUP) {
                    Debug::log(CRIT, "[core] Disconnected from pollfd id {}", i);
                    attemptRestoreOnDeath();
                    m_bTerminate = true;
                    exit(1);
                }
            }

            if (ret != 0) {
                Debug::log(TRACE, "[core] got poll event");
                std::lock_guard<std::mutex> lg2(m_sLoopState.eventLoopMutex);
                m_sLoopState.event = true;
                m_sLoopState.loopCV.notify_all();
            }
        }
    });

    std::thread timersThr([this]() {
        while (!m_bTerminate) {
            // calc nearest thing
            m_sLoopState.timersMutex.lock();

            float least = 10000;
            for (auto& t : m_vTimers) {
                const auto TIME = std::clamp(t->leftMs(), 1.f, INFINITY);
                if (TIME < least)
                    least = TIME;
            }

            m_sLoopState.timersMutex.unlock();

            std::unique_lock lk(m_sLoopState.timerRequestMutex);
            m_sLoopState.timerCV.wait_for(lk, std::chrono::milliseconds((int)least + 1), [this] { return m_sLoopState.timerEvent; });
            m_sLoopState.timerEvent = false;

            // notify main
            std::lock_guard<std::mutex> lg2(m_sLoopState.eventLoopMutex);
            Debug::log(TRACE, "timer thread firing");
            m_sLoopState.event = true;
            m_sLoopState.loopCV.notify_all();
        }
    });

    m_sLoopState.event = true; // let it process once

    while (!m_bTerminate) {
        std::unique_lock lk(m_sLoopState.eventRequestMutex);
        if (m_sLoopState.event == false)
            m_sLoopState.loopCV.wait_for(lk, std::chrono::milliseconds(5000), [this] { return m_sLoopState.event; });

        if (!NOFADEOUT && m_bFadeStarted && std::chrono::system_clock::now() > m_tFadeEnds) {
            releaseSessionLock();
            break;
        }

        if (m_bTerminate)
            break;

        std::lock_guard<std::mutex> lg(m_sLoopState.eventLoopMutex);

        m_sLoopState.event = false;

        if (pollfds[1].revents & POLLIN /* dbus */) {
            while (dbusConn && dbusConn->processPendingEvent()) {
                ;
            }
        }

        if (pollfds[0].revents & POLLIN /* wl */) {
            Debug::log(TRACE, "got wl event");
            wl_display_flush(m_sWaylandState.display);
            if (wl_display_prepare_read(m_sWaylandState.display) == 0) {
                wl_display_read_events(m_sWaylandState.display);
                wl_display_dispatch_pending(m_sWaylandState.display);
            } else {
                wl_display_dispatch(m_sWaylandState.display);
            }
        }

        // finalize wayland dispatching. Dispatch pending on the queue
        int ret = 0;
        do {
            ret = wl_display_dispatch_pending(m_sWaylandState.display);
            wl_display_flush(m_sWaylandState.display);
        } while (ret > 0 && !m_bTerminate);

        // do timers
        m_sLoopState.timersMutex.lock();
        auto timerscpy = m_vTimers;
        m_sLoopState.timersMutex.unlock();

        std::vector<std::shared_ptr<CTimer>> passed;

        for (auto& t : timerscpy) {
            if (t->passed() && !t->cancelled()) {
                t->call(t);
                passed.push_back(t);
            }

            if (t->cancelled())
                passed.push_back(t);
        }

        m_sLoopState.timersMutex.lock();
        std::erase_if(m_vTimers, [passed](const auto& timer) { return std::find(passed.begin(), passed.end(), timer) != passed.end(); });
        m_sLoopState.timersMutex.unlock();

        passed.clear();

        if (!NOFADEOUT && m_bFadeStarted && std::chrono::system_clock::now() > m_tFadeEnds) {
            releaseSessionLock();
            break;
        }
    }

    const auto DPY = m_sWaylandState.display;

    m_sLoopState.timerEvent = true;
    m_sLoopState.timerCV.notify_all();
    g_pRenderer->asyncResourceGatherer->notify();
    g_pRenderer->asyncResourceGatherer->await();
    m_sWaylandState = {};
    dma             = {};

    m_vOutputs.clear();
    g_pEGL.reset();
    g_pRenderer.reset();
    g_pSeatManager.reset();

    wl_display_disconnect(DPY);

    pthread_kill(pollThr.native_handle(), SIGRTMIN);

    g_pAuth->terminate();

    // wait for threads to exit cleanly to avoid a coredump
    pollThr.join();
    timersThr.join();

    Debug::log(LOG, "Reached the end, exiting");
}

void CHyprlock::unlock() {
    static auto* const PNOFADEOUT = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:no_fade_out");

    if (**PNOFADEOUT || m_sCurrentDesktop != "Hyprland") {
        releaseSessionLock();
        return;
    }

    m_tFadeEnds    = std::chrono::system_clock::now() + std::chrono::milliseconds(500);
    m_bFadeStarted = true;

    renderAllOutputs();
}

bool CHyprlock::isUnlocked() {
    return m_bFadeStarted || m_bTerminate;
}

void CHyprlock::clearPasswordBuffer() {
    if (m_sPasswordState.passBuffer.empty())
        return;

    m_sPasswordState.passBuffer = "";

    renderAllOutputs();
}

void CHyprlock::renderOutput(const std::string& stringPort) {
    const auto MON = std::find_if(m_vOutputs.begin(), m_vOutputs.end(), [stringPort](const auto& other) { return other->stringPort == stringPort; });

    if (MON == m_vOutputs.end() || !MON->get())
        return;

    const auto PMONITOR = MON->get();

    if (!PMONITOR->sessionLockSurface)
        return;

    PMONITOR->sessionLockSurface->render();
}

void CHyprlock::renderAllOutputs() {
    for (auto& o : m_vOutputs) {
        if (!o->sessionLockSurface)
            continue;

        o->sessionLockSurface->render();
    }
}

void CHyprlock::startKeyRepeat(xkb_keysym_t sym) {
    if (m_pKeyRepeatTimer) {
        m_pKeyRepeatTimer->cancel();
        m_pKeyRepeatTimer.reset();
    }

    if (g_pSeatManager->m_pXKBComposeState)
        xkb_compose_state_reset(g_pSeatManager->m_pXKBComposeState);

    if (m_iKeebRepeatDelay <= 0)
        return;

    m_pKeyRepeatTimer = addTimer(std::chrono::milliseconds(m_iKeebRepeatDelay), [sym](std::shared_ptr<CTimer> self, void* data) { g_pHyprlock->repeatKey(sym); }, nullptr);
}

void CHyprlock::repeatKey(xkb_keysym_t sym) {
    if (m_iKeebRepeatRate <= 0)
        return;

    handleKeySym(sym, false);

    // This condition is for backspace and delete keys, but should also be ok for other keysyms since our buffer won't be empty anyways
    if (bool CONTINUE = m_sPasswordState.passBuffer.length() > 0; CONTINUE)
        m_pKeyRepeatTimer = addTimer(std::chrono::milliseconds(m_iKeebRepeatRate), [sym](std::shared_ptr<CTimer> self, void* data) { g_pHyprlock->repeatKey(sym); }, nullptr);

    renderAllOutputs();
}

void CHyprlock::onKey(uint32_t key, bool down) {
    if (m_bFadeStarted || m_bTerminate)
        return;

    if (down && std::chrono::system_clock::now() < m_tGraceEnds) {
        unlock();
        return;
    }

    if (down && std::find(m_vPressedKeys.begin(), m_vPressedKeys.end(), key) != m_vPressedKeys.end()) {
        Debug::log(ERR, "Invalid key down event (key already pressed?)");
        return;
    } else if (!down && std::find(m_vPressedKeys.begin(), m_vPressedKeys.end(), key) == m_vPressedKeys.end()) {
        Debug::log(ERR, "Invalid key down event (stray release event?)");
        return;
    }

    if (down)
        m_vPressedKeys.push_back(key);
    else {
        std::erase(m_vPressedKeys, key);
        if (m_pKeyRepeatTimer) {
            m_pKeyRepeatTimer->cancel();
            m_pKeyRepeatTimer.reset();
        }
    }

    if (g_pAuth->checkWaiting()) {
        renderAllOutputs();
        return;
    }

    if (down) {
        m_bCapsLock = xkb_state_mod_name_is_active(g_pSeatManager->m_pXKBState, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED);
        m_bNumLock  = xkb_state_mod_name_is_active(g_pSeatManager->m_pXKBState, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED);
        m_bCtrl     = xkb_state_mod_name_is_active(g_pSeatManager->m_pXKBState, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE);

        const auto              SYM = xkb_state_key_get_one_sym(g_pSeatManager->m_pXKBState, key + 8);

        enum xkb_compose_status composeStatus = XKB_COMPOSE_NOTHING;
        if (g_pSeatManager->m_pXKBComposeState) {
            xkb_compose_state_feed(g_pSeatManager->m_pXKBComposeState, SYM);
            composeStatus = xkb_compose_state_get_status(g_pSeatManager->m_pXKBComposeState);
        }

        handleKeySym(SYM, composeStatus == XKB_COMPOSE_COMPOSED);

        if (SYM == XKB_KEY_BackSpace || SYM == XKB_KEY_Delete) // keys allowed to repeat
            startKeyRepeat(SYM);

    } else if (g_pSeatManager->m_pXKBComposeState && xkb_compose_state_get_status(g_pSeatManager->m_pXKBComposeState) == XKB_COMPOSE_COMPOSED)
        xkb_compose_state_reset(g_pSeatManager->m_pXKBComposeState);

    renderAllOutputs();
}

void CHyprlock::handleKeySym(xkb_keysym_t sym, bool composed) {
    const auto SYM = sym;

    if (SYM == XKB_KEY_Escape || (m_bCtrl && (SYM == XKB_KEY_u || SYM == XKB_KEY_BackSpace))) {
        Debug::log(LOG, "Clearing password buffer");

        m_sPasswordState.passBuffer = "";
    } else if (SYM == XKB_KEY_Return || SYM == XKB_KEY_KP_Enter) {
        Debug::log(LOG, "Authenticating");

        static auto* const PIGNOREEMPTY = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:ignore_empty_input");

        if (m_sPasswordState.passBuffer.empty() && **PIGNOREEMPTY) {
            Debug::log(LOG, "Ignoring empty input");
            return;
        }

        g_pAuth->submitInput(m_sPasswordState.passBuffer);
    } else if (SYM == XKB_KEY_BackSpace || SYM == XKB_KEY_Delete) {
        if (m_sPasswordState.passBuffer.length() > 0) {
            // handle utf-8
            while ((m_sPasswordState.passBuffer.back() & 0xc0) == 0x80)
                m_sPasswordState.passBuffer.pop_back();
            m_sPasswordState.passBuffer = m_sPasswordState.passBuffer.substr(0, m_sPasswordState.passBuffer.length() - 1);
        }
    } else if (SYM == XKB_KEY_Caps_Lock) {
        m_bCapsLock = !m_bCapsLock;
    } else if (SYM == XKB_KEY_Num_Lock) {
        m_bNumLock = !m_bNumLock;
    } else {
        char buf[16] = {0};
        int  len     = (composed) ? xkb_compose_state_get_utf8(g_pSeatManager->m_pXKBComposeState, buf, sizeof(buf)) /* nullbyte */ + 1 :
                                    xkb_keysym_to_utf8(SYM, buf, sizeof(buf)) /* already includes a nullbyte */;

        if (len > 1)
            m_sPasswordState.passBuffer += std::string{buf, len - 1};
    }
}

void CHyprlock::acquireSessionLock() {
    Debug::log(LOG, "Locking session");
    m_sLockState.lock = makeShared<CCExtSessionLockV1>(m_sWaylandState.sessionLock->sendLock());

    m_sLockState.lock->setLocked([this](CCExtSessionLockV1* r) { onLockLocked(); });

    m_sLockState.lock->setFinished([this](CCExtSessionLockV1* r) { onLockFinished(); });

    // roundtrip in case the compositor sends `finished` right away
    wl_display_roundtrip(m_sWaylandState.display);
}

void CHyprlock::releaseSessionLock() {
    Debug::log(LOG, "Unlocking session");
    if (m_bTerminate) {
        Debug::log(ERR, "Unlock already happend?");
        return;
    }

    if (!m_sLockState.lock) {
        Debug::log(ERR, "Unlock without a lock object!");
        return;
    }

    if (!m_bLocked) {
        // Would be a protocol error to allow this
        Debug::log(ERR, "Trying to unlock the session, but never recieved the locked event!");
        return;
    }

    m_sLockState.lock->sendUnlockAndDestroy();
    m_sLockState.lock = nullptr;

    Debug::log(LOG, "Unlocked, exiting!");

    m_bTerminate = true;
    m_bLocked    = false;

    wl_display_roundtrip(m_sWaylandState.display);
}

void CHyprlock::createSessionLockSurfaces() {
    for (auto& o : m_vOutputs) {
        o->sessionLockSurface = std::make_unique<CSessionLockSurface>(o.get());
    }
}

void CHyprlock::onLockLocked() {
    Debug::log(LOG, "onLockLocked called");

    m_bLocked = true;
}

void CHyprlock::onLockFinished() {
    Debug::log(LOG, "onLockFinished called. Seems we got yeeten. Is another lockscreen running?");

    if (!m_sLockState.lock) {
        Debug::log(ERR, "onLockFinished without a lock object!");
        return;
    }

    if (m_bLocked)
        // The `finished` event specifies that whenever the `locked` event has been recieved and the compositor sends `finished`,
        // `unlock_and_destroy` should be called by the client.
        // This does not mean the session gets unlocked! That is ultimately the responsiblity of the compositor.
        m_sLockState.lock->sendUnlockAndDestroy();
    else
        m_sLockState.lock.reset();

    m_sLockState.lock = nullptr;
    m_bTerminate      = true;
}

SP<CCExtSessionLockManagerV1> CHyprlock::getSessionLockMgr() {
    return m_sWaylandState.sessionLock;
}

SP<CCExtSessionLockV1> CHyprlock::getSessionLock() {
    return m_sLockState.lock;
}

SP<CCWlCompositor> CHyprlock::getCompositor() {
    return m_sWaylandState.compositor;
}

wl_display* CHyprlock::getDisplay() {
    return m_sWaylandState.display;
}

SP<CCWpFractionalScaleManagerV1> CHyprlock::getFractionalMgr() {
    return m_sWaylandState.fractional;
}

SP<CCWpViewporter> CHyprlock::getViewporter() {
    return m_sWaylandState.viewporter;
}

size_t CHyprlock::getPasswordBufferLen() {
    return m_sPasswordState.passBuffer.length();
}

size_t CHyprlock::getPasswordBufferDisplayLen() {
    // Counts utf-8 codepoints in the buffer. A byte is counted if it does not match 0b10xxxxxx.
    return std::count_if(m_sPasswordState.passBuffer.begin(), m_sPasswordState.passBuffer.end(), [](char c) { return (c & 0xc0) != 0x80; });
}

std::shared_ptr<CTimer> CHyprlock::addTimer(const std::chrono::system_clock::duration& timeout, std::function<void(std::shared_ptr<CTimer> self, void* data)> cb_, void* data,
                                            bool force) {
    std::lock_guard<std::mutex> lg(m_sLoopState.timersMutex);
    const auto                  T = m_vTimers.emplace_back(std::make_shared<CTimer>(timeout, cb_, data, force));
    m_sLoopState.timerEvent       = true;
    m_sLoopState.timerCV.notify_all();
    return T;
}

std::vector<std::shared_ptr<CTimer>> CHyprlock::getTimers() {
    return m_vTimers;
}

void CHyprlock::enqueueForceUpdateTimers() {
    addTimer(
        std::chrono::milliseconds(1),
        [](std::shared_ptr<CTimer> self, void* data) {
            for (auto& t : g_pHyprlock->getTimers()) {
                if (t->canForceUpdate()) {
                    t->call(t);
                    t->cancel();
                }
            }
        },
        nullptr, false);
}

std::string CHyprlock::spawnSync(const std::string& cmd) {
    CProcess proc("/bin/sh", {"-c", cmd});
    if (!proc.runSync()) {
        Debug::log(ERR, "Failed to run \"{}\"", cmd);
        return "";
    }

    if (!proc.stdErr().empty())
        Debug::log(ERR, "Shell command \"{}\" STDERR:\n{}", cmd, proc.stdErr());

    return proc.stdOut();
}

SP<CCZwlrScreencopyManagerV1> CHyprlock::getScreencopy() {
    return m_sWaylandState.screencopy;
}

void CHyprlock::attemptRestoreOnDeath() {
    if (m_bTerminate || m_sCurrentDesktop != "Hyprland")
        return;

    const auto XDG_RUNTIME_DIR = getenv("XDG_RUNTIME_DIR");
    const auto HIS             = getenv("HYPRLAND_INSTANCE_SIGNATURE");

    if (!XDG_RUNTIME_DIR || !HIS)
        return;

    // dirty hack
    uint64_t   timeNowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - std::chrono::system_clock::from_time_t(0)).count();

    const auto LASTRESTARTPATH = std::string{XDG_RUNTIME_DIR} + "/.hyprlockrestart";

    if (std::filesystem::exists(LASTRESTARTPATH)) {
        std::ifstream ifs(LASTRESTARTPATH);
        std::string   content((std::istreambuf_iterator<char>(ifs)), (std::istreambuf_iterator<char>()));
        uint64_t      timeEncoded = 0;
        try {
            timeEncoded = std::stoull(content);
        } catch (std::exception& e) {
            // oops?
            ifs.close();
            std::filesystem::remove(LASTRESTARTPATH);
            return;
        }
        ifs.close();

        if (timeNowMs - timeEncoded < 4000 /* 4s, seems reasonable */) {
            Debug::log(LOG, "Not restoring on death; less than 4s since last death");
            return;
        }
    }

    std::ofstream ofs(LASTRESTARTPATH, std::ios::trunc);
    ofs << timeNowMs;
    ofs.close();

    if (m_bLocked && m_sLockState.lock) {
        m_sLockState.lock.reset();

        // Destroy sessionLockSurfaces
        m_vOutputs.clear();
    }

    spawnSync("hyprctl keyword misc:allow_session_lock_restore true");
    spawnSync("hyprctl dispatch exec \"hyprlock --immediate --immediate-render\"");
}
