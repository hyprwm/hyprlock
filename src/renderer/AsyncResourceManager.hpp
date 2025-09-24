#pragma once

#include "../defines.hpp"
#include "./Texture.hpp"
#include "./Screencopy.hpp"

#include <hyprgraphics/resource/AsyncResourceGatherer.hpp>
#include <hyprgraphics/resource/resources/AsyncResource.hpp>
#include <hyprgraphics/resource/resources/TextResource.hpp>
#include <hyprgraphics/resource/resources/ImageResource.hpp>
#include <hyprutils/os/FileDescriptor.hpp>

class CAsyncResourceManager {
  public:
    // Notes on resource lifetimes:
    // Resources id's are the result of hashing the requested resource parameters.
    // When a new request is made, adding a new entry to the m_assets map is done immediatly
    // within a critical section. Subsequent passes through this section with the same resource id
    // will increment the texture's references. The manager will release the resource when refs reaches 0,
    // while the resource itelf may outlife it's reference in the manager.
    // Why not use ASP/AWP for this?
    // The problem is that we want to to increment the reference as soon as requesting the resource id.
    // Not only when actually retrieving the asset with `getAssetById`.
    // Also, this way a resource is static as long as it is not unloaded by all instances that requested it.
    // TODO:: Make a wrapper object that contains the resource id and unload with RAII.
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

    ASP<CTexture> getAssetByID(size_t id);

    void          unload(ASP<CTexture> resource);
    void          unloadById(size_t id);

    void          enqueueStaticAssets();
    void          enqueueScreencopyFrames();
    void          screencopyToTexture(const CScreencopyFrame& scFrame);
    void          gatherInitialResources(wl_display* display);

  private:
    void resourceToAsset(size_t id);
    void onScreencopyDone();

    // for polling when using gatherInitialResources
    Hyprutils::OS::CFileDescriptor m_gatheredEventfd;

    bool                           m_exit = false;

    int                            m_loadedAssets = 0;

    // not shared between threads
    std::unordered_map<size_t, SPreloadedTexture> m_assets;
    std::vector<UP<CScreencopyFrame>>             m_scFrames;
    // shared between threads
    std::mutex                                                    m_resourcesMutex;
    std::unordered_map<size_t, ASP<Hyprgraphics::IAsyncResource>> m_resources;

    Hyprgraphics::CAsyncResourceGatherer                          m_gatherer;
};

inline UP<CAsyncResourceManager> g_asyncResourceManager;
