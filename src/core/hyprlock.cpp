#include "hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"
#include "../renderer/Renderer.hpp"
#include "Password.hpp"
#include "Egl.hpp"
#include <sys/mman.h>
#include <cuchar>

CHyprlock::CHyprlock(const std::string& wlDisplay) {
    m_sWaylandState.display = wl_display_connect(wlDisplay.empty() ? nullptr : wlDisplay.c_str());
    if (!m_sWaylandState.display) {
        Debug::log(CRIT, "Couldn't connect to a wayland compositor");
        exit(1);
    }

    g_pEGL = std::make_unique<CEGL>(m_sWaylandState.display);

    m_pXKBContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!m_pXKBContext)
        Debug::log(ERR, "Failed to create xkb context");

    g_pRenderer = std::make_unique<CRenderer>();
}

// wl_seat

static void                   handleCapabilities(void* data, wl_seat* wl_seat, uint32_t capabilities);
static void                   handleName(void* data, struct wl_seat* wl_seat, const char* name);

inline const wl_seat_listener seatListener = {
    .capabilities = handleCapabilities,
    .name         = handleName,
};

// end wl_seat

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
    }
}

void CHyprlock::onGlobalRemoved(void* data, struct wl_registry* registry, uint32_t name) {
    Debug::log(LOG, "  | removed iface {}", name);
    std::erase_if(m_vOutputs, [name](const auto& other) { return other->name == name; });
}

// end wl_registry

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

    lockSession();

    while (wl_display_dispatch(m_sWaylandState.display) != -1) {
        if (m_bTerminate)
            break;
    }

    Debug::log(LOG, "Reached the end, exiting");
}

// wl_seat

static void handlePointerEnter(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    if (!g_pHyprlock->m_pCursorShape)
        return;

    g_pHyprlock->m_pCursorShape->setShape(serial, wp_cursor_shape_device_v1_shape::WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER);
}

static void handlePointerLeave(void* data, struct wl_pointer* wl_pointer, uint32_t serial, struct wl_surface* surface) {
    ;
}

static void handlePointerAxis(void* data, wl_pointer* wl_pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
    // ignored
}

static void handlePointerMotion(void* data, struct wl_pointer* wl_pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    ;
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
        Debug::log(ERR, "Failed to mmap xkb keymap: %d", errno);
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
    if (state != WL_KEYBOARD_KEY_STATE_PRESSED)
        return;

    g_pHyprlock->onKey(key);
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

void CHyprlock::onKey(uint32_t key) {
    const auto SYM = xkb_state_key_get_one_sym(m_pXKBState, key + 8);

    if (SYM == XKB_KEY_BackSpace) {
        if (m_sPasswordState.passBuffer.length() > 0)
            m_sPasswordState.passBuffer = m_sPasswordState.passBuffer.substr(0, m_sPasswordState.passBuffer.length() - 1);
    } else if (SYM == XKB_KEY_Return) {
        Debug::log(LOG, "Authenticating");

        const auto RESULT = g_pPassword->verify(m_sPasswordState.passBuffer);

        Debug::log(LOG, "Password auth result: {}", RESULT.failReason);

        if (RESULT.success)
            unlockSession();

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
    ext_session_lock_v1_unlock_and_destroy(m_sLockState.lock);
    m_sLockState.lock = nullptr;

    m_vOutputs.clear();
    g_pEGL.reset();
    Debug::log(LOG, "Unlocked, exiting!");

    m_bTerminate = true;

    wl_display_roundtrip(m_sWaylandState.display);

    exit(0);
}

void CHyprlock::onLockLocked() {
    Debug::log(LOG, "onLockLocked called");

    for (auto& o : m_vOutputs) {
        o->sessionLockSurface = std::make_unique<CSessionLockSurface>(o.get());
    }
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
