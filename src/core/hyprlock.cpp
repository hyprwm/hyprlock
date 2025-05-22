#include "hyprlock.hpp"
#include "AnimationManager.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"
#include "../renderer/Renderer.hpp"
#include "../auth/Auth.hpp"
#include "../auth/Fingerprint.hpp"
#include "Egl.hpp"
#include <chrono>
#include <hyprutils/memory/UniquePtr.hpp>
#include <sys/wait.h>
#include <sys/poll.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <csignal>
#include <unistd.h>
#include <cassert>
#include <cstring>
#include <xf86drm.h>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <sdbus-c++/sdbus-c++.h>
#include <hyprutils/os/Process.hpp>
#include <malloc.h>

using namespace Hyprutils::OS;

static void setMallocThreshold() {
#ifdef M_TRIM_THRESHOLD
    // The default is 128 pages,
    // which is very large and can lead to a lot of memory used for no reason
    // because trimming hasn't happened
    static const int PAGESIZE = sysconf(_SC_PAGESIZE);
    mallopt(M_TRIM_THRESHOLD, 6 * PAGESIZE);
#endif
}

CHyprlock::CHyprlock(const std::string& wlDisplay, const bool immediate, const bool immediateRender) {
    setMallocThreshold();

    m_sWaylandState.display = wl_display_connect(wlDisplay.empty() ? nullptr : wlDisplay.c_str());
    RASSERT(m_sWaylandState.display, "Couldn't connect to a wayland compositor");

    g_pEGL = makeUnique<CEGL>(m_sWaylandState.display);

    if (!immediate) {
        static const auto GRACE = g_pConfigManager->getValue<Hyprlang::INT>("general:grace");
        m_tGraceEnds            = *GRACE ? std::chrono::system_clock::now() + std::chrono::seconds(*GRACE) : std::chrono::system_clock::from_time_t(0);
    } else
        m_tGraceEnds = std::chrono::system_clock::from_time_t(0);

    static const auto IMMEDIATERENDER = g_pConfigManager->getValue<Hyprlang::INT>("general:immediate_render");
    m_bImmediateRender                = immediateRender || *IMMEDIATERENDER;

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
        g_pAuth->enqueueUnlock();
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
        static_assert(sizeof(fm_entry) == 16);

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
        RASSERT(drmGetDeviceFromDevId(device, /* flags */ 0, &drmDev) == 0, "unable to open main device?");

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

            g_pSeatManager->registerSeat(makeShared<CCWlSeat>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wl_seat_interface, 8)));
        } else if (IFACE == ext_session_lock_manager_v1_interface.name)
            m_sWaylandState.sessionLock =
                makeShared<CCExtSessionLockManagerV1>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &ext_session_lock_manager_v1_interface, 1));
        else if (IFACE == wl_output_interface.name) {
            const auto POUTPUT = makeShared<COutput>();
            POUTPUT->create(POUTPUT, makeShared<CCWlOutput>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wl_output_interface, 4)), name);
            m_vOutputs.emplace_back(POUTPUT);
        } else if (IFACE == wp_cursor_shape_manager_v1_interface.name)
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
        else if (IFACE == wl_shm_interface.name)
            m_sWaylandState.shm = makeShared<CCWlShm>((wl_proxy*)wl_registry_bind((wl_registry*)r->resource(), name, &wl_shm_interface, 1));
        else
            return;

        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    });
    m_sWaylandState.registry->setGlobalRemove([this](CCWlRegistry* r, uint32_t name) {
        Debug::log(LOG, "  | removed iface {}", name);
        auto outputIt = std::ranges::find_if(m_vOutputs, [id = name](const auto& other) { return other->m_ID == id; });
        if (outputIt != m_vOutputs.end()) {
            g_pRenderer->removeWidgetsFor((*outputIt)->m_ID);
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

    g_pRenderer = makeUnique<CRenderer>();
    g_pAuth     = makeUnique<CAuth>();
    g_pAuth->start();

    Debug::log(LOG, "Running on {}", m_sCurrentDesktop);

    {
        // Gather background resources and screencopy frames before locking the screen.
        // We need to do this because as soon as we lock the screen, workspaces may not be captureable anymore
        // Bypass with --immediate-render (can cause flickers and missing or bad screencopy frames)
        const auto MAXDELAYMS    = 2000; // 2 Seconds
        const auto STARTGATHERTP = std::chrono::system_clock::now();
        if (!g_pHyprlock->m_bImmediateRender) {
            while (!g_pRenderer->asyncResourceGatherer->gathered) {
                if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - STARTGATHERTP).count() > MAXDELAYMS) {
                    Debug::log(WARN, "Gathering resources timed out, backgrounds may render `background:color` at first.");
                    break;
                }
                wl_display_flush(m_sWaylandState.display);
                if (wl_display_prepare_read(m_sWaylandState.display) == 0) {
                    wl_display_read_events(m_sWaylandState.display);
                    wl_display_dispatch_pending(m_sWaylandState.display);
                } else {
                    wl_display_dispatch(m_sWaylandState.display);
                }
            }

            Debug::log(LOG, "Resources gathered after {} milliseconds",
                       std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - STARTGATHERTP).count());
        }
    }

    // Failed to lock the session
    if (!acquireSessionLock()) {
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
            bool preparedToRead = wl_display_prepare_read(m_sWaylandState.display) == 0;

            int  events = 0;
            if (preparedToRead) {
                events = poll(pollfds, fdcount, 5000);

                if (events < 0) {
                    RASSERT(errno == EINTR, "[core] Polling fds failed with {}", errno);
                    wl_display_cancel_read(m_sWaylandState.display);
                    continue;
                }

                for (size_t i = 0; i < fdcount; ++i) {
                    RASSERT(!(pollfds[i].revents & POLLHUP), "[core] Disconnected from pollfd id {}", i);
                }

                wl_display_read_events(m_sWaylandState.display);
                m_sLoopState.wlDispatched = false;
            }

            if (events > 0 || !preparedToRead) {
                Debug::log(TRACE, "[core] got poll event");
                std::unique_lock lk(m_sLoopState.eventLoopMutex);
                m_sLoopState.event = true;
                m_sLoopState.loopCV.notify_all();

                m_sLoopState.wlDispatchCV.wait_for(lk, std::chrono::milliseconds(100), [this] { return m_sLoopState.wlDispatched; });
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
                least           = std::min(TIME, least);
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
    g_pRenderer->startFadeIn();

    while (!m_bTerminate) {
        std::unique_lock lk(m_sLoopState.eventRequestMutex);
        if (!m_sLoopState.event)
            m_sLoopState.loopCV.wait_for(lk, std::chrono::milliseconds(5000), [this] { return m_sLoopState.event; });

        if (m_bTerminate)
            break;

        std::lock_guard<std::mutex> lg(m_sLoopState.eventLoopMutex);

        m_sLoopState.event = false;

        wl_display_dispatch_pending(m_sWaylandState.display);
        wl_display_flush(m_sWaylandState.display);

        m_sLoopState.wlDispatched = true;
        m_sLoopState.wlDispatchCV.notify_all();

        if (pollfds[1].revents & POLLIN /* dbus */) {
            while (dbusConn && dbusConn->processPendingEvent()) {
                ;
            }
        }

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
    if (!m_bLocked) {
        Debug::log(WARN, "Unlock called, but not locked yet. This can happen when dpms is off during the grace period.");
        return;
    }

    g_pRenderer->startFadeOut(true);

    renderAllOutputs();
}

bool CHyprlock::isUnlocked() {
    return !m_bLocked;
}

void CHyprlock::clearPasswordBuffer() {
    if (m_sPasswordState.passBuffer.empty())
        return;

    m_sPasswordState.passBuffer = "";

    renderAllOutputs();
}

void CHyprlock::renderOutput(const std::string& stringPort) {
    const auto MON = std::ranges::find_if(m_vOutputs, [stringPort](const auto& other) { return other->stringPort == stringPort; });

    if (MON == m_vOutputs.end() || !*MON)
        return;

    const auto& PMONITOR = *MON;

    if (!PMONITOR->m_sessionLockSurface)
        return;

    PMONITOR->m_sessionLockSurface->render();
}

void CHyprlock::renderAllOutputs() {
    for (auto& o : m_vOutputs) {
        if (!o->m_sessionLockSurface)
            continue;

        o->m_sessionLockSurface->render();
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
    if (isUnlocked())
        return;

    if (down && std::chrono::system_clock::now() < m_tGraceEnds) {
        unlock();
        return;
    }

    if (down && std::ranges::find(m_vPressedKeys, key) != m_vPressedKeys.end()) {
        Debug::log(ERR, "Invalid key down event (key already pressed?)");
        return;
    } else if (!down && std::ranges::find(m_vPressedKeys, key) == m_vPressedKeys.end()) {
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

    if (g_pAuth->m_bDisplayFailText)
        g_pAuth->resetDisplayFail();

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

        static const auto IGNOREEMPTY = g_pConfigManager->getValue<Hyprlang::INT>("general:ignore_empty_input");

        if (m_sPasswordState.passBuffer.empty() && *IGNOREEMPTY) {
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

void CHyprlock::onClick(uint32_t button, bool down, const Vector2D& pos) {
    if (!m_focusedOutput.lock())
        return;

    // TODO: add the UNLIKELY marco from Hyprland
    if (!m_focusedOutput->m_sessionLockSurface)
        return;

    const auto SCALEDPOS = pos * m_focusedOutput->m_sessionLockSurface->fractionalScale;
    const auto widgets   = g_pRenderer->getOrCreateWidgetsFor(*m_focusedOutput->m_sessionLockSurface);
    for (const auto& widget : widgets) {
        if (widget->containsPoint(SCALEDPOS))
            widget->onClick(button, down, pos);
    }
}

void CHyprlock::onHover(const Vector2D& pos) {
    if (!m_focusedOutput.lock())
        return;

    if (!m_focusedOutput->m_sessionLockSurface)
        return;

    bool       outputNeedsRedraw = false;
    bool       cursorChanged     = false;

    const auto SCALEDPOS = pos * m_focusedOutput->m_sessionLockSurface->fractionalScale;
    const auto widgets   = g_pRenderer->getOrCreateWidgetsFor(*m_focusedOutput->m_sessionLockSurface);
    for (const auto& widget : widgets) {
        const bool CONTAINSPOINT = widget->containsPoint(SCALEDPOS);
        const bool HOVERED       = widget->isHovered();

        if (CONTAINSPOINT) {
            if (!HOVERED) {
                widget->setHover(true);
                widget->onHover(pos);
                outputNeedsRedraw = true;
            }

            if (!cursorChanged)
                cursorChanged = true;

        } else if (HOVERED) {
            widget->setHover(false);
            outputNeedsRedraw = true;
        }
    }

    if (!cursorChanged)
        g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);

    if (outputNeedsRedraw)
        m_focusedOutput->m_sessionLockSurface->render();
}

bool CHyprlock::acquireSessionLock() {
    Debug::log(LOG, "Locking session");
    m_sLockState.lock = makeShared<CCExtSessionLockV1>(m_sWaylandState.sessionLock->sendLock());
    if (!m_sLockState.lock) {
        Debug::log(ERR, "Failed to create a lock object!");
        return false;
    }

    m_sLockState.lock->setLocked([this](CCExtSessionLockV1* r) { onLockLocked(); });

    m_sLockState.lock->setFinished([this](CCExtSessionLockV1* r) { onLockFinished(); });

    // roundtrip in case the compositor sends `finished` right away
    wl_display_roundtrip(m_sWaylandState.display);

    // recieved finished right away (probably already locked)
    if (m_bTerminate)
        return false;

    m_lockAquired = true;

    // create a session lock surface for exiting outputs
    for (auto& o : m_vOutputs) {
        if (!o->done)
            continue;

        o->createSessionLockSurface();
    }

    return true;
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

SP<CCZwlrScreencopyManagerV1> CHyprlock::getScreencopy() {
    return m_sWaylandState.screencopy;
}

SP<CCWlShm> CHyprlock::getShm() {
    return m_sWaylandState.shm;
}
