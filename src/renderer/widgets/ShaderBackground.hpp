#pragma once

#include "IWidget.hpp"
#include "../../defines.hpp"

#include "../../helpers/Math.hpp"
#include <hyprlang.hpp>
#include <GLES3/gl32.h>
#include <string>
#include <unordered_map>
#include <any>

#include <chrono>

class COutput;

// intended to be used as a background (low zindex), so other widgets (inputs, labels, etc.) render on top.
class CShaderBackground : public IWidget {
  public:
    CShaderBackground()  = default;
    ~CShaderBackground() = default;

    // registration helper to keep a weak reference to self for timers/callbacks
    void registerSelf(const ASP<CShaderBackground>& self);

    // widget
    void configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) override;
    bool draw(const SRenderData& data) override;
    void onAssetUpdate(ResourceID id, ASP<CTexture> newAsset) override;

    // lifecycle
    void reset();

  private:
    // shader program lifecycle
    void loadProgram();
    void destroyProgram();

  private:
    // self-ref
    AWP<CShaderBackground> m_self;

    // config
    std::string m_fragPath;

    // output info
    Vector2D    m_viewport = {0, 0};
    std::string m_monitorName;

    // shader program and locations
    GLuint m_program       = 0;
    GLint  m_locProj       = -1;
    GLint  m_locTime       = -1;
    GLint  m_locResolution = -1;
    GLint  m_posAttrib     = -1;

    // animation/time
    std::chrono::system_clock::time_point m_startTime = std::chrono::system_clock::now();
};
