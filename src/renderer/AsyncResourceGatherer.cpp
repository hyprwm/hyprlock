#include "AsyncResourceGatherer.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/Egl.hpp"
#include <cairo/cairo.h>
#include <algorithm>

CAsyncResourceGatherer::CAsyncResourceGatherer() {
    thread = std::thread([this]() { this->gather(); });
    thread.detach();
}

SPreloadedAsset* CAsyncResourceGatherer::getAssetByID(const std::string& id) {
    for (auto& a : assets) {
        if (a.first == id)
            return &a.second;
    }

    return nullptr;
}

void CAsyncResourceGatherer::gather() {
    const auto CWIDGETS = g_pConfigManager->getWidgetConfigs();

    g_pEGL->makeCurrent(nullptr);

    // gather resources to preload
    // clang-format off
    int preloads = std::count_if(CWIDGETS.begin(), CWIDGETS.end(), [](const auto& w) {
        return w.type == "background";
    });
    // clang-format on

    progress = 0;
    for (auto& c : CWIDGETS) {
        if (c.type == "background") {
            progress += 1.0 / (preloads + 1.0);

            std::string path = std::any_cast<Hyprlang::STRING>(c.values.at("path"));
            std::string id   = std::string{"background:"} + path;

            // preload bg img
            const auto CAIROISURFACE = cairo_image_surface_create_from_png(path.c_str());

            const auto CAIRO = cairo_create(CAIROISURFACE);
            cairo_scale(CAIRO, 1, 1);

            const auto TARGET = &preloadTargets.emplace_back(CAsyncResourceGatherer::SPreloadTarget{});

            TARGET->size = {cairo_image_surface_get_width(CAIROISURFACE), cairo_image_surface_get_height(CAIROISURFACE)};
            TARGET->type = TARGET_IMAGE;
            TARGET->id   = id;

            const auto DATA      = cairo_image_surface_get_data(CAIROISURFACE);
            TARGET->cairo        = CAIRO;
            TARGET->cairosurface = CAIROISURFACE;
            TARGET->data         = DATA;
        }
    }

    ready = true;
}

void CAsyncResourceGatherer::apply() {

    for (auto& t : preloadTargets) {
        if (t.type == TARGET_IMAGE) {
            const auto  ASSET = &assets[t.id];

            const auto  CAIROFORMAT = cairo_image_surface_get_format((cairo_surface_t*)t.cairosurface);
            const GLint glIFormat   = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_RGB32F : GL_RGBA;
            const GLint glFormat    = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_RGB : GL_RGBA;
            const GLint glType      = CAIROFORMAT == CAIRO_FORMAT_RGB96F ? GL_FLOAT : GL_UNSIGNED_BYTE;

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
        }
    }

    applied = true;
}