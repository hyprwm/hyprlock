#include "DMAFrame.hpp"
#include "linux-dmabuf-unstable-v1-protocol.h"
#include "wlr-screencopy-unstable-v1-protocol.h"
#include "../helpers/Log.hpp"
#include "../core/hyprlock.hpp"
#include "../core/Egl.hpp"
#include <EGL/eglext.h>
#include <libdrm/drm_fourcc.h>
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>
#include <unistd.h>

static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = nullptr;
static PFNEGLQUERYDMABUFMODIFIERSEXTPROC   eglQueryDmaBufModifiersEXT   = nullptr;

//
static void wlrOnBuffer(void* data, zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height, uint32_t stride) {
    const auto PDATA = (SScreencopyData*)data;

    Debug::log(TRACE, "[sc] wlrOnBuffer for {}", (void*)PDATA);

    PDATA->size   = stride * height;
    PDATA->stride = stride;
}

static void wlrOnFlags(void* data, zwlr_screencopy_frame_v1* frame, uint32_t flags) {
    ;
}

static void wlrOnReady(void* data, zwlr_screencopy_frame_v1* frame, uint32_t tv_sec_hi, uint32_t tv_sec_lo, uint32_t tv_nsec) {
    const auto PDATA = (SScreencopyData*)data;

    Debug::log(TRACE, "[sc] wlrOnReady for {}", (void*)PDATA);

    if (!PDATA->frame->onBufferReady()) {
        Debug::log(ERR, "onBufferReady failed");
        return;
    }

    zwlr_screencopy_frame_v1_destroy(frame);
}

static void wlrOnFailed(void* data, zwlr_screencopy_frame_v1* frame) {
    ;
}

static void wlrOnDamage(void* data, zwlr_screencopy_frame_v1* frame, uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    ;
}

static void wlrOnDmabuf(void* data, zwlr_screencopy_frame_v1* frame, uint32_t format, uint32_t width, uint32_t height) {
    const auto PDATA = (SScreencopyData*)data;

    Debug::log(TRACE, "[sc] wlrOnDmabuf for {}", (void*)PDATA);

    PDATA->w   = width;
    PDATA->h   = height;
    PDATA->fmt = format;

    Debug::log(TRACE, "[sc] DMABUF format reported: {:x}", format);
}

static void wlrOnBufferDone(void* data, zwlr_screencopy_frame_v1* frame) {
    const auto PDATA = (SScreencopyData*)data;

    Debug::log(TRACE, "[sc] wlrOnBufferDone for {}", (void*)PDATA);

    if (!PDATA->frame->onBufferDone()) {
        Debug::log(ERR, "onBufferDone failed");
        return;
    }

    zwlr_screencopy_frame_v1_copy(frame, PDATA->frame->wlBuffer);

    Debug::log(TRACE, "[sc] wlr frame copied");
}

static const zwlr_screencopy_frame_v1_listener wlrFrameListener = {
    .buffer       = wlrOnBuffer,
    .flags        = wlrOnFlags,
    .ready        = wlrOnReady,
    .failed       = wlrOnFailed,
    .damage       = wlrOnDamage,
    .linux_dmabuf = wlrOnDmabuf,
    .buffer_done  = wlrOnBufferDone,
};

std::string CDMAFrame::getResourceId(COutput* output) {
    return std::format("dma:{}-{}x{}", output->stringPort, output->size.x, output->size.y);
}

CDMAFrame::CDMAFrame(COutput* output_) {
    resourceID = getResourceId(output_);

    if (!glEGLImageTargetTexture2DOES) {
        glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
        if (!glEGLImageTargetTexture2DOES) {
            Debug::log(ERR, "No glEGLImageTargetTexture2DOES??");
            return;
        }
    }

    if (!eglQueryDmaBufModifiersEXT)
        eglQueryDmaBufModifiersEXT = (PFNEGLQUERYDMABUFMODIFIERSEXTPROC)eglGetProcAddress("eglQueryDmaBufModifiersEXT");

    // firstly, plant a listener for the frame
    frameCb = zwlr_screencopy_manager_v1_capture_output(g_pHyprlock->getScreencopy(), false, output_->output);

    scdata.frame = this;

    zwlr_screencopy_frame_v1_add_listener(frameCb, &wlrFrameListener, &scdata);
}

CDMAFrame::~CDMAFrame() {
    if (g_pEGL)
        eglDestroyImage(g_pEGL->eglDisplay, image);

    // leaks bo and stuff but lives throughout so for now who cares
}

bool CDMAFrame::onBufferDone() {
    uint32_t flags = GBM_BO_USE_RENDERING;

    if (!eglQueryDmaBufModifiersEXT) {
        Debug::log(WARN, "Querying modifiers without eglQueryDmaBufModifiersEXT support");
        bo = gbm_bo_create(g_pHyprlock->dma.gbmDevice, scdata.w, scdata.h, scdata.fmt, flags);
    } else {
        std::vector<uint64_t> mods;
        mods.resize(64);
        std::vector<EGLBoolean> externalOnly;
        externalOnly.resize(64);
        int num = 0;
        if (!eglQueryDmaBufModifiersEXT(g_pEGL->eglDisplay, scdata.fmt, 64, mods.data(), externalOnly.data(), &num) || num == 0) {
            Debug::log(WARN, "eglQueryDmaBufModifiersEXT failed, falling back to regular bo");
            bo = gbm_bo_create(g_pHyprlock->dma.gbmDevice, scdata.w, scdata.h, scdata.fmt, flags);
        } else {
            Debug::log(LOG, "eglQueryDmaBufModifiersEXT found {} mods", num);
            std::vector<uint64_t> goodMods;
            for (int i = 0; i < num; ++i) {
                if (externalOnly[i]) {
                    Debug::log(TRACE, "Modifier {:x} failed test", mods[i]);
                    continue;
                }

                Debug::log(TRACE, "Modifier {:x} passed test", mods[i]);
                goodMods.push_back(mods[i]);
            }

            uint64_t zero      = 0;
            bool     hasLinear = std::find(goodMods.begin(), goodMods.end(), 0) != goodMods.end();

            bo = gbm_bo_create_with_modifiers2(g_pHyprlock->dma.gbmDevice, scdata.w, scdata.h, scdata.fmt, hasLinear ? &zero : goodMods.data(), hasLinear ? 1 : goodMods.size(),
                                               flags);
        }
    }

    if (!bo) {
        Debug::log(ERR, "Couldn't create a drm buffer");
        return false;
    }

    planes = gbm_bo_get_plane_count(bo);

    uint64_t mod = gbm_bo_get_modifier(bo);
    Debug::log(LOG, "bo chose modifier {:x}", mod);

    zwp_linux_buffer_params_v1* params = zwp_linux_dmabuf_v1_create_params((zwp_linux_dmabuf_v1*)g_pHyprlock->dma.linuxDmabuf);
    if (!params) {
        Debug::log(ERR, "zwp_linux_dmabuf_v1_create_params failed");
        gbm_bo_destroy(bo);
        return false;
    }

    for (size_t plane = 0; plane < (size_t)planes; plane++) {
        size[plane]   = 0;
        stride[plane] = gbm_bo_get_stride_for_plane(bo, plane);
        offset[plane] = gbm_bo_get_offset(bo, plane);
        fd[plane]     = gbm_bo_get_fd_for_plane(bo, plane);

        if (fd[plane] < 0) {
            Debug::log(ERR, "gbm_bo_get_fd_for_plane failed");
            zwp_linux_buffer_params_v1_destroy(params);
            gbm_bo_destroy(bo);
            for (size_t plane_tmp = 0; plane_tmp < plane; plane_tmp++) {
                close(fd[plane_tmp]);
            }
            return false;
        }

        zwp_linux_buffer_params_v1_add(params, fd[plane], plane, offset[plane], stride[plane], mod >> 32, mod & 0xffffffff);
    }

    wlBuffer = zwp_linux_buffer_params_v1_create_immed(params, scdata.w, scdata.h, scdata.fmt, 0);
    zwp_linux_buffer_params_v1_destroy(params);

    if (!wlBuffer) {
        Debug::log(ERR, "[pw] zwp_linux_buffer_params_v1_create_immed failed");
        gbm_bo_destroy(bo);
        for (size_t plane = 0; plane < (size_t)planes; plane++)
            close(fd[plane]);

        return false;
    }

    return true;
}

bool CDMAFrame::onBufferReady() {
    static const int general_attribs    = 3;
    static const int plane_attribs      = 5;
    static const int entries_per_attrib = 2;
    EGLAttrib        attribs[(general_attribs + plane_attribs * 4) * entries_per_attrib + 1];
    int              attr = 0;
    Vector2D         size{scdata.w, scdata.h};

    attribs[attr++] = EGL_WIDTH;
    attribs[attr++] = size.x;
    attribs[attr++] = EGL_HEIGHT;
    attribs[attr++] = size.y;
    attribs[attr++] = EGL_LINUX_DRM_FOURCC_EXT;
    attribs[attr++] = scdata.fmt;
    attribs[attr++] = EGL_DMA_BUF_PLANE0_FD_EXT;
    attribs[attr++] = fd[0];
    attribs[attr++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
    attribs[attr++] = offset[0];
    attribs[attr++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    attribs[attr++] = stride[0];
    attribs[attr]   = EGL_NONE;

    image = eglCreateImage(g_pEGL->eglDisplay, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, attribs);

    if (image == EGL_NO_IMAGE) {
        Debug::log(ERR, "failed creating an egl image");
        return false;
    }

    asset.texture.allocate();
    asset.texture.m_vSize = size;
    glBindTexture(GL_TEXTURE_2D, asset.texture.m_iTexID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
    glBindTexture(GL_TEXTURE_2D, 0);

    Debug::log(LOG, "Got dma frame with size {}", size);

    asset.ready = true;

    return true;
}