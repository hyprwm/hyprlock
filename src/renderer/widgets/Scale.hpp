#pragma once

#include "IWidget.hpp"
#include "../../helpers/Color.hpp"
#include "../../helpers/Math.hpp"
#include "../../helpers/AnimatedVariable.hpp"
#include "../../core/Timer.hpp"
#include <string>
#include <unordered_map>
#include <any>

class CScale : public IWidget {
  public:
    CScale(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props, const std::string& output);
    ~CScale();

    virtual bool draw(const SRenderData& data);

    void         onTimerUpdate();
    void         plantTimer();

  private:
    void                    updateValue();
    int                     getValue();

    std::string             valuePreFormat;
    IWidget::SFormatResult  value;

    std::shared_ptr<CTimer> valueTimer = nullptr;

    Vector2D                viewport;
    Vector2D                pos;
    Vector2D                size;
    Vector2D                configPos;
    Vector2D                configSize;

    std::string             halign, valign;
    int                     min, max, borderSize, rounding, zindex;
    CHyprColor              borderColor, color, backgroundColor;

    std::string             outputStringPort;

    PHLANIMVAR<float>       animatedValue;
};
