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

    wlr_matrix_identity(projMatrix.data());

    asyncResourceGatherer = std::make_unique<CAsyncResourceGatherer>();
}

static int frames = 0;

//
CRenderer::SRenderFeedback CRenderer::renderLock(const CSessionLockSurface& surf) {
    static auto* const PDISABLEBAR = (Hyprlang::INT* const*)g_pConfigManager->getValuePtr("general:disable_loading_bar");

    matrixProjection(projection.data(), surf.size.x, surf.size.y, WL_OUTPUT_TRANSFORM_NORMAL);

    g_pEGL->makeCurrent(surf.eglSurface);
    glViewport(0, 0, surf.size.x, surf.size.y);

    glScissor(frames, 0, surf.size.x, surf.size.y);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    SRenderFeedback feedback;

    const float     bga = asyncResourceGatherer->applied ?
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

        // render widgets
        const auto WIDGETS = getOrCreateWidgetsFor(&surf);
        for (auto& w : *WIDGETS) {
            feedback.needsFrame = w->draw({bga}) || feedback.needsFrame;
        }
    }

    frames++;

    Debug::log(TRACE, "frame {}", frames);

    feedback.needsFrame = feedback.needsFrame || !asyncResourceGatherer->ready || bga < 1.0;

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

void CRenderer::renderTexture(const CBox& box, const CTexture& tex, float a, int rounding) {
    float matrix[9];
    wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_FLIPPED_180 /* ugh coordinate spaces */, 0,
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
                widgets[surf].emplace_back(
                    std::make_unique<CBackground>(surf->size, PATH.empty() ? "" : std::string{"background:"} + PATH, std::any_cast<Hyprlang::INT>(c.values.at("color"))));
            } else if (c.type == "input-field") {
                const auto SIZE = std::any_cast<Hyprlang::VEC2>(c.values.at("size"));
                widgets[surf].emplace_back(std::make_unique<CPasswordInputField>(
                    surf->size, Vector2D{SIZE.x, SIZE.y}, std::any_cast<Hyprlang::INT>(c.values.at("dot_color")), std::any_cast<Hyprlang::INT>(c.values.at("outer_color")), std::any_cast<Hyprlang::INT>(c.values.at("inner_color")),
                    std::any_cast<Hyprlang::INT>(c.values.at("outline_thickness")), std::any_cast<Hyprlang::INT>(c.values.at("fade_on_empty"))));
            } else if (c.type == "label") {
                widgets[surf].emplace_back(std::make_unique<CLabel>(surf->size, c.values));
            }
        }
    }

    return &widgets[surf];
}
