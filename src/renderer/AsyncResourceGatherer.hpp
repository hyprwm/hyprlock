#pragma once

#include "Shader.hpp"
#include "../helpers/Box.hpp"
#include "../helpers/Color.hpp"
#include "Texture.hpp"
#include <thread>
#include <atomic>
#include <vector>
#include <unordered_map>

struct SPreloadedAsset {
    CTexture texture;
};

class CAsyncResourceGatherer {
  public:
    CAsyncResourceGatherer();
    std::atomic<bool>  ready   = false;
    std::atomic<bool>  applied = false;

    std::atomic<float> progress = 0;

    SPreloadedAsset*   getAssetByID(const std::string& id);

    void               apply();

  private:
    std::thread thread;

    enum eTargetType {
        TARGET_IMAGE = 0,
    };

    struct SPreloadTarget {
        eTargetType type = TARGET_IMAGE;
        std::string id   = "";

        void*       data;
        void*       cairo;
        void*       cairosurface;

        Vector2D    size;
    };

    std::vector<SPreloadTarget>                      preloadTargets;
    std::unordered_map<std::string, SPreloadedAsset> assets;

    void                                             gather();
};