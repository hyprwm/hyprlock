#include "Seat.hpp"
#include "hyprlock.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"
#include <chrono>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/input-event-codes.h>

CSeatManager::~CSeatManager() {
    if (m_pCursorShape && m_pCursorShape->shapeChanged)
        m_pCursorShape->setShape(wpCursorShapeDeviceV1Shape::WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);

    if (m_pXKBState)
        xkb_state_unref(m_pXKBState);
    if (m_pXKBKeymap)
        xkb_keymap_unref(m_pXKBKeymap);
    if (m_pXKBContext)
        xkb_context_unref(m_pXKBContext);
}

void CSeatManager::registerSeat(SP<CCWlSeat> seat) {
    m_pSeat = seat;

    m_pXKBContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!m_pXKBContext)
        Debug::log(ERR, "Failed to create xkb context");

    m_pSeat->setCapabilities([this](CCWlSeat* r, wl_seat_capability caps) {
        if (caps & WL_SEAT_CAPABILITY_POINTER) {
            m_pPointer = makeShared<CCWlPointer>(r->sendGetPointer());

            static const auto HIDECURSOR = g_pConfigManager->getValue<Hyprlang::INT>("general:hide_cursor");
            m_pPointer->setMotion([](CCWlPointer* r, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
                g_pHyprlock->m_vMouseLocation = {wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y)};

                if (!*HIDECURSOR)
                    g_pHyprlock->onHover(g_pHyprlock->m_vMouseLocation);

                if (std::chrono::system_clock::now() > g_pHyprlock->m_tGraceEnds)
                    return;

                if (!g_pHyprlock->isUnlocked() && g_pHyprlock->m_vLastEnterCoords.distance({wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y)}) > 5) {
                    Debug::log(LOG, "In grace and cursor moved more than 5px, unlocking!");
                    g_pHyprlock->unlock();
                }
            });

            m_pPointer->setEnter([this](CCWlPointer* r, uint32_t serial, wl_proxy* surf, wl_fixed_t surface_x, wl_fixed_t surface_y) {
                if (!m_pCursorShape)
                    return;

                m_pCursorShape->lastCursorSerial = serial;

                if (*HIDECURSOR)
                    m_pCursorShape->hideCursor();
                else
                    m_pCursorShape->setShape(wpCursorShapeDeviceV1Shape::WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);

                g_pHyprlock->m_vLastEnterCoords = {wl_fixed_to_double(surface_x), wl_fixed_to_double(surface_y)};

                if (*HIDECURSOR)
                    return;

                for (const auto& POUTPUT : g_pHyprlock->m_vOutputs) {
                    if (!POUTPUT->m_sessionLockSurface)
                        continue;

                    const auto& PWLSURFACE = POUTPUT->m_sessionLockSurface->getWlSurface();
                    if (PWLSURFACE->resource() == surf)
                        g_pHyprlock->m_focusedOutput = POUTPUT;
                }
            });

            m_pPointer->setLeave([](CCWlPointer* r, uint32_t serial, wl_proxy* surf) { g_pHyprlock->m_focusedOutput.reset(); });

            m_pPointer->setButton([](CCWlPointer* r, uint32_t serial, uint32_t time, uint32_t button, wl_pointer_button_state state) {
                if (*HIDECURSOR)
                    return;
                g_pHyprlock->onClick(button, state == WL_POINTER_BUTTON_STATE_PRESSED, g_pHyprlock->m_vMouseLocation);
            });
        }
        if (caps & WL_SEAT_CAPABILITY_TOUCH) {
            m_pTouch = makeShared<CCWlTouch>(r->sendGetTouch());
            m_pTouch->setDown([](CCWlTouch* r, uint32_t serial, uint32_t time, wl_proxy* surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
                g_pHyprlock->onClick(BTN_LEFT, true, {wl_fixed_to_double(x), wl_fixed_to_double(y)});
            });
            m_pTouch->setUp([](CCWlTouch* r, uint32_t serial, uint32_t time, int32_t id) { g_pHyprlock->onClick(BTN_LEFT, false, {0, 0}); });
        };
        if (caps & WL_SEAT_CAPABILITY_KEYBOARD) {
            m_pKeeb = makeShared<CCWlKeyboard>(r->sendGetKeyboard());

            m_pKeeb->setKeymap([this](CCWlKeyboard*, wl_keyboard_keymap_format format, int32_t fd, uint32_t size) {
                if (!m_pXKBContext)
                    return;

                if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
                    Debug::log(ERR, "Could not recognise keymap format");
                    return;
                }

                const char* buf = (const char*)mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
                if (buf == MAP_FAILED) {
                    Debug::log(ERR, "Failed to mmap xkb keymap: {}", errno);
                    return;
                }

                m_pXKBKeymap = xkb_keymap_new_from_buffer(m_pXKBContext, buf, size - 1, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);

                munmap((void*)buf, size);
                close(fd);

                if (!m_pXKBKeymap) {
                    Debug::log(ERR, "Failed to compile xkb keymap");
                    return;
                }

                m_pXKBState = xkb_state_new(m_pXKBKeymap);
                if (!m_pXKBState) {
                    Debug::log(ERR, "Failed to create xkb state");
                    return;
                }

                const auto PCOMOPOSETABLE = xkb_compose_table_new_from_locale(m_pXKBContext, setlocale(LC_CTYPE, nullptr), XKB_COMPOSE_COMPILE_NO_FLAGS);

                if (!PCOMOPOSETABLE) {
                    Debug::log(ERR, "Failed to create xkb compose table");
                    return;
                }

                m_pXKBComposeState = xkb_compose_state_new(PCOMOPOSETABLE, XKB_COMPOSE_STATE_NO_FLAGS);
            });

            m_pKeeb->setKey([](CCWlKeyboard* r, uint32_t serial, uint32_t time, uint32_t key, wl_keyboard_key_state state) {
                g_pHyprlock->onKey(key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
            });

            m_pKeeb->setModifiers([this](CCWlKeyboard* r, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
                if (!m_pXKBState)
                    return;

                if (group != g_pHyprlock->m_uiActiveLayout) {
                    g_pHyprlock->m_uiActiveLayout = group;
                    for (auto& t : g_pHyprlock->getTimers()) {
                        if (t->canForceUpdate()) {
                            t->call(t);
                            t->cancel();
                        }
                    }
                }

                xkb_state_update_mask(m_pXKBState, mods_depressed, mods_latched, mods_locked, 0, 0, group);
                g_pHyprlock->m_bCapsLock = xkb_state_mod_name_is_active(m_pXKBState, XKB_MOD_NAME_CAPS, XKB_STATE_MODS_LOCKED);
                g_pHyprlock->m_bNumLock  = xkb_state_mod_name_is_active(m_pXKBState, XKB_MOD_NAME_NUM, XKB_STATE_MODS_LOCKED);
            });

            m_pKeeb->setRepeatInfo([](CCWlKeyboard* r, int32_t rate, int32_t delay) {
                g_pHyprlock->m_iKeebRepeatRate  = rate;
                g_pHyprlock->m_iKeebRepeatDelay = delay;
            });
        }
    });

    m_pSeat->setName([](CCWlSeat* r, const char* name) { Debug::log(LOG, "Exposed seat name: {}", name ? name : "nullptr"); });
}

void CSeatManager::registerCursorShape(SP<CCWpCursorShapeManagerV1> shape) {
    m_pCursorShape = makeUnique<CCursorShape>(shape);
}

bool CSeatManager::registered() {
    return m_pSeat;
}
