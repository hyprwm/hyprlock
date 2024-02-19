#pragma once

class IWidget {
  public:
    struct SRenderData {
        float opacity = 1;
    };

    virtual bool draw(const SRenderData& data) = 0;
};