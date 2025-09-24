#include "AsyncResourceManager.hpp"

#include "./resources/TextCmdResource.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "../core/hyprlock.hpp"
#include "../config/ConfigManager.hpp"

#include <algorithm>
#include <functional>
#include <sys/eventfd.h>
#include <sys/poll.h>

using namespace Hyprgraphics;
using namespace Hyprutils::OS;

template <>
struct std::hash<CTextResource::STextResourceData> {
    std::size_t operator()(CTextResource::STextResourceData const& s) const noexcept {
        const auto H1 = std::hash<std::string>{}(s.text);
        const auto H2 = std::hash<double>{}(s.color.asRgb().r);
        const auto H3 = std::hash<double>{}(s.color.asRgb().g);
        const auto H4 = std::hash<double>{}(s.color.asRgb().b);

        return H1 ^ H2 ^ H3 ^ H4;
    }
};

size_t CAsyncResourceManager::requestText(const CTextResource::STextResourceData& request, std::function<void()> callback) {
    const auto RESOURCEID = std::hash<CTextResource::STextResourceData>{}(request);
    if (m_assets.contains(RESOURCEID)) {
        Debug::log(TRACE, "Text resource text:\"{}\" (resourceID: {}) already requested, incrementing refcount!", request.text, RESOURCEID);
        m_assets[RESOURCEID].refs++;
        return RESOURCEID;
    } else
        m_assets.emplace(RESOURCEID, SPreloadedTexture{.texture = nullptr, .refs = 1});

    auto                                 resource = makeAtomicShared<CTextResource>(CTextResource::STextResourceData{request});
    CAtomicSharedPointer<IAsyncResource> resourceGeneric{resource};

    m_gatherer.enqueue(resourceGeneric);

    m_resourcesMutex.lock();
    if (m_resources.contains(RESOURCEID)) {
        m_resourcesMutex.unlock();
        return RESOURCEID;
    }
    m_resources[RESOURCEID] = std::move(resourceGeneric);
    m_resourcesMutex.unlock();

    resource->m_events.finished.listenStatic([RESOURCEID, callback]() {
        g_pHyprlock->addTimer(
            std::chrono::milliseconds(0),
            [RESOURCEID, callback](auto, auto) {
                g_asyncResourceManager->resourceToAsset(RESOURCEID);
                callback();
            },
            nullptr);
    });

    Debug::log(TRACE, "Enqueued text:\"{}\" (resourceID: {}) successfully.", request.text, RESOURCEID);

    return RESOURCEID;
}

size_t CAsyncResourceManager::requestTextCmd(const CTextResource::STextResourceData& request, std::function<void()> callback) {
    const auto RESOURCEID = std::hash<CTextResource::STextResourceData>{}(request);
    if (m_assets.contains(RESOURCEID)) {
        Debug::log(TRACE, "Text cmd resource text:\"{}\" (resourceID: {}) already requested, incrementing refcount!", request.text, RESOURCEID);
        m_assets[RESOURCEID].refs++;
        return RESOURCEID;
    } else
        m_assets.emplace(RESOURCEID, SPreloadedTexture{.texture = nullptr, .refs = 1});

    auto                                 resource = makeAtomicShared<CTextCmdResource>(CTextResource::STextResourceData{request});
    CAtomicSharedPointer<IAsyncResource> resourceGeneric{resource};

    m_gatherer.enqueue(resourceGeneric);

    m_resourcesMutex.lock();
    if (m_resources.contains(RESOURCEID))
        Debug::log(ERR, "Resource already enqueued! This is a bug.");

    m_resources[RESOURCEID] = std::move(resourceGeneric);
    m_resourcesMutex.unlock();

    resource->m_events.finished.listenStatic([RESOURCEID, callback]() {
        g_pHyprlock->addTimer(
            std::chrono::milliseconds(0),
            [RESOURCEID, callback](auto, auto) {
                g_asyncResourceManager->resourceToAsset(RESOURCEID);
                callback();
            },
            nullptr);
    });

    Debug::log(TRACE, "Enqueued text cmd:\"{}\" (resourceID: {}) successfully.", request.text, RESOURCEID);

    return RESOURCEID;
}

size_t CAsyncResourceManager::requestImage(const std::string& path, std::function<void()> callback) {
    const auto RESOURCEID = std::hash<std::string>{}(absolutePath(path, ""));
    if (m_assets.contains(RESOURCEID)) {
        Debug::log(TRACE, "Image resource image:\"{}\" (resourceID: {}) already requested, incrementing refcount!", path, RESOURCEID);
        m_assets[RESOURCEID].refs++;
        return RESOURCEID;
    } else
        m_assets.emplace(RESOURCEID, SPreloadedTexture{.texture = nullptr, .refs = 1});

    auto                                 resource = makeAtomicShared<CImageResource>(absolutePath(path, ""));
    CAtomicSharedPointer<IAsyncResource> resourceGeneric{resource};

    m_gatherer.enqueue(resourceGeneric);

    m_resourcesMutex.lock();
    if (m_resources.contains(RESOURCEID))
        Debug::log(ERR, "Resource already enqueued! This is a bug.");

    m_resources[RESOURCEID] = std::move(resourceGeneric);
    m_resourcesMutex.unlock();

    resource->m_events.finished.listenStatic([RESOURCEID, callback]() {
        g_pHyprlock->addTimer(
            std::chrono::milliseconds(0),
            [RESOURCEID, callback](auto, auto) {
                Debug::log(LOG, "CALLBACK!!!");
                g_asyncResourceManager->resourceToAsset(RESOURCEID);
                callback();
            },
            nullptr);
    });

    Debug::log(TRACE, "Enqueued image:\"{}\" (resourceID: {}) successfully.", path, RESOURCEID);

    return RESOURCEID;
}

ASP<CTexture> CAsyncResourceManager::getAssetByID(size_t id) {
    if (!m_assets.contains(id))
        return nullptr;

    return m_assets[id].texture;
}

void CAsyncResourceManager::enqueueStaticAssets() {
    const auto CWIDGETS = g_pConfigManager->getWidgetConfigs();

    for (auto& c : CWIDGETS) {
        if (c.type == "background" || c.type == "image") {
            std::string path = std::any_cast<Hyprlang::STRING>(c.values.at("path"));

            if (path.empty() || path == "screenshot")
                continue;

            requestImage(path, [this]() {
                if (!g_pHyprlock->m_bImmediateRender && m_resources.empty()) {
                    if (m_gatheredEventfd.isValid())
                        eventfd_write(m_gatheredEventfd.get(), 1);
                }
            });
        }
    }
}

void CAsyncResourceManager::enqueueScreencopyFrames() {
    if (g_pHyprlock->m_vOutputs.empty())
        return;

    static const auto ANIMATIONSENABLED = g_pConfigManager->getValue<Hyprlang::INT>("animations:enabled");

    const auto        FADEINCFG  = g_pConfigManager->m_AnimationTree.getConfig("fadeIn");
    const auto        FADEOUTCFG = g_pConfigManager->m_AnimationTree.getConfig("fadeOut");

    const bool        FADENEEDSSC = *ANIMATIONSENABLED &&
        ((FADEINCFG->pValues && FADEINCFG->pValues->internalEnabled) || // fadeIn or fadeOut enabled
         (FADEOUTCFG->pValues && FADEOUTCFG->pValues->internalEnabled));

    const auto BGSCREENSHOT = std::ranges::any_of(g_pConfigManager->getWidgetConfigs(), [](const auto& w) { //
        return w.type == "background" && std::string{std::any_cast<Hyprlang::STRING>(w.values.at("path"))} == "screenshot";
    });

    if (!BGSCREENSHOT && !FADENEEDSSC) {
        Debug::log(LOG, "Skipping screencopy");
        return;
    }

    for (const auto& MON : g_pHyprlock->m_vOutputs) {
        m_scFrames.emplace_back(makeUnique<CScreencopyFrame>());
        auto* frame = m_scFrames.back().get();
        frame->capture(MON);
        m_assets.emplace(frame->m_resourceID, SPreloadedTexture{.texture = nullptr, .refs = 1});
    }
}

void CAsyncResourceManager::onScreencopyDone() {
    // We are done with screencopy.
    Debug::log(TRACE, "Gathered all screencopy frames - removing dmabuf listeners");
    g_pHyprlock->removeDmabufListener();
}

void CAsyncResourceManager::gatherInitialResources(wl_display* display) {
    // Gather background resources and screencopy frames before locking the screen.
    // We need to do this because as soon as we lock the screen, workspaces frames can no longer be captured. It either won't work at all, or we will capture hyprlock itself.
    // Bypass with --immediate-render (can cause the background first rendering a solid color and missing or inaccurate screencopy frames)
    const auto MAXDELAYMS    = 2000; // 2 Seconds
    const auto STARTGATHERTP = std::chrono::system_clock::now();

    m_gatheredEventfd = CFileDescriptor{eventfd(0, EFD_CLOEXEC)};

    int    fdcount = 1;
    pollfd pollfds[2];
    pollfds[0] = {
        .fd     = wl_display_get_fd(display),
        .events = POLLIN,
    };

    if (m_gatheredEventfd.isValid()) {
        pollfds[1] = {
            .fd     = m_gatheredEventfd.get(),
            .events = POLLIN,
        };

        fdcount++;
    }

    bool gathered = false;
    while (!gathered) {
        wl_display_flush(display);
        if (wl_display_prepare_read(display) == 0) {
            if (poll(pollfds, fdcount, /* 100ms timeout */ 100) < 0) {
                RASSERT(errno == EINTR, "[core] Polling fds failed with {}", errno);
                wl_display_cancel_read(display);
                continue;
            }
            wl_display_read_events(display);
            wl_display_dispatch_pending(display);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            wl_display_dispatch(display);
        }

        g_pHyprlock->processTimers();

        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - STARTGATHERTP).count() > MAXDELAYMS) {
            Debug::log(WARN, "Gathering resources timed out after {} milliseconds. Backgrounds may be delayed and render `background:color` at first.", MAXDELAYMS);
            break;
        }

        gathered = m_resources.empty() && m_scFrames.empty();
    }

    Debug::log(LOG, "Resources gathered after {} milliseconds", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - STARTGATHERTP).count());
}

void CAsyncResourceManager::unload(ASP<CTexture> texture) {
    auto preload = std::ranges::find_if(m_assets, [texture](const auto& a) { return a.second.texture == texture; });
    if (preload == m_assets.end())
        return;

    preload->second.refs--;
    if (preload->second.refs == 0)
        m_assets.erase(preload->first);
}

void CAsyncResourceManager::unloadById(size_t id) {
    if (!m_assets.contains(id))
        return;

    m_assets.erase(id);
    m_assets[id].refs--;
    if (m_assets[id].refs == 0)
        m_assets.erase(id);
}

void CAsyncResourceManager::resourceToAsset(size_t id) {
    if (!m_resources.contains(id))
        return;

    m_resourcesMutex.lock();
    const auto RESOURCE = m_resources[id];
    m_resources.erase(id);
    m_resourcesMutex.unlock();

    if (!RESOURCE || !RESOURCE->m_asset.cairoSurface)
        return;

    Debug::log(TRACE, "Resource to texture id:{}", id);

    const auto           texture = makeAtomicShared<CTexture>();

    const cairo_status_t SURFACESTATUS = (cairo_status_t)RESOURCE->m_asset.cairoSurface->status();
    const auto           CAIROFORMAT   = cairo_image_surface_get_format(RESOURCE->m_asset.cairoSurface->cairo());
    const GLint          glIFormat     = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_RGB32F : GL_RGBA;
    const GLint          glFormat      = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_RGB : GL_RGBA;
    const GLint          glType        = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_FLOAT : GL_UNSIGNED_BYTE;

    if (SURFACESTATUS != CAIRO_STATUS_SUCCESS) {
        Debug::log(ERR, "RESOURCE {} invalid ({})", id, cairo_status_to_string(SURFACESTATUS));
        texture->m_iType = TEXTURE_INVALID;
    }

    texture->m_vSize = RESOURCE->m_asset.pixelSize;
    texture->allocate();

    glBindTexture(GL_TEXTURE_2D, texture->m_iTexID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    if (CAIROFORMAT != CAIRO_FORMAT_RGB96F) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, glIFormat, texture->m_vSize.x, texture->m_vSize.y, 0, glFormat, glType, RESOURCE->m_asset.cairoSurface->data());

    m_assets[id].texture = texture;
}

void CAsyncResourceManager::screencopyToTexture(const CScreencopyFrame& scFrame) {
    RASSERT(scFrame.m_ready && m_assets.contains(scFrame.m_resourceID), "Logic error in screencopy gathering.");
    m_assets[scFrame.m_resourceID].texture = scFrame.m_asset;

    std::erase_if(m_scFrames, [&scFrame](const auto& f) { return f.get() == &scFrame; });

    Debug::log(LOG, "Done sc frame {}", scFrame.m_resourceID);
    if (m_scFrames.empty())
        onScreencopyDone();
}
