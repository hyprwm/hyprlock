#include "SessionPicker.hpp"
#include "../Renderer.hpp"
#include "../../helpers/Log.hpp"
#include "../../helpers/Color.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../config/LoginSessionManager.hpp"
#include "../../core/Seat.hpp"
#include "../../core/hyprlock.hpp"
#include <algorithm>
#include <hyprlang.hpp>
#include <hyprutils/math/Vector2D.hpp>
#include <stdexcept>

void CSessionPicker::registerSelf(const ASP<CSessionPicker>& self) {
    m_self = self;
}

void CSessionPicker::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    m_viewport = pOutput->getViewport();

    m_shadow.configure(m_self, props, m_viewport);

    try {
        m_configPos                  = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(m_viewport);
        m_size                       = CLayoutValueData::fromAnyPv(props.at("size"))->getAbsolute(m_viewport);
        m_rounding                   = std::any_cast<Hyprlang::INT>(props.at("rounding"));
        m_borderSize                 = std::any_cast<Hyprlang::INT>(props.at("border_size"));
        m_entrySpacing               = std::any_cast<Hyprlang::FLOAT>(props.at("entry_spacing"));
        m_colorConfig.inner          = std::any_cast<Hyprlang::INT>(props.at("inner_color"));
        m_colorConfig.selected       = std::any_cast<Hyprlang::INT>(props.at("selected_color"));
        m_colorConfig.border         = *CGradientValueData::fromAnyPv(props.at("border_color"));
        m_colorConfig.selectedBorder = *CGradientValueData::fromAnyPv(props.at("selected_border_color"));
        m_halign                     = std::any_cast<Hyprlang::STRING>(props.at("halign"));
        m_valign                     = std::any_cast<Hyprlang::STRING>(props.at("valign"));
    } catch (const std::bad_any_cast& e) {
        RASSERT(false, "Failed to construct CSessionPicker: {}", e.what()); //
    } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CSessionPicker: {}", e.what()); //
    }

    setupSessionEntryTexts();
}

bool CSessionPicker::draw(const SRenderData& data) {
    const size_t   SELECTEDENTRYINDEX = g_pLoginSessionManager->getSelectedLoginSessionIndex();

    const double   PAD = std::abs((m_size.y - m_biggestEntryAssetSize.y) / 2);
    const Vector2D SIZE{std::max(m_size.x, m_biggestEntryAssetSize.x + PAD), m_size.y};
    m_box = CBox{
        posFromHVAlign(m_viewport, SIZE, m_configPos, m_halign, m_valign),
        SIZE,
    };

    const auto ENTRYHEIGHT = m_box.h / (m_loginSessions.size() + 1);
    const auto TOPLEFT     = m_box.pos() + Vector2D{0.0, m_box.h};

    for (size_t i = 0; i < m_loginSessions.size(); ++i) {
        auto&      sessionEntry = m_loginSessions[i];

        const CBox ENTRYBOX{
            TOPLEFT.x,
            TOPLEFT.y - ENTRYHEIGHT - (i * (ENTRYHEIGHT + m_entrySpacing)),
            m_box.w,
            ENTRYHEIGHT,
        };

        const auto ENTRYROUND = roundingForBox(ENTRYBOX, m_rounding);
        const bool SELECTED   = i == SELECTEDENTRYINDEX;

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
            g_pRenderer->renderBorder(ENTRYBORDERBOX, (SELECTED) ? m_colorConfig.selectedBorder : m_colorConfig.border, m_borderSize, ENTRYBORDERROUND, data.opacity);
        }

        if (!sessionEntry.m_textAsset) {
            sessionEntry.m_textAsset = g_pRenderer->asyncResourceGatherer->getAssetByID(sessionEntry.m_textResourceID);
            if (sessionEntry.m_textAsset)
                m_biggestEntryAssetSize = Vector2D{
                    std::max<double>(m_biggestEntryAssetSize.x, sessionEntry.m_textAsset->texture.m_vSize.x),
                    sessionEntry.m_textAsset->texture.m_vSize.y,
                };
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

    return false; // rely on the asset update callback in case m_textAsset is a nullptr
}

void CSessionPicker::setupSessionEntryTexts() {
    const auto& LOGINSESSIONS           = g_pLoginSessionManager->getLoginSessions();
    const auto& LOGINSESSIONRESOURCEIDS = g_pLoginSessionManager->getLoginSessionResourceIds();

    RASSERT(LOGINSESSIONS.size() == LOGINSESSIONRESOURCEIDS.size(), "Login session resource IDs size does not match login sessions size");

    m_loginSessions.resize(LOGINSESSIONS.size());
    for (size_t i = 0; i < LOGINSESSIONS.size(); i++) {
        m_loginSessions[i] = {
            .m_loginSession   = LOGINSESSIONS[i],
            .m_textResourceID = LOGINSESSIONRESOURCEIDS[i],
        };
    };
}

CBox CSessionPicker::getBoundingBoxWl() const {
    return {
        Vector2D{m_box.pos().x, m_viewport.y - m_box.pos().y - m_box.size().y},
        m_box.size(),
    };
}

void CSessionPicker::onClick(uint32_t button, bool down, const Vector2D& pos) {
    const auto   DIFFERENTIAL   = pos.y - (m_viewport.y - m_box.pos().y - m_box.size().y);
    const auto   HEIGHTPERENTRY = m_box.size().y / m_loginSessions.size();
    const size_t SELECTEDENTRY  = std::floor(DIFFERENTIAL / HEIGHTPERENTRY);
    g_pLoginSessionManager->selectSession(SELECTEDENTRY);
    Debug::log(LOG, "clicked on entry: DIFF {} H/E {} SEL {}", DIFFERENTIAL, HEIGHTPERENTRY, SELECTEDENTRY);
    g_pHyprlock->renderAllOutputs();
}

void CSessionPicker::onHover(const Vector2D& pos) {
    g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
}
