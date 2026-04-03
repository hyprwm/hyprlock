#include "SessionPicker.hpp"
#include "../AsyncResourceManager.hpp"
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

CSessionPicker::~CSessionPicker() {
    if (!g_asyncResourceManager)
        return;

    for (auto& loginSessionEntry : m_loginSessions) {
        loginSessionEntry.m_textAsset.reset();
        g_asyncResourceManager->unloadById(loginSessionEntry.m_textResourceID);
    }
}

void CSessionPicker::registerSelf(const ASP<CSessionPicker>& self) {
    m_self = self;
}

void CSessionPicker::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& output) {
    m_viewport = output->getViewport();

    m_shadow.configure(m_self, props, m_viewport);

    try {
        m_configPos                  = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(m_viewport);
        m_entrySize                  = CLayoutValueData::fromAnyPv(props.at("entry_size"))->getAbsolute(m_viewport);
        m_fontSize                   = std::any_cast<Hyprlang::INT>(props.at("font_size"));
        m_rounding                   = std::any_cast<Hyprlang::INT>(props.at("rounding"));
        m_borderSize                 = std::any_cast<Hyprlang::INT>(props.at("border_size"));
        m_entrySpacing               = std::any_cast<Hyprlang::FLOAT>(props.at("entry_spacing"));
        m_colorConfig.text           = std::any_cast<Hyprlang::INT>(props.at("text_color"));
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

    m_textAlpha = m_colorConfig.text.a;

    requestSessionEntryTexts();
}

void CSessionPicker::requestSessionEntryTexts() {
    const auto LOGINSESSIONS = g_loginSessionManager->getLoginSessions();

    m_loginSessions.resize(LOGINSESSIONS.size());
    for (size_t i = 0; i < LOGINSESSIONS.size(); i++) {
        const auto&                                    LOGINSESSION = LOGINSESSIONS[i];
        Hyprgraphics::CTextResource::STextResourceData request;

        request.text = LOGINSESSION.name;
        // request.font     = fontFamily;
        request.fontSize = m_fontSize;
        request.color    = m_colorConfig.text.asRGB();

        m_loginSessions[i] = {
            .m_loginSession   = LOGINSESSION,
            .m_textResourceID = g_asyncResourceManager->requestText(request, m_self),
        };
    };
}

void CSessionPicker::onAssetUpdate(ResourceID id, ASP<CTexture> newAsset) {
    Log::logger->log(Log::TRACE, "CSessionPicker text for resourceID {}", id);

    m_biggestEntryAssetSize = Vector2D{
        std::max<double>(m_biggestEntryAssetSize.x, newAsset->m_vSize.x),
        newAsset->m_vSize.y,
    };

    m_entrySize.x = std::max(m_entrySize.x, m_biggestEntryAssetSize.x);
    m_entrySize.y = std::max(m_entrySize.y, m_biggestEntryAssetSize.y);

    for (auto& loginSessionEntry : m_loginSessions) {
        if (loginSessionEntry.m_textResourceID == id)
            loginSessionEntry.m_textAsset = newAsset;
    }
}

bool CSessionPicker::draw(const SRenderData& data) {
    if (std::ranges::any_of(m_loginSessions, [](const auto& sessionEntry) { return sessionEntry.m_textAsset == nullptr; }))
        return false; // rely on callback

    const size_t   SELECTEDENTRYINDEX = g_loginSessionManager->getSelectedLoginSessionIndex();

    const double   PAD = m_biggestEntryAssetSize.y / 2;
    const Vector2D SIZE{
        m_entrySize.x + PAD + (m_borderSize * 2),
        ((m_entrySize.y + m_borderSize) * m_loginSessions.size() + m_borderSize) + ((m_loginSessions.size() > 1) ? m_entrySpacing * (m_loginSessions.size() - 1) : 0),
    };

    m_box = CBox{
        posFromHVAlign(m_viewport, SIZE, m_configPos, m_halign, m_valign),
        SIZE,
    };

    for (size_t i = 0; i < m_loginSessions.size(); ++i) {
        auto&      sessionEntry = m_loginSessions[i];

        const CBox ENTRYBOX{
            m_box.x,
            (m_box.y + m_box.h) - (m_entrySize.y + 2 * m_borderSize) - (i * (m_entrySize.y + m_entrySpacing + m_borderSize)),
            m_box.w,
            m_entrySize.y + (2 * m_borderSize),
        };

        const CBox INNERBOX{ENTRYBOX.pos() + Vector2D{m_borderSize, m_borderSize}, ENTRYBOX.size() - Vector2D{2 * m_borderSize, 2 * m_borderSize}};

        const auto INNERROUND = roundingForBox(INNERBOX, m_rounding);
        const bool SELECTED   = i == SELECTEDENTRYINDEX;

        CHyprColor entryCol;
        if (SELECTED)
            entryCol = CHyprColor{m_colorConfig.selected.asRGB(), m_colorConfig.selected.a * data.opacity};
        else
            entryCol = CHyprColor{m_colorConfig.inner.asRGB(), m_colorConfig.inner.a * data.opacity};

        g_pRenderer->renderRect(INNERBOX, entryCol, INNERROUND);

        if (m_borderSize > 0) {
            const auto ENTRYBORDERROUND = roundingForBorderBox(ENTRYBOX, m_rounding, m_borderSize);
            g_pRenderer->renderBorder(ENTRYBOX, (SELECTED) ? m_colorConfig.selectedBorder : m_colorConfig.border, m_borderSize, ENTRYBORDERROUND, data.opacity);
        }

        if (sessionEntry.m_textAsset) {
            const CBox ASSETBOXCENTERED{
                INNERBOX.pos() +
                    Vector2D{
                        (INNERBOX.size().x / 2) - (sessionEntry.m_textAsset->m_vSize.x / 2),
                        (INNERBOX.size().y / 2) - (sessionEntry.m_textAsset->m_vSize.y / 2),
                    },
                sessionEntry.m_textAsset->m_vSize,
            };
            g_pRenderer->renderTexture(ASSETBOXCENTERED, *sessionEntry.m_textAsset, data.opacity * m_textAlpha);
        }
    }

    return false;
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
    g_loginSessionManager->selectSession(SELECTEDENTRY);
    g_pHyprlock->renderAllOutputs();
}

void CSessionPicker::onHover(const Vector2D& pos) {
    g_pSeatManager->m_pCursorShape->setShape(WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT);
}
