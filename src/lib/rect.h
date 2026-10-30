#pragma once

// The game's integer rectangle, point and size, and rectangle arithmetic over them with USER32's rules (SetRect ...
// UnionRect), as plain integer code: the UI layout, hit tests and dirty rectangles. A rectangle is empty when
// right <= left or bottom <= top; nothing here normalises a rectangle, and the right and bottom edges are outside it.
// The three have the layout of Windows' RECT, POINT and SIZE (four, two and two 32-bit integers), which the win32
// side static_asserts where it hands one to the system.

#include <algorithm>
#include <cstdint>

struct Rect {
    std::int32_t left, top, right, bottom;
};

struct Point {
    std::int32_t x, y;
};

struct Size {
    std::int32_t cx, cy;
};

namespace rects {

inline void Set(Rect* rect, int left, int top, int right, int bottom) {
    rect->left = left;
    rect->top = top;
    rect->right = right;
    rect->bottom = bottom;
}

inline void SetEmpty(Rect* rect) { Set(rect, 0, 0, 0, 0); }

inline void Copy(Rect* to, const Rect* from) { *to = *from; }

inline bool IsEmpty(const Rect* rect) { return rect->right <= rect->left || rect->bottom <= rect->top; }

// The right and bottom edges are outside.
inline bool Contains(const Rect* rect, Point point) {
    return point.x >= rect->left && point.x < rect->right && point.y >= rect->top && point.y < rect->bottom;
}

inline void Offset(Rect* rect, int dx, int dy) {
    rect->left += dx;
    rect->right += dx;
    rect->top += dy;
    rect->bottom += dy;
}

// `to` (may be a or b) becomes the common part, or the all-zero rectangle when it is empty (returns false then).
inline bool Intersect(Rect* to, const Rect* a, const Rect* b) {
    if (IsEmpty(a) || IsEmpty(b) || a->left >= b->right || b->left >= a->right || a->top >= b->bottom ||
        b->top >= a->bottom) {
        SetEmpty(to);
        return false;
    }
    Set(to, (std::max)(a->left, b->left), (std::max)(a->top, b->top), (std::min)(a->right, b->right),
        (std::min)(a->bottom, b->bottom));
    return true;
}

// `to` (may be a or b) becomes the bounding rectangle of the non-empty operands (an empty operand is ignored); the
// all-zero rectangle and false when both are empty.
inline bool Union(Rect* to, const Rect* a, const Rect* b) {
    const bool emptyA = IsEmpty(a), emptyB = IsEmpty(b);
    if (emptyA && emptyB) {
        SetEmpty(to);
        return false;
    }
    if (emptyA) {
        *to = *b;
    } else if (emptyB) {
        *to = *a;
    } else {
        Set(to, (std::min)(a->left, b->left), (std::min)(a->top, b->top), (std::max)(a->right, b->right),
            (std::max)(a->bottom, b->bottom));
    }
    return true;
}

}  // namespace rects
