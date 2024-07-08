#pragma once
#include "Vector2D.hpp"

class CBox {
  public:
    CBox(double x_, double y_, double w_, double h_) {
        x = x_;
        y = y_;
        w = w_;
        h = h_;
    }

    CBox() {
        w = 0;
        h = 0;
    }

    CBox(const double d) {
        x = d;
        y = d;
        w = d;
        h = d;
    }

    CBox(const Vector2D& pos, const Vector2D& size) {
        x = pos.x;
        y = pos.y;
        w = size.x;
        h = size.y;
    }

    CBox&    scale(double scale);
    CBox&    scaleFromCenter(double scale);
    CBox&    scale(const Vector2D& scale);
    CBox&    translate(const Vector2D& vec);
    CBox&    round();
    CBox&    expand(const double& value);
    CBox&    noNegativeSize();

    CBox     copy() const;

    Vector2D middle() const;
    Vector2D pos() const;
    Vector2D size() const;

    bool     containsPoint(const Vector2D& vec) const;
    bool     empty() const;

    double   x = 0, y = 0;
    union {
        double w;
        double width;
    };
    union {
        double h;
        double height;
    };

    double rot = 0; /* rad, ccw */

    //
    bool operator==(const CBox& rhs) const {
        return x == rhs.x && y == rhs.y && w == rhs.w && h == rhs.h;
    }

  private:
    CBox roundInternal();
};
