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

    virtual eConfigValueDataTypes getDataType() {
        return CVD_TYPE_LAYOUT;
    }

    virtual std::string toString() {
        return std::format("{}{},{}{}", m_vValues.x, (m_sIsRelative.x) ? "%" : "px", m_vValues.y, (m_sIsRelative.y) ? "%" : "px");
    }

    static CLayoutValueData* fromAnyPv(const std::any& v) {
        RASSERT(v.type() == typeid(void*), "Invalid config value type");
        const auto P = (CLayoutValueData*)std::any_cast<void*>(v);
        RASSERT(P, "Empty config value");
        return P;
    }

    Hyprutils::Math::Vector2D getAbsolute(const Hyprutils::Math::Vector2D& viewport) {
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
};

class CGradientValueData : public ICustomConfigValueData {
  public:
    CGradientValueData() {};
    CGradientValueData(CColor col) {
        m_vColors.push_back(col);
    };
    virtual ~CGradientValueData() {};

    virtual eConfigValueDataTypes getDataType() {
        return CVD_TYPE_GRADIENT;
    }

    void reset(CColor col) {
        m_vColors.clear();
        m_vColors.emplace_back(col);
        m_fAngle = 0;
    }

    /* Vector containing the colors */
    std::vector<CColor> m_vColors;

    /* Float corresponding to the angle (rad) */
    float m_fAngle = 0;

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
