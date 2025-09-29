#include "AsyncResourceManager.hpp"

#include "./resources/TextCmdResource.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "../core/hyprlock.hpp"
#include "../config/ConfigManager.hpp"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <sys/eventfd.h>
#include <sys/poll.h>

using namespace Hyprgraphics;
using namespace Hyprutils::OS;

static inline ResourceID scopeResourceID(uint8_t scope, size_t in) {
    return (in & ~0xff) | scope;
}

ResourceID CAsyncResourceManager::resourceIDForTextRequest(const CTextResource::STextResourceData& s) {
    // TODO: Currently ignores the font string and resulting distribution is probably not perfect.
    const auto H1 = std::hash<std::string>{}(s.text);
    const auto H2 = std::hash<double>{}(s.color.asRgb().r);
    const auto H3 = std::hash<double>{}(s.color.asRgb().g) + s.fontSize;
    const auto H4 = std::hash<double>{}(s.color.asRgb().b) + s.align;

    return scopeResourceID(1, H1 ^ (H2 << 1) ^ (H3 << 2) ^ (H4 << 3));
}

ResourceID CAsyncResourceManager::resourceIDForTextCmdRequest(const CTextResource::STextResourceData& s, size_t revision) {
    return scopeResourceID(2, resourceIDForTextRequest(s) ^ (revision << 32));
}

ResourceID CAsyncResourceManager::resourceIDForImageRequest(const std::string& path, size_t revision) {
    return scopeResourceID(3, std::hash<std::string>{}(path) ^ (revision << 32));
}

ResourceID CAsyncResourceManager::resourceIDForScreencopy(const std::string& port) {
    return scopeResourceID(4, std::hash<std::string>{}(port));
}

ResourceID CAsyncResourceManager::requestText(const CTextResource::STextResourceData& params, const AWP<IWidget>& widget) {
    const auto RESOURCEID = resourceIDForTextRequest(params);
    if (request(RESOURCEID, widget)) {
        Debug::log(TRACE, "Reusing text resource \"{}\" (resourceID: {}, widget: 0x{})", params.text, RESOURCEID, (uintptr_t)widget.get());
        return RESOURCEID;
    }

    auto                                 resource = makeAtomicShared<CTextResource>(CTextResource::STextResourceData{params});
    CAtomicSharedPointer<IAsyncResource> resourceGeneric{resource};

    Debug::log(TRACE, "Requesting text resource \"{}\" (resourceID: {}, widget: 0x{})", params.text, RESOURCEID, (uintptr_t)widget.get());
    enqueue(RESOURCEID, resourceGeneric, widget);
    return RESOURCEID;
}

ResourceID CAsyncResourceManager::requestTextCmd(const CTextResource::STextResourceData& params, size_t revision, const AWP<IWidget>& widget) {
    const auto RESOURCEID = resourceIDForTextCmdRequest(params, revision);
    if (request(RESOURCEID, widget)) {
        Debug::log(TRACE, "Reusing text cmd resource \"{}\" revision {} (resourceID: {}, widget: 0x{})", params.text, revision, RESOURCEID, (uintptr_t)widget.get());
        return RESOURCEID;
    }

    auto                                 resource = makeAtomicShared<CTextCmdResource>(CTextResource::STextResourceData{params});
    CAtomicSharedPointer<IAsyncResource> resourceGeneric{resource};

    Debug::log(TRACE, "Requesting text cmd resource \"{}\" revision {} (resourceID: {}, widget: 0x{})", params.text, revision, RESOURCEID, (uintptr_t)widget.get());
    enqueue(RESOURCEID, resourceGeneric, widget);
    return RESOURCEID;
}

ResourceID CAsyncResourceManager::requestImage(const std::string& path, size_t revision, const AWP<IWidget>& widget) {
    const auto RESOURCEID = resourceIDForImageRequest(path, revision);
    if (request(RESOURCEID, widget)) {
        Debug::log(TRACE, "Reusing image resource {} revision {} (resourceID: {}, widget: 0x{})", path, revision, RESOURCEID, (uintptr_t)widget.get());
        return RESOURCEID;
    }

    auto                                 resource = makeAtomicShared<CImageResource>(absolutePath(path, ""));
    CAtomicSharedPointer<IAsyncResource> resourceGeneric{resource};

    Debug::log(TRACE, "Requesting image resource {} revision {} (resourceID: {}, widget: 0x{})", path, revision, RESOURCEID, (uintptr_t)widget.get());
    enqueue(RESOURCEID, resourceGeneric, widget);
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

            requestImage(path, 0, nullptr);
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

void CAsyncResourceManager::screencopyToTexture(const CScreencopyFrame& scFrame) {
    if (!scFrame.m_ready || !m_assets.contains(scFrame.m_resourceID)) {
        Debug::log(ERR, "Bogus call to CAsyncResourceManager::screencopyToTexture. This is a bug!");
        return;
    }

    m_assets[scFrame.m_resourceID].texture = scFrame.m_asset;

    Debug::log(TRACE, "Done sc frame {}", scFrame.m_resourceID);

    std::erase_if(m_scFrames, [&scFrame](const auto& f) { return f.get() == &scFrame; });

    if (m_scFrames.empty()) {
        Debug::log(LOG, "Gathered all screencopy frames - removing dmabuf listeners");
        g_pHyprlock->removeDmabufListener();
    }
}

void CAsyncResourceManager::gatherInitialResources(wl_display* display) {
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

bool CAsyncResourceManager::checkIdPresent(ResourceID id) {
    return m_assets.contains(id);
}

void CAsyncResourceManager::unload(ASP<CTexture> texture) {
    auto preload = std::ranges::find_if(m_assets, [texture](const auto& a) { return a.second.texture == texture; });
    if (preload == m_assets.end())
        return;

    preload->second.refs--;

    if (preload->second.refs == 0) {
        Debug::log(TRACE, "Releasing resourceID: {}!", preload->first);
        m_assets.erase(preload->first);
    }
}

void CAsyncResourceManager::unloadById(ResourceID id) {
    if (!m_assets.contains(id))
        return;

    m_assets[id].refs--;

    if (m_assets[id].refs == 0) {
        Debug::log(TRACE, "Releasing resourceID: {}!", id);
        m_assets.erase(id);
    }
}

bool CAsyncResourceManager::request(ResourceID id, const AWP<IWidget>& widget) {
    if (!m_assets.contains(id)) {
        // New asset!!
        m_assets.emplace(id, SPreloadedTexture{.texture = nullptr, .refs = 1});
        return false;
    }

    m_assets[id].refs++;

    if (m_assets[id].texture) {
        // Asset already present. Dispatch the asset callback function.
        const auto& TEXTURE = m_assets[id].texture;
        if (widget)
            g_pHyprlock->addTimer(
                std::chrono::milliseconds(0),
                [widget, TEXTURE](auto, auto) {
                    if (const auto WIDGET = widget.lock(); WIDGET)
                        WIDGET->onAssetUpdate(TEXTURE);
                },
                nullptr);
    } else if (widget) {
        // Asset currently in-flight. Add the widget reference to in order for the callback to get dispatched later.
        m_resourcesMutex.lock();
        if (!m_resources.contains(id)) {
            Debug::log(ERR, "In-flight resourceID: {} not found! This is a bug.", id);
            m_resourcesMutex.unlock();
            return true;
        }
        m_resources[id].second.emplace_back(widget);
        m_resourcesMutex.unlock();
    }

    return true;
}

void CAsyncResourceManager::enqueue(ResourceID resourceID, const ASP<IAsyncResource>& resource, const AWP<IWidget>& widget) {
    m_gatherer.enqueue(resource);

    m_resourcesMutex.lock();
    if (m_resources.contains(resourceID))
        Debug::log(ERR, "Resource already enqueued! This is a bug.");

    m_resources[resourceID] = {resource, {widget}};
    m_resourcesMutex.unlock();

    resource->m_events.finished.listenStatic([resourceID]() {
        g_pHyprlock->addTimer(std::chrono::milliseconds(0), [](auto, void* resourceID) { g_asyncResourceManager->onResourceFinished((size_t)resourceID); }, (void*)resourceID);
    });
}

void CAsyncResourceManager::onResourceFinished(ResourceID id) {
    m_resourcesMutex.lock();
    if (!m_resources.contains(id)) {
        m_resourcesMutex.unlock();
        return;
    }

    const auto RESOURCE = m_resources[id].first;
    const auto WIDGETS  = m_resources[id].second;
    m_resources.erase(id);
    m_resourcesMutex.unlock();

    if (!m_assets.contains(id) || m_assets[id].refs == 0) // Not referenced? Drop it
        return;

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
        Debug::log(ERR, "resourceID: {} invalid ({})", id, cairo_status_to_string(SURFACESTATUS));
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

    for (const auto& widget : WIDGETS) {
        if (widget)
            widget->onAssetUpdate(texture);
    }

    if (!m_gathered && !g_pHyprlock->m_bImmediateRender) {
        m_resourcesMutex.lock();
        if (m_resources.empty()) {
            m_gathered = true;
            if (m_gatheredEventfd.isValid())
                eventfd_write(m_gatheredEventfd.get(), 1);

            m_gatheredEventfd.reset();
        }
        m_resourcesMutex.unlock();
    }
}
