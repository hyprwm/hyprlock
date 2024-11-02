#pragma once
#include "../helpers/Log.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <any>
#include <string>

enum eConfigValueDataTypes {
    CVD_TYPE_INVALID = -1,
    CVD_TYPE_LAYOUT  = 0,
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

    static CLayoutValueData* fromAny(const std::any& v) {
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
