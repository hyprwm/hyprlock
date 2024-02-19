#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
#include <chrono>
#include <vector>

class CPasswordInputField : public IWidget {
  public:
    CPasswordInputField(const Vector2D& viewport, const Vector2D& size, const CColor& dot_color, const CColor& outer, const CColor& inner, int out_thick, bool fade_empty);

    virtual bool draw(const SRenderData& data);

  private:
    void     updateDots();
    void     updateFade();

    Vector2D size;
    Vector2D pos;

    int      out_thick;

    CColor   dot_color, inner, outer;

    struct {
        float                                 currentAmount  = 0;
        float                                 speedPerSecond = 5; // actually per... something. I am unsure xD
        std::chrono::system_clock::time_point lastFrame;
    } dots;

    struct {
        std::chrono::system_clock::time_point start;
        float                                 a         = 0;
        bool                                  appearing = true;
        bool                                  animated  = false;
    } fade;

    bool fadeOnEmpty;
};
