#include "Framebuffer.hpp"
#include "../helpers/Log.hpp"
#include <libdrm/drm_fourcc.h>

static uint32_t drmFormatToGL(uint32_t drm) {
    switch (drm) {
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_XBGR8888: return GL_RGBA; // doesn't matter, opengl is gucci in this case.
        case DRM_FORMAT_XRGB2101010:
        case DRM_FORMAT_XBGR2101010: return GL_RGB10_A2;
        default: return GL_RGBA;
    }
    return GL_RGBA;
}

static uint32_t glFormatToType(uint32_t gl) {
    return gl != GL_RGBA ? GL_UNSIGNED_INT_2_10_10_10_REV : GL_UNSIGNED_BYTE;
}

bool CFramebuffer::alloc(int w, int h, bool highres) {
    bool     firstAlloc = false;

    uint32_t glFormat = highres ? GL_RGBA16F : drmFormatToGL(DRM_FORMAT_XRGB2101010); // TODO: revise only 10b when I find a way to figure out without sc whether display is 10b
    uint32_t glType   = highres ? GL_FLOAT : glFormatToType(glFormat);

    if (m_iFb == (uint32_t)-1) {
        firstAlloc = true;
        glGenFramebuffers(1, &m_iFb);
    }

    if (m_cTex.m_iTexID == 0) {
        firstAlloc = true;
        glGenTextures(1, &m_cTex.m_iTexID);
        glBindTexture(GL_TEXTURE_2D, m_cTex.m_iTexID);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        m_cTex.m_vSize = {w, h};
    }

    if (firstAlloc || m_vSize != Vector2D(w, h)) {
        glBindTexture(GL_TEXTURE_2D, m_cTex.m_iTexID);
        glTexImage2D(GL_TEXTURE_2D, 0, glFormat, w, h, 0, GL_RGBA, glType, nullptr);

        glBindFramebuffer(GL_FRAMEBUFFER, m_iFb);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_cTex.m_iTexID, 0);

        if (m_pStencilTex) {
            glBindTexture(GL_TEXTURE_2D, m_pStencilTex->m_iTexID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, w, h, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);

            glBindFramebuffer(GL_FRAMEBUFFER, m_iFb);

            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_pStencilTex->m_iTexID, 0);
        }

        auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            Debug::log(ERR, "Framebuffer incomplete, couldn't create! (FB status: {})", status);
            abort();
        }

        Debug::log(LOG, "Framebuffer created, status {}", status);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_vSize = Vector2D(w, h);

    return true;
}

void CFramebuffer::addStencil() {
    if (!m_pStencilTex) {
        Debug::log(ERR, "No stencil texture allocated.");
        return;
    }

    glBindTexture(GL_TEXTURE_2D, m_pStencilTex->m_iTexID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, m_vSize.x, m_vSize.y, 0, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);

    glBindFramebuffer(GL_FRAMEBUFFER, m_iFb);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_pStencilTex->m_iTexID, 0);

    auto status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    RASSERT((status == GL_FRAMEBUFFER_COMPLETE), "Failed adding a stencil to fbo! (FB status: {})", status);

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CFramebuffer::bind() const {
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_iFb);
    glViewport(0, 0, m_vSize.x, m_vSize.y);
}

void CFramebuffer::release() {
    if (m_iFb != (uint32_t)-1 && m_iFb) {
        glDeleteFramebuffers(1, &m_iFb);
        m_iFb = (uint32_t)-1;
    }

    if (m_cTex.m_iTexID) {
        glDeleteTextures(1, &m_cTex.m_iTexID);
        m_cTex.m_iTexID = 0;
    }

    m_vSize = Vector2D();
}

CFramebuffer::~CFramebuffer() {
    release();
}

bool CFramebuffer::isAllocated() {
    return m_iFb != (GLuint)-1;
}