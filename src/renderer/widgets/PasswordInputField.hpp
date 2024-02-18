#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
#include <chrono>
#include <vector>

class CPasswordInputField : public IWidget {
  public:
    CPasswordInputField(const Vector2D& viewport, const Vector2D& size, const CColor& outer, const CColor& inner, int out_thick);

    virtual bool draw();

  private:
    void     updateDots();

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

    std::vector<dot> dots;
};