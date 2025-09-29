#pragma once

#include "Screencopy.hpp"
#include <cstdint>
#include <pango/pangocairo.h>
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <condition_variable>
#include <any>
#include "Shared.hpp"
#include <hyprgraphics/cairo/CairoSurface.hpp>
#include <hyprutils/os/FileDescriptor.hpp>

class CAsyncResourceGatherer {
  public:
    CAsyncResourceGatherer();
    std::atomic<bool>              gathered = false;
    Hyprutils::OS::CFileDescriptor gatheredEventfd;

    std::atomic<float>             progress = 0;

    /* only call from ogl thread */
    SPreloadedAsset* getAssetByID(const std::string& id);

    bool             apply();

    enum eTargetType : uint8_t {
        TARGET_IMAGE = 0,
        TARGET_EMBEDDED_IMAGE,
        TARGET_TEXT
    };

    struct SPreloadRequest {
        eTargetType                               type;
        std::string                               asset;
        std::string                               id;

        std::span<uint8_t>                        image_buffer;

        std::unordered_map<std::string, std::any> props;

        // optional. Callbacks will be dispatched from the main thread,
        // so wayland/gl calls are OK.
        // will fire once the resource is fully loaded and ready.
        std::function<void()> callback = nullptr;
    };

    Vector2D getTextAssetSize(const SPreloadRequest& request);

    void     requestAsyncAssetPreload(const SPreloadRequest& request);
    void     unloadAsset(SPreloadedAsset* asset);
    void     notify();
    void     await();

  private:
    std::thread  asyncLoopThread;
    std::thread  initialGatherThread;

    PangoLayout* getPangoLayout(const SPreloadRequest& rq);

    void         asyncAssetSpinLock();
    void         renderText(const SPreloadRequest& rq);
    void         renderImage(const SPreloadRequest& rq);

    struct {
        std::condition_variable      requestsCV;
        std::mutex                   requestsMutex;

        std::vector<SPreloadRequest> requests;
        bool                         pending = false;

        bool                         busy = false;
    } asyncLoopState;

    struct SPreloadTarget {
        eTargetType                     type = TARGET_IMAGE;
        std::string                     id   = "";

        void*                           data  = nullptr;
        void*                           cairo = nullptr;
        SP<Hyprgraphics::CCairoSurface> cairosurface;

        Vector2D                        size;
    };

    std::vector<UP<CScreencopyFrame>>                scframes;

    std::vector<SPreloadTarget>                      preloadTargets;
    std::mutex                                       preloadTargetsMutex;

    std::unordered_map<std::string, SPreloadedAsset> assets;

    void                                             gather();
    void                                             enqueueScreencopyFrames();
};
