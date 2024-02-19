#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
#include <chrono>
#include <vector>

class CPasswordInputField : public IWidget {
  public:
    CPasswordInputField(const Vector2D& viewport, const Vector2D& size, const CColor& outer, const CColor& inner, int out_thick, bool fade_empty);

    virtual bool draw();

  private:
    void     updateDots();
    void     updateFade();

    Vector2D size;
    Vector2D pos;

    int      out_thick;

    CColor   inner, outer;

    struct dot {
        size_t                                idx       = 0;
        bool                                  appearing = false;
        bool                                  animated  = false;
        float                                 a         = 0;
        std::chrono::system_clock::time_point start;
    };

    struct {
        std::chrono::system_clock::time_point start;
        float                                 a         = 0;
        bool                                  appearing = true;
        bool                                  animated  = false;
    } fade;

    bool             fadeOnEmpty;

    std::vector<dot> dots;
};