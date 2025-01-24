#include "Scale.hpp"
#include "../Renderer.hpp"
#include "../../core/AnimationManager.hpp"
#include "../../helpers/Log.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../config/ConfigManager.hpp"
#include "../../core/hyprlock.hpp"
#include <hyprlang.hpp>

CScale::~CScale() {
    if (valueTimer) {
        valueTimer->cancel();
        valueTimer.reset();
    }
}

static void onTimer(std::shared_ptr<CTimer> self, void* data) {
    if (data == nullptr)
        return;

    const auto PSCALE = (CScale*)data;

    PSCALE->onTimerUpdate();
    PSCALE->plantTimer();
}

void CScale::onTimerUpdate() {
    updateValue();
    g_pHyprlock->renderOutput(outputStringPort);
}

void CScale::plantTimer() {
    if (value.updateEveryMs != 0)
        valueTimer = g_pHyprlock->addTimer(std::chrono::milliseconds((int)value.updateEveryMs), onTimer, this, value.allowForceUpdate);
    else if (value.updateEveryMs == 0 && value.allowForceUpdate)
        valueTimer = g_pHyprlock->addTimer(std::chrono::hours(1), onTimer, this, true);
}

CScale::CScale(const Vector2D& viewport_, const std::unordered_map<std::string, std::any>& props, const std::string& output) : viewport(viewport_), outputStringPort(output) {
    try {
        min            = std::any_cast<Hyprlang::INT>(props.at("min"));
        max            = std::any_cast<Hyprlang::INT>(props.at("max"));
        valuePreFormat = std::any_cast<Hyprlang::STRING>(props.at("value"));
        borderSize     = std::any_cast<Hyprlang::INT>(props.at("border_size"));
        borderColor    = std::any_cast<Hyprlang::INT>(props.at("border_color"));
        rounding       = std::any_cast<Hyprlang::INT>(props.at("rounding"));
        configSize     = CLayoutValueData::fromAnyPv(props.at("size"))->getAbsolute(viewport_);
        configPos      = CLayoutValueData::fromAnyPv(props.at("position"))->getAbsolute(viewport_);
        halign         = std::any_cast<Hyprlang::STRING>(props.at("halign"));
        valign         = std::any_cast<Hyprlang::STRING>(props.at("valign"));
        zindex         = std::any_cast<Hyprlang::INT>(props.at("zindex"));
        color          = std::any_cast<Hyprlang::INT>(props.at("color"));

        value = formatString(valuePreFormat);

        backgroundColor = std::any_cast<Hyprlang::INT>(props.at("background_color"));

    } catch (const std::bad_any_cast& e) { RASSERT(false, "Failed to construct CScale: {}", e.what()); } catch (const std::out_of_range& e) {
        RASSERT(false, "Missing property for CScale: {}", e.what());
    }

    pos  = posFromHVAlign(viewport, size, configPos, halign, valign);
    size = configSize;

    g_pAnimationManager->createAnimation(0.f, animatedValue, g_pConfigManager->m_AnimationTree.getConfig("inputFieldWidth"));

    updateValue();

    plantTimer();
}

void CScale::updateValue() {
    int value = getValue();
    if (value < min)
        value = min;
    else if (value > max)
        value = max;

    *animatedValue = static_cast<float>(value - min) / (max - min);
}

bool CScale::draw(const SRenderData& data) {
    CBox box = {pos.x, pos.y, size.x, size.y};
    g_pRenderer->renderRect(box, backgroundColor, rounding);

    CBox progress = {pos.x, pos.y, size.x * animatedValue->value(), size.y};
    g_pRenderer->renderRect(progress, color, rounding);

    if (borderSize > 0)
        g_pRenderer->renderBorder(box, borderColor, borderSize, rounding, data.opacity);

    return false;
}

int CScale::getValue() {
    if (value.cmd) {
        const auto _value = g_pHyprlock->spawnSync(value.formatted);
        if (_value.empty())
            return 0;
        else
            return std::stoi(_value);
    }

    return std::stoi(value.formatted);
}