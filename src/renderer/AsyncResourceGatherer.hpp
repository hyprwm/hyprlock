#pragma once

#include "Shader.hpp"
#include "../helpers/Box.hpp"
#include "../helpers/Color.hpp"
#include "DMAFrame.hpp"
#include "Texture.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>
#include <condition_variable>
#include <any>
#include "Shared.hpp"

class CAsyncResourceGatherer {
  public:
    CAsyncResourceGatherer();
    std::atomic<bool>  ready   = false;
    std::atomic<bool>  applied = false;

    std::atomic<float> progress = 0;

    /* only call from ogl thread */
    SPreloadedAsset* getAssetByID(const std::string& id);

    void             apply();

    enum eTargetType {
        TARGET_IMAGE = 0,
        TARGET_TEXT
    };

    struct SPreloadRequest {
        eTargetType                               type;
        std::string                               asset;
        std::string                               id;

        std::unordered_map<std::string, std::any> props;
    };

    void requestAsyncAssetPreload(const SPreloadRequest& request);
    void unloadAsset(SPreloadedAsset* asset);
    void notify();
    void await();

  private:
    std::thread initThread;
    std::thread asyncLoopThread;

    void        asyncAssetSpinLock();
    void        renderText(const SPreloadRequest& rq);

    struct {
        std::condition_variable      loopGuard;
        std::mutex                   loopMutex;

        std::mutex                   requestMutex;

        std::mutex                   assetsMutex;

        std::vector<SPreloadRequest> requests;
        bool                         pending = false;

        bool                         busy = false;
    } asyncLoopState;

    struct SPreloadTarget {
        eTargetType type = TARGET_IMAGE;
        std::string id   = "";

        void*       data;
        void*       cairo;
        void*       cairosurface;

        Vector2D    size;
    };

    std::vector<std::unique_ptr<CDMAFrame>>          dmas;

    std::vector<SPreloadTarget>                      preloadTargets;
    std::unordered_map<std::string, SPreloadedAsset> assets;

    void                                             gather();
};