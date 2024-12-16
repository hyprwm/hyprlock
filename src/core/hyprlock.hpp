#pragma once

#include <wayland-client.h>
#include "ext-session-lock-v1-protocol.h"
#include "fractional-scale-v1-protocol.h"
#include "wlr-screencopy-unstable-v1-protocol.h"
#include "viewporter-protocol.h"
#include "Output.hpp"
#include "CursorShape.hpp"
#include "Timer.hpp"
#include <memory>
#include <vector>
#include <condition_variable>
#include <optional>

#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-compose.h>

#include <gbm.h>
#include <xf86drm.h>

struct SDMABUFModifier {
    uint32_t fourcc = 0;
    uint64_t mod    = 0;
};

class CHyprlock {
  public:
    CHyprlock(const std::string& wlDisplay, const bool immediate, const bool immediateRender, const bool noFadeIn);
    ~CHyprlock();

    void                            run();

    void                            unlock();
    bool                            isUnlocked();

    void                            onGlobal(void* data, struct wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
    void                            onGlobalRemoved(void* data, struct wl_registry* registry, uint32_t name);

    std::shared_ptr<CTimer>         addTimer(const std::chrono::system_clock::duration& timeout, std::function<void(std::shared_ptr<CTimer> self, void* data)> cb_, void* data,
                                             bool force = false);

    void                            enqueueForceUpdateTimers();

    void                            onLockLocked();
    void                            onLockFinished();

    void                            acquireSessionLock();
    void                            releaseSessionLock();

    void                            createSessionLockSurfaces();

    void                            attemptRestoreOnDeath();

    std::string                     spawnSync(const std::string& cmd);

    void                            onKey(uint32_t key, bool down);
    void                            startKeyRepeat(xkb_keysym_t sym);
    void                            repeatKey(xkb_keysym_t sym);
    void                            handleKeySym(xkb_keysym_t sym, bool compose);
    void                            onPasswordCheckTimer();
    void                            clearPasswordBuffer();
    bool                            passwordCheckWaiting();
    std::optional<std::string>      passwordLastFailReason();

    void                            renderOutput(const std::string& stringPort);
    void                            renderAllOutputs();

    size_t                          getPasswordBufferLen();
    size_t                          getPasswordBufferDisplayLen();

    ext_session_lock_manager_v1*    getSessionLockMgr();
    ext_session_lock_v1*            getSessionLock();
    wl_compositor*                  getCompositor();
    wl_display*                     getDisplay();
    wp_fractional_scale_manager_v1* getFractionalMgr();
    wp_viewporter*                  getViewporter();
    zwlr_screencopy_manager_v1*     getScreencopy();

    wl_pointer*                     m_pPointer = nullptr;
    std::unique_ptr<CCursorShape>   m_pCursorShape;

    wl_keyboard*                    m_pKeeb            = nullptr;
    xkb_context*                    m_pXKBContext      = nullptr;
    xkb_keymap*                     m_pXKBKeymap       = nullptr;
    xkb_state*                      m_pXKBState        = nullptr;
    xkb_compose_state*              m_pXKBComposeState = nullptr;

    int32_t                         m_iKeebRepeatRate  = 25;
    int32_t                         m_iKeebRepeatDelay = 600;

    xkb_layout_index_t              m_uiActiveLayout = 0;

    bool                            m_bTerminate = false;

    bool                            m_bLocked = false;

    bool                            m_bCapsLock    = false;
    bool                            m_bNumLock     = false;
    bool                            m_bCtrl        = false;
    bool                            m_bFadeStarted = false;

    bool                            m_bImmediateRender = false;

    bool                            m_bNoFadeIn = false;

    std::string                     m_sCurrentDesktop = "";

    //
    std::chrono::system_clock::time_point m_tGraceEnds;
    std::chrono::system_clock::time_point m_tFadeEnds;
    Vector2D                              m_vLastEnterCoords = {};

    std::shared_ptr<CTimer>               m_pKeyRepeatTimer = nullptr;

    std::vector<std::unique_ptr<COutput>> m_vOutputs;
    std::vector<std::shared_ptr<CTimer>>  getTimers();

    struct {
        void*                        linuxDmabuf         = nullptr;
        void*                        linuxDmabufFeedback = nullptr;

        gbm_bo*                      gbm       = nullptr;
        gbm_device*                  gbmDevice = nullptr;

        void*                        formatTable     = nullptr;
        size_t                       formatTableSize = 0;
        bool                         deviceUsed      = false;

        std::vector<SDMABUFModifier> dmabufMods;
    } dma;
    gbm_device* createGBMDevice(drmDevice* dev);

  private:
    struct {
        wl_display*                     display     = nullptr;
        wl_registry*                    registry    = nullptr;
        wl_seat*                        seat        = nullptr;
        ext_session_lock_manager_v1*    sessionLock = nullptr;
        wl_compositor*                  compositor  = nullptr;
        wp_fractional_scale_manager_v1* fractional  = nullptr;
        wp_viewporter*                  viewporter  = nullptr;
        zwlr_screencopy_manager_v1*     screencopy  = nullptr;
    } m_sWaylandState;

    struct {
        ext_session_lock_v1* lock = nullptr;
    } m_sLockState;

    struct {
        std::string passBuffer      = "";
        size_t      failedAttempts  = 0;
        bool        displayFailText = false;
    } m_sPasswordState;

    struct {
        std::mutex              timersMutex;
        std::mutex              eventRequestMutex;
        std::mutex              eventLoopMutex;
        std::condition_variable loopCV;
        bool                    event = false;

        std::condition_variable timerCV;
        std::mutex              timerRequestMutex;
        bool                    timerEvent = false;
    } m_sLoopState;

    std::vector<std::shared_ptr<CTimer>> m_vTimers;

    std::vector<uint32_t>                m_vPressedKeys;
};

inline std::unique_ptr<CHyprlock> g_pHyprlock;
