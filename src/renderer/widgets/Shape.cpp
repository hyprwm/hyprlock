#include "Shape.hpp"
#include "../Renderer.hpp"
#include "../../config/ConfigDataValues.hpp"
#include "../../helpers/Log.hpp"
#include <hyprlang.hpp>
#include <GLES3/gl32.h>
#include <cmath>
#include <optional> // Added for SBlurParams::std::optional<CHyprColor>

void CShape::registerSelf(const SP<CShape>& self) {
    m_self = self;
}

std::string CShape::type() const {
    return "shape";
}

void CShape::configure(const std::unordered_map<std::string, std::any>& props, const SP<COutput>& pOutput) {
    viewport = pOutput->getViewport();

    try {
        // Parse position
        pos = {0, 0};
        if (props.contains("position")) {
            auto val = props.at("position");
            if (val.type() == typeid(Hyprlang::VEC2)) {
                auto vec = std::any_cast<Hyprlang::VEC2>(val);
                pos = {static_cast<double>(vec.x), static_cast<double>(vec.y)};
            } else {
                Debug::log(WARN, "Shape position has unexpected type, defaulting to (0, 0)");
            }
        }

        // Parse size
        size = {100, 100};
        if (props.contains("size")) {
            auto val = props.at("size");
            if (val.type() == typeid(Hyprlang::VEC2)) {
                auto vec = std::any_cast<Hyprlang::VEC2>(val);
                size = {static_cast<double>(vec.x), static_cast<double>(vec.y)};
            } else {
                Debug::log(WARN, "Shape size has unexpected type, defaulting to 100x100");
            }
        }

        // Parse color
        color = CHyprColor(1.0, 1.0, 1.0, 0.5); // Semi-transparent white default
        if (props.contains("color")) {
            auto colorVal = props.at("color");
            if (colorVal.type() == typeid(Hyprlang::STRING)) {
                std::string colorStr = std::any_cast<Hyprlang::STRING>(colorVal);
                if (colorStr.starts_with("0x") || colorStr.starts_with("#"))
                    colorStr = colorStr.substr(2);
                uint64_t colorValue = std::stoull(colorStr, nullptr, 16);
                color = CHyprColor(colorValue);
            } else if (colorVal.type() == typeid(Hyprlang::INT)) {
                uint64_t colorValue = std::any_cast<Hyprlang::INT>(colorVal);
                color = CHyprColor(colorValue);
            } else {
                Debug::log(WARN, "Shape color has unexpected type, defaulting to semi-transparent white");
            }
        }

        // Parse shape type
        shapeType = "rectangle";
        if (props.contains("shape")) {
            auto val = props.at("shape");
            if (val.type() == typeid(Hyprlang::STRING)) {
                shapeType = std::any_cast<Hyprlang::STRING>(val);
            } else {
                Debug::log(WARN, "Shape type has unexpected type, defaulting to rectangle");
            }
        }

        // Parse blur
        blurEnabled = false;
        blurParams = {.size = 0, .passes = 0}; // Initialize local blurParams struct
        if (props.contains("blur")) {
            auto val = props.at("blur");
            if (val.type() == typeid(Hyprlang::INT) && std::any_cast<Hyprlang::INT>(val) > 0) {
                blurEnabled = true;
                blurParams.size = std::any_cast<Hyprlang::INT>(val);
                blurParams.passes = 3; // Default passes
            } else if (val.type() == typeid(Hyprlang::FLOAT) && std::any_cast<Hyprlang::FLOAT>(val) > 0) {
                blurEnabled = true;
                blurParams.size = static_cast<int>(std::any_cast<Hyprlang::FLOAT>(val));
                blurParams.passes = 3;
            } else {
                Debug::log(WARN, "Shape blur has unexpected type or value, disabling blur");
            }
        }

        // Parse zindex
        zindex = 10; // Default: above background, below input-field
        if (props.contains("zindex")) {
            auto val = props.at("zindex");
            if (val.type() == typeid(Hyprlang::INT)) {
                zindex = std::any_cast<Hyprlang::INT>(val);
            } else {
                Debug::log(WARN, "Shape zindex has unexpected type, defaulting to 10");
            }
        }

        // Parse rotation (angle in degrees)
        angle = 0.0;
        if (props.contains("rotate")) {
            auto val = props.at("rotate");
            if (val.type() == typeid(Hyprlang::FLOAT)) {
                angle = std::any_cast<Hyprlang::FLOAT>(val);
            } else if (val.type() == typeid(Hyprlang::INT)) {
                angle = static_cast<float>(std::any_cast<Hyprlang::INT>(val));
            } else {
                Debug::log(WARN, "Shape rotate has unexpected type, defaulting to 0");
            }
        }
        angle = angle * M_PI / 180.0; // Convert to radians

        // Parse border (optional)
        border = 0;
        if (props.contains("border_size")) {
            auto val = props.at("border_size");
            if (val.type() == typeid(Hyprlang::INT)) {
                border = std::any_cast<Hyprlang::INT>(val);
            } else {
                Debug::log(WARN, "Shape border_size has unexpected type, defaulting to 0");
            }
        }

        // Parse border gradient (optional)
        borderGrad = CGradientValueData();
        if (props.contains("border_color")) {
            try {
                borderGrad = *CGradientValueData::fromAnyPv(props.at("border_color"));
            } catch (const std::exception& e) {
                Debug::log(WARN, "Failed to parse border_color, defaulting to empty gradient: {}", e.what());
            }
        }

        // Parse halign and valign
        halign = "left";
        if (props.contains("halign")) {
            auto val = props.at("halign");
            if (val.type() == typeid(Hyprlang::STRING)) {
                halign = std::any_cast<Hyprlang::STRING>(val);
            }
        }
        valign = "top";
        if (props.contains("valign")) {
            auto val = props.at("valign");
            if (val.type() == typeid(Hyprlang::STRING)) {
                valign = std::any_cast<Hyprlang::STRING>(val);
            }
        }

        // Adjust position based on halign/valign
        Vector2D realSize = size + Vector2D{border * 2.0, border * 2.0};
        pos = posFromHVAlign(viewport, realSize, pos, halign, valign, angle);
    } catch (const std::exception& e) {
        Debug::log(ERR, "CShape::configure failed: {}", e.what());
        // Set safe defaults
        pos = {0, 0};
        size = {100, 100};
        color = CHyprColor(1.0, 1.0, 1.0, 0.5);
        shapeType = "rectangle";
        zindex = 10;
        angle = 0.0;
        border = 0;
        blurEnabled = false;
    }
}

bool CShape::draw(const SRenderData& data) {
    try {
        CBox box = {pos.x, pos.y, size.x, size.y};
        box.round();
        box.rot = angle;

        if (blurEnabled) {
            if (!shapeFB.isAllocated()) {
                shapeFB.alloc((int)(size.x + border * 2), (int)(size.y + border * 2), true);
            }

            shapeFB.bind();
            glClearColor(0.0, 0.0, 0.0, 0.0);
            glClear(GL_COLOR_BUFFER_BIT);

            // Draw shape (rectangle for now)
            if (shapeType == "rectangle") {
                CBox shapeBox = {border, border, size.x, size.y};
                g_pRenderer->renderRect(shapeBox, color, 0);
                if (border > 0 && !borderGrad.m_vColorsOkLabA.empty()) {
                    CBox borderBox = {0, 0, size.x + border * 2, size.y + border * 2};
                    g_pRenderer->renderBorder(borderBox, borderGrad, border, 0, data.opacity);
                }
            } else {
                Debug::log(WARN, "Shape type {} not implemented, rendering rectangle", shapeType);
                g_pRenderer->renderRect(box, color, 0);
            }

            // Apply blur
            CRenderer::SBlurParams rendererBlurParams = {
                .size = blurParams.size,
                .passes = blurParams.passes,
                // Default values for other fields
            };
            g_pRenderer->blurFB(shapeFB, rendererBlurParams);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

            // Render blurred texture
            CBox texBox = {pos.x - border, pos.y - border, size.x + border * 2, size.y + border * 2};
            texBox.round();
            texBox.rot = angle;
            g_pRenderer->renderTexture(texBox, shapeFB.m_cTex, data.opacity, 0, HYPRUTILS_TRANSFORM_FLIPPED_180);
        } else {
            // Draw without blur
            if (shapeType == "rectangle") {
                g_pRenderer->renderRect(box, color, 0);
                if (border > 0 && !borderGrad.m_vColorsOkLabA.empty()) {
                    CBox borderBox = {pos.x - border, pos.y - border, size.x + border * 2, size.y + border * 2};
                    borderBox.round();
                    borderBox.rot = angle;
                    g_pRenderer->renderBorder(borderBox, borderGrad, border, 0, data.opacity);
                }
            } else {
                Debug::log(WARN, "Shape type {} not implemented, rendering rectangle", shapeType);
                g_pRenderer->renderRect(box, color, 0);
            }
        }

        return data.opacity < 1.0;
    } catch (const std::exception& e) {
        Debug::log(ERR, "CShape::draw failed: {}", e.what());
        return false;
    }
}