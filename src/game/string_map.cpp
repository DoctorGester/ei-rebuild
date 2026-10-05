// The debug information panel at 0079B930 (vtable 0073BFA4 on the abstract 0073BFAC): an STLport
// map<string, string> at +4 with a setter (slot 0) and a remover (slot 1), drawn as "key  value"
// lines (shadowed, 15 pixels apart) into a panel surface of the interface at most twice a second.
// 004447D0 returns the panel. The map's algorithms and the MFC CFont members are library code. The
// constructor (00444740) runs from a static initialiser before the hooks are installed and is not
// rewritten. See docs/notes/unmapped_410_470.md ("Ninth round").
//   +0x10 the interface, +0x14 the panel surface (0x38 bytes), +0x18 the font (CFont),
//   +0x1C the rectangle on screen, +0x2C the rectangle asked for (+0x34 / +0x38 its corner),
//   +0x3C shown, +0x40 the tick of the last drawing.
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp.h"

#include <cstdint>
#include <cstring>
#include <new>
#include "originals_library.h"
#include "originals_ui.h"

namespace strmap {

namespace {

struct StringRep {               // STLport basic_string
    char* start;
    char* finish;
    char* eos;
};
struct Node {                    // _Rb_tree node (0x28 bytes)
    std::uint32_t color;
    Node* parent;
    Node* left;
    Node* right;
    StringRep key;               // +0x10
    StringRep value;             // +0x1C
};
static_assert(sizeof(Node) == 0x28);
struct Tree {                    // the map at +4 of the table
    Node* header;
    std::uint32_t count;
};

// STLport (library).
__declspec(naked) Node** __fastcall LowerBound(Tree*, void*, Node** /*out*/, const StringRep*) EI_ORIGINAL(0x004456E0)
__declspec(naked) Node** __fastcall Find(Tree*, void*, Node** /*out*/, const StringRep*) EI_ORIGINAL(0x00445620)
__declspec(naked) bool __fastcall Less(const StringRep* /*a*/, const StringRep* /*b*/) EI_ORIGINAL(0x00445780)
inline void __fastcall StringReserve(StringRep* a0, void*, const void* a2, std::size_t a3) { StringReserveOriginal((void*)a0, nullptr, a2, a3); }
__declspec(naked) void* __fastcall PairConstruct(void* /*pair*/, void*, const StringRep*, const StringRep*) EI_ORIGINAL(0x00445170)
__declspec(naked) Node** __fastcall InsertHint(Tree*, void*, Node** /*out*/, Node* /*hint*/, const void* /*pair*/) EI_ORIGINAL(0x004450B0)
__declspec(naked) void __fastcall PairDestruct(void*) EI_ORIGINAL(0x00445040)
void __fastcall StringErase(StringRep* a0, void*, char* a2, char* a3) { StringEraseOriginal((void*)a0, nullptr, a2, a3); }
void __fastcall StringAppend(StringRep* a0, void*, const char* a2, const char* a3, int a4) { StringAppendRangeOriginal((void*)a0, nullptr, a2, a3, (const void*)a4); }
__declspec(naked) void __fastcall Erase(Tree*, void*, Node* /*position*/) EI_ORIGINAL(0x00445010)
__declspec(naked) void __fastcall EraseSubtree(Tree*, void*, Node*) EI_ORIGINAL(0x00445310)
__declspec(naked) void __fastcall PairDelete(void*, void*, unsigned) EI_ORIGINAL(0x004457F0)
__declspec(naked) void __fastcall NodeFree(Node*, std::size_t) EI_ORIGINAL(0x004452D0)

Tree& TreeOf(void* self) { return *reinterpret_cast<Tree*>(static_cast<std::uint8_t*>(self) + 4); }

void ReleaseString(StringRep& s) {
    if (s.start) stlp::deallocate(s.start, static_cast<std::size_t>(s.eos - s.start));
}

} // namespace

// 004447D0.
void* __cdecl StringTable() { return reinterpret_cast<void*>(0x0079B930); }
EI_REPLACE(0x004447D0, StringTable);

// 004447E0 (slot 0): table[key] = value (a missing key is inserted at its lower bound with an
// empty value first).
void __fastcall StringTableSet(void* self, void*, const char* key, const char* value) {
    StringRep k{};
    new (&k) stlp::string(key, key + std::strlen(key));
    Tree& tree = TreeOf(self);
    Node* it;
    LowerBound(&tree, nullptr, &it, &k);
    if (it == tree.header || Less(&k, &it->key)) {
        StringRep empty;
        char alloc;
        StringReserve(&empty, nullptr, &alloc, 8);
        *empty.start = 0;
        alignas(4) std::uint8_t pair[sizeof(StringRep) * 2];
        PairConstruct(pair, nullptr, &k, &empty);
        Node* inserted;
        InsertHint(&tree, nullptr, &inserted, it, pair);
        it = inserted;
        PairDestruct(pair);
        StlStringDestroy(&empty);
    }
    StringRep& s = it->value;
    std::size_t length = std::strlen(value);
    std::size_t size = static_cast<std::size_t>(s.finish - s.start);
    if (length <= size) {
        std::memcpy(s.start, value, length);
        StringErase(&s, nullptr, s.start + length, s.finish);
    } else {
        std::memcpy(s.start, value, size);
        StringAppend(&s, nullptr, value + size, value + length, 0);
    }
    ReleaseString(k);
}
EI_REPLACE(0x004447E0, StringTableSet);

// 004449A0 (slot 1): removes `key`.
void __fastcall StringTableRemove(void* self, void*, const char* key) {
    StringRep k{};
    new (&k) stlp::string(key, key + std::strlen(key));
    Tree& tree = TreeOf(self);
    Node* it;
    Find(&tree, nullptr, &it, &k);
    ReleaseString(k);
    if (it != tree.header) Erase(&tree, nullptr, it);
}
EI_REPLACE(0x004449A0, StringTableRemove);

// 00444690: the table's destructor (at exit): every node destroyed and freed, then the header.
void __fastcall StringTableDestruct(void* self) {
    Tree& tree = TreeOf(self);
    if (tree.count) {
        Node* node = tree.header->parent;
        while (node) {
            EraseSubtree(&tree, nullptr, node->right);
            Node* left = node->left;
            PairDelete(&node->key, nullptr, 0);
            NodeFree(node, 1);
            node = left;
        }
        tree.header->left = tree.header;
        tree.header->parent = nullptr;
        tree.header->right = tree.header;
        tree.count = 0;
    }
    if (Node* header = tree.header) {
        *reinterpret_cast<void**>(header) = *reinterpret_cast<void**>(0x0079B234);
        *reinterpret_cast<void**>(0x0079B234) = header;
    }
}
EI_REPLACE(0x00444690, StringTableDestruct);

// ---- The panel ---------------------------------------------------------------------------------

namespace {

template <typename T>
T& At(void* base, std::size_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }

// The font (MFC CFont, library), the interface's panel rectangles, the panel surface and its DC
// (an MFC CDC: slots 10 SelectObject(CFont*), 12 SetTextColor, 26 DrawText), the red-black tree's
// increment (library).
__declspec(naked) int __fastcall FontCreatePoint(void*, void*, int /*size*/, const char* /*face*/, void* /*dc*/) EI_ORIGINAL(0x0070BBE5)
__declspec(naked) void __fastcall InterfaceFitRect(void*, void*, RECT*) EI_ORIGINAL(0x005F7CA0)
__declspec(naked) void __fastcall InterfaceSetPanelRect(void*, void*, RECT*) EI_ORIGINAL(0x005F7DA0)
__declspec(naked) void* __fastcall PanelConstruct(void*, void*, void* /*ui*/, RECT*, int) EI_ORIGINAL(0x005FCDD0)
__declspec(naked) void __fastcall PanelDestruct(void*) EI_ORIGINAL(0x005FD120)
void __fastcall DcFillSolidRect(void* a0, void*, int a2, int a3, int a4, int a5, COLORREF a6) { DcFillSolidRectOriginal(a0, nullptr, a2, a3, a4, a5, a6); }
int __fastcall DcSetBkMode(void* a0, void*, int a2) { return DcSetBkModeOriginal(a0, nullptr, a2); }
void __fastcall TreeIncrement(Node** a0) { TreeIncrementOriginal((void*)a0); }
__declspec(naked) void __fastcall PanelRedrawAt(void*) EI_ORIGINAL(0x00444D00)
__declspec(naked) void __fastcall PanelShowAt(void*, void*, std::uint8_t) EI_ORIGINAL(0x00444F50)
__declspec(naked) void __fastcall PanelSetRectAt(void*, void*, const RECT*) EI_ORIGINAL(0x00444C10)

constexpr int kDcSelectFont = 10;
constexpr int kDcSetTextColor = 12;
constexpr int kDcDrawText = 26;

void DrawString(void* dc, const StringRep& s, RECT* rect) {
    ei::VCall<int>(dc, kDcDrawText, static_cast<const char*>(s.start), static_cast<int>(s.finish - s.start), rect, 0x800u);
}

} // namespace

// 00444A70: binds the panel to the interface: an 8-point font, the rectangle (10, 10, 290, 290).
void __fastcall PanelInit(void* self, void*, void* ui) {
    At<void*>(self, 0x10) = ui;
    At<void*>(self, 0x14) = nullptr;
    At<std::uint8_t>(self, 0x3C) = 0;
    auto* font = static_cast<std::uint32_t*>(gamecrt::operator_new(8));
    if (font) {
        font[1] = 0;
        font[0] = 0x0073BFB4;   // CFont
    }
    At<void*>(self, 0x18) = font;
    if (!FontCreatePoint(font, nullptr, 0x5A, reinterpret_cast<const char*>(0x007842C8), nullptr)) {
        if (void* f = At<void*>(self, 0x18)) ei::VCall<void>(f, 1, 1);
        At<void*>(self, 0x18) = nullptr;
    }
    RECT rect = {10, 10, 0x122, 0x122};
    PanelSetRectAt(self, nullptr, &rect);
}
EI_REPLACE(0x00444A70, PanelInit);

// 00444BD0: frees the panel surface and the font.
void __fastcall PanelRelease(void* self) {
    if (void* surface = At<void*>(self, 0x14)) {
        PanelDestruct(surface);
        gamecrt::operator_delete(surface);
    }
    At<void*>(self, 0x14) = nullptr;
    if (void* font = At<void*>(self, 0x18)) ei::VCall<void>(font, 1, 1);
    At<void*>(self, 0x18) = nullptr;
}
EI_REPLACE(0x00444BD0, PanelRelease);

// 00444C10: the panel's rectangle: fitted by the interface, at least 100 x 15 and at most the
// screen (the interface's +0 / +4); a shown panel is made again.
void __fastcall PanelSetRect(void* self, void*, const RECT* rect) {
    void* ui = At<void*>(self, 0x10);
    int width = At<int>(ui, 0);
    int height = At<int>(ui, 4);
    RECT& asked = At<RECT>(self, 0x2C);
    asked = *rect;
    InterfaceFitRect(ui, nullptr, &asked);
    int dx = asked.right - asked.left;
    int dy = asked.bottom - asked.top;
    if (dx < 100) dx = 100;
    if (dx > width) dx = width;
    if (dy < 15) dy = 15;
    if (dy > height) dy = height;
    asked.right = asked.left + dx;
    asked.bottom = asked.top + dy;
    RECT& shown = At<RECT>(self, 0x1C);
    shown = asked;
    InterfaceSetPanelRect(At<void*>(self, 0x10), nullptr, &shown);
    if (At<std::uint8_t>(self, 0x3C)) {
        PanelShowAt(self, nullptr, 0);
        PanelShowAt(self, nullptr, 1);
    }
}
EI_REPLACE(0x00444C10, PanelSetRect);

// 00444CD0: redraws at most every 500 ms, then the panel surface's slot 0.
void __fastcall PanelTick(void* self) {
    DWORD now = gameapi::timeGetTime();
    if (now - At<DWORD>(self, 0x40) > 500) {
        At<DWORD>(self, 0x40) = now;
        PanelRedrawAt(self);
    }
    if (void* surface = At<void*>(self, 0x14)) ei::VCall<void>(surface, 0);
}
EI_REPLACE(0x00444CD0, PanelTick);

// 00444D00: clears the panel and draws each entry (the key in the left half, the value in the
// right half; a dark copy one pixel down and right first) until the next line would not fit.
void __fastcall PanelRedraw(void* self) {
    void* surface = At<void*>(self, 0x14);
    if (!surface) return;
    void* dc = SurfaceGetDC(surface);
    RECT& asked = At<RECT>(self, 0x2C);
    DcFillSolidRect(dc, nullptr, 0, 0, asked.right - asked.left, asked.bottom - asked.top, 0);
    DcSetBkMode(dc, nullptr, 1);
    void* oldFont = ei::VCall<void*>(dc, kDcSelectFont, At<void*>(self, 0x18));
    RECT key = {0, 0, (asked.right - asked.left) / 2, 20};
    RECT value = {(asked.right - asked.left) / 2, 0, asked.right - asked.left, 20};
    Tree& tree = TreeOf(self);
    Node* it = tree.header->left;
    while (it != tree.header) {
        ei::VCall<COLORREF>(dc, kDcSetTextColor, static_cast<COLORREF>(0x080808));
        RECT shadow = key;
        gameapi::OffsetRect(&shadow, 1, 1);
        RECT r = shadow;
        DrawString(dc, it->key, &r);
        shadow = value;
        gameapi::OffsetRect(&shadow, 1, 1);
        r = shadow;
        DrawString(dc, it->value, &r);
        ei::VCall<COLORREF>(dc, kDcSetTextColor, static_cast<COLORREF>(0xFFFFFF));
        DrawString(dc, it->key, &key);
        DrawString(dc, it->value, &value);
        gameapi::OffsetRect(&key, 0, 15);
        gameapi::OffsetRect(&value, 0, 15);
        if (key.bottom > asked.bottom - asked.top) break;
        TreeIncrement(&it);
    }
    ei::VCall<void*>(dc, kDcSelectFont, oldFont);
    SurfaceReleaseDC(At<void*>(self, 0x14));
}
EI_REPLACE(0x00444D00, PanelRedraw);

// 00444F50: shows the panel (its surface made when missing, drawn at once) or hides it (the
// surface freed).
void __fastcall PanelShow(void* self, void*, std::uint8_t show) {
    At<std::uint8_t>(self, 0x3C) = show;
    if (show) {
        if (!At<void*>(self, 0x14)) {
            void* memory = gamecrt::operator_new(0x38);
            At<void*>(self, 0x14) =
                memory ? PanelConstruct(memory, nullptr, At<void*>(self, 0x10), &At<RECT>(self, 0x1C), 5) : nullptr;
        }
        At<DWORD>(self, 0x40) = gameapi::timeGetTime();
        PanelRedrawAt(self);
        return;
    }
    if (void* surface = At<void*>(self, 0x14)) {
        PanelDestruct(surface);
        gamecrt::operator_delete(surface);
    }
    At<void*>(self, 0x14) = nullptr;
}
EI_REPLACE(0x00444F50, PanelShow);

} // namespace strmap
