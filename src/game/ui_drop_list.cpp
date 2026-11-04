// The drop-down list (UiDropList, ui.h; ours, the options' resolution row): its row, the open list, its keys and
// mouse. See docs/notes/ui-native.md, "The drop-down list".
#include "ui.h"

#include "game_strings.h"
#include "texture.h"
#include "lib/rect.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

constexpr int kFont = 1;
constexpr int kLayoutHeight = 600;

constexpr int kEntryHeight = 20;
constexpr int kMaxShown = 10;
constexpr int kBorder = 5;           // the frame's width (the save list's frame art)
constexpr int kGap = 2;              // between the row and the list's frame
constexpr int kBarWidth = 10;        // the scroll bar's strip at the entries' right
constexpr int kArrowWidth = 10;      // the row's arrow: the scroll bar's down arrow
constexpr int kArrowHeight = 12;
constexpr int kTextInset = 4;

// The flat colours (ARGB) of the list's shade (the save list's black, denser: what the list covers hardly shows), the
// active entry's highlight (the rows') and the chosen entry's darker one. A window's sprites draw with alpha reference
// 200, so an alpha below 0xC8 draws nothing.
constexpr std::uint32_t kShadeColour = 0xE0000000;
constexpr std::uint32_t kActiveColour = 0xC8A06800;
constexpr std::uint32_t kChosenColour = 0xC8503400;

// Depths: the row's arrow with the rows' controls (3); the list in front of them and of the rows' highlight, behind
// the near plane (NearClipDistance, 2: nothing nearer is drawn), the scroll bar in front of the list.
constexpr float kRowDepth = 3.0f;
constexpr float kShadeDepth = 2.6f;
constexpr float kChosenDepth = 2.55f;
constexpr float kActiveDepth = 2.5f;
constexpr float kFrameDepth = 2.4f;
constexpr float kBarDepth = 2.3f;

enum Sprite { kArrow, kShade, kChosenMark, kActiveMark, kFrame };

// A flat sprite (no texture) of `colour` over `area`.
void Flat(UiMesh* sprite, float depth, const Rect& area, std::uint32_t colour) {
    sprite->SetRectangle(depth, 100.0f, area, 0, 0, 0x100, 0x100);
    sprite->texture = nullptr;
    sprite->flags |= FigureMeshFlag::Flat;
    sprite->flatColour = colour;
}

} // namespace

UiDropList::UiDropList(Interface3D* ui, UiContainer* newOwner, Rect newRow) : UiContainer(ui), owner(newOwner), row(newRow) {
    rect = row;
    LoadTexture("Scrollbar", TextureKind::Interface);
    LoadTexture("saveload", TextureKind::Interface);
    for (int i = 0; i <= kFrame; ++i) AddBlankSprite();
    const int middle = (row.top + row.bottom) / 2;
    sceneObjects[kArrow]->SetRectangle(kRowDepth, 100.0f,
                                    Rect{row.right - kArrowWidth, middle - kArrowHeight / 2, row.right,
                                         middle + kArrowHeight / 2},
                                    0x28, 0x80, 0x50, 0xB0);
    for (int i = kShade; i <= kFrame; ++i) sceneObjects[i]->visible = false;

    UiSurface* surface = AddSurface(new UiSurface(ui, Rect{0, 0, 800, kLayoutHeight}, SurfaceBlend::Keyed, true));
    surface->PaintWith([this](UiSurface& out) {
        PaintRow(out);
        if (open) PaintList(out);
    });

    scroll = new UiScrollbar(ui, row, kBarDepth, 0, 1.0f);
    scroll->SetEnabled(false);
    AddChild(scroll);
}

void UiDropList::AddText(std::string_view text) { texts.emplace_back(text); }

bool UiDropList::TakeChanged() {
    if (!changed) return false;
    changed = false;
    return true;
}

void UiDropList::SetValue(int newValue) {
    value = newValue >= 0 && newValue < std::ssize(texts) ? newValue : 0;
    changed = true;
}

void UiDropList::Step(int by) {
    const int count = static_cast<int>(std::ssize(texts));
    if (count != 0) SetValue(((value + by) % count + count) % count);
}

// --- Opening and closing ------------------------------------------------------------------------

void UiDropList::Open() {
    if (open || texts.empty()) return;
    const int count = static_cast<int>(std::ssize(texts));
    // Below the row when the entries fit there (at most kMaxShown), else on the side with more room.
    const int below = kLayoutHeight - row.bottom - kGap - 2 * kBorder;
    const int above = row.top - kGap - 2 * kBorder;
    const int wanted = std::min(count, kMaxShown) * kEntryHeight;
    const bool down = wanted <= below || below >= above;
    shown = std::max(1, std::min({count, kMaxShown, (down ? below : above) / kEntryHeight}));
    const int height = shown * kEntryHeight + 2 * kBorder;
    const int top = down ? row.bottom + kGap : row.top - kGap - height;
    frame = Rect{row.left - kBorder, top, row.right + kBorder, top + height};

    const Rect entries = frame.Inset(kBorder);
    const bool bar = count > shown;
    scroll->SetRect(&entries);
    scroll->SetMaximum(count - shown);
    scroll->SetEnabled(bar);
    const Rect mark{entries.left, 0, entries.right - (bar ? kBarWidth : 0), kEntryHeight};   // placed by PlaceMarks
    Flat(sceneObjects[kShade], kShadeDepth, entries, kShadeColour);
    Flat(sceneObjects[kChosenMark], kChosenDepth, mark, kChosenColour);
    Flat(sceneObjects[kActiveMark], kActiveDepth, mark, kActiveColour);
    sceneObjects[kFrame]->texture = objects.size() > 1 ? objects[1] : nullptr;
    sceneObjects[kFrame]->SetFrame(kFrameDepth, 100.0f, frame, static_cast<float>(kBorder), 2, 0xFE, 0xB4, 0xF9);
    sceneObjects[kShade]->visible = true;
    sceneObjects[kFrame]->visible = true;

    // The owner's children under the list are left out while it is open.
    covered.clear();
    for (UiWindow* child : owner->children) {
        if (child == this || !child->enabled || child->rect.Intersection(frame).IsEmpty()) continue;
        child->SetEnabled(false);
        covered.push_back(child);
    }
    open = true;
    owner->popup = this;
    active = value;
    scroll->SetPosition(std::clamp(value - shown / 2, 0, count - shown));
    PlaceMarks();
}

void UiDropList::Close() {
    if (!open) return;
    open = false;
    if (owner->popup == this) owner->popup = nullptr;
    for (UiWindow* child : covered) child->SetEnabled(true);
    covered.clear();
    scroll->SetEnabled(false);
    for (int i = kShade; i <= kFrame; ++i) sceneObjects[i]->visible = false;
}

// --- Entries ----------------------------------------------------------------------------------------

int UiDropList::FirstShown() const { return scroll->Position(); }

Rect UiDropList::EntryRect(int index) const {
    const int first = FirstShown();
    if (!open || index < first || index >= first + shown || index >= std::ssize(texts)) return Rect{};
    const Rect entries = frame.Inset(kBorder);
    const int top = entries.top + (index - first) * kEntryHeight;
    return Rect{entries.left, top, entries.right - (scroll->enabled ? kBarWidth : 0), top + kEntryHeight};
}

int UiDropList::EntryAt(Point point) const {
    ui->ScalePointFromScreen(&point);
    for (int i = FirstShown(); i < FirstShown() + shown; ++i)
        if (EntryRect(i).Contains(point)) return i;
    return -1;
}

void UiDropList::ScrollTo(int index) {
    const int first = FirstShown();
    if (index < first) scroll->SetPosition(index);
    else if (index >= first + shown) scroll->SetPosition(index - shown + 1);
    PlaceMarks();
}

void UiDropList::SetActive(int index) {
    active = std::clamp(index, 0, static_cast<int>(std::ssize(texts)) - 1);
    ScrollTo(active);
}

void UiDropList::Choose(int index) {
    ui->PlayInterfaceSound(game::OkSoundText());
    SetValue(index);
    Close();
}

void UiDropList::PlaceMarks() {
    auto place = [this](int sprite, int index, float depth) {
        const Rect entry = EntryRect(index);
        UiMesh* mark = sceneObjects[sprite];
        mark->visible = !entry.IsEmpty();
        if (mark->visible) mark->Place((entry.left + entry.right) * 0.5f, (entry.top + entry.bottom) * 0.5f, depth);
    };
    place(kChosenMark, value, kChosenDepth);
    place(kActiveMark, active, kActiveDepth);
}

// --- Drawing --------------------------------------------------------------------------------------

void UiDropList::PaintRow(UiSurface& surface) const {
    if (texts.empty()) return;
    const Rect area = row.Inset(0, 0, kArrowWidth + kGap, 0);
    surface.DrawTextIn(texts[value], area, TextStyle{kFont, kUiTextColour});
}

void UiDropList::PaintList(UiSurface& surface) const {
    for (int i = FirstShown(); i < FirstShown() + shown; ++i) {
        const Rect entry = EntryRect(i);
        if (entry.IsEmpty()) continue;
        const Rect text = entry.Inset(kTextInset, 0, 0, 0);
        surface.DrawTextIn(texts[i], text, TextStyle{kFont, kUiTextColour});
    }
}

void UiDropList::Update() {
    UiContainer::Update();
    if (open) PlaceMarks();
}

// --- Input ------------------------------------------------------------------------------------------

void UiDropList::Deactivate() {
    UiContainer::Deactivate();
    Close();
}

void UiDropList::LeftDown(Point point) {
    handled = true;
    if (!open) {
        ui->PlayInterfaceSound(game::OkSoundText());
        Open();
        return;
    }
    if (scroll->enabled) {
        scroll->handled = false;
        scroll->LeftDown(point);
        if (scroll->handled) return;
    }
    if (const int index = EntryAt(point); index >= 0) {
        Choose(index);
        return;
    }
    Point at = point;
    ui->ScalePointFromScreen(&at);
    if (!frame.Contains(at)) Close();
}

void UiDropList::MouseMove(Point point) {
    if (!open) return;
    if (const int index = EntryAt(point); index >= 0 && index != active) {
        active = index;
        PlaceMarks();
    }
}

void UiDropList::Wheel(int delta, Point point) {
    if (open && scroll->enabled) scroll->Wheel(delta, point);
}

void UiDropList::KeyDown(int key, int, bool) {
    if (!open) return;
    switch (key) {
    case platform::kKeyUp: SetActive(active - 1); break;
    case platform::kKeyDown: SetActive(active + 1); break;
    case platform::kKeyHome: SetActive(0); break;
    case platform::kKeyEnd: SetActive(static_cast<int>(std::ssize(texts)) - 1); break;
    case platform::kKeyReturn: Choose(active); break;
    case platform::kKeyEscape:
        ui->PlayInterfaceSound(game::CancelSoundText());
        Close();
        break;
    default: break;
    }
}
