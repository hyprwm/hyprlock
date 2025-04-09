#include "Renderer.hpp"
#include "Shaders.hpp"
#include "Screencopy.hpp"
#include "../config/ConfigManager.hpp"
#include "../core/AnimationManager.hpp"
#include "../core/Egl.hpp"
#include "../core/Output.hpp"
#include "../core/hyprlock.hpp"
#include "../helpers/Color.hpp"
#include "../helpers/Log.hpp"
#include "widgets/PasswordInputField.hpp"
#include "widgets/Background.hpp"
#include "widgets/Label.hpp"
#include "widgets/Image.hpp"
#include "widgets/Shape.hpp"
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <GLES2/gl2ext.h>
#include <algorithm>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <stdexcept>
#include <mutex> // Added for mpvpaper safeguards

inline const float fullVerts[] = {
    1, 0, // top right
    0, 0, // top left
    1, 1, // bottom right
    0, 1, // bottom left
};

GLuint compileShader(const GLuint& type, std::string src) {
    try {
        auto shader = glCreateShader(type);
        auto shaderSource = src.c_str();
        glShaderSource(shader, 1, &shaderSource, nullptr);
        glCompileShader(shader);

        GLint ok;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (ok == GL_FALSE) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            Debug::log(ERR, "compileShader failed: {}", infoLog);
            glDeleteShader(shader);
            throw std::runtime_error("Shader compilation failed");
        }
        return shader;
    } catch (const std::exception& e) {
        Debug::log(ERR, "compileShader threw: {}", e.what());
        throw;
    }
}

GLuint createProgram(const std::string& vert, const std::string& frag) {
    try {
        auto vertCompiled = compileShader(GL_VERTEX_SHADER, vert);
        if (!vertCompiled)
            throw std::runtime_error("Vertex shader compilation returned NULL");

        auto fragCompiled = compileShader(GL_FRAGMENT_SHADER, frag);
        if (!fragCompiled)
            throw std::runtime_error("Fragment shader compilation returned NULL");

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
        if (ok == GL_FALSE) {
            char infoLog[512];
            glGetProgramInfoLog(prog, 512, nullptr, infoLog);
            Debug::log(ERR, "createProgram failed: {}", infoLog);
            glDeleteProgram(prog);
            throw std::runtime_error("Program linking failed");
        }
        return prog;
    } catch (const std::exception& e) {
        Debug::log(ERR, "createProgram threw: {}", e.what());
        throw;
    }
}

static void glMessageCallbackA(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
    if (type != GL_DEBUG_TYPE_ERROR)
        return;
    Debug::log(LOG, "[gl] {}", (const char*)message);
}

CRenderer::CRenderer() {
    try {
        g_pEGL->makeCurrent(nullptr);
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(glMessageCallbackA, nullptr);

        GLuint prog = createProgram(QUADVERTSRC, QUADFRAGSRC);
        rectShader.program = prog;
        rectShader.proj = glGetUniformLocation(prog, "proj");
        rectShader.color = glGetUniformLocation(prog, "color");
        rectShader.posAttrib = glGetAttribLocation(prog, "pos");
        rectShader.topLeft = glGetUniformLocation(prog, "topLeft");
        rectShader.fullSize = glGetUniformLocation(prog, "fullSize");
        rectShader.radius = glGetUniformLocation(prog, "radius");

        prog = createProgram(TEXVERTSRC, TEXFRAGSRCRGBA);
        texShader.program = prog;
        texShader.proj = glGetUniformLocation(prog, "proj");
        texShader.tex = glGetUniformLocation(prog, "tex");
        texShader.alphaMatte = glGetUniformLocation(prog, "texMatte");
        texShader.alpha = glGetUniformLocation(prog, "alpha");
        texShader.texAttrib = glGetAttribLocation(prog, "texcoord");
        texShader.matteTexAttrib = glGetAttribLocation(prog, "texcoordMatte");
        texShader.posAttrib = glGetAttribLocation(prog, "pos");
        texShader.discardOpaque = glGetUniformLocation(prog, "discardOpaque");
        texShader.discardAlpha = glGetUniformLocation(prog, "discardAlpha");
        texShader.discardAlphaValue = glGetUniformLocation(prog, "discardAlphaValue");
        texShader.topLeft = glGetUniformLocation(prog, "topLeft");
        texShader.fullSize = glGetUniformLocation(prog, "fullSize");
        texShader.radius = glGetUniformLocation(prog, "radius");
        texShader.applyTint = glGetUniformLocation(prog, "applyTint");
        texShader.tint = glGetUniformLocation(prog, "tint");
        texShader.useAlphaMatte = glGetUniformLocation(prog, "useAlphaMatte");

        prog = createProgram(TEXVERTSRC, TEXMIXFRAGSRCRGBA);
        texMixShader.program = prog;
        texMixShader.proj = glGetUniformLocation(prog, "proj");
        texMixShader.tex = glGetUniformLocation(prog, "tex1");
        texMixShader.tex2 = glGetUniformLocation(prog, "tex2");
        texMixShader.alphaMatte = glGetUniformLocation(prog, "texMatte");
        texMixShader.alpha = glGetUniformLocation(prog, "alpha");
        texMixShader.mixFactor = glGetUniformLocation(prog, "mixFactor");
        texMixShader.texAttrib = glGetAttribLocation(prog, "texcoord");
        texMixShader.matteTexAttrib = glGetAttribLocation(prog, "texcoordMatte");
        texMixShader.posAttrib = glGetAttribLocation(prog, "pos");
        texMixShader.discardOpaque = glGetUniformLocation(prog, "discardOpaque");
        texMixShader.discardAlpha = glGetUniformLocation(prog, "discardAlpha");
        texMixShader.discardAlphaValue = glGetUniformLocation(prog, "discardAlphaValue");
        texMixShader.topLeft = glGetUniformLocation(prog, "topLeft");
        texMixShader.fullSize = glGetUniformLocation(prog, "fullSize");
        texMixShader.radius = glGetUniformLocation(prog, "radius");
        texMixShader.applyTint = glGetUniformLocation(prog, "applyTint");
        texMixShader.tint = glGetUniformLocation(prog, "tint");
        texMixShader.useAlphaMatte = glGetUniformLocation(prog, "useAlphaMatte");

        prog = createProgram(TEXVERTSRC, FRAGBLUR1);
        blurShader1.program = prog;
        blurShader1.tex = glGetUniformLocation(prog, "tex");
        blurShader1.alpha = glGetUniformLocation(prog, "alpha");
        blurShader1.proj = glGetUniformLocation(prog, "proj");
        blurShader1.posAttrib = glGetAttribLocation(prog, "pos");
        blurShader1.texAttrib = glGetAttribLocation(prog, "texcoord");
        blurShader1.radius = glGetUniformLocation(prog, "radius");
        blurShader1.halfpixel = glGetUniformLocation(prog, "halfpixel");
        blurShader1.passes = glGetUniformLocation(prog, "passes");
        blurShader1.vibrancy = glGetUniformLocation(prog, "vibrancy");
        blurShader1.vibrancy_darkness = glGetUniformLocation(prog, "vibrancy_darkness");

        prog = createProgram(TEXVERTSRC, FRAGBLUR2);
        blurShader2.program = prog;
        blurShader2.tex = glGetUniformLocation(prog, "tex");
        blurShader2.alpha = glGetUniformLocation(prog, "alpha");
        blurShader2.proj = glGetUniformLocation(prog, "proj");
        blurShader2.posAttrib = glGetAttribLocation(prog, "pos");
        blurShader2.texAttrib = glGetAttribLocation(prog, "texcoord");
        blurShader2.radius = glGetUniformLocation(prog, "radius");
        blurShader2.halfpixel = glGetUniformLocation(prog, "halfpixel");

        prog = createProgram(TEXVERTSRC, FRAGBLURPREPARE);
        blurPrepareShader.program = prog;
        blurPrepareShader.tex = glGetUniformLocation(prog, "tex");
        blurPrepareShader.proj = glGetUniformLocation(prog, "proj");
        blurPrepareShader.posAttrib = glGetAttribLocation(prog, "pos");
        blurPrepareShader.texAttrib = glGetAttribLocation(prog, "texcoord");
        blurPrepareShader.contrast = glGetUniformLocation(prog, "contrast");
        blurPrepareShader.brightness = glGetUniformLocation(prog, "brightness");

        prog = createProgram(TEXVERTSRC, FRAGBLURFINISH);
        blurFinishShader.program = prog;
        blurFinishShader.tex = glGetUniformLocation(prog, "tex");
        blurFinishShader.proj = glGetUniformLocation(prog, "proj");
        blurFinishShader.posAttrib = glGetAttribLocation(prog, "pos");
        blurFinishShader.texAttrib = glGetAttribLocation(prog, "texcoord");
        blurFinishShader.brightness = glGetUniformLocation(prog, "brightness");
        blurFinishShader.noise = glGetUniformLocation(prog, "noise");
        blurFinishShader.colorize = glGetUniformLocation(prog, "colorize");
        blurFinishShader.colorizeTint = glGetUniformLocation(prog, "colorizeTint");
        blurFinishShader.boostA = glGetUniformLocation(prog, "boostA");

        prog = createProgram(QUADVERTSRC, FRAGBORDER);
        borderShader.program = prog;
        borderShader.proj = glGetUniformLocation(prog, "proj");
        borderShader.thick = glGetUniformLocation(prog, "thick");
        borderShader.posAttrib = glGetAttribLocation(prog, "pos");
        borderShader.texAttrib = glGetAttribLocation(prog, "texcoord");
        borderShader.topLeft = glGetUniformLocation(prog, "topLeft");
        borderShader.bottomRight = glGetUniformLocation(prog, "bottomRight");
        borderShader.fullSize = glGetUniformLocation(prog, "fullSize");
        borderShader.fullSizeUntransformed = glGetUniformLocation(prog, "fullSizeUntransformed");
        borderShader.radius = glGetUniformLocation(prog, "radius");
        borderShader.radiusOuter = glGetUniformLocation(prog, "radiusOuter");
        borderShader.gradient = glGetUniformLocation(prog, "gradient");
        borderShader.gradientLength = glGetUniformLocation(prog, "gradientLength");
        borderShader.angle = glGetUniformLocation(prog, "angle");
        borderShader.gradient2 = glGetUniformLocation(prog, "gradient2");
        borderShader.gradient2Length = glGetUniformLocation(prog, "gradient2Length");
        borderShader.angle2 = glGetUniformLocation(prog, "angle2");
        borderShader.gradientLerp = glGetUniformLocation(prog, "gradientLerp");
        borderShader.alpha = glGetUniformLocation(prog, "alpha");

        asyncResourceGatherer = makeUnique<CAsyncResourceGatherer>();
        g_pAnimationManager->createAnimation(0.f, opacity, g_pConfigManager->m_AnimationTree.getConfig("fadeIn"));
    } catch (const std::exception& e) {
        Debug::log(ERR, "CRenderer constructor failed: {}", e.what());
        throw;
    }
}

void CRenderer::renderBackground(const CSessionLockSurface& surf, float opacity) {
    try {
        auto widgets = getOrCreateWidgetsFor(surf);
        for (auto& w : widgets) {
            if (w->type() == "background") {
                w->draw({opacity});
            }
        }
    } catch (const std::exception& e) {
        Debug::log(ERR, "renderBackground failed for output {}: {}", surf.m_outputRef.lock()->stringPort, e.what());
    }
}

void CRenderer::renderShapes(const CSessionLockSurface& surf, float opacity) {
    try {
        auto widgets = getOrCreateWidgetsFor(surf);
        for (auto& w : widgets) {
            if (w->type() == "shape") {
                w->draw({opacity});
            }
        }
    } catch (const std::exception& e) {
        Debug::log(ERR, "renderShapes failed for output {}: {}", surf.m_outputRef.lock()->stringPort, e.what());
    }
}

void CRenderer::renderInputFields(const CSessionLockSurface& surf, float opacity) {
    try {
        auto widgets = getOrCreateWidgetsFor(surf);
        for (auto& w : widgets) {
            if (w->type() == "input-field") {
                w->draw({opacity});
            }
        }
    } catch (const std::exception& e) {
        Debug::log(ERR, "renderInputFields failed for output {}: {}", surf.m_outputRef.lock()->stringPort, e.what());
    }
}

CRenderer::SRenderFeedback CRenderer::renderLock(const CSessionLockSurface& surf) {
    try {
        projection = Mat3x3::outputProjection(surf.size, HYPRUTILS_TRANSFORM_NORMAL);
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
        const bool WAITFORASSETS = !g_pHyprlock->m_bImmediateRender && !asyncResourceGatherer->gathered;

        if (!WAITFORASSETS) {
            // Render in explicit order: background, shapes, input fields
            renderBackground(surf, opacity->value());
            renderShapes(surf, opacity->value());
            renderInputFields(surf, opacity->value());

            // Track if any widget needs another frame
            auto widgets = getOrCreateWidgetsFor(surf);
            for (auto& w : widgets) {
                feedback.needsFrame = w->draw({opacity->value()}) || feedback.needsFrame;
            }
        }

        feedback.needsFrame = feedback.needsFrame || !asyncResourceGatherer->gathered;

        glDisable(GL_BLEND);
        popFb();
        return feedback;
    } catch (const std::exception& e) {
        Debug::log(ERR, "renderLock failed for output {}: {}", surf.m_outputRef.lock()->stringPort, e.what());
        glDisable(GL_BLEND);
        popFb();
        return {};
    }
}
void CRenderer::renderRect(const CBox& box, const CHyprColor& col, int rounding) {
    try {
        const auto ROUNDEDBOX = box.copy().round();
        Mat3x3 matrix = projMatrix.projectBox(ROUNDEDBOX, HYPRUTILS_TRANSFORM_NORMAL, box.rot);
        Mat3x3 glMatrix = projection.copy().multiply(matrix);

        glUseProgram(rectShader.program);
        glUniformMatrix3fv(rectShader.proj, 1, GL_TRUE, glMatrix.getMatrix().data());
        glUniform4f(rectShader.color, col.r * col.a, col.g * col.a, col.b * col.a, col.a);

        const auto TOPLEFT = Vector2D(ROUNDEDBOX.x, ROUNDEDBOX.y);
        const auto FULLSIZE = Vector2D(ROUNDEDBOX.width, ROUNDEDBOX.height);
        glUniform2f(rectShader.topLeft, (float)TOPLEFT.x, (float)TOPLEFT.y);
        glUniform2f(rectShader.fullSize, (float)FULLSIZE.x, (float)FULLSIZE.y);
        glUniform1f(rectShader.radius, rounding);

        glVertexAttribPointer(rectShader.posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
        glEnableVertexAttribArray(rectShader.posAttrib);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(rectShader.posAttrib);
    } catch (const std::exception& e) {
        Debug::log(ERR, "renderRect failed: {}", e.what());
    }
}

void CRenderer::renderBorder(const CBox& box, const CGradientValueData& gradient, int thickness, int rounding, float alpha) {
    try {
        const auto ROUNDEDBOX = box.copy().round();
        Mat3x3 matrix = projMatrix.projectBox(ROUNDEDBOX, HYPRUTILS_TRANSFORM_NORMAL, box.rot);
        Mat3x3 glMatrix = projection.copy().multiply(matrix);

        glUseProgram(borderShader.program);
        glUniformMatrix3fv(borderShader.proj, 1, GL_TRUE, glMatrix.getMatrix().data());
        // Ensure gradient data is valid and non-empty to prevent buffer overruns
        if (!gradient.m_vColorsOkLabA.empty()) {
            glUniform4fv(borderShader.gradient, gradient.m_vColorsOkLabA.size(), (float*)gradient.m_vColorsOkLabA.data());
            glUniform1i(borderShader.gradientLength, gradient.m_vColorsOkLabA.size() / 4);
        } else {
            // Fallback to a single transparent color if gradient is empty
            float fallbackColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            glUniform4fv(borderShader.gradient, 1, fallbackColor);
            glUniform1i(borderShader.gradientLength, 1);
        }
        glUniform1f(borderShader.angle, (int)(gradient.m_fAngle / (M_PI / 180.0)) % 360 * (M_PI / 180.0));
        glUniform1f(borderShader.alpha, alpha);
        glUniform1i(borderShader.gradient2Length, 0);

        const auto TOPLEFT = Vector2D(ROUNDEDBOX.x, ROUNDEDBOX.y);
        const auto FULLSIZE = Vector2D(ROUNDEDBOX.width, ROUNDEDBOX.height);
        glUniform2f(borderShader.topLeft, (float)TOPLEFT.x, (float)TOPLEFT.y);
        glUniform2f(borderShader.fullSize, (float)FULLSIZE.x, (float)FULLSIZE.y);
        glUniform2f(borderShader.fullSizeUntransformed, (float)box.width, (float)box.height);
        glUniform1f(borderShader.radius, rounding);
        glUniform1f(borderShader.radiusOuter, rounding);
        glUniform1f(borderShader.thick, thickness);

        glVertexAttribPointer(borderShader.posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
        glVertexAttribPointer(borderShader.texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
        glEnableVertexAttribArray(borderShader.posAttrib);
        glEnableVertexAttribArray(borderShader.texAttrib);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableVertexAttribArray(borderShader.posAttrib);
        glDisableVertexAttribArray(borderShader.texAttrib);
    } catch (const std::exception& e) {
        Debug::log(ERR, "renderBorder failed: {}", e.what());
    }
}

void CRenderer::renderTexture(const CBox& box, const CTexture& tex, float a, int rounding, std::optional<eTransform> tr) {
    try {
        const auto ROUNDEDBOX = box.copy().round();
        Mat3x3 matrix = projMatrix.projectBox(ROUNDEDBOX, tr.value_or(HYPRUTILS_TRANSFORM_FLIPPED_180), box.rot);
        Mat3x3 glMatrix = projection.copy().multiply(matrix);

        CShader* shader = &texShader;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(tex.m_iTarget, tex.m_iTexID);
        glUseProgram(shader->program);
        glUniformMatrix3fv(shader->proj, 1, GL_TRUE, glMatrix.getMatrix().data());
        glUniform1i(shader->tex, 0);
        glUniform1f(shader->alpha, a);

        const auto TOPLEFT = Vector2D(ROUNDEDBOX.x, ROUNDEDBOX.y);
        const auto FULLSIZE = Vector2D(ROUNDEDBOX.width, ROUNDEDBOX.height);
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
    } catch (const std::exception& e) {
        Debug::log(ERR, "renderTexture failed: {}", e.what());
    }
}

void CRenderer::renderTextureMix(const CBox& box, const CTexture& tex, const CTexture& tex2, float a, float mixFactor, int rounding, std::optional<eTransform> tr) {
    try {
        const auto ROUNDEDBOX = box.copy().round();
        Mat3x3 matrix = projMatrix.projectBox(ROUNDEDBOX, tr.value_or(HYPRUTILS_TRANSFORM_FLIPPED_180), box.rot);
        Mat3x3 glMatrix = projection.copy().multiply(matrix);

        CShader* shader = &texMixShader;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(tex.m_iTarget, tex.m_iTexID);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(tex2.m_iTarget, tex2.m_iTexID);
        glUseProgram(shader->program);
        glUniformMatrix3fv(shader->proj, 1, GL_TRUE, glMatrix.getMatrix().data());
        glUniform1i(shader->tex, 0);
        glUniform1i(shader->tex2, 1);
        glUniform1f(shader->alpha, a);
        glUniform1f(shader->mixFactor, mixFactor);
        const auto TOPLEFT = Vector2D(ROUNDEDBOX.x, ROUNDEDBOX.y);
        const auto FULLSIZE = Vector2D(ROUNDEDBOX.width, ROUNDEDBOX.height);
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
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(tex2.m_iTarget, 0);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(tex.m_iTarget, 0);
    } catch (const std::exception& e) {
        Debug::log(ERR, "renderTextureMix failed: {}", e.what());
    }
}

template <class Widget>
static void createWidget(std::vector<SP<IWidget>>& widgets) {
    const auto W = makeShared<Widget>();
    W->registerSelf(W);
    widgets.emplace_back(W);
}

std::vector<SP<IWidget>>& CRenderer::getOrCreateWidgetsFor(const CSessionLockSurface& surf) {
    try {
        RASSERT(surf.m_outputID != OUTPUT_INVALID, "Invalid output ID!");
        if (!widgets.contains(surf.m_outputID)) {
            auto CWIDGETS = g_pConfigManager->getWidgetConfigs();
            // Sort widgets by zindex to ensure correct render order
            std::ranges::sort(CWIDGETS, [](const CConfigManager::SWidgetConfig& a, const CConfigManager::SWidgetConfig& b) {
                int zindexA = 0, zindexB = 0;
                try {
                    zindexA = std::any_cast<Hyprlang::INT>(a.values.at("zindex"));
                } catch (...) {
                    Debug::log(WARN, "Widget {} missing zindex, defaulting to 0", a.type);
                }
                try {
                    zindexB = std::any_cast<Hyprlang::INT>(b.values.at("zindex"));
                } catch (...) {
                    Debug::log(WARN, "Widget {} missing zindex, defaulting to 0", b.type);
                }
                return zindexA < zindexB;
            });

            const auto POUTPUT = surf.m_outputRef.lock();
            for (auto& c : CWIDGETS) {
                if (!c.monitor.empty() && c.monitor != POUTPUT->stringPort && 
                    !POUTPUT->stringDesc.starts_with(c.monitor) && 
                    !POUTPUT->stringDesc.starts_with("desc:" + c.monitor))
                    continue;

                if (c.type == "background") {
                    createWidget<CBackground>(widgets[surf.m_outputID]);
                } else if (c.type == "input-field") {
                    createWidget<CPasswordInputField>(widgets[surf.m_outputID]);
                } else if (c.type == "label") {
                    createWidget<CLabel>(widgets[surf.m_outputID]);
                } else if (c.type == "shape") {
                    createWidget<CShape>(widgets[surf.m_outputID]);
                } else if (c.type == "image") {
                    createWidget<CImage>(widgets[surf.m_outputID]);
                } else {
                    Debug::log(ERR, "Unknown widget type: {}", c.type);
                    continue;
                }
                try {
                    widgets[surf.m_outputID].back()->configure(c.values, POUTPUT);
                } catch (const std::exception& e) {
                    Debug::log(ERR, "Failed to configure widget type {}: {}", c.type, e.what());
                    widgets[surf.m_outputID].pop_back(); // Remove faulty widget
                }
            }
            Debug::log(LOG, "Created {} widgets for output {}", widgets[surf.m_outputID].size(), POUTPUT->stringPort);
        }
        return widgets[surf.m_outputID];
    } catch (const std::exception& e) {
        Debug::log(ERR, "getOrCreateWidgetsFor failed for output {}: {}", surf.m_outputRef.lock()->stringPort, e.what());
        static std::vector<SP<IWidget>> empty;
        return empty;
    }
}
void CRenderer::blurFB(const CFramebuffer& outfb, SBlurParams params) {
    try {
        glDisable(GL_BLEND);
        glDisable(GL_STENCIL_TEST);

        CBox box{0, 0, outfb.m_vSize.x, outfb.m_vSize.y};
        box.round();
        Mat3x3 matrix = projMatrix.projectBox(box, HYPRUTILS_TRANSFORM_NORMAL, 0);
        Mat3x3 glMatrix = projection.copy().multiply(matrix);

        CFramebuffer mirrors[2];
        mirrors[0].alloc(outfb.m_vSize.x, outfb.m_vSize.y, true);
        mirrors[1].alloc(outfb.m_vSize.x, outfb.m_vSize.y, true);

        CFramebuffer* currentRenderToFB = &mirrors[0];

        {
            mirrors[1].bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(outfb.m_cTex.m_iTarget, outfb.m_cTex.m_iTexID);
            glTexParameteri(outfb.m_cTex.m_iTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glUseProgram(blurPrepareShader.program);
            glUniformMatrix3fv(blurPrepareShader.proj, 1, GL_TRUE, glMatrix.getMatrix().data());
            glUniform1f(blurPrepareShader.contrast, params.contrast);
            glUniform1f(blurPrepareShader.brightness, params.brightness);
            glUniform1i(blurPrepareShader.tex, 0);
            glVertexAttribPointer(blurPrepareShader.posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
            glEnableVertexAttribArray(blurPrepareShader.posAttrib);
            glVertexAttribPointer(blurPrepareShader.texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
            glEnableVertexAttribArray(blurPrepareShader.texAttrib);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisableVertexAttribArray(blurPrepareShader.posAttrib);
            glDisableVertexAttribArray(blurPrepareShader.texAttrib);
            currentRenderToFB = &mirrors[1];
        }

        auto drawPass = [&](CShader* pShader) {
            if (currentRenderToFB == &mirrors[0])
                mirrors[1].bind();
            else
                mirrors[0].bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(currentRenderToFB->m_cTex.m_iTarget, currentRenderToFB->m_cTex.m_iTexID);
            glTexParameteri(currentRenderToFB->m_cTex.m_iTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glUseProgram(pShader->program);
            glUniformMatrix3fv(pShader->proj, 1, GL_TRUE, glMatrix.getMatrix().data());
            glUniform1f(pShader->radius, params.size);
            if (pShader == &blurShader1) {
                glUniform2f(blurShader1.halfpixel, 0.5f / (outfb.m_vSize.x / 2.f), 0.5f / (outfb.m_vSize.y / 2.f));
                glUniform1i(blurShader1.passes, params.passes);
                glUniform1f(blurShader1.vibrancy, params.vibrancy);
                glUniform1f(blurShader1.vibrancy_darkness, params.vibrancy_darkness);
            } else {
                glUniform2f(blurShader2.halfpixel, 0.5f / (outfb.m_vSize.x * 2.f), 0.5f / (outfb.m_vSize.y * 2.f));
            }
            glUniform1i(pShader->tex, 0);
            glVertexAttribPointer(pShader->posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
            glEnableVertexAttribArray(pShader->posAttrib);
            glVertexAttribPointer(pShader->texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
            glEnableVertexAttribArray(pShader->texAttrib);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisableVertexAttribArray(pShader->posAttrib);
            glDisableVertexAttribArray(pShader->texAttrib);
            currentRenderToFB = (currentRenderToFB == &mirrors[0]) ? &mirrors[1] : &mirrors[0];
        };

        mirrors[0].bind();
        glBindTexture(mirrors[1].m_cTex.m_iTarget, mirrors[1].m_cTex.m_iTexID);
        for (int i = 1; i <= params.passes; ++i) {
            drawPass(&blurShader1);
        }
        for (int i = params.passes - 1; i >= 0; --i) {
            drawPass(&blurShader2);
        }

        {
            if (currentRenderToFB == &mirrors[0])
                mirrors[1].bind();
            else
                mirrors[0].bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(currentRenderToFB->m_cTex.m_iTarget, currentRenderToFB->m_cTex.m_iTexID);
            glTexParameteri(currentRenderToFB->m_cTex.m_iTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glUseProgram(blurFinishShader.program);
            glUniformMatrix3fv(blurFinishShader.proj, 1, GL_TRUE, glMatrix.getMatrix().data());
            glUniform1f(blurFinishShader.noise, params.noise);
            glUniform1f(blurFinishShader.brightness, params.brightness);
            glUniform1i(blurFinishShader.colorize, params.colorize.has_value());
            if (params.colorize.has_value())
                glUniform3f(blurFinishShader.colorizeTint, params.colorize->r, params.colorize->g, params.colorize->b);
            glUniform1f(blurFinishShader.boostA, params.boostA);
            glUniform1i(blurFinishShader.tex, 0);
            glVertexAttribPointer(blurFinishShader.posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
            glEnableVertexAttribArray(blurFinishShader.posAttrib);
            glVertexAttribPointer(blurFinishShader.texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
            glEnableVertexAttribArray(blurFinishShader.texAttrib);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDisableVertexAttribArray(blurFinishShader.posAttrib);
            glDisableVertexAttribArray(blurFinishShader.texAttrib);
            currentRenderToFB = (currentRenderToFB == &mirrors[0]) ? &mirrors[1] : &mirrors[0];
        }

        outfb.bind();
        renderTexture(box, currentRenderToFB->m_cTex, 1.0, 0, HYPRUTILS_TRANSFORM_NORMAL);
        glEnable(GL_BLEND);
    } catch (const std::exception& e) {
        Debug::log(ERR, "blurFB failed: {}", e.what());
        outfb.bind();
        glEnable(GL_BLEND);
    }
}

void CRenderer::pushFb(GLint fb) {
    try {
        boundFBs.push_back(fb);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb);
    } catch (const std::exception& e) {
        Debug::log(ERR, "pushFb failed: {}", e.what());
    }
}

void CRenderer::popFb() {
    try {
        if (!boundFBs.empty()) {
            boundFBs.pop_back();
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, boundFBs.empty() ? 0 : boundFBs.back());
        }
    } catch (const std::exception& e) {
        Debug::log(ERR, "popFb failed: {}", e.what());
    }
}

void CRenderer::removeWidgetsFor(OUTPUTID id) {
    try {
        widgets.erase(id);
        Debug::log(LOG, "Removed widgets for output ID {}", id);
    } catch (const std::exception& e) {
        Debug::log(ERR, "removeWidgetsFor failed for ID {}: {}", id, e.what());
    }
}

void CRenderer::reconfigureWidgetsFor(OUTPUTID id) {
    try {
        removeWidgetsFor(id);
        Debug::log(LOG, "Reconfigured widgets for output ID {}", id);
    } catch (const std::exception& e) {
        Debug::log(ERR, "reconfigureWidgetsFor failed for ID {}: {}", id, e.what());
    }
}

void CRenderer::startFadeIn() {
    try {
        Debug::log(LOG, "Starting fade in");
        *opacity = 1.f;
        opacity->setCallbackOnEnd([this](auto) { opacity->setConfig(g_pConfigManager->m_AnimationTree.getConfig("fadeOut")); }, true);
    } catch (const std::exception& e) {
        Debug::log(ERR, "startFadeIn failed: {}", e.what());
    }
}

void CRenderer::startFadeOut(bool unlock, bool immediate) {
    try {
        if (immediate)
            opacity->setValueAndWarp(0.f);
        else
            *opacity = 0.f;
        if (unlock)
            opacity->setCallbackOnEnd([](auto) { g_pHyprlock->releaseSessionLock(); }, true);
    } catch (const std::exception& e) {
        Debug::log(ERR, "startFadeOut failed: {}", e.what());
    }
}

bool CRenderer::startMpvpaper(const std::string& monitor, const std::string& videoPath) {
    try {
        std::lock_guard<std::mutex> lock(mpvpaperMutex);
        if (mpvpaperPids.find(monitor) != mpvpaperPids.end()) {
            Debug::log(LOG, "mpvpaper already running for monitor {}, PID {}", monitor, mpvpaperPids[monitor]);
            return true;
        }
        Debug::log(LOG, "Starting mpvpaper for monitor {} with video {}", monitor, videoPath);
        pid_t pid = fork();
        if (pid == 0) {
            // Redirect stderr to log file for debugging
            freopen("/tmp/mpvpaper.log", "w", stderr);
            // Use minimal mpvpaper options, relying on default Wayland surface layering
            execlp("mpvpaper", "mpvpaper", "-o", "loop panscan=1.0 mute=yes", monitor.c_str(), videoPath.c_str(), (char*)nullptr);
            Debug::log(ERR, "execlp failed for mpvpaper on monitor {} with video {}: errno {}", monitor, videoPath, errno);
            _exit(1);
        } else if (pid > 0) {
            mpvpaperPids[monitor] = pid;
            mpvpaperVideoPaths[monitor] = videoPath;
            Debug::log(LOG, "Started mpvpaper for monitor {} with PID {}", monitor, pid);
            return true;
        } else {
            Debug::log(ERR, "Fork failed for mpvpaper on monitor {}: errno {}", monitor, errno);
            return false;
        }
    } catch (const std::exception& e) {
        Debug::log(ERR, "startMpvpaper failed for monitor {}: {}", monitor, e.what());
        return false;
    }
}

void CRenderer::stopMpvpaper() {
    try {
        std::lock_guard<std::mutex> lock(mpvpaperMutex);
        for (auto it = mpvpaperPids.begin(); it != mpvpaperPids.end();) {
            pid_t pid = it->second;
            if (pid > 0) {
                Debug::log(LOG, "Stopping mpvpaper for monitor {} with PID {}", it->first, pid);
                kill(pid, SIGTERM);
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    Debug::log(LOG, "mpvpaper PID {} exited with status {}", pid, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    Debug::log(WARN, "mpvpaper PID {} terminated by signal {}", pid, WTERMSIG(status));
                }
            }
            it = mpvpaperPids.erase(it);
        }
        mpvpaperVideoPaths.clear();
        Debug::log(LOG, "All mpvpaper processes stopped");
    } catch (const std::exception& e) {
        Debug::log(ERR, "stopMpvpaper failed: {}", e.what());
    }
}

void CRenderer::stopMpvpaper(const std::string& monitor) {
    try {
        std::lock_guard<std::mutex> lock(mpvpaperMutex);
        auto it = mpvpaperPids.find(monitor);
        if (it != mpvpaperPids.end()) {
            pid_t pid = it->second;
            if (pid > 0) {
                Debug::log(LOG, "Stopping mpvpaper for monitor {} with PID {}", monitor, pid);
                kill(pid, SIGTERM);
                int status;
                waitpid(pid, &status, 0);
                if (WIFEXITED(status)) {
                    Debug::log(LOG, "mpvpaper PID {} exited with status {}", pid, WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    Debug::log(WARN, "mpvpaper PID {} terminated by signal {}", pid, WTERMSIG(status));
                }
            }
            mpvpaperPids.erase(it);
            mpvpaperVideoPaths.erase(monitor);
        } else {
            Debug::log(LOG, "No mpvpaper process found for monitor {}", monitor);
        }
    } catch (const std::exception& e) {
        Debug::log(ERR, "stopMpvpaper for monitor {} failed: {}", monitor, e.what());
    }
}