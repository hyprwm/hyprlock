#pragma once

#include "../defines.hpp"
#include "wayland.hpp"
#include "ext-session-lock-v1.hpp"
#include "fractional-scale-v1.hpp"
#include "wlr-screencopy-unstable-v1.hpp"
#include "linux-dmabuf-v1.hpp"
#include "viewporter.hpp"
#include "Output.hpp"
#include "Seat.hpp"
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
    CHyprlock(const std::string& wlDisplay, const bool immediateRender, const int gracePeriod);
    ~CHyprlock();

    void                       run();

    void                       unlock();
    bool                       isUnlocked();

    ASP<CTimer>                addTimer(const std::chrono::system_clock::duration& timeout, std::function<void(ASP<CTimer> self, void* data)> cb_, void* data, bool force = false);

    void                       enqueueForceUpdateTimers();

    void                       onLockLocked();
    void                       onLockFinished();

    bool                       acquireSessionLock();
    void                       releaseSessionLock();

    void                       onKey(uint32_t key, bool down);
    void                       onClick(uint32_t button, bool down, const Vector2D& pos);
    void                       onHover(const Vector2D& pos);
    void                       startKeyRepeat(xkb_keysym_t sym);
    void                       repeatKey(xkb_keysym_t sym);
    void                       handleKeySym(xkb_keysym_t sym, bool compose);
    void                       onPasswordCheckTimer();
    void                       clearPasswordBuffer();
    bool                       passwordCheckWaiting();
    std::optional<std::string> passwordLastFailReason();

    void                       renderOutput(const std::string& stringPort);
    void                       renderAllOutputs();

    size_t                     getPasswordBufferLen();
    size_t                     getPasswordBufferDisplayLen();
    std::string&               getPasswordBuffer();
    bool                       getPasswordShow();

    void                       togglePasswordShow();

    SP<CCExtSessionLockManagerV1>    getSessionLockMgr();
    SP<CCExtSessionLockV1>           getSessionLock();
    SP<CCWlCompositor>               getCompositor();
    wl_display*                      getDisplay();
    SP<CCWpFractionalScaleManagerV1> getFractionalMgr();
    SP<CCWpViewporter>               getViewporter();
    SP<CCZwlrScreencopyManagerV1>    getScreencopy();
    SP<CCWlShm>                      getShm();

    int32_t                          m_iKeebRepeatRate  = 25;
    int32_t                          m_iKeebRepeatDelay = 600;

    xkb_layout_index_t               m_uiActiveLayout = 0;

    bool                             m_bTerminate = false;

    bool                             m_lockAquired = false;
    bool                             m_bLocked     = false;

    bool                             m_bCapsLock = false;
    bool                             m_bNumLock  = false;
    bool                             m_bCtrl     = false;

    bool                             m_bImmediateRender = false;

    std::string                      m_sCurrentDesktop = "";

    //
    std::chrono::system_clock::time_point m_tGraceEnds;
    Vector2D                              m_vLastEnterCoords = {};
    WP<COutput>                           m_focusedOutput;

    Vector2D                              m_vMouseLocation = {};

    ASP<CTimer>                           m_pKeyRepeatTimer = nullptr;

    std::vector<SP<COutput>>              m_vOutputs;
    std::vector<ASP<CTimer>>              getTimers();

    struct {
        SP<CCZwpLinuxDmabufV1>         linuxDmabuf         = nullptr;
        SP<CCZwpLinuxDmabufFeedbackV1> linuxDmabufFeedback = nullptr;

        gbm_bo*                        gbm       = nullptr;
        gbm_device*                    gbmDevice = nullptr;

        void*                          formatTable     = nullptr;
        size_t                         formatTableSize = 0;
        bool                           deviceUsed      = false;

        std::vector<SDMABUFModifier>   dmabufMods;
    } dma;
    gbm_device* createGBMDevice(drmDevice* dev);

    void        addDmabufListener();
    void        removeDmabufListener();

  private:
    struct {
        wl_display*                      display     = nullptr;
        SP<CCWlRegistry>                 registry    = nullptr;
        SP<CCExtSessionLockManagerV1>    sessionLock = nullptr;
        SP<CCWlCompositor>               compositor  = nullptr;
        SP<CCWpFractionalScaleManagerV1> fractional  = nullptr;
        SP<CCWpViewporter>               viewporter  = nullptr;
        SP<CCZwlrScreencopyManagerV1>    screencopy  = nullptr;
        SP<CCWlShm>                      shm         = nullptr;
    } m_sWaylandState;

    struct {
        SP<CCExtSessionLockV1> lock = nullptr;
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

        std::condition_variable wlDispatchCV;
        bool                    wlDispatched = false;

        std::condition_variable timerCV;
        std::mutex              timerRequestMutex;
        bool                    timerEvent = false;
    } m_sLoopState;

    std::vector<ASP<CTimer>> m_vTimers;

    std::vector<uint32_t>    m_vPressedKeys;
};

inline UP<CHyprlock> g_pHyprlock;
