#pragma once

#include "IWidget.hpp"
#include "../../helpers/Vector2D.hpp"
#include <string>
#include <unordered_map>
#include <any>

struct SPreloadedAsset;

class CLabel : public IWidget {
  public:
    CLabel(const Vector2D& viewport, const std::unordered_map<std::string, std::any>& props);

    virtual bool draw(const SRenderData& data);

  private:
    std::string      formatString(std::string in);

    Vector2D         viewport;
    Vector2D         pos;
    std::string      resourceID;
    std::string      label;
    std::string      halign, valign;
    SPreloadedAsset* asset = nullptr;
};