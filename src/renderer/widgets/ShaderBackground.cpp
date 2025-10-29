#include "ShaderBackground.hpp"

#include "../Renderer.hpp"
#include "../Shaders.hpp"
#include "../../helpers/Log.hpp"
#include "../../helpers/Color.hpp"
#include "../../core/hyprlock.hpp"

#include <GLES3/gl32.h>
#include <fstream>
#include <sstream>

// local full-screen quad in clip space (triangle strip)
static const float FS_VERTS[] = {
    -1.0f, 1.0f,  // top-left
    1.0f,  1.0f,  // top-right
    -1.0f, -1.0f, // bottom-left
    1.0f,  -1.0f, // bottom-right
};

// simple UVs
static const float FS_UVS[] = {
    0.0f, 1.0f, // top-left
    1.0f, 1.0f, // top-right
    0.0f, 0.0f, // bottom-left
    1.0f, 0.0f, // bottom-right
};

static std::string readFileToString(const std::string& path) {
    std::ifstream ifs(path);
    if (!ifs.good())
        return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static GLuint compileShader(GLenum type, const std::string& src) {
    GLuint      shader = glCreateShader(type);
    const char* s      = src.c_str();
    glShaderSource(shader, 1, &s, nullptr);
    glCompileShader(shader);

    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        if (len > 0)
            glGetShaderInfoLog(shader, len, nullptr, log.data());
        Debug::log(ERR, "ShaderBackground: shader compile failed: {}", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint linkProgram(const std::string& vert, const std::string& frag) {
    GLuint vs = compileShader(GL_VERTEX_SHADER, vert);
    if (!vs)
        return 0;

    GLuint fs = compileShader(GL_FRAGMENT_SHADER, frag);
    if (!fs) {
        glDeleteShader(vs);
        return 0;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    glDetachShader(prog, vs);
    glDetachShader(prog, fs);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (ok != GL_TRUE) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::string log(len, '\0');
        if (len > 0)
            glGetProgramInfoLog(prog, len, nullptr, log.data());
        Debug::log(ERR, "ShaderBackground: program link failed: {}", log);
        glDeleteProgram(prog);
        return 0;
    }

    return prog;
}

void CShaderBackground::registerSelf(const ASP<CShaderBackground>& self) {
    m_self = self;
}

void CShaderBackground::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    reset();

    m_viewport    = pOutput->getViewport();
    m_monitorName = pOutput->stringPort;

    try {
        m_fragPath = std::any_cast<Hyprlang::STRING>(props.at("frag_path"));
    } catch (const std::bad_any_cast& e) { RASSERT(false, "ShaderBackground: invalid config values: {}", e.what()); } catch (const std::out_of_range& e) {
        RASSERT(false, "ShaderBackground: missing required config values: {}", e.what());
    }

    loadProgram();
    m_startTime = std::chrono::system_clock::now();
}

void CShaderBackground::reset() {
    destroyProgram();
}

void CShaderBackground::destroyProgram() {
    if (m_program) {
        glDeleteProgram(m_program);
        m_program = 0;
    }
    m_locProj       = -1;
    m_locTime       = -1;
    m_locResolution = -1;
    m_posAttrib     = -1;
}

void CShaderBackground::loadProgram() {
    if (m_fragPath.empty()) {
        Debug::log(WARN, "ShaderBackground: frag_path not set; skipping load");
        return;
    }

    const std::string fragSrc = readFileToString(m_fragPath);
    if (fragSrc.empty()) {
        Debug::log(ERR, "ShaderBackground: failed to read fragment shader '{}'", m_fragPath);
        return;
    }

    // use the common quad vertex shader provided by hyprlock
    GLuint prog = linkProgram(QUADVERTSRC, fragSrc);
    if (!prog) {
        Debug::log(ERR, "ShaderBackground: failed to build program from '{}'", m_fragPath);
        return; // keep previous program if any
    }

    // swap in new program
    if (m_program)
        glDeleteProgram(m_program);
    m_program = prog;

    // cache locations
    m_locProj       = glGetUniformLocation(m_program, "proj");
    m_locTime       = glGetUniformLocation(m_program, "time");
    m_locResolution = glGetUniformLocation(m_program, "resolution");
    m_posAttrib     = glGetAttribLocation(m_program, "pos");

    Debug::log(LOG, "ShaderBackground: loaded '{}'", m_fragPath);
}

bool CShaderBackground::draw(const SRenderData& data) {
    // if no program, do nothing.
    if (!m_program) {
        return false;
    }

    glUseProgram(m_program);

    // proj as identity so positions are in clip space already
    if (m_locProj >= 0) {
        const float I[9] = {
            1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f,
        };
        glUniformMatrix3fv(m_locProj, 1, GL_FALSE, I);
    }

    // time (seconds since start)
    if (m_locTime >= 0) {
        const auto  now = std::chrono::system_clock::now();
        const float t   = std::chrono::duration<float>(now - m_startTime).count();
        glUniform1f(m_locTime, t);
    }

    // resolution in pixels
    if (m_locResolution >= 0) {
        glUniform2f(m_locResolution, m_viewport.x, m_viewport.y);
    }

    // attributes
    if (m_posAttrib < 0)
        m_posAttrib = glGetAttribLocation(m_program, "pos");

    if (m_posAttrib >= 0) {
        glVertexAttribPointer(m_posAttrib, 2, GL_FLOAT, GL_FALSE, 0, FS_VERTS);
        glEnableVertexAttribArray(m_posAttrib);
    }

    // bind optional attributes if present (safe if they are optimized out)
    const GLint texAttrib      = glGetAttribLocation(m_program, "texcoord");
    const GLint matteTexAttrib = glGetAttribLocation(m_program, "texcoordMatte");
    if (texAttrib >= 0) {
        glVertexAttribPointer(texAttrib, 2, GL_FLOAT, GL_FALSE, 0, FS_UVS);
        glEnableVertexAttribArray(texAttrib);
    }
    if (matteTexAttrib >= 0) {
        glVertexAttribPointer(matteTexAttrib, 2, GL_FLOAT, GL_FALSE, 0, FS_UVS);
        glEnableVertexAttribArray(matteTexAttrib);
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    if (m_posAttrib >= 0)
        glDisableVertexAttribArray(m_posAttrib);
    if (texAttrib >= 0)
        glDisableVertexAttribArray(texAttrib);
    if (matteTexAttrib >= 0)
        glDisableVertexAttribArray(matteTexAttrib);

    // return true to keep frames flowing for animated shaders
    return true;
}

void CShaderBackground::onAssetUpdate(ResourceID id, ASP<CTexture> newAsset) {
    // no external assets used; nothing to do.
    (void)id;
    (void)newAsset;
}
