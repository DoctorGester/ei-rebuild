#pragma once

// Rectangle arithmetic over RECT / POINT with USER32's rules (SetRect ... UnionRect), as plain integer code: the
// game's UI layout, hit tests and dirty rectangles. A rectangle is empty when right <= left or bottom <= top;
// nothing here normalises a rectangle, and the right and bottom edges are outside it.

#include "lib/win_types.h"

#include <algorithm>

namespace rects {

inline void Set(RECT* rect, int left, int top, int right, int bottom) {
    rect->left = left;
    rect->top = top;
    rect->right = right;
    rect->bottom = bottom;
}

inline void SetEmpty(RECT* rect) { Set(rect, 0, 0, 0, 0); }

inline void Copy(RECT* to, const RECT* from) { *to = *from; }

inline bool IsEmpty(const RECT* rect) { return rect->right <= rect->left || rect->bottom <= rect->top; }

// The right and bottom edges are outside.
inline bool Contains(const RECT* rect, POINT point) {
    return point.x >= rect->left && point.x < rect->right && point.y >= rect->top && point.y < rect->bottom;
}

inline void Offset(RECT* rect, int dx, int dy) {
    rect->left += dx;
    rect->right += dx;
    rect->top += dy;
    rect->bottom += dy;
}

// `to` (may be a or b) becomes the common part, or the all-zero rectangle when it is empty (returns false then).
inline bool Intersect(RECT* to, const RECT* a, const RECT* b) {
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
inline bool Union(RECT* to, const RECT* a, const RECT* b) {
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
