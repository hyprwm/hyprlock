#include "SessionPicker.hpp"
#include "../Renderer.hpp"
#include "../../helpers/Log.hpp"
#include "../../core/hyprlock.hpp"
#include "../../helpers/Color.hpp"
#include "../../config/ConfigDataValues.hpp"
#include <hyprlang.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <stdexcept>

void CSessionPicker::registerSelf(const SP<CSessionPicker>& self) {
    m_self = self;
}

void CSessionPicker::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    m_viewport = pOutput->getViewport();

    m_shadow.configure(m_self.lock(), props, m_viewport);

    try {
        m_configPos                  = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(m_viewport);
        m_size                       = CLayoutValueData::fromAnyPv(props.at("size"))->getAbsolute(m_viewport);
        m_rounding                   = std::any_cast<Hyprlang::INT>(props.at("rounding"));
        m_borderSize                 = std::any_cast<Hyprlang::INT>(props.at("border_size"));
        m_entrySpacing               = std::any_cast<Hyprlang::FLOAT>(props.at("entry_spacing"));
        m_colorConfig.inner          = std::any_cast<Hyprlang::INT>(props.at("inner_color"));
        m_colorConfig.selected       = std::any_cast<Hyprlang::INT>(props.at("selected_color"));
        m_colorConfig.border         = CGradientValueData::fromAnyPv(props.at("border_color"));
        m_colorConfig.selectedBorder = CGradientValueData::fromAnyPv(props.at("selected_border_color"));
        m_halign                     = std::any_cast<Hyprlang::STRING>(props.at("halign"));
        m_valign                     = std::any_cast<Hyprlang::STRING>(props.at("valign"));
    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CSessionPicker: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CSessionPicker: {}", e.what()); //
    }

    requestSessionEntryTexts();
}

bool CSessionPicker::draw(const SRenderData& data) {
    const Vector2D SIZE{std::max(m_size.x, static_cast<double>(m_biggestEntryTextWidth)), m_size.y};
    const CBox     RECTBOX{
        posFromHVAlign(m_viewport, SIZE, m_configPos, m_halign, m_valign),
        SIZE,
    };

    const auto ENTRYHEIGHT = RECTBOX.h / (m_loginSessions.size() + 1);
    const auto TOPLEFT     = RECTBOX.pos() + Vector2D{0.0, RECTBOX.h};

    for (size_t i = 0; i < m_loginSessions.size(); ++i) {
        auto&      sessionEntry = m_loginSessions[i];
        const CBox ENTRYBOX{
            TOPLEFT.x,
            TOPLEFT.y - ENTRYHEIGHT - (i * (ENTRYHEIGHT + m_entrySpacing)),
            RECTBOX.w,
            ENTRYHEIGHT,
        };

        const auto ENTRYROUND = roundingForBox(ENTRYBOX, m_rounding);
        const bool SELECTED   = i == g_pHyprlock->m_sGreetdLoginSessionState.iSelectedLoginSession;

        CHyprColor entryCol;
        if (SELECTED)
            entryCol = CHyprColor{m_colorConfig.selected.asRGB(), m_colorConfig.selected.a * data.opacity};
        else
            entryCol = CHyprColor{m_colorConfig.inner.asRGB(), m_colorConfig.inner.a * data.opacity};

        g_pRenderer->renderRect(ENTRYBOX, entryCol, ENTRYROUND);
        if (m_borderSize > 0) {
            const CBox ENTRYBORDERBOX{
                ENTRYBOX.pos() - Vector2D{m_borderSize, m_borderSize},
                ENTRYBOX.size() + Vector2D{2 * m_borderSize, 2 * m_borderSize},
            };

            const auto ENTRYBORDERROUND = roundingForBorderBox(ENTRYBORDERBOX, m_rounding, m_borderSize);
            g_pRenderer->renderBorder(ENTRYBORDERBOX, (SELECTED) ? *m_colorConfig.selectedBorder : *m_colorConfig.border, m_borderSize, ENTRYBORDERROUND, data.opacity);
        }

        if (sessionEntry.m_textAsset) {
            const CBox ASSETBOXCENTERED{
                ENTRYBOX.pos() +
                    Vector2D{
                        (ENTRYBOX.size().x / 2) - (sessionEntry.m_textAsset->texture.m_vSize.x / 2),
                        (ENTRYBOX.size().y / 2) - (sessionEntry.m_textAsset->texture.m_vSize.y / 2),
                    },
                sessionEntry.m_textAsset->texture.m_vSize,
            };
            g_pRenderer->renderTexture(ASSETBOXCENTERED, sessionEntry.m_textAsset->texture, data.opacity);
        }
    }

    return false;
}

// TODO: Move this out of here, so it does not get called for each monitor

static void onAssetCallback(WP<CSessionPicker> self, const std::string& sessionName) {
    if (auto PSELF = self.lock())
        PSELF->onGotSessionEntryAsset(sessionName);
}

void CSessionPicker::onGotSessionEntryAsset(const std::string& sessionName) {
    auto session = std::ranges::find_if(m_loginSessions, [&sessionName](const SSessionAsset& s) { return s.m_loginSession.name == sessionName; });
    if (session == m_loginSessions.end()) {
        Debug::log(ERR, "Failed to find session entry for {}", sessionName);
        return;
    }

    session->m_textAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(session->m_textResourceID);
    if (!session->m_textAsset)
        Debug::log(ERR, "Failed to get asset for session entry {}", sessionName);
    else
        m_biggestEntryTextWidth = std::max(m_biggestEntryTextWidth, static_cast<size_t>(session->m_textAsset->texture.m_vSize.x));
}

void CSessionPicker::requestSessionEntryTexts() {
    m_loginSessions.resize(g_pHyprlock->m_sGreetdLoginSessionState.vLoginSessions.size());
    for (size_t i = 0; i < m_loginSessions.size(); ++i) {
        const auto& SESSIONCONFIG           = g_pHyprlock->m_sGreetdLoginSessionState.vLoginSessions[i];
        m_loginSessions[i].m_textResourceID = std::format("session:{}-{}", (uintptr_t)this, SESSIONCONFIG.name);

        // request asset preload
        CAsyncResourceGatherer::SPreloadRequest request;
        request.id    = m_loginSessions[i].m_textResourceID;
        request.asset = SESSIONCONFIG.name;
        request.type  = CAsyncResourceGatherer::eTargetType::TARGET_TEXT;
        //request.props["font_family"] = fontFamily;
        //request.props["color"]     = colorConfig.font;
        //request.props["font_size"] = rowHeight;
        request.callback = [REF = m_self, sessionName = SESSIONCONFIG.name]() { onAssetCallback(REF, sessionName); };

        m_loginSessions[i].m_loginSession = SESSIONCONFIG;

        g_pRenderer->asyncResourceGatherer->requestAsyncAssetPreload(request);
    }
}
