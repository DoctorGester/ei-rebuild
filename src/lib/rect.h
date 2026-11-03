#pragma once

// The 2D integer types of the interface, the screen and the platform layer: a point, a size and a rectangle, with the
// arithmetic of USER32's rectangles (SetRect ... UnionRect) as plain integer code for the UI layout, hit tests and
// dirty rectangles. A rectangle is empty when right <= left or bottom <= top; nothing here normalises a rectangle, and
// the right and bottom edges are outside it. All three are aggregates (`Rect{l, t, r, b}`). The float pair is
// Vector2 (vecmath.h).

#include <algorithm>
#include <cstdint>

struct Point {
    std::int32_t x = 0, y = 0;
};

inline Point operator+(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }
inline Point operator-(Point a, Point b) { return {a.x - b.x, a.y - b.y}; }
inline Point operator-(Point a) { return {-a.x, -a.y}; }
inline Point operator*(Point a, int s) { return {a.x * s, a.y * s}; }
inline Point operator/(Point a, int s) { return {a.x / s, a.y / s}; }
inline Point& operator+=(Point& a, Point b) { a = a + b; return a; }
inline Point& operator-=(Point& a, Point b) { a = a - b; return a; }
inline bool operator==(Point a, Point b) { return a.x == b.x && a.y == b.y; }
inline bool operator!=(Point a, Point b) { return !(a == b); }

// A width and a height.
struct Size {
    std::int32_t w = 0, h = 0;
};

inline bool operator==(Size a, Size b) { return a.w == b.w && a.h == b.h; }
inline bool operator!=(Size a, Size b) { return !(a == b); }

struct Rect {
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;

    static Rect FromCorners(Point topLeft, Point bottomRight) {
        return {topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    }

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    Size Extent() const { return {Width(), Height()}; }
    Point TopLeft() const { return {left, top}; }
    Point BottomRight() const { return {right, bottom}; }

    bool IsEmpty() const { return right <= left || bottom <= top; }

    // The right and bottom edges are outside.
    bool Contains(Point point) const {
        return point.x >= left && point.x < right && point.y >= top && point.y < bottom;
    }

    // Moved by `by` (the rectangle itself, or a copy).
    Rect& Offset(Point by) {
        left += by.x;
        right += by.x;
        top += by.y;
        bottom += by.y;
        return *this;
    }
    Rect& Offset(int dx, int dy) { return Offset(Point{dx, dy}); }
    Rect Offsetted(Point by) const { Rect copy = *this; return copy.Offset(by); }
    Rect Offsetted(int dx, int dy) const { return Offsetted(Point{dx, dy}); }

    // The common part, or the all-zero rectangle when there is none.
    Rect Intersection(const Rect& other) const {
        if (IsEmpty() || other.IsEmpty() || left >= other.right || other.left >= right || top >= other.bottom ||
            other.top >= bottom) {
            return {};
        }
        return {(std::max)(left, other.left), (std::max)(top, other.top), (std::min)(right, other.right),
                (std::min)(bottom, other.bottom)};
    }

    // The bounding rectangle of the non-empty operands (an empty operand is ignored); all zero when both are empty.
    Rect Union(const Rect& other) const {
        if (IsEmpty()) return other.IsEmpty() ? Rect{} : other;
        if (other.IsEmpty()) return *this;
        return {(std::min)(left, other.left), (std::min)(top, other.top), (std::max)(right, other.right),
                (std::max)(bottom, other.bottom)};
    }
};

inline bool operator==(const Rect& a, const Rect& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
inline bool operator!=(const Rect& a, const Rect& b) { return !(a == b); }
