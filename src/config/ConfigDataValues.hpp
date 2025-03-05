#pragma once
#include "../helpers/Log.hpp"
#include "../helpers/Color.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <hyprutils/string/VarList.hpp>
#include <any>
#include <string>
#include <vector>
#include <cmath>

using namespace Hyprutils::String;

enum eConfigValueDataTypes {
    CVD_TYPE_INVALID  = -1,
    CVD_TYPE_LAYOUT   = 0,
    CVD_TYPE_GRADIENT = 1,
};

class ICustomConfigValueData {
  public:
    virtual ~ICustomConfigValueData() = 0;

    virtual eConfigValueDataTypes getDataType() = 0;

    virtual std::string           toString() = 0;
};

class CLayoutValueData : public ICustomConfigValueData {
  public:
    CLayoutValueData() {};
    virtual ~CLayoutValueData() {};

    struct SExecuteResult {
        Hyprutils::Math::Vector2D values;
        struct {
            bool x = false;
            bool y = false;
        } isRelative;
    };

    SExecuteResult executeCommand() {
        if (!m_bIsCommand)
            return {{0,0}, {false, false}};
            
        FILE* pipe = popen(m_sCommand.c_str(), "r");
        if (!pipe) {
            Debug::log(ERR, "Failed to execute position command");
            return {{0,0}, {false, false}};
        }

        char buffer[128];
        std::string result = "";
        while (!feof(pipe)) {
            if (fgets(buffer, 128, pipe) != NULL)
                result += buffer;
        }
        pclose(pipe);

        if (!result.empty() && result.back() == '\n')
            result.pop_back();

        SExecuteResult out;
        const auto SPLIT = result.find(',');
        if (SPLIT == std::string::npos) {
            Debug::log(ERR, "Position command output must be x,y format");
            return {{0,0}, {false, false}};
        }

        auto lhs = result.substr(0, SPLIT);
        auto rhs = result.substr(SPLIT + 1);
        if (rhs.starts_with(" "))
            rhs = rhs.substr(1);

        if (lhs.ends_with("%")) {
            out.isRelative.x = true;
            lhs.pop_back();
        }

        if (rhs.ends_with("%")) {
            out.isRelative.y = true;
            rhs.pop_back();
        }

        try {
            out.values = {std::stof(lhs), std::stof(rhs)};
        } catch (std::exception& e) {
            Debug::log(ERR, "Failed to parse command output as coordinates");
            return {{0,0}, {false, false}};
        }

        return out;
    }

    bool needsUpdate() const {
        return m_bIsCommand;
    }

    float updateMs() const {
        return m_bIsCommand ? m_iUpdateMs : 0;
    }

    void update() {
        if (!m_bIsCommand)
            return;
            
        auto result = executeCommand();
        m_vValues = result.values;
        m_sIsRelative.x = result.isRelative.x;
        m_sIsRelative.y = result.isRelative.y;
    }

    virtual eConfigValueDataTypes getDataType() {
        return CVD_TYPE_LAYOUT;
    }

    virtual std::string toString() {
        if (m_bIsCommand)
            return std::format("cmd[update:{}]{}", m_iUpdateMs, m_sCommand);
        return std::format("{}{},{}{}", m_vValues.x, (m_sIsRelative.x) ? "%" : "px", m_vValues.y, (m_sIsRelative.y) ? "%" : "px");
    }

    static CLayoutValueData* fromAnyPv(const std::any& v) {
        RASSERT(v.type() == typeid(void*), "Invalid config value type");
        const auto P = (CLayoutValueData*)std::any_cast<void*>(v);
        RASSERT(P, "Empty config value");
        return P;
    }

    Hyprutils::Math::Vector2D getAbsolute(const Hyprutils::Math::Vector2D& viewport) {
        if (m_bIsCommand)
            update();
        return {
            (m_sIsRelative.x ? (m_vValues.x / 100) * viewport.x : m_vValues.x),
            (m_sIsRelative.y ? (m_vValues.y / 100) * viewport.y : m_vValues.y),
        };
    }

    Hyprutils::Math::Vector2D m_vValues;
    struct {
        bool x = false;
        bool y = false;
    } m_sIsRelative;
    
    bool        m_bIsCommand = false;
    std::string m_sCommand = "";
    int         m_iUpdateMs = 0;
};

class CGradientValueData : public ICustomConfigValueData {
  public:
    CGradientValueData() {};
    CGradientValueData(CHyprColor col) {
        m_vColors.push_back(col);
        updateColorsOk();
    };
    virtual ~CGradientValueData() {};

    virtual eConfigValueDataTypes getDataType() {
        return CVD_TYPE_GRADIENT;
    }

    void reset(CHyprColor col) {
        m_vColors.clear();
        m_vColors.emplace_back(col);
        m_fAngle = 0;
        updateColorsOk();
    }

    void updateColorsOk() {
        m_vColorsOkLabA.clear();
        for (auto& c : m_vColors) {
            const auto OKLAB = c.asOkLab();
            m_vColorsOkLabA.emplace_back(OKLAB.l);
            m_vColorsOkLabA.emplace_back(OKLAB.a);
            m_vColorsOkLabA.emplace_back(OKLAB.b);
            m_vColorsOkLabA.emplace_back(c.a);
        }
    }

    /* Vector containing the colors */
    std::vector<CHyprColor> m_vColors;

    /* Vector containing pure colors for shoving into opengl */
    std::vector<float> m_vColorsOkLabA;

    /* Float corresponding to the angle (rad) */
    float m_fAngle = 0;

    /* Whether this gradient stores a fallback value (not exlicitly set) */
    bool m_bIsFallback = false;

    //
    bool operator==(const CGradientValueData& other) const {
        if (other.m_vColors.size() != m_vColors.size() || m_fAngle != other.m_fAngle)
            return false;

        for (size_t i = 0; i < m_vColors.size(); ++i)
            if (m_vColors[i] != other.m_vColors[i])
                return false;

        return true;
    }

    virtual std::string toString() {
        std::string result;
        for (auto& c : m_vColors) {
            result += std::format("{:x} ", c.getAsHex());
        }

        result += std::format("{}deg", (int)(m_fAngle * 180.0 / M_PI));
        return result;
    }

    static CGradientValueData* fromAnyPv(const std::any& v) {
        RASSERT(v.type() == typeid(void*), "Invalid config value type");
        const auto P = (CGradientValueData*)std::any_cast<void*>(v);
        RASSERT(P, "Empty config value");
        return P;
    }
};
