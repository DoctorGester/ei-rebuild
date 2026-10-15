// Checks for STLport's red-black tree (src/lib/stlp_tree.cpp), all against the original.
//
// - _M_increment / _M_decrement are pure: both run, the iterators compared.
// - The rotations and both rebalances only relink and recolour nodes near a path: the nodes that
//   can change (the path from the node up to the root and the header, each path node's descendants
//   three levels down) and the root / leftmost / rightmost words are copied, the original runs,
//   its result is copied and the nodes put back, then ours runs: the same bytes in every one of
//   those nodes, the same words and the same node returned. A path is O(log n) nodes, so the check
//   stays cheap on large trees.
#include "verify.h"

#include "../../src/lib/stlp_tree.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using stlp::RbNodeBase;

// Nodes and their 16 link bytes (color, parent, left, right).
struct Nodes {
    std::vector<RbNodeBase*> nodes;
    std::vector<unsigned char> bytes;

    void Add(RbNodeBase* n) {
        if (n) nodes.push_back(n);
    }
    void AddDescendants(RbNodeBase* n, int depth) {
        if (!n || depth < 0) return;
        Add(n);
        AddDescendants(n->left, depth - 1);
        AddDescendants(n->right, depth - 1);
    }
    // The path from `n` up to the root, the header above it, each path node with three levels of
    // descendants.
    void AddPath(RbNodeBase* n, RbNodeBase* root) {
        for (int guard = 0; n && guard < 200; ++guard) {
            AddDescendants(n, 3);
            if (n == root) {
                Add(n->parent);   // the header
                break;
            }
            n = n->parent;
        }
    }
    void Seal() {
        std::sort(nodes.begin(), nodes.end());
        nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
        bytes = Read();
    }
    std::vector<unsigned char> Read() const {
        std::vector<unsigned char> out(nodes.size() * sizeof(RbNodeBase));
        for (std::size_t i = 0; i < nodes.size(); ++i) std::memcpy(&out[i * sizeof(RbNodeBase)], nodes[i], sizeof(RbNodeBase));
        return out;
    }
    void Restore() const {
        for (std::size_t i = 0; i < nodes.size(); ++i) std::memcpy(nodes[i], &bytes[i * sizeof(RbNodeBase)], sizeof(RbNodeBase));
    }
    // The first node whose bytes differ, for the mismatch line.
    std::size_t FirstDifference(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) const {
        for (std::size_t i = 0; i < nodes.size(); ++i)
            if (std::memcmp(&a[i * sizeof(RbNodeBase)], &b[i * sizeof(RbNodeBase)], sizeof(RbNodeBase)) != 0) return i;
        return nodes.size();
    }
};

// --- Iterators ----------------------------------------------------------------------------------

void(__fastcall* s_increment)(RbNodeBase**);
void(__fastcall* s_decrement)(RbNodeBase**);

void __fastcall IncrementVerify(RbNodeBase** it) {
    RbNodeBase* theirs = *it;
    s_increment(&theirs);
    RbNodeBase* from = *it;
    stlp::rb::Increment(it);
    ei::verify::Check("stlp::_Rb_tree_base_iterator::_M_increment", theirs, *it, "from %p", static_cast<void*>(from));
}

void __fastcall DecrementVerify(RbNodeBase** it) {
    RbNodeBase* theirs = *it;
    s_decrement(&theirs);
    RbNodeBase* from = *it;
    stlp::rb::Decrement(it);
    ei::verify::Check("stlp::_Rb_tree_base_iterator::_M_decrement", theirs, *it, "from %p", static_cast<void*>(from));
}

// --- Relinking ----------------------------------------------------------------------------------

void(__fastcall* s_rotateLeft)(RbNodeBase*, RbNodeBase*&);
void(__fastcall* s_rotateRight)(RbNodeBase*, RbNodeBase*&);
void(__fastcall* s_rebalance)(RbNodeBase*, RbNodeBase*&);
RbNodeBase*(__fastcall* s_rebalanceForErase)(RbNodeBase*, RbNodeBase*&, RbNodeBase*&, RbNodeBase*&);

// Runs `theirs` then `ours` on the same nodes and words (put back between them) and reports.
template <std::size_t N, typename Theirs, typename Ours>
void Compare(const char* name, Nodes& nodes, RbNodeBase** const (&words)[N], Theirs theirs, Ours ours) {
    nodes.Seal();
    RbNodeBase* before[N];
    for (std::size_t i = 0; i < N; ++i) before[i] = *words[i];
    RbNodeBase* theirResult = theirs();
    const std::vector<unsigned char> theirBytes = nodes.Read();
    RbNodeBase* theirWords[N];
    for (std::size_t i = 0; i < N; ++i) theirWords[i] = *words[i];
    nodes.Restore();
    for (std::size_t i = 0; i < N; ++i) *words[i] = before[i];
    RbNodeBase* ourResult = ours();
    const std::vector<unsigned char> ourBytes = nodes.Read();
    bool ok = theirResult == ourResult && theirBytes == ourBytes;
    for (std::size_t i = 0; i < N; ++i) ok = ok && theirWords[i] == *words[i];
    char detail[128] = "";
    if (!ok) {
        const std::size_t at = nodes.FirstDifference(theirBytes, ourBytes);
        std::snprintf(detail, sizeof(detail), "%u nodes, first differing %p, result %p / %p",
                      static_cast<unsigned>(nodes.nodes.size()),
                      at < nodes.nodes.size() ? static_cast<void*>(nodes.nodes[at]) : nullptr,
                      static_cast<void*>(theirResult), static_cast<void*>(ourResult));
    }
    ei::verify::Report(name, ok, detail);
}

template <bool Left>
void __fastcall RotateVerify(RbNodeBase* x, RbNodeBase*& root) {
    Nodes nodes;
    nodes.Add(x->parent);
    nodes.AddDescendants(x, 2);
    RbNodeBase** const words[] = {&root};
    Compare(
        Left ? "stlp::_Rb_global::_Rotate_left" : "stlp::_Rb_global::_Rotate_right", nodes, words,
        [&]() -> RbNodeBase* {
            (Left ? s_rotateLeft : s_rotateRight)(x, root);
            return nullptr;
        },
        [&]() -> RbNodeBase* {
            (Left ? stlp::rb::RotateLeft : stlp::rb::RotateRight)(x, root);
            return nullptr;
        });
}

void __fastcall RebalanceVerify(RbNodeBase* x, RbNodeBase*& root) {
    Nodes nodes;
    nodes.AddPath(x, root);
    RbNodeBase** const words[] = {&root};
    Compare(
        "stlp::_Rb_global::_Rebalance", nodes, words,
        [&]() -> RbNodeBase* {
            s_rebalance(x, root);
            return nullptr;
        },
        [&]() -> RbNodeBase* {
            stlp::rb::Rebalance(x, root);
            return nullptr;
        });
}

RbNodeBase* __fastcall RebalanceForEraseVerify(RbNodeBase* z, RbNodeBase*& root, RbNodeBase*& leftmost,
                                               RbNodeBase*& rightmost) {
    Nodes nodes;
    // The node that leaves its place: z, or its successor when z has two children.
    RbNodeBase* y = z->left && z->right ? RbNodeBase::Minimum(z->right) : z;
    nodes.AddPath(y, root);
    nodes.AddPath(z, root);
    RbNodeBase** const words[] = {&root, &leftmost, &rightmost};
    RbNodeBase* result = nullptr;
    Compare(
        "stlp::_Rb_global::_Rebalance_for_erase", nodes, words,
        [&] { return s_rebalanceForErase(z, root, leftmost, rightmost); },
        [&] { return result = stlp::rb::RebalanceForErase(z, root, leftmost, rightmost); });
    return result;
}

// --- Start-up check -----------------------------------------------------------------------------
// The suite erases from few trees, so a map<int, int> of the game's node pool is filled with
// random keys and emptied again in random order: every erase through RebalanceForEraseVerify
// (the original against ours), the tree's red-black and order invariants checked after each step.

using IntMap = stlp::map<int, int>;
using IntNode = IntMap::Node;

// Black height of the subtree at `n` (with its parent `parent`), or -1 when a rule is broken: the
// links both ways, keys ordered within (lo, hi), no red node with a red child, equal black heights.
int BlackHeight(const IntNode* n, const IntNode* parent, const int* lo, const int* hi, std::size_t& count) {
    if (!n) return 1;
    ++count;
    if (n->parent != parent || (lo && !(*lo < n->first)) || (hi && !(n->first < *hi))) return -1;
    if (n->color == RbNodeBase::kRed && ((n->left && n->left->color == RbNodeBase::kRed) ||
                                         (n->right && n->right->color == RbNodeBase::kRed)))
        return -1;
    const int l = BlackHeight(n->left, n, lo, &n->first, count);
    const int r = BlackHeight(n->right, n, &n->first, hi, count);
    if (l < 0 || l != r) return -1;
    return l + (n->color == RbNodeBase::kBlack ? 1 : 0);
}

bool Valid(IntMap& m) {
    IntNode* h = m.end();
    IntNode* root = m.Root();
    if (!root) return m.size() == 0 && h->left == h && h->right == h;
    std::size_t count = 0;
    return root->color == RbNodeBase::kBlack && BlackHeight(root, h, nullptr, nullptr, count) > 0 &&
           count == m.size() && h->left == IntNode::From(RbNodeBase::Minimum(root->Base())) &&
           h->right == IntNode::From(RbNodeBase::Maximum(root->Base())) && h->color == RbNodeBase::kRed;
}

void TreeStartupCheck() {
    if (!s_rebalanceForErase) return;
    std::uint32_t seed = 12345;
    auto next = [&] { return (seed = seed * 1103515245u + 12345u) >> 8; };
    for (int round = 0; round < 4; ++round) {
        IntMap m;
        m.Construct();
        const int keys = round == 0 ? 8 : round == 1 ? 60 : 700;
        std::vector<IntNode*> nodes;
        for (int i = 0; i < keys * 2; ++i) {
            auto inserted = m.InsertUnique(IntMap::Value{static_cast<int>(next() % (keys * 3)), i});
            if (inserted.second) nodes.push_back(inserted.first);
        }
        bool ok = Valid(m);
        // Shuffled, then erased one by one (the last round from the smallest key up).
        if (round < 3) {
            for (std::size_t i = nodes.size(); i > 1; --i) std::swap(nodes[i - 1], nodes[next() % i]);
        } else {
            std::sort(nodes.begin(), nodes.end(), [](IntNode* a, IntNode* b) { return a->first < b->first; });
        }
        for (IntNode* n : nodes) {
            RbNodeBase* h = m.end()->Base();
            RbNodeBase* y = RebalanceForEraseVerify(n->Base(), h->parent, h->left, h->right);
            ok = ok && y == n->Base();
            IntMap::DestroyNode(n);
            --m.Count();
            ok = ok && Valid(m);
        }
        ei::verify::Report("stlp::_Rb_tree start-up (insert / erase invariants)", ok, ok ? "" : "invariant broken");
        m.Destroy();
    }
}
EI_STARTUP_CHECK(TreeStartupCheck);

} // namespace

// 0043DBE0: MOV EAX,[ECX] (2), MOV EDX,[EAX+0xC] (3). 0043FBE0: PUSH ESI, MOV ESI,[ECX],
// CMP byte [ESI],0 (6). The rotations: MOV EAX,[ECX+0xC/8], PUSH ESI, MOV ESI,[EAX+8/0xC] (7).
// 0043FE90: MOV byte [ECX],0, MOV EAX,[EDX] (5). 0043D830: SUB ESP,8, PUSH EBX, PUSH EBP (5).
EI_VERIFY(0x0043DBE0, IncrementVerify, 5, s_increment);
EI_VERIFY(0x0043FBE0, DecrementVerify, 6, s_decrement);
EI_VERIFY(0x0043E300, RotateVerify<true>, 7, s_rotateLeft);
EI_VERIFY(0x0043E350, RotateVerify<false>, 7, s_rotateRight);
EI_VERIFY(0x0043FE90, RebalanceVerify, 5, s_rebalance);
EI_VERIFY(0x0043D830, RebalanceForEraseVerify, 5, s_rebalanceForErase);
