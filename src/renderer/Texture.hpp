#pragma once

#include <GLES3/gl32.h>
#include "../helpers/Vector2D.hpp"

enum TEXTURETYPE {
    TEXTURE_INVALID,  // Invalid
    TEXTURE_RGBA,     // 4 channels
    TEXTURE_RGBX,     // discard A
    TEXTURE_EXTERNAL, // EGLImage
};

class CTexture {
  public:
    CTexture();
    ~CTexture();

    void        destroyTexture();
    void        allocate();

    TEXTURETYPE m_iType      = TEXTURE_RGBA;
    GLenum      m_iTarget    = GL_TEXTURE_2D;
    bool        m_bAllocated = false;
    GLuint      m_iTexID     = 0;
    Vector2D    m_vSize;
};