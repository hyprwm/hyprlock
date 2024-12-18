#pragma once

#include <unordered_map>
#include <GLES3/gl32.h>
#include <string>

class CShader {
  public:
    ~CShader();

    GLuint  program           = 0;
    GLint   proj              = -1;
    GLint   color             = -1;
    GLint   alphaMatte        = -1;
    GLint   tex               = -1;
    GLint   tex2              = -1;
    GLint   alpha             = -1;
    GLfloat mixFactor         = -1;
    GLint   posAttrib         = -1;
    GLint   texAttrib         = -1;
    GLint   matteTexAttrib    = -1;
    GLint   discardOpaque     = -1;
    GLint   discardAlpha      = -1;
    GLfloat discardAlphaValue = -1;

    GLint   topLeft               = -1;
    GLint   bottomRight           = -1;
    GLint   fullSize              = -1;
    GLint   fullSizeUntransformed = -1;
    GLint   radius                = -1;
    GLint   radiusOuter           = -1;

    GLint   thick = -1;

    GLint   halfpixel = -1;

    GLint   range         = -1;
    GLint   shadowPower   = -1;
    GLint   useAlphaMatte = -1; // always inverted

    GLint   applyTint = -1;
    GLint   tint      = -1;

    GLint   gradient       = -1;
    GLint   gradientLength = -1;
    GLint   angle          = -1;

    GLint   time      = -1;
    GLint   distort   = -1;
    GLint   wl_output = -1;

    // Blur prepare
    GLint contrast = -1;

    // Blur
    GLint passes            = -1; // Used by `vibrancy`
    GLint vibrancy          = -1;
    GLint vibrancy_darkness = -1;

    // Blur finish
    GLint brightness = -1;
    GLint noise      = -1;

    // colorize
    GLint colorize     = -1;
    GLint colorizeTint = -1;
    GLint boostA       = -1;

    GLint getUniformLocation(const std::string&);

    void  destroy();

  private:
    std::unordered_map<std::string, GLint> m_muUniforms;
};