// STLport's _Rb_global<bool> (stlp_tree.h): the node-only algorithms every tree instantiation of
// the exe shares, registered at their addresses. Checked in game by verify_stlp_tree.cpp until S21d.
#include "stlp_tree.h"
#include "stlp_tree_cstring.h"

#include "crt.h"
#include "mfc_string.h"

#include "ei/hook.h"

#include <utility>

namespace stlp::rb {

namespace {
constexpr std::uint8_t kRed = RbNodeBase::kRed;
constexpr std::uint8_t kBlack = RbNodeBase::kBlack;

bool IsBlack(const RbNodeBase* n) { return !n || n->color == kBlack; }
} // namespace

void __fastcall Increment(RbNodeBase** it) {
    RbNodeBase* node = *it;
    if (node->right) {
        node = node->right;
        while (node->left) node = node->left;
        *it = node;
        return;
    }
    RbNodeBase* y = node->parent;
    while (node == y->right) {
        node = y;
        y = y->parent;
    }
    // The header's case: from the root with no parent above, the header stays.
    if (node->right != y) node = y;
    *it = node;
}

void __fastcall Decrement(RbNodeBase** it) {
    RbNodeBase* node = *it;
    if (node->color == kRed && node->parent->parent == node) {
        *it = node->right;   // end(): the rightmost
        return;
    }
    if (node->left) {
        RbNodeBase* y = node->left;
        while (y->right) y = y->right;
        *it = y;
        return;
    }
    RbNodeBase* y = node->parent;
    while (node == y->left) {
        node = y;
        y = y->parent;
    }
    *it = y;
}

void __fastcall RotateLeft(RbNodeBase* x, RbNodeBase*& root) {
    RbNodeBase* y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (x == root) root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

void __fastcall RotateRight(RbNodeBase* x, RbNodeBase*& root) {
    RbNodeBase* y = x->left;
    x->left = y->right;
    if (y->right) y->right->parent = x;
    y->parent = x->parent;
    if (x == root) root = y;
    else if (x == x->parent->right) x->parent->right = y;
    else x->parent->left = y;
    y->right = x;
    x->parent = y;
}

void __fastcall Rebalance(RbNodeBase* x, RbNodeBase*& root) {
    x->color = kRed;
    while (x != root && x->parent->color == kRed) {
        RbNodeBase* p = x->parent;
        RbNodeBase* g = p->parent;
        if (p == g->left) {
            RbNodeBase* y = g->right;
            if (y && y->color == kRed) {
                p->color = kBlack;
                y->color = kBlack;
                g->color = kRed;
                x = g;
            } else {
                if (x == p->right) {
                    x = p;
                    RotateLeft(x, root);
                }
                x->parent->color = kBlack;
                x->parent->parent->color = kRed;
                RotateRight(x->parent->parent, root);
            }
        } else {
            RbNodeBase* y = g->left;
            if (y && y->color == kRed) {
                p->color = kBlack;
                y->color = kBlack;
                g->color = kRed;
                x = g;
            } else {
                if (x == p->left) {
                    x = p;
                    RotateRight(x, root);
                }
                x->parent->color = kBlack;
                x->parent->parent->color = kRed;
                RotateLeft(x->parent->parent, root);
            }
        }
    }
    root->color = kBlack;
}

RbNodeBase* __fastcall RebalanceForErase(RbNodeBase* z, RbNodeBase*& root, RbNodeBase*& leftmost,
                                         RbNodeBase*& rightmost) {
    RbNodeBase* y = z;
    RbNodeBase* x = nullptr;
    RbNodeBase* xParent = nullptr;
    if (!y->left) {
        x = y->right;   // may be null
    } else if (!y->right) {
        x = y->left;
    } else {
        // Two children: y is z's successor.
        y = y->right;
        while (y->left) y = y->left;
        x = y->right;
    }
    if (y != z) {
        // y takes z's place.
        z->left->parent = y;
        y->left = z->left;
        if (y != z->right) {
            xParent = y->parent;
            if (x) x->parent = y->parent;
            y->parent->left = x;
            y->right = z->right;
            z->right->parent = y;
        } else {
            xParent = y;
        }
        if (root == z) root = y;
        else if (z->parent->left == z) z->parent->left = y;
        else z->parent->right = y;
        y->parent = z->parent;
        std::swap(y->color, z->color);
        y = z;   // the node to free
    } else {
        xParent = y->parent;
        if (x) x->parent = y->parent;
        if (root == z) root = x;
        else if (z->parent->left == z) z->parent->left = x;
        else z->parent->right = x;
        if (leftmost == z) leftmost = z->right ? RbNodeBase::Minimum(x) : z->parent;
        if (rightmost == z) rightmost = z->left ? RbNodeBase::Maximum(x) : z->parent;
    }
    if (y->color != kRed) {
        while (x != root && IsBlack(x)) {
            if (x == xParent->left) {
                RbNodeBase* w = xParent->right;
                if (w->color == kRed) {
                    w->color = kBlack;
                    xParent->color = kRed;
                    RotateLeft(xParent, root);
                    w = xParent->right;
                }
                if (IsBlack(w->left) && IsBlack(w->right)) {
                    w->color = kRed;
                    x = xParent;
                    xParent = xParent->parent;
                } else {
                    if (IsBlack(w->right)) {
                        if (w->left) w->left->color = kBlack;
                        w->color = kRed;
                        RotateRight(w, root);
                        w = xParent->right;
                    }
                    w->color = xParent->color;
                    xParent->color = kBlack;
                    if (w->right) w->right->color = kBlack;
                    RotateLeft(xParent, root);
                    break;
                }
            } else {
                RbNodeBase* w = xParent->left;
                if (w->color == kRed) {
                    w->color = kBlack;
                    xParent->color = kRed;
                    RotateRight(xParent, root);
                    w = xParent->left;
                }
                if (IsBlack(w->right) && IsBlack(w->left)) {
                    w->color = kRed;
                    x = xParent;
                    xParent = xParent->parent;
                } else {
                    if (IsBlack(w->left)) {
                        if (w->right) w->right->color = kBlack;
                        w->color = kRed;
                        RotateLeft(w, root);
                        w = xParent->left;
                    }
                    w->color = xParent->color;
                    xParent->color = kBlack;
                    if (w->left) w->left->color = kBlack;
                    RotateRight(xParent, root);
                    break;
                }
            }
        }
        if (x) x->color = kBlack;
    }
    return y;
}

} // namespace stlp::rb

EI_REWRITTEN(0x0043DBE0, stlp::rb::Increment);
EI_REWRITTEN(0x0043FBE0, stlp::rb::Decrement);
EI_REWRITTEN(0x0043E300, stlp::rb::RotateLeft);
EI_REWRITTEN(0x0043E350, stlp::rb::RotateRight);
EI_REWRITTEN(0x0043FE90, stlp::rb::Rebalance);
EI_REWRITTEN(0x0043D830, stlp::rb::RebalanceForErase);
namespace stlp {

bool CStringLess::Less(const mfc::CString& a, const mfc::CString& b) {
    return gamecrt::mbscmp(static_cast<const char*>(a),
                           static_cast<const char*>(b)) < 0;
}

} // namespace stlp
