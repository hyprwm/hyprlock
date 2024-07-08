#include "Box.hpp"
#include <cmath>
#include <limits>
#include <algorithm>

#define VECINRECT(vec, x1, y1, x2, y2) ((vec).x >= (x1) && (vec).x <= (x2) && (vec).y >= (y1) && (vec).y <= (y2))

CBox& CBox::scale(double scale) {
    x *= scale;
    y *= scale;
    w *= scale;
    h *= scale;

    return *this;
}

CBox& CBox::scale(const Vector2D& scale) {
    x *= scale.x;
    y *= scale.y;
    w *= scale.x;
    h *= scale.y;

    return *this;
}

CBox& CBox::translate(const Vector2D& vec) {
    x += vec.x;
    y += vec.y;

    return *this;
}

Vector2D CBox::middle() const {
    return Vector2D{x + w / 2.0, y + h / 2.0};
}

bool CBox::containsPoint(const Vector2D& vec) const {
    return VECINRECT(vec, x, y, x + w, y + h);
}

bool CBox::empty() const {
    return w == 0 || h == 0;
}

CBox& CBox::round() {
    float newW = x + w - std::round(x);
    float newH = y + h - std::round(y);
    x          = std::round(x);
    y          = std::round(y);
    w          = std::round(newW);
    h          = std::round(newH);

    return *this;
}

CBox& CBox::scaleFromCenter(double scale) {
    double oldW = w, oldH = h;

    w *= scale;
    h *= scale;

    x -= (w - oldW) / 2.0;
    y -= (h - oldH) / 2.0;

    return *this;
}

CBox& CBox::expand(const double& value) {
    x -= value;
    y -= value;
    w += value * 2.0;
    h += value * 2.0;

    return *this;
}

CBox& CBox::noNegativeSize() {
    std::clamp(w, 0.0, std::numeric_limits<double>::infinity());
    std::clamp(h, 0.0, std::numeric_limits<double>::infinity());

    return *this;
}

CBox CBox::roundInternal() {
    float newW = x + w - std::floor(x);
    float newH = y + h - std::floor(y);

    return CBox{std::floor(x), std::floor(y), std::floor(newW), std::floor(newH)};
}

CBox CBox::copy() const {
    return CBox{*this};
}

Vector2D CBox::pos() const {
    return {x, y};
}

Vector2D CBox::size() const {
    return {w, h};
}