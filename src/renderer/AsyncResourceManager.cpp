#include "AsyncResourceManager.hpp"

#include "./TextCmdResource.hpp"
#include "../helpers/Log.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "../core/hyprlock.hpp"

#include <functional>

using namespace Hyprgraphics;

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
    if (m_textures.contains(RESOURCEID)) {
        Debug::log(TRACE, "Text resource text:\"{}\" (resourceID: {}) already requested, incrementing refcount!", request.text, RESOURCEID);
        m_textures[RESOURCEID].refs++;
        return RESOURCEID;
    } else {
        m_textures[RESOURCEID] = {
            .texture = nullptr,
            .refs    = 1,
        };
    }

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
                g_asyncResourceManager->resourceToTexture(RESOURCEID);
                callback();
            },
            nullptr);
    });

    Debug::log(TRACE, "Enqueued text:\"{}\" (resourceID: {}) successfully.", request.text, RESOURCEID);

    return RESOURCEID;
}

size_t CAsyncResourceManager::requestTextCmd(const CTextResource::STextResourceData& request, std::function<void()> callback) {
    const auto RESOURCEID = std::hash<CTextResource::STextResourceData>{}(request);
    if (m_textures.contains(RESOURCEID)) {
        Debug::log(TRACE, "Text cmd resource text:\"{}\" (resourceID: {}) already requested, incrementing refcount!", request.text, RESOURCEID);
        m_textures[RESOURCEID].refs++;
        return RESOURCEID;
    } else {
        m_textures[RESOURCEID] = {
            .texture = nullptr,
            .refs    = 1,
        };
    }

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
                g_asyncResourceManager->resourceToTexture(RESOURCEID);
                callback();
            },
            nullptr);
    });

    Debug::log(TRACE, "Enqueued text cmd:\"{}\" (resourceID: {}) successfully.", request.text, RESOURCEID);

    return RESOURCEID;
}

size_t CAsyncResourceManager::requestImage(const std::string& path, std::function<void()> callback) {
    const auto RESOURCEID = std::hash<std::string>{}(path);
    if (m_textures.contains(RESOURCEID)) {
        Debug::log(TRACE, "Image resource image:\"{}\" (resourceID: {}) already requested, incrementing refcount!", path, RESOURCEID);
        m_textures[RESOURCEID].refs++;
        return RESOURCEID;
    } else {
        m_textures[RESOURCEID] = {
            .texture = nullptr,
            .refs    = 1,
        };
    }

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
                g_asyncResourceManager->resourceToTexture(RESOURCEID);
                callback();
            },
            nullptr);
    });

    Debug::log(TRACE, "Enqueued image:\"{}\" (resourceID: {}) successfully.", path, RESOURCEID);

    return RESOURCEID;
}

void CAsyncResourceManager::unload(ASP<CTexture> texture) {
    auto preload = std::ranges::find_if(m_textures, [texture](const auto& a) { return a.second.texture == texture; });
    if (preload == m_textures.end())
        return;

    preload->second.refs--;
    if (preload->second.refs == 0)
        m_textures.erase(preload->first);
}

void CAsyncResourceManager::unloadById(size_t id) {
    if (!m_textures.contains(id))
        return;

    m_textures.erase(id);
    m_textures[id].refs--;
    if (m_textures[id].refs == 0)
        m_textures.erase(id);
}

ASP<CTexture> CAsyncResourceManager::getAssetById(size_t id) {
    if (!m_textures.contains(id))
        return nullptr;

    return m_textures[id].texture;
}

void CAsyncResourceManager::resourceToTexture(size_t id) {
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

    m_textures[id].texture = texture;
}
