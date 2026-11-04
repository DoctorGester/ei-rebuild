#pragma once

// The 2D integer types of the interface, the screen and the platform layer: a point, a size and a rectangle, with the
// arithmetic of USER32's rectangles (SetRect ... UnionRect) as plain integer code for the UI layout, hit tests and
// dirty rectangles. A rectangle is empty when right <= left or bottom <= top; nothing here normalises a rectangle, and
// the right and bottom edges are outside it. All three are aggregates (`Rect{l, t, r, b}`). The float pair is
// Vector2 (vecmath.h).

#include <cstdint>

namespace rect_detail {
// std::min / std::max without <algorithm>, which this header would cost every includer.
inline constexpr std::int32_t Smaller(std::int32_t a, std::int32_t b) { return b < a ? b : a; }
inline constexpr std::int32_t Larger(std::int32_t a, std::int32_t b) { return a < b ? b : a; }
} // namespace rect_detail

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
// Each coordinate's smaller and larger.
inline Point Min(Point a, Point b) { return {rect_detail::Smaller(a.x, b.x), rect_detail::Smaller(a.y, b.y)}; }
inline Point Max(Point a, Point b) { return {rect_detail::Larger(a.x, b.x), rect_detail::Larger(a.y, b.y)}; }
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
    static Rect Sized(Point topLeft, Size size) { return {topLeft.x, topLeft.y, topLeft.x + size.w, topLeft.y + size.h}; }

    int Width() const { return right - left; }
    int Height() const { return bottom - top; }
    Size Extent() const { return {Width(), Height()}; }
    Point TopLeft() const { return {left, top}; }
    Point BottomRight() const { return {right, bottom}; }

    bool IsEmpty() const { return right <= left || bottom <= top; }

    // Each edge moved inwards (outwards when negative): all by `d`, by (dx, dy), or each by its own amount.
    Rect Inset(int d) const { return Inset(d, d); }
    Rect Inset(int dx, int dy) const { return Inset(dx, dy, dx, dy); }
    Rect Inset(int dLeft, int dTop, int dRight, int dBottom) const {
        return {left + dLeft, top + dTop, right - dRight, bottom - dBottom};
    }

    // The band `size` wide or high along the inside of one edge.
    Rect LeftStrip(int size) const { return {left, top, left + size, bottom}; }
    Rect RightStrip(int size) const { return {right - size, top, right, bottom}; }
    Rect TopStrip(int size) const { return {left, top, right, top + size}; }
    Rect BottomStrip(int size) const { return {left, bottom - size, right, bottom}; }

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
        return {rect_detail::Larger(left, other.left), rect_detail::Larger(top, other.top), rect_detail::Smaller(right, other.right),
                rect_detail::Smaller(bottom, other.bottom)};
    }

    // The bounding rectangle of the non-empty operands (an empty operand is ignored); all zero when both are empty.
    Rect Union(const Rect& other) const {
        if (IsEmpty()) return other.IsEmpty() ? Rect{} : other;
        if (other.IsEmpty()) return *this;
        return {rect_detail::Smaller(left, other.left), rect_detail::Smaller(top, other.top), rect_detail::Larger(right, other.right),
                rect_detail::Larger(bottom, other.bottom)};
    }
};

inline bool operator==(const Rect& a, const Rect& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
inline bool operator!=(const Rect& a, const Rect& b) { return !(a == b); }
