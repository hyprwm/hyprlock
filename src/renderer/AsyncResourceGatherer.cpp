#include "AsyncResourceGatherer.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/Egl.hpp"
#include <cairo/cairo.h>
#include <magic.h>
#include <pango/pangocairo.h>
#include <algorithm>
#include <filesystem>
#include "../core/hyprlock.hpp"
#include "../helpers/MiscFunctions.hpp"
#include "../helpers/Jpeg.hpp"
#include "../helpers/Webp.hpp"
#include "src/helpers/Color.hpp"
#include "src/helpers/Log.hpp"

CAsyncResourceGatherer::CAsyncResourceGatherer() {
    if (g_pHyprlock->getScreencopy())
        enqueueDMAFrames();

    initialGatherThread = std::thread([this]() { this->gather(); });
    initialGatherThread.detach();

    asyncLoopThread = std::thread([this]() { this->asyncAssetSpinLock(); });
    asyncLoopThread.detach();
}

void CAsyncResourceGatherer::enqueueDMAFrames() {
    // some things can't be done async :(
    // gather background textures when needed

    const auto               CWIDGETS = g_pConfigManager->getWidgetConfigs();

    std::vector<std::string> mons;

    for (auto& c : CWIDGETS) {
        if (c.type != "background")
            continue;

        if (std::string{std::any_cast<Hyprlang::STRING>(c.values.at("path"))} != "screenshot")
            continue;

        // mamma mia
        if (c.monitor.empty()) {
            mons.clear();
            for (auto& m : g_pHyprlock->m_vOutputs) {
                mons.push_back(m->stringPort);
            }
            break;
        } else
            mons.push_back(c.monitor);
    }

    for (auto& mon : mons) {
        const auto MON = std::find_if(g_pHyprlock->m_vOutputs.begin(), g_pHyprlock->m_vOutputs.end(),
                                      [mon](const auto& other) { return other->stringPort == mon || other->stringDesc.starts_with(mon); });

        if (MON == g_pHyprlock->m_vOutputs.end())
            continue;

        const auto PMONITOR = MON->get();

        dmas.emplace_back(std::make_unique<CDMAFrame>(PMONITOR));
    }
}

SPreloadedAsset* CAsyncResourceGatherer::getAssetByID(const std::string& id) {
    for (auto& a : assets) {
        if (a.first == id)
            return &a.second;
    }

    if (apply()) {
        for (auto& a : assets) {
            if (a.first == id)
                return &a.second;
        }
    };

    for (auto& dma : dmas) {
        if (id == dma->resourceID)
            return dma->asset.ready ? &dma->asset : nullptr;
    }

    return nullptr;
}

enum class FileType {
    PNG,
    JPEG,
    WEBP,
    UNKNOWN,
};

FileType getFileType(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    // convert the extension to lower case
    std::transform(ext.begin(), ext.end(), ext.begin(), [](char c) { return c <= 'Z' && c >= 'A' ? c - ('Z' - 'z') : c; });

    FileType ft = FileType::UNKNOWN;
    Debug::log(TRACE, "Extension: {}", ext);
    if (ext == ".png")
        ft = FileType::PNG;
    else if (ext == ".jpg" || ext == ".jpeg")
        ft = FileType::JPEG;
    else if (ext == ".webp")
        ft = FileType::WEBP;
    else {
        // magic is slow, so only use it when no recognized extension is found
        auto handle = magic_open(MAGIC_NONE | MAGIC_COMPRESS);
        magic_load(handle, nullptr);

        const auto type_str   = std::string(magic_file(handle, path.c_str()));
        const auto first_word = type_str.substr(0, type_str.find(" "));
        magic_close(handle);

        if (first_word == "PNG")
            ft = FileType::PNG;
        else if (first_word == "JPEG")
            ft = FileType::JPEG;
        else if (first_word == "RIFF" && type_str.find("Web/P image") != std::string::npos)
            ft = FileType::WEBP;
    }

    return ft;
}

cairo_surface_t* getCairoSurfaceFromImageFile(const std::filesystem::path& path) {
    cairo_surface_t* cairoSurface = nullptr;
    switch (getFileType(path)) {
        case FileType::PNG: cairoSurface = cairo_image_surface_create_from_png(path.c_str()); break;
        case FileType::JPEG: cairoSurface = JPEG::createSurfaceFromJPEG(path); break;
        case FileType::WEBP: cairoSurface = WEBP::createSurfaceFromWEBP(path); break;
        default: Debug::log(ERR, "unrecognized image format of {}", path.c_str());
    }

    return cairoSurface;
}

void CAsyncResourceGatherer::gather() {
    const auto CWIDGETS = g_pConfigManager->getWidgetConfigs();

    g_pEGL->makeCurrent(nullptr);

    // gather resources to preload
    // clang-format off
    int preloads = std::count_if(CWIDGETS.begin(), CWIDGETS.end(), [](const auto& w) {
        return w.type == "background" || w.type == "image";
    });
    // clang-format on

    progress = 0;
    for (auto& c : CWIDGETS) {
        if (c.type == "background" || c.type == "image") {
#if defined(_LIBCPP_VERSION) && _LIBCPP_VERSION < 180100
            progress = progress + 1.0 / (preloads + 1.0);
#else
            progress += 1.0 / (preloads + 1.0);
#endif

            std::string path = std::any_cast<Hyprlang::STRING>(c.values.at("path"));

            if (path.empty() || path == "screenshot")
                continue;

            std::string id = (c.type == "background" ? std::string{"background:"} : std::string{"image:"}) + path;

            // render the image directly, since we are in a seperate thread
            CAsyncResourceGatherer::SPreloadRequest rq;
            rq.type  = CAsyncResourceGatherer::TARGET_IMAGE;
            rq.asset = path;
            rq.id    = id;

            renderImage(rq);
        }
    }

    while (std::any_of(dmas.begin(), dmas.end(), [](const auto& d) { return !d->asset.ready; })) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    gathered = true;
}

bool CAsyncResourceGatherer::apply() {
    preloadTargetsMutex.lock();

    if (preloadTargets.empty()) {
        preloadTargetsMutex.unlock();
        return false;
    }

    auto currentPreloadTargets = preloadTargets;
    preloadTargets.clear();
    preloadTargetsMutex.unlock();

    for (auto& t : currentPreloadTargets) {
        if (t.type == TARGET_IMAGE) {
            const auto  ASSET = &assets[t.id];

            const auto  SURFACESTATUS = cairo_surface_status((cairo_surface_t*)t.cairosurface);
            const auto  CAIROFORMAT   = cairo_image_surface_get_format((cairo_surface_t*)t.cairosurface);
            const GLint glIFormat     = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_RGB32F : GL_RGBA;
            const GLint glFormat      = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_RGB : GL_RGBA;
            const GLint glType        = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_FLOAT : GL_UNSIGNED_BYTE;

            if (SURFACESTATUS != CAIRO_STATUS_SUCCESS) {
                Debug::log(ERR, "Resource {} invalid ({})", t.id, cairo_status_to_string(SURFACESTATUS));
                ASSET->texture.m_iType = TEXTURE_INVALID;
            }

            ASSET->texture.m_vSize = t.size;
            ASSET->texture.allocate();

            glBindTexture(GL_TEXTURE_2D, ASSET->texture.m_iTexID);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            if (CAIROFORMAT != CAIRO_FORMAT_RGB96F) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_R, GL_BLUE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_B, GL_RED);
            }
            glTexImage2D(GL_TEXTURE_2D, 0, glIFormat, ASSET->texture.m_vSize.x, ASSET->texture.m_vSize.y, 0, glFormat, glType, t.data);

            cairo_destroy((cairo_t*)t.cairo);
            cairo_surface_destroy((cairo_surface_t*)t.cairosurface);

        } else {
            Debug::log(ERR, "Unsupported type in ::apply() {}", (int)t.type);
        }
    }

    return true;
}

void CAsyncResourceGatherer::renderImage(const SPreloadRequest& rq) {
    SPreloadTarget target;
    target.type = TARGET_IMAGE;
    target.id   = rq.id;

    std::filesystem::path ABSOLUTEPATH(absolutePath(rq.asset, ""));
    const auto            CAIROISURFACE = getCairoSurfaceFromImageFile(ABSOLUTEPATH);

    if (!CAIROISURFACE) {
        Debug::log(ERR, "No cairo surface!");
        return;
    }

    const auto CAIRO = cairo_create(CAIROISURFACE);
    cairo_scale(CAIRO, 1, 1);

    target.cairo        = CAIRO;
    target.cairosurface = CAIROISURFACE;
    target.data         = cairo_image_surface_get_data(CAIROISURFACE);
    target.size         = {(double)cairo_image_surface_get_width(CAIROISURFACE), (double)cairo_image_surface_get_height(CAIROISURFACE)};

    std::lock_guard lg{preloadTargetsMutex};
    preloadTargets.push_back(target);
}

void CAsyncResourceGatherer::renderText(const SPreloadRequest& rq) {
    SPreloadTarget target;
    target.type = TARGET_IMAGE; /* text is just an image lol */
    target.id   = rq.id;

    const int          FONTSIZE   = rq.props.contains("font_size") ? std::any_cast<int>(rq.props.at("font_size")) : 16;
    const CColor       FONTCOLOR  = rq.props.contains("color") ? std::any_cast<CColor>(rq.props.at("color")) : CColor(1.0, 1.0, 1.0, 1.0);
    const std::string  FONTFAMILY = rq.props.contains("font_family") ? std::any_cast<std::string>(rq.props.at("font_family")) : "Sans";
    const bool         ISCMD      = rq.props.contains("cmd") ? std::any_cast<bool>(rq.props.at("cmd")) : false;

    static auto* const TRIM = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:text_trim");
    std::string        TEXT = ISCMD ? g_pHyprlock->spawnSync(rq.asset) : rq.asset;

    if (**TRIM) {
        TEXT.erase(0, TEXT.find_first_not_of(" \n\r\t"));
        TEXT.erase(TEXT.find_last_not_of(" \n\r\t") + 1);
    }

    auto CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080 /* dummy value */);
    auto CAIRO        = cairo_create(CAIROSURFACE);

    // draw title using Pango
    PangoLayout*          layout = pango_cairo_create_layout(CAIRO);

    PangoFontDescription* fontDesc = pango_font_description_from_string(FONTFAMILY.c_str());
    pango_font_description_set_size(fontDesc, FONTSIZE * PANGO_SCALE);
    pango_layout_set_font_description(layout, fontDesc);
    pango_font_description_free(fontDesc);

    if (rq.props.contains("text_align")) {
        const std::string TEXTALIGN = std::any_cast<std::string>(rq.props.at("text_align"));
        PangoAlignment    align     = PANGO_ALIGN_LEFT;
        if (TEXTALIGN == "center")
            align = PANGO_ALIGN_CENTER;
        else if (TEXTALIGN == "right")
            align = PANGO_ALIGN_RIGHT;

        pango_layout_set_alignment(layout, align);
    }

    PangoAttrList* attrList = nullptr;
    GError*        gError   = nullptr;
    char*          buf      = nullptr;
    if (pango_parse_markup(TEXT.c_str(), -1, 0, &attrList, &buf, nullptr, &gError))
        pango_layout_set_text(layout, buf, -1);
    else {
        Debug::log(ERR, "Pango markup parsing for {} failed: {}", TEXT, gError->message);
        g_error_free(gError);
        pango_layout_set_text(layout, TEXT.c_str(), -1);
    }

    if (!attrList)
        attrList = pango_attr_list_new();

    if (buf)
        free(buf);

    pango_attr_list_insert(attrList, pango_attr_scale_new(1));
    pango_layout_set_attributes(layout, attrList);
    pango_attr_list_unref(attrList);

    int layoutWidth, layoutHeight;
    pango_layout_get_size(layout, &layoutWidth, &layoutHeight);

    // TODO: avoid this?
    cairo_destroy(CAIRO);
    cairo_surface_destroy(CAIROSURFACE);
    CAIROSURFACE = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, layoutWidth / PANGO_SCALE, layoutHeight / PANGO_SCALE);
    CAIRO        = cairo_create(CAIROSURFACE);

    // clear the pixmap
    cairo_save(CAIRO);
    cairo_set_operator(CAIRO, CAIRO_OPERATOR_CLEAR);
    cairo_paint(CAIRO);
    cairo_restore(CAIRO);

    // render the thing
    cairo_set_source_rgba(CAIRO, FONTCOLOR.r, FONTCOLOR.g, FONTCOLOR.b, FONTCOLOR.a);

    cairo_move_to(CAIRO, 0, 0);
    pango_cairo_show_layout(CAIRO, layout);

    g_object_unref(layout);

    cairo_surface_flush(CAIROSURFACE);

    target.cairo        = CAIRO;
    target.cairosurface = CAIROSURFACE;
    target.data         = cairo_image_surface_get_data(CAIROSURFACE);
    target.size         = {layoutWidth / (double)PANGO_SCALE, layoutHeight / (double)PANGO_SCALE};

    std::lock_guard lg{preloadTargetsMutex};
    preloadTargets.push_back(target);
}

struct STimerCallbackData {
    void (*cb)(void*) = nullptr;
    void* data        = nullptr;
};

static void timerCallback(std::shared_ptr<CTimer> self, void* data_) {
    auto data = (STimerCallbackData*)data_;
    data->cb(data->data);
    delete data;
}

void CAsyncResourceGatherer::asyncAssetSpinLock() {
    while (!g_pHyprlock->m_bTerminate) {

        std::unique_lock lk(asyncLoopState.requestsMutex);
        if (asyncLoopState.pending == false) // avoid a lock if a thread managed to request something already since we .unlock()ed
            asyncLoopState.requestsCV.wait_for(lk, std::chrono::seconds(5), [this] { return asyncLoopState.pending; }); // wait for events

        asyncLoopState.pending = false;

        if (asyncLoopState.requests.empty()) {
            lk.unlock();
            continue;
        }

        auto requests = asyncLoopState.requests;
        asyncLoopState.requests.clear();

        lk.unlock();

        // process requests
        for (auto& r : requests) {
            Debug::log(TRACE, "Processing requested resourceID {}", r.id);

            if (r.type == TARGET_TEXT) {
                renderText(r);
            } else if (r.type == TARGET_IMAGE) {
                renderImage(r);
            } else {
                Debug::log(ERR, "Unsupported async preload type {}??", (int)r.type);
                continue;
            }

            // plant timer for callback
            if (r.callback)
                g_pHyprlock->addTimer(std::chrono::milliseconds(0), timerCallback, new STimerCallbackData{r.callback, r.callbackData});
        }
    }
}

void CAsyncResourceGatherer::requestAsyncAssetPreload(const SPreloadRequest& request) {
    Debug::log(TRACE, "Requesting label resource {}", request.id);

    std::lock_guard<std::mutex> lg(asyncLoopState.requestsMutex);
    asyncLoopState.requests.push_back(request);
    asyncLoopState.pending = true;
    asyncLoopState.requestsCV.notify_all();
}

void CAsyncResourceGatherer::unloadAsset(SPreloadedAsset* asset) {
    std::erase_if(assets, [asset](const auto& a) { return &a.second == asset; });
}

void CAsyncResourceGatherer::notify() {
    std::lock_guard<std::mutex> lg(asyncLoopState.requestsMutex);
    asyncLoopState.requests.clear();
    asyncLoopState.pending = true;
    asyncLoopState.requestsCV.notify_all();
}

void CAsyncResourceGatherer::await() {
    if (initialGatherThread.joinable())
        initialGatherThread.join();
    if (asyncLoopThread.joinable())
        asyncLoopThread.join();
}
