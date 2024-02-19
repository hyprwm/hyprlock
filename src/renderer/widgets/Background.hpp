#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include "../../helpers/Color.hpp"
#include <string>

struct SPreloadedAsset;

class CBackground : public IWidget {
  public:
    CBackground(const Vector2D& viewport, const std::string& resourceID, const CColor& color);

    virtual bool draw(const SRenderData& data);

  private:
    Vector2D         viewport;
    std::string      resourceID;
    CColor           color;
    SPreloadedAsset* asset = nullptr;
};