#pragma once

#include <hyprgraphics/resource/resources/AsyncResource.hpp>
#include <hyprgraphics/resource/resources/TextResource.hpp>

class CTextCmdResource : public Hyprgraphics::IAsyncResource {
  public:
    CTextCmdResource(Hyprgraphics::CTextResource::STextResourceData&& data);
    virtual ~CTextCmdResource() = default;

    virtual void render();

  private:
    Hyprgraphics::CTextResource::STextResourceData m_data;
};
