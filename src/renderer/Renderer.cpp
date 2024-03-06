#include "Renderer.hpp"
#include "../core/Egl.hpp"
#include "../config/ConfigManager.hpp"
#include "../helpers/Color.hpp"
#include "../core/Output.hpp"
#include "mtx.hpp"

#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>

#include <algorithm>

#include "Shaders.hpp"

#include "widgets/PasswordInputField.hpp"
#include "widgets/Background.hpp"
#include "widgets/Label.hpp"

inline const float fullVerts[] = {
    1, 0, // top right
    0, 0, // top left
    1, 1, // bottom right
    0, 1, // bottom left
};

GLuint compileShader(const GLuint& type, std::string src) {
    auto shader = glCreateShader(type);

    auto shaderSource = src.c_str();

    glShaderSource(shader, 1, (const GLchar**)&shaderSource, nullptr);
    glCompileShader(shader);

    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    RASSERT(ok != GL_FALSE, "compileShader() failed! GL_COMPILE_STATUS not OK!");

    return shader;
}

GLuint createProgram(const std::string& vert, const std::string& frag) {
    auto vertCompiled = compileShader(GL_VERTEX_SHADER, vert);

    RASSERT(vertCompiled, "Compiling shader failed. VERTEX NULL! Shader source:\n\n{}", vert.c_str());

    auto fragCompiled = compileShader(GL_FRAGMENT_SHADER, frag);

    RASSERT(fragCompiled, "Compiling shader failed. FRAGMENT NULL! Shader source:\n\n{}", frag.c_str());

    auto prog = glCreateProgram();
    glAttachShader(prog, vertCompiled);
    glAttachShader(prog, fragCompiled);
    glLinkProgram(prog);

    glDetachShader(prog, vertCompiled);
    glDetachShader(prog, fragCompiled);
    glDeleteShader(vertCompiled);
    glDeleteShader(fragCompiled);

    GLint ok;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);

    RASSERT(ok != GL_FALSE, "createProgram() failed! GL_LINK_STATUS not OK!");

    return prog;
}

static void glMessageCallbackA(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (type != GL_DEBUG_TYPE_ERROR)
        return;
    Debug::log(LOG, "[gl] {}", (const char*)message);
}

CRenderer::CRenderer() {
    g_pEGL->makeCurrent(nullptr);

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(glMessageCallbackA, 0);

    GLuint prog          = createProgram(QUADVERTSRC, QUADFRAGSRC);
    rectShader.program   = prog;
    rectShader.proj      = glGetUniformLocation(prog, "proj");
    rectShader.color     = glGetUniformLocation(prog, "color");
    rectShader.posAttrib = glGetAttribLocation(prog, "pos");
    rectShader.topLeft   = glGetUniformLocation(prog, "topLeft");
    rectShader.fullSize  = glGetUniformLocation(prog, "fullSize");
    rectShader.radius    = glGetUniformLocation(prog, "radius");

    prog                        = createProgram(TEXVERTSRC, TEXFRAGSRCRGBA);
    texShader.program           = prog;
    texShader.proj              = glGetUniformLocation(prog, "proj");
    texShader.tex               = glGetUniformLocation(prog, "tex");
    texShader.alphaMatte        = glGetUniformLocation(prog, "texMatte");
    texShader.alpha             = glGetUniformLocation(prog, "alpha");
    texShader.texAttrib         = glGetAttribLocation(prog, "texcoord");
    texShader.matteTexAttrib    = glGetAttribLocation(prog, "texcoordMatte");
    texShader.posAttrib         = glGetAttribLocation(prog, "pos");
    texShader.discardOpaque     = glGetUniformLocation(prog, "discardOpaque");
    texShader.discardAlpha      = glGetUniformLocation(prog, "discardAlpha");
    texShader.discardAlphaValue = glGetUniformLocation(prog, "discardAlphaValue");
    texShader.topLeft           = glGetUniformLocation(prog, "topLeft");
    texShader.fullSize          = glGetUniformLocation(prog, "fullSize");
    texShader.radius            = glGetUniformLocation(prog, "radius");
    texShader.applyTint         = glGetUniformLocation(prog, "applyTint");
    texShader.tint              = glGetUniformLocation(prog, "tint");
    texShader.useAlphaMatte     = glGetUniformLocation(prog, "useAlphaMatte");

    prog                          = createProgram(TEXVERTSRC, FRAGBLUR1);
    blurShader1.program           = prog;
    blurShader1.tex               = glGetUniformLocation(prog, "tex");
    blurShader1.alpha             = glGetUniformLocation(prog, "alpha");
    blurShader1.proj              = glGetUniformLocation(prog, "proj");
    blurShader1.posAttrib         = glGetAttribLocation(prog, "pos");
    blurShader1.texAttrib         = glGetAttribLocation(prog, "texcoord");
    blurShader1.radius            = glGetUniformLocation(prog, "radius");
    blurShader1.halfpixel         = glGetUniformLocation(prog, "halfpixel");
    blurShader1.passes            = glGetUniformLocation(prog, "passes");
    blurShader1.vibrancy          = glGetUniformLocation(prog, "vibrancy");
    blurShader1.vibrancy_darkness = glGetUniformLocation(prog, "vibrancy_darkness");

    prog                  = createProgram(TEXVERTSRC, FRAGBLUR2);
    blurShader2.program   = prog;
    blurShader2.tex       = glGetUniformLocation(prog, "tex");
    blurShader2.alpha     = glGetUniformLocation(prog, "alpha");
    blurShader2.proj      = glGetUniformLocation(prog, "proj");
    blurShader2.posAttrib = glGetAttribLocation(prog, "pos");
    blurShader2.texAttrib = glGetAttribLocation(prog, "texcoord");
    blurShader2.radius    = glGetUniformLocation(prog, "radius");
    blurShader2.halfpixel = glGetUniformLocation(prog, "halfpixel");

    prog                         = createProgram(TEXVERTSRC, FRAGBLURPREPARE);
    blurPrepareShader.program    = prog;
    blurPrepareShader.tex        = glGetUniformLocation(prog, "tex");
    blurPrepareShader.proj       = glGetUniformLocation(prog, "proj");
    blurPrepareShader.posAttrib  = glGetAttribLocation(prog, "pos");
    blurPrepareShader.texAttrib  = glGetAttribLocation(prog, "texcoord");
    blurPrepareShader.contrast   = glGetUniformLocation(prog, "contrast");
    blurPrepareShader.brightness = glGetUniformLocation(prog, "brightness");

    prog                          = createProgram(TEXVERTSRC, FRAGBLURFINISH);
    blurFinishShader.program      = prog;
    blurFinishShader.tex          = glGetUniformLocation(prog, "tex");
    blurFinishShader.proj         = glGetUniformLocation(prog, "proj");
    blurFinishShader.posAttrib    = glGetAttribLocation(prog, "pos");
    blurFinishShader.texAttrib    = glGetAttribLocation(prog, "texcoord");
    blurFinishShader.brightness   = glGetUniformLocation(prog, "brightness");
    blurFinishShader.noise        = glGetUniformLocation(prog, "noise");
    blurFinishShader.colorize     = glGetUniformLocation(prog, "colorize");
    blurFinishShader.colorizeTint = glGetUniformLocation(prog, "colorizeTint");
    blurFinishShader.boostA       = glGetUniformLocation(prog, "boostA");

    wlr_matrix_identity(projMatrix.data());

    asyncResourceGatherer = std::make_unique<CAsyncResourceGatherer>();
}

static int frames = 0;

//
CRenderer::SRenderFeedback CRenderer::renderLock(const CSessionLockSurface& surf) {
    static auto* const PDISABLEBAR = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:disable_loading_bar");
    static auto* const PNOFADEIN   = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:no_fade_in");

    matrixProjection(projection.data(), surf.size.x, surf.size.y, WL_OUTPUT_TRANSFORM_NORMAL);

    g_pEGL->makeCurrent(surf.eglSurface);
    glViewport(0, 0, surf.size.x, surf.size.y);

    GLint fb = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fb);
    pushFb(fb);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    SRenderFeedback feedback;

    float           bga = asyncResourceGatherer->applied ?
                  std::clamp(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - gatheredAt).count() / 500000.0, 0.0, 1.0) :
                  0.0;

    if (!asyncResourceGatherer->ready) {
        // render status
        if (!**PDISABLEBAR) {
            CBox progress = {0, 0, asyncResourceGatherer->progress * surf.size.x, 2};
            renderRect(progress, CColor{0.2f, 0.1f, 0.1f, 1.f}, 0);
        }
    } else {

        if (!asyncResourceGatherer->applied) {
            asyncResourceGatherer->apply();
            gatheredAt = std::chrono::system_clock::now();
        }

        if (**PNOFADEIN)
            bga = 1.0;

        // render widgets
        const auto WIDGETS = getOrCreateWidgetsFor(&surf);
        for (auto& w : *WIDGETS) {
            feedback.needsFrame = w->draw({bga}) || feedback.needsFrame;
        }
    }

    frames++;

    Debug::log(TRACE, "frame {}", frames);

    feedback.needsFrame = feedback.needsFrame || !asyncResourceGatherer->ready || bga < 1.0;

    glDisable(GL_BLEND);

    return feedback;
}

void CRenderer::renderRect(const CBox& box, const CColor& col, int rounding) {
    float matrix[9];
    wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
                           projMatrix.data()); // TODO: write own, don't use WLR here

    float glMatrix[9];
    wlr_matrix_multiply(glMatrix, projection.data(), matrix);

    glUseProgram(rectShader.program);

    glUniformMatrix3fv(rectShader.proj, 1, GL_TRUE, glMatrix);

    // premultiply the color as well as we don't work with straight alpha
    glUniform4f(rectShader.color, col.r * col.a, col.g * col.a, col.b * col.a, col.a);

    const auto TOPLEFT  = Vector2D(box.x, box.y);
    const auto FULLSIZE = Vector2D(box.width, box.height);

    // Rounded corners
    glUniform2f(rectShader.topLeft, (float)TOPLEFT.x, (float)TOPLEFT.y);
    glUniform2f(rectShader.fullSize, (float)FULLSIZE.x, (float)FULLSIZE.y);
    glUniform1f(rectShader.radius, rounding);

    glVertexAttribPointer(rectShader.posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);

    glEnableVertexAttribArray(rectShader.posAttrib);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(rectShader.posAttrib);
}

void CRenderer::renderTexture(const CBox& box, const CTexture& tex, float a, int rounding, std::optional<wl_output_transform> tr) {
    float matrix[9];
    wlr_matrix_project_box(matrix, &box, tr.value_or(WL_OUTPUT_TRANSFORM_FLIPPED_180) /* ugh coordinate spaces */, 0,
                           projMatrix.data()); // TODO: write own, don't use WLR here

    float glMatrix[9];
    wlr_matrix_multiply(glMatrix, projection.data(), matrix);

    CShader* shader = &texShader;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(tex.m_iTarget, tex.m_iTexID);

    glUseProgram(shader->program);

    glUniformMatrix3fv(shader->proj, 1, GL_TRUE, glMatrix);
    glUniform1i(shader->tex, 0);
    glUniform1f(shader->alpha, a);
    const auto TOPLEFT  = Vector2D(box.x, box.y);
    const auto FULLSIZE = Vector2D(box.width, box.height);

    // Rounded corners
    glUniform2f(shader->topLeft, TOPLEFT.x, TOPLEFT.y);
    glUniform2f(shader->fullSize, FULLSIZE.x, FULLSIZE.y);
    glUniform1f(shader->radius, rounding);

    glUniform1i(shader->discardOpaque, 0);
    glUniform1i(shader->discardAlpha, 0);
    glUniform1i(shader->applyTint, 0);

    glVertexAttribPointer(shader->posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
    glVertexAttribPointer(shader->texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);

    glEnableVertexAttribArray(shader->posAttrib);
    glEnableVertexAttribArray(shader->texAttrib);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(shader->posAttrib);
    glDisableVertexAttribArray(shader->texAttrib);

    glBindTexture(tex.m_iTarget, 0);
}

std::vector<std::unique_ptr<IWidget>>* CRenderer::getOrCreateWidgetsFor(const CSessionLockSurface* surf) {
    if (!widgets.contains(surf)) {

        const auto CWIDGETS = g_pConfigManager->getWidgetConfigs();

        for (auto& c : CWIDGETS) {
            if (!c.monitor.empty() && c.monitor != surf->output->stringPort)
                continue;

            // by type
            if (c.type == "background") {
                const std::string PATH = std::any_cast<Hyprlang::STRING>(c.values.at("path"));

                std::string       resourceID = "";
                if (PATH == "screenshot")
                    resourceID = "dma:" + surf->output->stringPort;
                else if (!PATH.empty())
                    resourceID = "background:" + PATH;

                widgets[surf].emplace_back(std::make_unique<CBackground>(surf->size, surf->output, resourceID, c.values, PATH == "screenshot"));
            } else if (c.type == "input-field") {
                widgets[surf].emplace_back(std::make_unique<CPasswordInputField>(surf->size, c.values));
            } else if (c.type == "label") {
                widgets[surf].emplace_back(std::make_unique<CLabel>(surf->size, c.values, /* evil */ const_cast<CSessionLockSurface*>(surf)));
            }
        }
    }

    return &widgets[surf];
}

void CRenderer::blurFB(const CFramebuffer& outfb, SBlurParams params) {
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);

    float matrix[9];
    CBox  box{0, 0, outfb.m_vSize.x, outfb.m_vSize.y};
    wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, 0,
                           projMatrix.data()); // TODO: write own, don't use WLR here

    float glMatrix[9];
    wlr_matrix_multiply(glMatrix, projection.data(), matrix);

    CFramebuffer mirrors[2];
    mirrors[0].alloc(outfb.m_vSize.x, outfb.m_vSize.y, true);
    mirrors[1].alloc(outfb.m_vSize.x, outfb.m_vSize.y, true);

    CFramebuffer* currentRenderToFB = &mirrors[0];

    // Begin with base color adjustments - global brightness and contrast
    // TODO: make this a part of the first pass maybe to save on a drawcall?
    {
        mirrors[1].bind();

        glActiveTexture(GL_TEXTURE0);

        glBindTexture(outfb.m_cTex.m_iTarget, outfb.m_cTex.m_iTexID);

        glTexParameteri(outfb.m_cTex.m_iTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glUseProgram(blurPrepareShader.program);

        glUniformMatrix3fv(blurPrepareShader.proj, 1, GL_TRUE, glMatrix);
        glUniform1f(blurPrepareShader.contrast, params.contrast);
        glUniform1f(blurPrepareShader.brightness, params.brightness);
        glUniform1i(blurPrepareShader.tex, 0);

        glVertexAttribPointer(blurPrepareShader.posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
        glVertexAttribPointer(blurPrepareShader.texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);

        glEnableVertexAttribArray(blurPrepareShader.posAttrib);
        glEnableVertexAttribArray(blurPrepareShader.texAttrib);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(blurPrepareShader.posAttrib);
        glDisableVertexAttribArray(blurPrepareShader.texAttrib);

        currentRenderToFB = &mirrors[1];
    }

    // declare the draw func
    auto drawPass = [&](CShader* pShader) {
        if (currentRenderToFB == &mirrors[0])
            mirrors[1].bind();
        else
            mirrors[0].bind();

        glActiveTexture(GL_TEXTURE0);

        glBindTexture(currentRenderToFB->m_cTex.m_iTarget, currentRenderToFB->m_cTex.m_iTexID);

        glTexParameteri(currentRenderToFB->m_cTex.m_iTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glUseProgram(pShader->program);

        // prep two shaders
        glUniformMatrix3fv(pShader->proj, 1, GL_TRUE, glMatrix);
        glUniform1f(pShader->radius, params.size);
        if (pShader == &blurShader1) {
            glUniform2f(blurShader1.halfpixel, 0.5f / (outfb.m_vSize.x / 2.f), 0.5f / (outfb.m_vSize.y / 2.f));
            glUniform1i(blurShader1.passes, params.passes);
            glUniform1f(blurShader1.vibrancy, params.vibrancy);
            glUniform1f(blurShader1.vibrancy_darkness, params.vibrancy_darkness);
        } else
            glUniform2f(blurShader2.halfpixel, 0.5f / (outfb.m_vSize.x * 2.f), 0.5f / (outfb.m_vSize.y * 2.f));
        glUniform1i(pShader->tex, 0);

        glVertexAttribPointer(pShader->posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
        glVertexAttribPointer(pShader->texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);

        glEnableVertexAttribArray(pShader->posAttrib);
        glEnableVertexAttribArray(pShader->texAttrib);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(pShader->posAttrib);
        glDisableVertexAttribArray(pShader->texAttrib);

        if (currentRenderToFB != &mirrors[0])
            currentRenderToFB = &mirrors[0];
        else
            currentRenderToFB = &mirrors[1];
    };

    // draw the things.
    // first draw is swap -> mirr
    mirrors[0].bind();
    glBindTexture(mirrors[1].m_cTex.m_iTarget, mirrors[1].m_cTex.m_iTexID);

    for (int i = 1; i <= params.passes; ++i) {
        drawPass(&blurShader1); // down
    }

    for (int i = params.passes - 1; i >= 0; --i) {
        drawPass(&blurShader2); // up
    }

    // finalize the image
    {
        if (currentRenderToFB == &mirrors[0])
            mirrors[1].bind();
        else
            mirrors[0].bind();

        glActiveTexture(GL_TEXTURE0);

        glBindTexture(currentRenderToFB->m_cTex.m_iTarget, currentRenderToFB->m_cTex.m_iTexID);

        glTexParameteri(currentRenderToFB->m_cTex.m_iTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

        glUseProgram(blurFinishShader.program);

        glUniformMatrix3fv(blurFinishShader.proj, 1, GL_TRUE, glMatrix);
        glUniform1f(blurFinishShader.noise, params.noise);
        glUniform1f(blurFinishShader.brightness, params.brightness);
        glUniform1i(blurFinishShader.colorize, params.colorize.has_value());
        if (params.colorize.has_value())
            glUniform3f(blurFinishShader.colorizeTint, params.colorize->r, params.colorize->g, params.colorize->b);
        glUniform1f(blurFinishShader.boostA, params.boostA);

        glUniform1i(blurFinishShader.tex, 0);

        glVertexAttribPointer(blurFinishShader.posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
        glVertexAttribPointer(blurFinishShader.texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);

        glEnableVertexAttribArray(blurFinishShader.posAttrib);
        glEnableVertexAttribArray(blurFinishShader.texAttrib);

        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        glDisableVertexAttribArray(blurFinishShader.posAttrib);
        glDisableVertexAttribArray(blurFinishShader.texAttrib);

        if (currentRenderToFB != &mirrors[0])
            currentRenderToFB = &mirrors[0];
        else
            currentRenderToFB = &mirrors[1];
    }

    // finish
    outfb.bind();
    renderTexture(box, currentRenderToFB->m_cTex, 1.0, 0, WL_OUTPUT_TRANSFORM_NORMAL);

    glEnable(GL_BLEND);
}

void CRenderer::pushFb(GLint fb) {
    boundFBs.push_back(fb);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb);
}

void CRenderer::popFb() {
    boundFBs.pop_back();
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, boundFBs.empty() ? 0 : boundFBs.back());
}

void CRenderer::onEmptyPasswordFade() {
    for (auto& [surf, w] : widgets) {
        for (auto& widget : w) {
            widget->onEmptyPasswordTimer();
        }
    }
}