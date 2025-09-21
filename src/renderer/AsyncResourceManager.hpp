#pragma once

#include "../defines.hpp"
#include "./Texture.hpp"

#include <hyprgraphics/resource/AsyncResourceGatherer.hpp>
#include <hyprgraphics/resource/resources/AsyncResource.hpp>
#include <hyprgraphics/resource/resources/TextResource.hpp>
#include <hyprgraphics/resource/resources/ImageResource.hpp>

class CAsyncResourceManager {
  public:
    struct SPreloadedTexture {
        ASP<CTexture> texture;
        size_t        refs = 0;
    };

    CAsyncResourceManager()  = default;
    ~CAsyncResourceManager() = default;

    // requesting an asset returns a unique id used to retrieve it later
    size_t requestText(const CTextResource::STextResourceData& request, std::function<void()> callback);
    // same as requestText but substitute the text with what launching sh -c request.text returns
    size_t        requestTextCmd(const CTextResource::STextResourceData& request, std::function<void()> callback);
    size_t        requestImage(const std::string& path, std::function<void()> callback);

    ASP<CTexture> getAssetById(size_t id);

    void          unload(ASP<CTexture> resource);
    void          unloadById(size_t id);

  private:
    bool apply();
    void resourceToTexture(size_t id);

    //std::mutex                                                    m_wakeupMutex;
    //std::condition_variable                                       m_wakeup;

    bool m_exit = false;

    int  m_loadedAssets = 0;

    // shared between threads
    std::mutex                                                    m_resourcesMutex;
    std::unordered_map<size_t, ASP<Hyprgraphics::IAsyncResource>> m_resources;
    // not shared between threads
    std::unordered_map<size_t, SPreloadedTexture> m_textures;

    Hyprgraphics::CAsyncResourceGatherer          m_gatherer;
};

inline UP<CAsyncResourceManager> g_asyncResourceManager;
