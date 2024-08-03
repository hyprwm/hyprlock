#include "Renderer.hpp"
#include "../core/Egl.hpp"
#include "../config/ConfigManager.hpp"
#include "../helpers/Color.hpp"
#include "../core/Output.hpp"
#include "../core/hyprlock.hpp"
#include "../renderer/DMAFrame.hpp"
#include "mtx.hpp"
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
#include <algorithm>
#include "Shaders.hpp"
#include "src/helpers/Log.hpp"
#include "src/renderer/Shader.hpp"
#include "widgets/PasswordInputField.hpp"
#include "widgets/Background.hpp"
#include "widgets/Label.hpp"
#include "widgets/Image.hpp"
#include "widgets/Shape.hpp"

constexpr float fullVerts[]{
    1, 0, // top right
    0, 0, // top left
    1, 1, // bottom right
    0, 1, // bottom left
};

[[nodiscard]] GLuint compileShader(GLenum type, const std::string& src) noexcept {
    GLuint      shader = glCreateShader(type);

    const char* shaderSource = src.c_str();

    glShaderSource(shader, 1, &shaderSource, nullptr);
    glCompileShader(shader);

    GLint ok;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);

    RASSERT(ok != GL_FALSE, "compileShader() failed! GL_COMPILE_STATUS not OK!");

    return shader;
}

[[nodiscard]] GLuint createProgram(const std::string& vert, const std::string& frag) noexcept {
    GLuint vertCompiled = compileShader(GL_VERTEX_SHADER, vert);

    RASSERT(vertCompiled, "Compiling shader failed. VERTEX NULL! Shader source:\n\n{}", vert);

    GLuint fragCompiled = compileShader(GL_FRAGMENT_SHADER, frag);

    RASSERT(fragCompiled, "Compiling shader failed. FRAGMENT NULL! Shader source:\n\n{}", frag);

    GLuint prog = glCreateProgram();
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

void glMessageCallbackA(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) noexcept {
    if (type != GL_DEBUG_TYPE_ERROR)
        return;
    Debug::log(LOG, "[gl] {}", (const char*)message);
}

CRenderer::CRenderer() {
    g_pEGL->makeCurrent(nullptr);

    glEnable(GL_DEBUG_OUTPUT);
    glDebugMessageCallback(glMessageCallbackA, nullptr);

    const auto initShader = [&](CShader& shader, const std::string& vertSrc, const std::string& fragSrc) {
        shader.program   = createProgram(vertSrc, fragSrc);
        shader.proj      = glGetUniformLocation(shader.program, "proj");
        shader.posAttrib = glGetAttribLocation(shader.program, "pos");
    };

    initShader(rectShader, QUADVERTSRC, QUADFRAGSRC);
    rectShader.color    = glGetUniformLocation(rectShader.program, "color");
    rectShader.topLeft  = glGetUniformLocation(rectShader.program, "topLeft");
    rectShader.fullSize = glGetUniformLocation(rectShader.program, "fullSize");
    rectShader.radius   = glGetUniformLocation(rectShader.program, "radius");

    initShader(texShader, TEXVERTSRC, TEXFRAGSRCRGBA);
    texShader.tex               = glGetUniformLocation(texShader.program, "tex");
    texShader.alphaMatte        = glGetUniformLocation(texShader.program, "texMatte");
    texShader.alpha             = glGetUniformLocation(texShader.program, "alpha");
    texShader.texAttrib         = glGetAttribLocation(texShader.program, "texcoord");
    texShader.matteTexAttrib    = glGetAttribLocation(texShader.program, "texcoordMatte");
    texShader.discardOpaque     = glGetUniformLocation(texShader.program, "discardOpaque");
    texShader.discardAlpha      = glGetUniformLocation(texShader.program, "discardAlpha");
    texShader.discardAlphaValue = glGetUniformLocation(texShader.program, "discardAlphaValue");
    texShader.topLeft           = glGetUniformLocation(texShader.program, "topLeft");
    texShader.fullSize          = glGetUniformLocation(texShader.program, "fullSize");
    texShader.radius            = glGetUniformLocation(texShader.program, "radius");
    texShader.applyTint         = glGetUniformLocation(texShader.program, "applyTint");
    texShader.tint              = glGetUniformLocation(texShader.program, "tint");
    texShader.useAlphaMatte     = glGetUniformLocation(texShader.program, "useAlphaMatte");

    initShader(blurShader1, TEXVERTSRC, FRAGBLUR1);
    blurShader1.tex               = glGetUniformLocation(blurShader1.program, "tex");
    blurShader1.alpha             = glGetUniformLocation(blurShader1.program, "alpha");
    blurShader1.texAttrib         = glGetAttribLocation(blurShader1.program, "texcoord");
    blurShader1.radius            = glGetUniformLocation(blurShader1.program, "radius");
    blurShader1.halfpixel         = glGetUniformLocation(blurShader1.program, "halfpixel");
    blurShader1.passes            = glGetUniformLocation(blurShader1.program, "passes");
    blurShader1.vibrancy          = glGetUniformLocation(blurShader1.program, "vibrancy");
    blurShader1.vibrancy_darkness = glGetUniformLocation(blurShader1.program, "vibrancy_darkness");

    initShader(blurShader2, TEXVERTSRC, FRAGBLUR2);
    blurShader2.tex       = glGetUniformLocation(blurShader2.program, "tex");
    blurShader2.alpha     = glGetUniformLocation(blurShader2.program, "alpha");
    blurShader2.texAttrib = glGetAttribLocation(blurShader2.program, "texcoord");
    blurShader2.radius    = glGetUniformLocation(blurShader2.program, "radius");
    blurShader2.halfpixel = glGetUniformLocation(blurShader2.program, "halfpixel");

    initShader(blurPrepareShader, TEXVERTSRC, FRAGBLURPREPARE);
    blurPrepareShader.tex        = glGetUniformLocation(blurPrepareShader.program, "tex");
    blurPrepareShader.texAttrib  = glGetAttribLocation(blurPrepareShader.program, "texcoord");
    blurPrepareShader.contrast   = glGetUniformLocation(blurPrepareShader.program, "contrast");
    blurPrepareShader.brightness = glGetUniformLocation(blurPrepareShader.program, "brightness");

    initShader(blurFinishShader, TEXVERTSRC, FRAGBLURFINISH);

    blurFinishShader.tex          = glGetUniformLocation(blurFinishShader.program, "tex");
    blurFinishShader.texAttrib    = glGetAttribLocation(blurFinishShader.program, "texcoord");
    blurFinishShader.brightness   = glGetUniformLocation(blurFinishShader.program, "brightness");
    blurFinishShader.noise        = glGetUniformLocation(blurFinishShader.program, "noise");
    blurFinishShader.colorize     = glGetUniformLocation(blurFinishShader.program, "colorize");
    blurFinishShader.colorizeTint = glGetUniformLocation(blurFinishShader.program, "colorizeTint");
    blurFinishShader.boostA       = glGetUniformLocation(blurFinishShader.program, "boostA");

    wlr_matrix_identity(projMatrix.data());

    asyncResourceGatherer = std::make_unique<CAsyncResourceGatherer>();
}

inline int                 frames         = 0;
inline bool                firstFullFrame = false;

CRenderer::SRenderFeedback CRenderer::renderLock(const CSessionLockSurface& surf) {
    static auto* const PDISABLEBAR = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:disable_loading_bar");
    static auto* const PNOFADEIN   = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:no_fade_in");
    static auto* const PNOFADEOUT  = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:no_fade_out");

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
    float           bga           = 0.0;
    const bool      WAITFORASSETS = !g_pHyprlock->m_bImmediateRender && !asyncResourceGatherer->gathered;

    if (WAITFORASSETS) {

        // render status
        if (!*PDISABLEBAR) {
            CBox progress = {0, 0, asyncResourceGatherer->progress * surf.size.x, 2};
            renderRect(progress, CColor{0.2f, 0.1f, 0.1f, 1.f}, 0);
        }
    } else {
        if (!firstFullFrame) {
            firstFullFrameTime = std::chrono::system_clock::now();
            firstFullFrame     = true;
        }

        bga = std::clamp(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now() - firstFullFrameTime).count() / 500000.0, 0.0, 1.0);

        if (*PNOFADEIN)
            bga = 1.0;

        if (g_pHyprlock->m_bFadeStarted && !*PNOFADEOUT) {
            bga =
                std::clamp(std::chrono::duration_cast<std::chrono::microseconds>(g_pHyprlock->m_tFadeEnds - std::chrono::system_clock::now()).count() / 500000.0 - 0.02, 0.0, 1.0);
            // - 0.02 so that the fade ends a little earlier than the final second
        }

        // render widgets
        const auto WIDGETS = getOrCreateWidgetsFor(&surf);
        for (auto& w : *WIDGETS) {
            feedback.needsFrame = w->draw({bga}) || feedback.needsFrame;
        }
    }

    frames++;

    Debug::log(TRACE, "frame {}", frames);

    feedback.needsFrame = feedback.needsFrame || !asyncResourceGatherer->gathered || bga < 1.0;

    glDisable(GL_BLEND);

    return feedback;
}

void CRenderer::setUpShader(const CShader& shader, const float* glMatrix, const CColor& col = CColor(), int rounding = 0) {
    glUseProgram(shader.program);
    glUniformMatrix3fv(shader.proj, 1, GL_TRUE, glMatrix);

    if (col.a >= 0.0f) { // Check if color is provided
        glUniform4f(shader.color, col.r * col.a, col.g * col.a, col.b * col.a, col.a);
    }

    if (rounding > 0) { // Check if rounding is provided
        glUniform1f(shader.radius, rounding);
    }
}

void CRenderer::setVertexAttributes(const CShader& shader, const CBox& box) {
    const auto TOPLEFT  = Vector2D(box.x, box.y);
    const auto FULLSIZE = Vector2D(box.width, box.height);

    glUniform2f(shader.topLeft, TOPLEFT.x, TOPLEFT.y);
    glUniform2f(shader.fullSize, FULLSIZE.x, FULLSIZE.y);

    glVertexAttribPointer(shader.posAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
    glEnableVertexAttribArray(shader.posAttrib);

    if (shader.texAttrib >= 0) {
        glVertexAttribPointer(shader.texAttrib, 2, GL_FLOAT, GL_FALSE, 0, fullVerts);
        glEnableVertexAttribArray(shader.texAttrib);
    }
}

void CRenderer::renderRect(const CBox& box, const CColor& col, int rounding) {
    float matrix[9];
    wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, box.rot,
                           projMatrix.data()); // TODO: write own, don't use WLR here

    float glMatrix[9];
    wlr_matrix_multiply(glMatrix, projection.data(), matrix);

    setUpShader(rectShader, glMatrix, col, rounding);
    setVertexAttributes(rectShader, box);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(rectShader.posAttrib);
}

void CRenderer::renderTexture(const CBox& box, const CTexture& tex, float alpha, int rounding, std::optional<wl_output_transform> tr) {
    float matrix[9];
    wlr_matrix_project_box(matrix, &box, tr.value_or(WL_OUTPUT_TRANSFORM_FLIPPED_180) /* ugh coordinate spaces */, box.rot,
                           projMatrix.data()); // TODO: write own, don't use WLR here

    float glMatrix[9];
    wlr_matrix_multiply(glMatrix, projection.data(), matrix);

    CShader* shader = &texShader;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(tex.m_iTarget, tex.m_iTexID);

    setUpShader(*shader, glMatrix, {}, rounding); 
    glUniform1f(shader->alpha, alpha);

    setVertexAttributes(*shader, box);

    glUniform1i(shader->tex, 0);
    glUniform1i(shader->discardOpaque, 0);
    glUniform1i(shader->discardAlpha, 0);
    glUniform1i(shader->applyTint, 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(shader->posAttrib);
    glDisableVertexAttribArray(shader->texAttrib);

    glBindTexture(tex.m_iTarget, 0);
}

std::vector<std::unique_ptr<IWidget>>* CRenderer::getOrCreateWidgetsFor(const CSessionLockSurface* surf) {
    if (!widgets.contains(surf)) {

        auto CWIDGETS = g_pConfigManager->getWidgetConfigs();

        // Sort widgets by zindex
        std::sort(CWIDGETS.begin(), CWIDGETS.end(), [](const CConfigManager::SWidgetConfig& a, const CConfigManager::SWidgetConfig& b) {
            return std::any_cast<Hyprlang::INT>(a.values.at("zindex")) < std::any_cast<Hyprlang::INT>(b.values.at("zindex"));
        });

        auto createWidget = [&](const CConfigManager::SWidgetConfig& config) {
            const auto& values  = config.values;
            const auto& monitor = config.monitor;
            const auto& output  = surf->output;

            if (!monitor.empty() && monitor != output->stringPort && !output->stringDesc.starts_with(monitor)) {
                return;
            }

            if (config.type == "background") {
                std::string path = std::any_cast<Hyprlang::STRING>(values.at("path"));
                std::string resourceID;

                if (path == "screenshot") {
                    resourceID = CDMAFrame::getResourceId(output);
                    if (asyncResourceGatherer->gathered && !asyncResourceGatherer->getAssetByID(resourceID)) {
                        resourceID = "";
                    }
                    if (!g_pHyprlock->getScreencopy()) {
                        Debug::log(ERR, "No screencopy support! path=screenshot won't work. Falling back to background color.");
                        resourceID = "";
                    }
                } else if (!path.empty()) {
                    resourceID = "background:" + path;
                }
                widgets[surf].emplace_back(std::make_unique<CBackground>(surf->size, output, resourceID, values, path == "screenshot"));
            } else if (config.type == "input-field") {
                widgets[surf].emplace_back(std::make_unique<CPasswordInputField>(surf->size, values, output->stringPort));
            } else if (config.type == "label") {
                widgets[surf].emplace_back(std::make_unique<CLabel>(surf->size, values, output->stringPort));
            } else if (config.type == "shape") {
                widgets[surf].emplace_back(std::make_unique<CShape>(surf->size, values));
            } else if (config.type == "image") {
                std::string path       = std::any_cast<Hyprlang::STRING>(values.at("path"));
                std::string resourceID = path.empty() ? "" : "image:" + path;
                widgets[surf].emplace_back(std::make_unique<CImage>(surf->size, output, resourceID, values));
            }
        };

        for (const auto& config : CWIDGETS) {
            createWidget(config);
        }
    }

    return &widgets[surf];
}

void CRenderer::blurFB(const CFramebuffer& outfb, SBlurParams params) {
    glDisable(GL_BLEND);
    glDisable(GL_STENCIL_TEST);

    // Setup projection matrix
    float matrix[9];
    CBox  box{0, 0, outfb.m_vSize.x, outfb.m_vSize.y};
    wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, 0, projMatrix.data()); // TODO: write own, don't use WLR here

    float glMatrix[9];
    wlr_matrix_multiply(glMatrix, projection.data(), matrix);

    // Allocate framebuffer
    CFramebuffer mirrors[2];
    mirrors[0].alloc(outfb.m_vSize.x, outfb.m_vSize.y, true);
    mirrors[1].alloc(outfb.m_vSize.x, outfb.m_vSize.y, true);
    CFramebuffer* currentRenderToFB = &mirrors[0];

    // Prepare shaders and texture settings
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

    // Function to set up shader parameters and draw
    auto drawPass = [&](CShader* pShader, CFramebuffer* targetFB) {
        targetFB->bind();

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

        currentRenderToFB = targetFB;
    };

    // draw the things.
    // first draw is swap -> mirr

    for (int i = 1; i <= params.passes; ++i) {
        drawPass(&blurShader1, (currentRenderToFB == &mirrors[0]) ? &mirrors[1] : &mirrors[0]); // down
    }

    for (int i = params.passes - 1; i >= 0; --i) {
        drawPass(&blurShader2, (currentRenderToFB == &mirrors[0]) ? &mirrors[1] : &mirrors[0]); // up
    }

    // finalize the image
    {
        CFramebuffer* targetFB = (currentRenderToFB == &mirrors[0]) ? &mirrors[1] : &mirrors[0];
        drawPass(&blurFinishShader, targetFB);

        glUniform1f(blurFinishShader.noise, params.noise);
        glUniform1f(blurFinishShader.brightness, params.brightness);
        glUniform1i(blurFinishShader.colorize, params.colorize.has_value());
        if (params.colorize.has_value())
            glUniform3f(blurFinishShader.colorizeTint, params.colorize->r, params.colorize->g, params.colorize->b);

        glUniform1f(blurFinishShader.boostA, params.boostA);
    }

    // Finish
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

void CRenderer::removeWidgetsFor(const CSessionLockSurface* surf) {
    widgets.erase(surf);
}
