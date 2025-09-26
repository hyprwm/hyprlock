#pragma once

#include "../defines.hpp"
#include "./Texture.hpp"
#include "./Screencopy.hpp"
#include "./widgets/IWidget.hpp"

#include <hyprgraphics/resource/AsyncResourceGatherer.hpp>
#include <hyprgraphics/resource/resources/AsyncResource.hpp>
#include <hyprgraphics/resource/resources/TextResource.hpp>
#include <hyprgraphics/resource/resources/ImageResource.hpp>
#include <hyprutils/os/FileDescriptor.hpp>

class CAsyncResourceManager {

  public:
    // Notes on resource lifetimes:
    // Resources id's are the result of hashing the requested resource parameters.
    // When a new request is made, adding a new entry to the m_assets map is done immediatly.
    // Subsequent calls through this section with the same resource id will increment the texture's references.
    // The manager will release the resource when refs reaches 0, while the resource itelf may outlife it's reference in the manager.
    // Why not use ASP/AWP for this?
    // The problem is that we want to to increment the reference as soon as requesting the resource id.
    // Not only when actually retrieving the asset with `getAssetById`.
    //
    // Improvement idea: Make a wrapper object that is returned when requesting and contains the resource id. Then we can unload with RAII.

    // Those are hash functions that return the id for a requested resource.
    static ResourceID resourceIDForTextRequest(const CTextResource::STextResourceData& s);
    static ResourceID resourceIDForTextCmdRequest(const CTextResource::STextResourceData& s);
    // Image paths may be file system links, thus this function supports a revision parameter that gets factored into the resource id.
    static ResourceID resourceIDForImageRequest(const std::string& path, size_t revision);
    static ResourceID resourceIDForScreencopy(const std::string& port);

    struct SPreloadedTexture {
        ASP<CTexture> texture;
        size_t        refs = 0;
    };

    CAsyncResourceManager()  = default;
    ~CAsyncResourceManager() = default;

    ResourceID requestText(const CTextResource::STextResourceData& params, const AWP<IWidget>& widget);
    // Same as requestText but substitute the text with what launching sh -c request.text returns.
    ResourceID    requestTextCmd(const CTextResource::STextResourceData& params, const AWP<IWidget>& widget);
    ResourceID    requestImage(const std::string& path, size_t revision, const AWP<IWidget>& widget);

    ASP<CTexture> getAssetByID(ResourceID id);

    void          unload(ASP<CTexture> resource);
    void          unloadById(ResourceID id);

    void          enqueueStaticAssets();
    void          enqueueScreencopyFrames();
    void          screencopyToTexture(const CScreencopyFrame& scFrame);
    void          gatherInitialResources(wl_display* display);

    bool          checkIdPresent(ResourceID id);

  private:
    // Returns whether or not the id was already requested.
    // Makes sure the widgets onAssetCallback function gets called.
    bool request(ResourceID id, const AWP<IWidget>& widget);
    // Adds a new resource to m_resources and passes it to m_gatherer.
    void enqueue(ResourceID resourceID, const ASP<IAsyncResource>& resource, const AWP<IWidget>& widget);
    // Callback for finished resources.
    // Copies the resources cairo surface to a GL_TEXTURE_2D and sets it in the asset map.
    // Removes the entry in m_resources.
    // Call onAssetUpdate for all stored widget references.
    void onResourceFinished(ResourceID id);

    // For polling when using gatherInitialResources.
    bool                           m_gathered = false;
    Hyprutils::OS::CFileDescriptor m_gatheredEventfd;

    bool                           m_exit = false;

    int                            m_loadedAssets = 0;

    // not shared between threads
    std::unordered_map<ResourceID, SPreloadedTexture> m_assets;
    std::vector<UP<CScreencopyFrame>>                 m_scFrames;
    // shared between threads
    std::mutex                                                                                              m_resourcesMutex;
    std::unordered_map<ResourceID, std::pair<ASP<Hyprgraphics::IAsyncResource>, std::vector<AWP<IWidget>>>> m_resources;

    Hyprgraphics::CAsyncResourceGatherer                                                                    m_gatherer;
};

inline UP<CAsyncResourceManager> g_asyncResourceManager;
