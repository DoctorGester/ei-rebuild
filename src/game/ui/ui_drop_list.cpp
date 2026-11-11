// The drop-down list (UiDropList, ui_drop_list.h; ours, the options' resolution row): its row, the open list, its keys and
// mouse. See docs/notes/ui-native.md, "The drop-down list".
#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"
#include "game/ui/ui_scrollbar.h"
#include "game/ui/ui_drop_list.h"

#include "engine/models/texture.h"
#include "lib/rect.h"
#include "platform/platform.h"
#include "game/ui/screen_layout.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace {

constexpr int kFont = 1;
constexpr TextStyle kEntryText{kFont, kUiTextColour};
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



// A flat sprite (no texture) of `colour` over `area`.
void Flat(UiMesh* sprite, float depth, const Rect& area, std::uint32_t colour) {
    sprite->SetRectangle(depth, kSpriteScale, area, 0, 0, kUiTextureSize, kUiTextureSize);
    sprite->texture = nullptr;
    sprite->flags |= FigureMeshFlag::Flat;
    sprite->flatColour = colour;
}

} // namespace

UiDropList::UiDropList(UiContainer* newOwner, Rect newRow) : owner(newOwner), row(newRow) {
    rect = row;
    LoadTexture("Scrollbar", TextureKind::Interface);
    LoadTexture("saveload", TextureKind::Interface);
    rowArrow = AddBlankSprite();
    listShade = AddBlankSprite();
    chosenMark = AddBlankSprite();
    activeMark = AddBlankSprite();
    listFrame = AddBlankSprite();
    const int middle = (row.top + row.bottom) / 2;
    rowArrow->SetRectangle(kRowDepth, kSpriteScale,
                           Rect{row.right - kArrowWidth, middle - kArrowHeight / 2, row.right, middle + kArrowHeight / 2},
                           0x28, 0x80, 0x50, 0xB0);
    for (UiMesh* sprite : {listShade, chosenMark, activeMark, listFrame}) sprite->visible = false;

    UiSurface* surface = AddSurface(std::make_unique<UiSurface>(Rect{0, 0, 800, kLayoutHeight}, SurfaceBlend::Keyed, true));
    surface->PaintWith([this](UiSurface& out) {
        PaintRow(out);
        if (open) PaintList(out);
    });

    scroll = AddChild(std::make_unique<UiScrollbar>(row, kBarDepth, 0, 1.0f));
    scroll->SetEnabled(false);
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
    scroll->SetRect(entries);
    scroll->SetMaximum(count - shown);
    scroll->SetEnabled(bar);
    const Rect mark{entries.left, 0, entries.right - (bar ? kBarWidth : 0), kEntryHeight};   // placed by PlaceMarks
    Flat(listShade, kShadeDepth, entries, kShadeColour);
    Flat(chosenMark, kChosenDepth, mark, kChosenColour);
    Flat(activeMark, kActiveDepth, mark, kActiveColour);
    listFrame->texture = objects.size() > 1 ? objects[1] : nullptr;
    listFrame->SetFrame(kFrameDepth, 100.0f, frame, static_cast<float>(kBorder), 2, 0xFE, 0xB4, 0xF9);
    listShade->visible = true;
    listFrame->visible = true;

    // The owner's children under the list are left out while it is open.
    covered.clear();
    for (const auto& owned : owner->children) {
        UiWindow* child = owned.get();
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
    for (UiMesh* sprite : {listShade, chosenMark, activeMark, listFrame}) sprite->visible = false;
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
    point = screen_layout::FromScreen(point);
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
    g_interface.PlayInterfaceSound(kMessageBoxOkSound);
    SetValue(index);
    Close();
}

void UiDropList::PlaceMarks() {
    auto place = [this](UiMesh* mark, int index, float depth) {
        const Rect entry = EntryRect(index);
        mark->visible = !entry.IsEmpty();
        if (mark->visible) mark->Place((entry.left + entry.right) * 0.5f, (entry.top + entry.bottom) * 0.5f, depth);
    };
    place(chosenMark, value, kChosenDepth);
    place(activeMark, active, kActiveDepth);
}

// --- Drawing --------------------------------------------------------------------------------------

void UiDropList::PaintRow(UiSurface& surface) const {
    if (texts.empty()) return;
    const Rect area = row.Inset(0, 0, kArrowWidth + kGap, 0);
    surface.DrawTextIn(texts[value], area, kEntryText);
}

void UiDropList::PaintList(UiSurface& surface) const {
    for (int i = FirstShown(); i < FirstShown() + shown; ++i) {
        const Rect entry = EntryRect(i);
        if (entry.IsEmpty()) continue;
        const Rect text = entry.Inset(kTextInset, 0, 0, 0);
        surface.DrawTextIn(texts[i], text, kEntryText);
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

void UiDropList::OnEvent(UiEvent& event) {
    switch (event.kind) {
    case UiEventKind::LeftDown: case UiEventKind::LeftDouble: LeftDown(event); break;
    case UiEventKind::MouseMove: MouseMove(event); break;
    case UiEventKind::KeyDown: KeyDown(event); break;
    case UiEventKind::Wheel: Wheel(event); break;
    default: UiContainer::OnEvent(event); break;
    }
}

// Every press is used: the scroll bar's when it takes it.
void UiDropList::LeftDown(UiEvent& event) {
    if (!open) {
        event.handled = true;
        g_interface.PlayInterfaceSound(kMessageBoxOkSound);
        Open();
        return;
    }
    if (scroll->enabled) {
        scroll->OnEvent(event);
        if (event.handled) return;
    }
    event.handled = true;
    if (const int index = EntryAt(event.at); index >= 0) {
        Choose(index);
        return;
    }
    Point at = screen_layout::FromScreen(event.at);
    if (!frame.Contains(at)) Close();
}

void UiDropList::MouseMove(UiEvent& event) {
    if (!open) return;
    if (const int index = EntryAt(event.at); index >= 0 && index != active) {
        active = index;
        PlaceMarks();
    }
}

void UiDropList::Wheel(UiEvent& event) {
    if (open && scroll->enabled) scroll->OnEvent(event);
}

void UiDropList::KeyDown(UiEvent& event) {
    if (!open) return;
    switch (event.key) {
    case platform::Key::Up: SetActive(active - 1); break;
    case platform::Key::Down: SetActive(active + 1); break;
    case platform::Key::Home: SetActive(0); break;
    case platform::Key::End: SetActive(static_cast<int>(std::ssize(texts)) - 1); break;
    case platform::Key::Return: Choose(active); break;
    case platform::Key::Escape:
        g_interface.PlayInterfaceSound(kMessageBoxCancelSound);
        Close();
        break;
    default: break;
    }
}
