#pragma once

#include "../helpers/Math.hpp"
#include <GLES3/gl32.h>
#include "Texture.hpp"

class CFramebuffer {
  public:
    ~CFramebuffer();

    bool      alloc(int w, int h, bool highres = false);
    void      addStencil();
    void      bind() const;
    void      release();
    void      reset();
    bool      isAllocated();

    Vector2D  m_vSize;

    CTexture  m_cTex;
    GLuint    m_iFb = -1;

    CTexture* m_pStencilTex = nullptr;
};