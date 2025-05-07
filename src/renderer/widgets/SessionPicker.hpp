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
    struct SSessionAsset {
        SLoginSessionConfig m_loginSession;
        std::string         m_textResourceID;
        SPreloadedAsset*    m_textAsset = nullptr;
    };

    CSessionPicker()  = default;
    ~CSessionPicker() = default;

    void         registerSelf(const SP<CSessionPicker>& self);

    virtual void configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput);
    virtual bool draw(const SRenderData& data);

    void         onGotSessionEntryAsset(const std::string& sessionName);

  private:
    void                       setupSessionEntryTexts();

    WP<CSessionPicker>         m_self;
    std::vector<SSessionAsset> m_loginSessions;

    Vector2D                   m_viewport;
    Vector2D                   m_configPos;
    Vector2D                   m_size;
    std::string                m_halign       = "";
    std::string                m_valign       = "";
    int                        m_rounding     = -1;
    int                        m_borderSize   = -1;
    int                        m_entryHeight  = -1;
    int                        m_entrySpacing = -1;

    Vector2D                   m_biggestEntryAssetSize;

    struct {
        CHyprColor         inner;
        CHyprColor         selected;

        CGradientValueData border;
        CGradientValueData selectedBorder;
    } m_colorConfig;

    CShadowable m_shadow;
    bool        m_updateShadow = true;
};
