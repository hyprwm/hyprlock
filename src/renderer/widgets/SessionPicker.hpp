#pragma once

#include "IWidget.hpp"
#include "Shadowable.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../helpers/Color.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <string>
#include <any>
#include <vector>

struct SPreloadedAsset;
class CSessionLockSurface;

class CSessionPicker : public IWidget {
  public:
    struct SSessionEntry {
        SLoginSessionConfig m_loginSession;
        ResourceID          m_textResourceID;
        ASP<CTexture>       m_textAsset = nullptr;
    };

    CSessionPicker() = default;
    ~CSessionPicker();

    void         registerSelf(const ASP<CSessionPicker>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& output);
    virtual bool draw(const SRenderData& data);
    virtual void onAssetUpdate(ResourceID id, ASP<CTexture> newAsset);
    virtual CBox getBoundingBoxWl() const;
    virtual void onClick(uint32_t button, bool down, const Vector2D& pos);
    virtual void onHover(const Vector2D& pos);

    void         onGotSessionEntryAsset(const std::string& sessionName);

  private:
    void                       requestSessionEntryTexts();

    AWP<CSessionPicker>        m_self;
    std::vector<SSessionEntry> m_loginSessions;

    Vector2D                   m_viewport;

    Vector2D                   m_configPos;
    Vector2D                   m_entrySize;
    int                        m_fontSize;
    std::string                m_halign       = "";
    std::string                m_valign       = "";
    int                        m_rounding     = -1;
    int                        m_borderSize   = -1;
    int                        m_entryHeight  = -1;
    int                        m_entrySpacing = -1;
    double                     m_textAlpha    = 1.0;

    Vector2D                   m_biggestEntryAssetSize;

    CBox                       m_box;

    struct {
        CHyprColor         text;
        CHyprColor         inner;
        CHyprColor         selected;

        CGradientValueData border;
        CGradientValueData selectedBorder;
    } m_colorConfig;

    CShadowable m_shadow;
    bool        m_updateShadow = true;
};
