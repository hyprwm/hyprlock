#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include <string>

struct SPreloadedAsset;

class CBackground : public IWidget {
  public:
    CBackground(const Vector2D& viewport, const std::string& resourceID);

    virtual bool draw();

  private:
    Vector2D         viewport;
    std::string      resourceID;
    SPreloadedAsset* asset = nullptr;
};