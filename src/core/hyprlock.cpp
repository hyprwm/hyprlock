#include "hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"
#include "../renderer/Renderer.hpp"
#include "Password.hpp"
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

CHyprlock::CHyprlock(const std::string& wlDisplay, const bool immediate) {
    m_sWaylandState.display = wl_display_connect(wlDisplay.empty() ? nullptr : wlDisplay.c_str());
    if (!m_sWaylandState.display) {
        Debug::log(CRIT, "Couldn't connect to a wayland compositor");
        exit(1);
    }

    g_pEGL = std::make_unique<CEGL>(m_sWaylandState.display);

    m_pXKBContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!m_pXKBContext)
        Debug::log(ERR, "Failed to create xkb context");

    if (!immediate) {
        const auto GRACE = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:grace");
        m_tGraceEnds     = **GRACE ? std::chrono::system_clock::now() + std::chrono::seconds(**GRACE) : std::chrono::system_clock::from_time_t(0);
    } else {
        m_tGraceEnds = std::chrono::system_clock::from_time_t(0);
    }
}

// wl_seat

static void                   handleCapabilities(void* data, wl_seat* wl_seat, uint32_t capabilities);
static void                   handleName(void* data, struct wl_seat* wl_seat, const char* name);

inline const wl_seat_listener seatListener = {
    .capabilities = handleCapabilities,
    .name         = handleName,
};

// end wl_seat

// dmabuf

static void handleDMABUFFormat(void* data, struct zwp_linux_dmabuf_v1* zwp_linux_dmabuf_v1, uint32_t format) {
    ;
}

static void handleDMABUFModifier(void* data, struct zwp_linux_dmabuf_v1* zwp_linux_dmabuf_v1, uint32_t format, uint32_t modifier_hi, uint32_t modifier_lo) {
    g_pHyprlock->dma.dmabufMods.push_back({format, (((uint64_t)modifier_hi) << 32) | modifier_lo});
}

inline const zwp_linux_dmabuf_v1_listener dmabufListener = {
    .format   = handleDMABUFFormat,
    .modifier = handleDMABUFModifier,
};

static void dmabufFeedbackMainDevice(void* data, zwp_linux_dmabuf_feedback_v1* feedback, wl_array* device_arr) {
    Debug::log(LOG, "[core] dmabufFeedbackMainDevice");

    RASSERT(!g_pHyprlock->dma.gbm, "double dmabuf feedback");

    dev_t device;
    assert(device_arr->size == sizeof(device));
    memcpy(&device, device_arr->data, sizeof(device));

    drmDevice* drmDev;
    if (drmGetDeviceFromDevId(device, /* flags */ 0, &drmDev) != 0) {
        Debug::log(WARN, "[dmabuf] unable to open main device?");
        exit(1);
    }

    g_pHyprlock->dma.gbmDevice = g_pHyprlock->createGBMDevice(drmDev);
}

static void dmabufFeedbackFormatTable(void* data, zwp_linux_dmabuf_feedback_v1* feedback, int fd, uint32_t size) {
    Debug::log(TRACE, "[core] dmabufFeedbackFormatTable");

    g_pHyprlock->dma.dmabufMods.clear();

    g_pHyprlock->dma.formatTable = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (g_pHyprlock->dma.formatTable == MAP_FAILED) {
        Debug::log(ERR, "[core] format table failed to mmap");
        g_pHyprlock->dma.formatTable     = nullptr;
        g_pHyprlock->dma.formatTableSize = 0;
        return;
    }

    g_pHyprlock->dma.formatTableSize = size;
}

static void dmabufFeedbackDone(void* data, zwp_linux_dmabuf_feedback_v1* feedback) {
    Debug::log(TRACE, "[core] dmabufFeedbackDone");

    if (g_pHyprlock->dma.formatTable)
        munmap(g_pHyprlock->dma.formatTable, g_pHyprlock->dma.formatTableSize);

    g_pHyprlock->dma.formatTable     = nullptr;
    g_pHyprlock->dma.formatTableSize = 0;
}

static void dmabufFeedbackTrancheTargetDevice(void* data, zwp_linux_dmabuf_feedback_v1* feedback, wl_array* device_arr) {
    Debug::log(TRACE, "[core] dmabufFeedbackTrancheTargetDevice");

    dev_t device;
    assert(device_arr->size == sizeof(device));
    memcpy(&device, device_arr->data, sizeof(device));

    drmDevice* drmDev;
    if (drmGetDeviceFromDevId(device, /* flags */ 0, &drmDev) != 0)
        return;

    if (g_pHyprlock->dma.gbmDevice) {
        drmDevice* drmDevRenderer = NULL;
        drmGetDevice2(gbm_device_get_fd(g_pHyprlock->dma.gbmDevice), /* flags */ 0, &drmDevRenderer);
        g_pHyprlock->dma.deviceUsed = drmDevicesEqual(drmDevRenderer, drmDev);
    } else {
        g_pHyprlock->dma.gbmDevice  = g_pHyprlock->createGBMDevice(drmDev);
        g_pHyprlock->dma.deviceUsed = g_pHyprlock->dma.gbm;
    }
}

static void dmabufFeedbackTrancheFlags(void* data, zwp_linux_dmabuf_feedback_v1* feedback, uint32_t flags) {
    ;
}

static void dmabufFeedbackTrancheFormats(void* data, zwp_linux_dmabuf_feedback_v1* feedback, wl_array* indices) {
    Debug::log(TRACE, "[core] dmabufFeedbackTrancheFormats");

    if (!g_pHyprlock->dma.deviceUsed || !g_pHyprlock->dma.formatTable)
        return;

    struct fm_entry {
        uint32_t format;
        uint32_t padding;
        uint64_t modifier;
    };
    // An entry in the table has to be 16 bytes long
    assert(sizeof(fm_entry) == 16);

    uint32_t  n_modifiers = g_pHyprlock->dma.formatTableSize / sizeof(fm_entry);
    fm_entry* fm_entry    = (struct fm_entry*)g_pHyprlock->dma.formatTable;
    uint16_t* idx;

    for (idx = (uint16_t*)indices->data; (const char*)idx < (const char*)indices->data + indices->size; idx++) {
        if (*idx >= n_modifiers)
            continue;

        Debug::log(TRACE, "GPU Reports supported format {:x} with modifier {:x}", (fm_entry + *idx)->format, (fm_entry + *idx)->modifier);

        g_pHyprlock->dma.dmabufMods.push_back({(fm_entry + *idx)->format, (fm_entry + *idx)->modifier});
    }
}

static void dmabufFeedbackTrancheDone(void* data, struct zwp_linux_dmabuf_feedback_v1* zwp_linux_dmabuf_feedback_v1) {
    Debug::log(TRACE, "[core] dmabufFeedbackTrancheDone");

    g_pHyprlock->dma.deviceUsed = false;
}

inline const zwp_linux_dmabuf_feedback_v1_listener dmabufFeedbackListener = {
    .done                  = dmabufFeedbackDone,
    .format_table          = dmabufFeedbackFormatTable,
    .main_device           = dmabufFeedbackMainDevice,
    .tranche_done          = dmabufFeedbackTrancheDone,
    .tranche_target_device = dmabufFeedbackTrancheTargetDevice,
    .tranche_formats       = dmabufFeedbackTrancheFormats,
    .tranche_flags         = dmabufFeedbackTrancheFlags,
};

static char* gbm_find_render_node(drmDevice* device) {
    drmDevice* devices[64];
    char*      render_node = NULL;

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
        return NULL;
    }

    free(renderNode);
    return gbm_create_device(fd);
}

// end dmabuf

// wl_registry

static void handleGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    g_pHyprlock->onGlobal(data, registry, name, interface, version);
}

static void handleGlobalRemove(void* data, struct wl_registry* registry, uint32_t name) {
    g_pHyprlock->onGlobalRemoved(data, registry, name);
}

inline const wl_registry_listener registryListener = {
    .global        = handleGlobal,
    .global_remove = handleGlobalRemove,
};

void CHyprlock::onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    const std::string IFACE = interface;
    Debug::log(LOG, "  | got iface: {} v{}", IFACE, version);

    if (IFACE == ext_session_lock_manager_v1_interface.name) {
        m_sWaylandState.sessionLock = (ext_session_lock_manager_v1*)wl_registry_bind(registry, name, &ext_session_lock_manager_v1_interface, version);
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    } else if (IFACE == wl_seat_interface.name) {
        if (m_sWaylandState.seat) {
            Debug::log(WARN, "Hyprlock does not support multi-seat configurations. Only binding to the first seat.");
            return;
        }

        m_sWaylandState.seat = (wl_seat*)wl_registry_bind(registry, name, &wl_seat_interface, version);
        wl_seat_add_listener(m_sWaylandState.seat, &seatListener, nullptr);
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    } else if (IFACE == wl_output_interface.name) {
        m_vOutputs.emplace_back(std::make_unique<COutput>((wl_output*)wl_registry_bind(registry, name, &wl_output_interface, version), name));
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    } else if (IFACE == wp_cursor_shape_manager_v1_interface.name) {
        m_pCursorShape = std::make_unique<CCursorShape>((wp_cursor_shape_manager_v1*)wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, version));
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    } else if (IFACE == wl_compositor_interface.name) {
        m_sWaylandState.compositor = (wl_compositor*)wl_registry_bind(registry, name, &wl_compositor_interface, version);
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    } else if (IFACE == wp_fractional_scale_manager_v1_interface.name) {
        m_sWaylandState.fractional = (wp_fractional_scale_manager_v1*)wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, version);
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    } else if (IFACE == wp_viewporter_interface.name) {
        m_sWaylandState.viewporter = (wp_viewporter*)wl_registry_bind(registry, name, &wp_viewporter_interface, version);
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    } else if (IFACE == zwp_linux_dmabuf_v1_interface.name) {
        if (version < 4) {
            Debug::log(ERR, "cannot use linux_dmabuf with ver < 4");
            return;
        }

        dma.linuxDmabuf         = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, version);
        dma.linuxDmabufFeedback = zwp_linux_dmabuf_v1_get_default_feedback((zwp_linux_dmabuf_v1*)dma.linuxDmabuf);
        zwp_linux_dmabuf_feedback_v1_add_listener((zwp_linux_dmabuf_feedback_v1*)dma.linuxDmabufFeedback, &dmabufFeedbackListener, nullptr);
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    } else if (IFACE == zwlr_screencopy_manager_v1_interface.name) {
        m_sWaylandState.screencopy = (zwlr_screencopy_manager_v1*)wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, version);
        Debug::log(LOG, "   > Bound to {} v{}", IFACE, version);
    }
}

void CHyprlock::onGlobalRemoved(void* data, struct wl_registry* registry, uint32_t name) {
    Debug::log(LOG, "  | removed iface {}", name);
    std::erase_if(m_vOutputs, [name](const auto& other) { return other->name == name; });
}

// end wl_registry

static void handleUnlockSignal(int sig) {
    if (sig == SIGUSR1) {
        Debug::log(LOG, "Unlocking with a SIGUSR1");
        g_pHyprlock->unlockSession();
    }
}

static void handlePollTerminate(int sig) {
    ;
}

static void handleCriticalSignal(int sig) {
    g_pHyprlock->attemptRestoreOnDeath();
    abort();
}

void CHyprlock::run() {
    m_sWaylandState.registry = wl_display_get_registry(m_sWaylandState.display);

    wl_registry_add_listener(m_sWaylandState.registry, &registryListener, nullptr);

    wl_display_roundtrip(m_sWaylandState.display);

    if (!m_sWaylandState.sessionLock) {
        Debug::log(CRIT, "Couldn't bind to ext-session-lock-v1, does your compositor support it?");
        exit(1);
    }

    // gather info about monitors
    wl_display_roundtrip(m_sWaylandState.display);

    g_pRenderer = std::make_unique<CRenderer>();

    const auto CURRENTDESKTOP = getenv("XDG_CURRENT_DESKTOP");
    const auto SZCURRENTD     = std::string{CURRENTDESKTOP ? CURRENTDESKTOP : ""};

    Debug::log(LOG, "Running on {}", SZCURRENTD);

    // Hyprland violates the protocol a bit to allow for this.
    if (SZCURRENTD != "Hyprland") {
        while (!g_pRenderer->asyncResourceGatherer->ready) {
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

    lockSession();

    signal(SIGUSR1, handleUnlockSignal);
    signal(SIGUSR2, handlePollTerminate);
    signal(SIGSEGV, handleCriticalSignal);
    signal(SIGABRT, handleCriticalSignal);

    pollfd pollfds[] = {
        {
            .fd     = wl_display_get_fd(m_sWaylandState.display),
            .events = POLLIN,
        },
    };

    std::thread pollThr([this, &pollfds]() {
        while (!m_bTerminate) {
            int ret = poll(pollfds, 1, 5000 /* 5 seconds, reasonable. Just in case we need to terminate and the signal fails */);

            if (ret < 0) {
                Debug::log(CRIT, "[core] Polling fds failed with {}", errno);
                attemptRestoreOnDeath();
                m_bTerminate = true;
                exit(1);
            }

            for (size_t i = 0; i < 1; ++i) {
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

    while (1) {
        std::unique_lock lk(m_sLoopState.eventRequestMutex);
        if (m_sLoopState.event == false)
            m_sLoopState.loopCV.wait_for(lk, std::chrono::milliseconds(5000), [this] { return m_sLoopState.event; });

        if (m_bTerminate)
            break;

        std::lock_guard<std::mutex> lg(m_sLoopState.eventLoopMutex);

        m_sLoopState.event = false;

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

        if (m_bTerminate)
            break;
    }

    m_sLoopState.timerEvent = true;
    m_sLoopState.timerCV.notify_all();
    g_pRenderer->asyncResourceGatherer->notify();
    g_pRenderer->asyncResourceGatherer->await();

    m_vOutputs.clear();
    g_pEGL.reset();

    wl_display_disconnect(m_sWaylandState.display);

    pthread_kill(pollThr.native_handle(), SIGUSR2);

    // wait for threads to exit cleanly to avoid a coredump
    pollThr.join();
    timersThr.join();

    Debug::log(LOG, "Reached the end, exiting");
}

// wl_seat

static void handlePointerEnter(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    if (!g_pHyprlock->m_pCursorShape)
        return;

    static auto* const PHIDE = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:hide_cursor");

    if (**PHIDE)
        g_pHyprlock->m_pCursorShape->hideCursor(serial);
    else
        g_pHyprlock->m_pCursorShape->setShape(serial, wp_cursor_shape_device_v1_shape::WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);

    g_pHyprlock->m_vLastEnterCoords = {wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y)};
}

static void handlePointerLeave(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface) {
    ;
}

static void handlePointerAxis(void* data, wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    // ignored
}

static void handlePointerMotion(void* data, struct wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    if (g_pHyprlock->m_vLastEnterCoords.distance({wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y)}) > 5 &&
        std::chrono::system_clock::now() < g_pHyprlock->m_tGraceEnds) {

        Debug::log(LOG, "In grace and cursor moved more than 5px, unlocking!");
        g_pHyprlock->unlockSession();
    }
}

static void handlePointerButton(void* data, struct wl_pointer* wl_pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t button_state) {
    ;
}

static void handleFrame(void* data, struct wl_pointer* wl_pointer) {
    ;
}

static void handleAxisSource(void* data, struct wl_pointer* wl_pointer, uint32_t axis_source) {
    ;
}

static void handleAxisStop(void* data, struct wl_pointer* wl_pointer, uint32_t time, uint32_t axis) {
    ;
}

static void handleAxisDiscrete(void* data, struct wl_pointer* wl_pointer, uint32_t axis, int32_t discrete) {
    ;
}

static void handleAxisValue120(void* data, struct wl_pointer* wl_pointer, uint32_t axis, int32_t value120) {
    ;
}

static void handleAxisRelativeDirection(void* data, struct wl_pointer* wl_pointer, uint32_t axis, uint32_t direction) {
    ;
}

inline const wl_pointer_listener pointerListener = {
    .enter                   = handlePointerEnter,
    .leave                   = handlePointerLeave,
    .motion                  = handlePointerMotion,
    .button                  = handlePointerButton,
    .axis                    = handlePointerAxis,
    .frame                   = handleFrame,
    .axis_source             = handleAxisSource,
    .axis_stop               = handleAxisStop,
    .axis_discrete           = handleAxisDiscrete,
    .axis_value120           = handleAxisValue120,
    .axis_relative_direction = handleAxisRelativeDirection,
};

static void handleKeyboardKeymap(void* data, wl_keyboard* wl_keyboard, uint format, int fd, uint size) {
    if (!g_pHyprlock->m_pXKBContext)
        return;

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        Debug::log(ERR, "Could not recognise keymap format");
        return;
    }

    const char* buf = (const char*)mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);
    if (buf == MAP_FAILED) {
        Debug::log(ERR, "Failed to mmap xkb keymap: {}", errno);
        return;
    }

    g_pHyprlock->m_pXKBKeymap = xkb_keymap_new_from_buffer(g_pHyprlock->m_pXKBContext, buf, size - 1, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);

    munmap((void*)buf, size);
    close(fd);

    if (!g_pHyprlock->m_pXKBKeymap) {
        Debug::log(ERR, "Failed to compile xkb keymap");
        return;
    }

    g_pHyprlock->m_pXKBState = xkb_state_new(g_pHyprlock->m_pXKBKeymap);
    if (!g_pHyprlock->m_pXKBState) {
        Debug::log(ERR, "Failed to create xkb state");
        return;
    }
}

static void handleKeyboardKey(void* data, struct wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
    g_pHyprlock->onKey(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

static void handleKeyboardEnter(void* data, wl_keyboard* wl_keyboard, uint serial, wl_surface* surface, wl_array* keys) {
    ;
}

static void handleKeyboardLeave(void* data, wl_keyboard* wl_keyboard, uint serial, wl_surface* surface) {
    ;
}

static void handleKeyboardModifiers(void* data, wl_keyboard* wl_keyboard, uint serial, uint mods_depressed, uint mods_latched, uint mods_locked, uint group) {
    if (!g_pHyprlock->m_pXKBState)
        return;

    xkb_state_update_mask(g_pHyprlock->m_pXKBState, mods_depressed, mods_latched, mods_locked, 0, 0, group);
}

static void handleRepeatInfo(void* data, struct wl_keyboard* wl_keyboard, int32_t rate, int32_t delay) {
    ;
}

inline const wl_keyboard_listener keyboardListener = {
    .keymap      = handleKeyboardKeymap,
    .enter       = handleKeyboardEnter,
    .leave       = handleKeyboardLeave,
    .key         = handleKeyboardKey,
    .modifiers   = handleKeyboardModifiers,
    .repeat_info = handleRepeatInfo,
};

static void handleCapabilities(void* data, wl_seat* wl_seat, uint32_t capabilities) {
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        g_pHyprlock->m_pPointer = wl_seat_get_pointer(wl_seat);
        wl_pointer_add_listener(g_pHyprlock->m_pPointer, &pointerListener, wl_seat);
    }

    if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
        g_pHyprlock->m_pKeeb = wl_seat_get_keyboard(wl_seat);
        wl_keyboard_add_listener(g_pHyprlock->m_pKeeb, &keyboardListener, wl_seat);
    }
}

static void handleName(void* data, struct wl_seat* wl_seat, const char* name) {
    ;
}

// end wl_seat

// session_lock

static void handleLocked(void* data, ext_session_lock_v1* ext_session_lock_v1) {
    g_pHyprlock->onLockLocked();
}

static void handleFinished(void* data, ext_session_lock_v1* ext_session_lock_v1) {
    g_pHyprlock->onLockFinished();
}

static const ext_session_lock_v1_listener sessionLockListener = {
    .locked   = handleLocked,
    .finished = handleFinished,
};

// end session_lock

void CHyprlock::onPasswordCheckTimer() {
    // check result
    if (m_sPasswordState.result->success) {
        unlockSession();
    } else {
        Debug::log(LOG, "Authentication failed: {}", m_sPasswordState.result->failReason);
        m_sPasswordState.lastFailReason = m_sPasswordState.result->failReason;
        m_sPasswordState.passBuffer     = "";

        for (auto& o : m_vOutputs) {
            o->sessionLockSurface->render();
        }
    }

    m_sPasswordState.result.reset();
}

bool CHyprlock::passwordCheckWaiting() {
    return m_sPasswordState.result.get();
}

std::optional<std::string> CHyprlock::passwordLastFailReason() {
    return m_sPasswordState.lastFailReason;
}

void CHyprlock::onKey(uint32_t key, bool down) {
    const auto SYM = xkb_state_key_get_one_sym(m_pXKBState, key + 8);

    if (down && std::chrono::system_clock::now() < g_pHyprlock->m_tGraceEnds) {
        unlockSession();
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
    else
        std::erase(m_vPressedKeys, key);

    if (!down) // we dont care about up events
        return;

    if (m_sPasswordState.result) {
        for (auto& o : m_vOutputs) {
            o->sessionLockSurface->render();
        }
        return;
    }

    if (SYM == XKB_KEY_BackSpace) {
        if (m_sPasswordState.passBuffer.length() > 0)
            m_sPasswordState.passBuffer = m_sPasswordState.passBuffer.substr(0, m_sPasswordState.passBuffer.length() - 1);
    } else if (SYM == XKB_KEY_Return || SYM == XKB_KEY_KP_Enter) {
        Debug::log(LOG, "Authenticating");

        m_sPasswordState.result = g_pPassword->verify(m_sPasswordState.passBuffer);
    } else if (SYM == XKB_KEY_Escape) {
        Debug::log(LOG, "Clearing password buffer");

        m_sPasswordState.passBuffer = "";
    } else {
        char buf[16] = {0};
        int  len     = xkb_keysym_to_utf8(SYM, buf, 16);
        if (len > 1)
            m_sPasswordState.passBuffer += std::string{buf, len - 1};
    }

    for (auto& o : m_vOutputs) {
        o->sessionLockSurface->render();
    }
}

void CHyprlock::lockSession() {
    Debug::log(LOG, "Locking session");
    m_sLockState.lock = ext_session_lock_manager_v1_lock(m_sWaylandState.sessionLock);
    ext_session_lock_v1_add_listener(m_sLockState.lock, &sessionLockListener, nullptr);
}

void CHyprlock::unlockSession() {
    Debug::log(LOG, "Unlocking session");
    if (m_bTerminate && !m_sLockState.lock) {
        Debug::log(ERR, "Unlock already happend?");
        return;
    }

    ext_session_lock_v1_unlock_and_destroy(m_sLockState.lock);
    m_sLockState.lock = nullptr;

    Debug::log(LOG, "Unlocked, exiting!");

    m_bTerminate = true;
    m_bLocked    = false;

    wl_display_roundtrip(m_sWaylandState.display);
}

void CHyprlock::onLockLocked() {
    Debug::log(LOG, "onLockLocked called");

    for (auto& o : m_vOutputs) {
        o->sessionLockSurface = std::make_unique<CSessionLockSurface>(o.get());
    }

    m_bLocked = true;
}

void CHyprlock::onLockFinished() {
    Debug::log(LOG, "onLockFinished called. Seems we got yeeten. Is another lockscreen running?");
    ext_session_lock_v1_unlock_and_destroy(m_sLockState.lock);
    m_sLockState.lock = nullptr;
}

ext_session_lock_manager_v1* CHyprlock::getSessionLockMgr() {
    return m_sWaylandState.sessionLock;
}

ext_session_lock_v1* CHyprlock::getSessionLock() {
    return m_sLockState.lock;
}

wl_compositor* CHyprlock::getCompositor() {
    return m_sWaylandState.compositor;
}

wl_display* CHyprlock::getDisplay() {
    return m_sWaylandState.display;
}

wp_fractional_scale_manager_v1* CHyprlock::getFractionalMgr() {
    return m_sWaylandState.fractional;
}

wp_viewporter* CHyprlock::getViewporter() {
    return m_sWaylandState.viewporter;
}

size_t CHyprlock::getPasswordBufferLen() {
    return m_sPasswordState.passBuffer.length();
}

std::shared_ptr<CTimer> CHyprlock::addTimer(const std::chrono::system_clock::duration& timeout, std::function<void(std::shared_ptr<CTimer> self, void* data)> cb_, void* data) {
    std::lock_guard<std::mutex> lg(m_sLoopState.timersMutex);
    const auto                  T = m_vTimers.emplace_back(std::make_shared<CTimer>(timeout, cb_, data));
    m_sLoopState.timerEvent       = true;
    m_sLoopState.timerCV.notify_all();
    return T;
}

void CHyprlock::spawnAsync(const std::string& args) {
    Debug::log(LOG, "Executing (async) {}", args);

    int socket[2];
    if (pipe(socket) != 0)
        Debug::log(LOG, "Unable to create pipe for fork");

    pid_t child, grandchild;
    child = fork();

    if (child < 0) {
        close(socket[0]);
        close(socket[1]);
        Debug::log(LOG, "Fail to create the first fork");
        return;
    }

    if (child == 0) {
        // run in child

        sigset_t set;
        sigemptyset(&set);
        sigprocmask(SIG_SETMASK, &set, NULL);

        grandchild = fork();

        if (grandchild == 0) {
            // run in grandchild
            close(socket[0]);
            close(socket[1]);
            execl("/bin/sh", "/bin/sh", "-c", args.c_str(), nullptr);
            // exit grandchild
            _exit(0);
        }

        close(socket[0]);
        write(socket[1], &grandchild, sizeof(grandchild));
        close(socket[1]);
        // exit child
        _exit(0);
    }

    // run in parent
    close(socket[1]);
    read(socket[0], &grandchild, sizeof(grandchild));
    close(socket[0]);
    // clear child and leave child to init
    waitpid(child, NULL, 0);

    if (child < 0) {
        Debug::log(LOG, "Failed to create the second fork");
        return;
    }

    Debug::log(LOG, "Process Created with pid {}", grandchild);
}

std::string CHyprlock::spawnSync(const std::string& cmd) {
    std::array<char, 128>                          buffer;
    std::string                                    result;
    const std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe)
        return "";

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

zwlr_screencopy_manager_v1* CHyprlock::getScreencopy() {
    return m_sWaylandState.screencopy;
}

void CHyprlock::attemptRestoreOnDeath() {
    if (m_bTerminate)
        return;

    // dirty hack
    uint64_t              timeNowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - std::chrono::system_clock::from_time_t({0})).count();

    constexpr const char* LASTRESTARTPATH = "/tmp/hypr/.hyprlockrestart";

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

    spawnSync("hyprctl keyword misc:allow_session_lock_restore true");
    spawnAsync("sleep 2 && hyprlock & disown");
}