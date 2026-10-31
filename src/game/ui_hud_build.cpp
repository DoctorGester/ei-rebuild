// The battle screen's weapon bar, quick items, spell column and action column: the four constructors, the
// functions that add a slot's sprite and the weapon bar's highlight. Rebirth patches all nine for its
// WideScreen layout, reproduced for every window (screen_layout.h): the weapon bar and the action column at
// the screen's left edge, the quick items and the spell column at its right (the panel offset while some of
// them run, layout constants moved by EdgeShift). See ui.h, docs/notes/ui.md ("Battle screen") and
// docs/notes/rebirth.md ("WideScreen").
#include "ui.h"

#include "text_manager.h"
#include "screen_layout.h"

#include <cstdint>
#include <string>
#include "lib/containers.h"
#include "lib/vecmath.h"
#include "game_singletons.h"
#include "world.h"

#include <vector>
#include <format>

namespace {

// A texel as a texture offset: 1 / 256.
constexpr float kTexelStep = 0.00390625f;
// The layout y the columns' bottom button sits at (490).
constexpr float kColumnBottom = 490.0f;

constexpr std::uint32_t kWhite = 0xFFFFFFFF;
constexpr std::uint32_t kColourOn = 0xFF282828;    // the weapon bar's highlighted slot
constexpr std::uint32_t kColourOff = 0xFF000000;
constexpr int kStep = 40;                          // the weapon slots' width, the columns' button height
constexpr int kIconSize = 0x40;                    // the items', spells' and actions' pictures
constexpr int kWeaponWidth = 0x2A, kWeaponHeight = 0x80;
constexpr int kMaxQuickItems = 8;

// The tooltips' labels: the texts "infoitem_<id>" in order.
template <int N>
std::vector<std::string> Labels(const int (&ids)[N]) {
    TextManager* texts = g_clientWorld.texts;
    std::vector<std::string> labels;
    std::string text;
    for (int id : ids) {
        text = std::format("infoitem_{}", id);
        text = texts->GetString(text);
        labels.push_back(text);
    }
    return labels;
}

// Where the n-th 64 x 64 picture goes in a panel's texture: four down a column, the columns from
// `firstColumn`.
struct IconPlace {
    int x;
    int y;
};
IconPlace IconAt(int n, int firstColumn) { return {firstColumn + (n / 4) * kIconSize, (n % 4) * kIconSize}; }

void SetOffsets(UiMesh* sprite, int x, int bottom) {
    sprite->textureOffsetU = x * kTexelStep;
    sprite->textureOffsetV = bottom * kTexelStep;
}

void MakePlain(UiMesh* sprite) {
    sprite->flatColour = kWhite;
    sprite->flags |= FigureMeshFlag::Flat;
}

// q * r as the weapon bar's first product sums it.
Quaternion ProductFirst(const Quaternion& q, const Quaternion& r) {
    Vector3 qv = {q.x, q.y, q.z};
    Vector3 rv = {r.x, r.y, r.z};
    Quaternion out;
    out.w = r.w * q.w - DotXYZ(qv, rv);
    out.x = ((q.y * r.z - r.y * q.z) + q.x * r.w) + r.x * q.w;
    out.y = ((r.x * q.z - q.x * r.z) + q.y * r.w) + r.y * q.w;
    out.z = ((q.x * r.y - q.y * r.x) + r.z * q.w) + r.w * q.z;
    return out;
}

// q * r as its second product sums it.
Quaternion ProductSecond(const Quaternion& q, const Quaternion& r) {
    Vector3 qv = {q.x, q.y, q.z};
    Vector3 rv = {r.x, r.y, r.z};
    Quaternion out;
    out.z = ((q.x * r.y - q.y * r.x) + q.w * r.z) + q.z * r.w;
    out.y = ((q.z * r.x - q.x * r.z) + q.w * r.y) + q.y * r.w;
    out.x = ((q.y * r.z - q.z * r.y) + q.w * r.x) + q.x * r.w;
    out.w = q.w * r.w - DotZYX(qv, rv);
    return out;
}

} // namespace

// --- Weapon bar ------------------------------------------------------------------------------------------------

HudWeaponBar::HudWeaponBar(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {
    // WideScreen: the rectangle and the first region from 80 - shift; the
    // regions still run up to x 240, so the bar has more of them (no tips beyond the fourth).
    const int shift = screen_layout::EdgeShift();
    rect = {80 - shift, 520, 240 - shift, 600};
    for (int x = 80 - shift; x < 240; x += kStep) AddRegion()->SetRect(x, 520, x + kStep, 600);
    ClearSceneObjects();
    selected = -1;
    count = 0;
    for (int i = 0; i < 4; ++i) regions[i]->SetTip(0);
    unit = nullptr;
    const int ids[] = {15, 20, 10, 5};
    labels = Labels(ids);
}

void HudWeaponBar::Highlight(int index, bool on) {
    screen_layout::EnterLeftPanel();   // addon's 100233ED; 10023406 at both returns
    if (index < 0 || index >= count) {
        screen_layout::LeavePanel();
        return;
    }
    UiMesh* sprite = SceneMesh(index);
    const int left = (index * 5 + 10) * 8;
    const int right = (index * 5 + 15) * 8;
    if (on) {
        float x = index * 40.0f + 100.0f;
        sprite->Place(x, 540.0f, 12.0f);
        sprite->SetScale(0.65f);
        sprite->flatSpecular = kColourOn;
        regions[index]->SetRect(left, 495, right, 600);
        screen_layout::LeavePanel();
        return;
    }
    // The more weapons, the larger each: 1.2 per slot in use, 24 deep at scale 1.
    float scale = 1.0f;
    for (int i = count; i > 0; --i) scale *= 1.2f;
    float depth = scale * 24.0f;
    float x = index * 40.0f + 100.0f;
    sprite->Place(x, 560.0f, depth);
    sprite->SetScale(scale);
    sprite->flatSpecular = kColourOff;
    regions[index]->SetRect(left, 520, right, 600);
    screen_layout::LeavePanel();
}

void HudWeaponBar::AddWeapon(std::string_view model, std::string_view image) {
    // Slots 0 and 1 share the texture's first column, 2 and 3 the second; odd slots the lower
    // half. (Left unset for a count over 3, which SetUnit never reaches.)
    const int x = count >= 2 ? kWeaponWidth : 0;
    const int y = (count & 1) ? kWeaponHeight : 0;
    CopyImage(objects[0], image, 0x80, 0x80, kWeaponWidth, kWeaponHeight, x, y);
    UiMesh* sprite = AddSprite(model);
    if (sprite) {
        // Turned about z, then y, then z again: pi * -0.05, pi * -1/3 and pi * 0.75.
        Quaternion first, second, third;
        first.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi * (-0.05f));
        second.SetAxisAngle(Vector3{0.0f, 1.0f, 0.0f}, kPi * (-0.33333334f));
        Quaternion both = ProductFirst(first, second);
        third.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi * 0.75f);
        sprite->localRotation = ProductSecond(both, third);
    } else {
        sprite = AddBlankSprite();
        // 80 and 120; WideScreen: both less the shift (addon points the FADDs at its two floats
        // written over).
        float offset = static_cast<float>(count * kStep);
        const float shift = static_cast<float>(screen_layout::EdgeShift());
        sprite->SetRectangle(12.0f, 40.0f, offset + (80.0f - shift), 520.0f,
                             offset + (120.0f - shift), 600.0f, 0, 0, kWeaponWidth, kWeaponHeight);
    }
    SetOffsets(sprite, x, y + kWeaponHeight);
    MakePlain(sprite);
    int index = count;
    count = index + 1;
    Highlight(index, false);
}

// --- Quick items -----------------------------------------------------------------------------------------------

// WideScreen: addon enters the right panels before the window's constructor (10023477); the window's reads no
// panel offset, so here it is the body's first step.
HudQuickItems::HudQuickItems(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {
    screen_layout::EnterRightPanel();   // addon's 10023477; 1002348E at the end
    const int shift = screen_layout::EdgeShift();
    rect = {520 + shift, 500, 720 + shift, 600};    // WideScreen
    // The bottom row (the unit's items), then the top row (the player's), each from the right.
    for (int x = 670; x > 470; x -= 50) AddRegion()->SetRect(x, 550, x + 50, 600);
    for (int x = 670; x > 470; x -= 50) AddRegion()->SetRect(x, 500, x + 50, 550);
    ClearSceneObjects();
    shownCount = 0;
    selected = -1;
    for (int i = 0; i < 8; ++i) {
        regions[i]->SetTip(0);
        regionItem[i] = -1;
    }
    entries.clear();
    unit = nullptr;
    const int ids[] = {7, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28};
    labels = Labels(ids);
    screen_layout::LeavePanel();
}

void HudQuickItems::AddItem(std::string_view model, std::string_view image) {
    if (shownCount >= kMaxQuickItems) return;
    const IconPlace at = IconAt(shownCount, 0x80);
    CopyImage(objects[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y);
    UiMesh* sprite = AddSprite(model);
    if (sprite) {
        Quaternion turn;
        sprite->localRotation = *turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, kPi);
    } else {
        sprite = AddBlankSprite();
        const float shift = static_cast<float>(screen_layout::EdgeShift());   // WideScreen
        sprite->SetRectangle(20.0f, 20.0f, 680.0f + shift, 560.0f, 720.0f + shift, 600.0f, 0, 0, kIconSize, kIconSize);
    }
    SetOffsets(sprite, at.x, at.y + kIconSize);
    MakePlain(sprite);
    ++shownCount;
}

// --- Spell column ----------------------------------------------------------------------------------------------

HudSpellColumn::HudSpellColumn(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {
    const int shift = screen_layout::EdgeShift();
    rect = {760 + shift, 190, 800 + shift, 510};    // WideScreen
    ClearSceneObjects();
    ClearRegions();
    count = 0;
    selected = -1;
    unit = nullptr;
    const int ids[] = {7, 18, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28};
    labels = Labels(ids);
}

void HudSpellColumn::AddSpell(std::string_view model, std::string_view image) {
    screen_layout::EnterRightPanel();   // addon's 1002344D; 10023464 at the end
    // What is there moves up a step; the new button takes the bottom place.
    for (int i = 0; i < std::ssize(regions); ++i) {
        int top = (i - count) * kStep;
        regions[i]->SetRect(760, top + 470, 800, top + 510);
    }
    AddRegion()->SetRect(760, 470, 800, 510);
    for (int i = 0; i < std::ssize(sceneObjects); ++i) {
        float y = kColumnBottom - ((count - i) * kStep);
        SceneMesh(i)->Place(780.0f, y, 40.0f);
    }
    const IconPlace at = IconAt(count, 0x80);
    CopyImage(objects[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y);
    UiMesh* sprite = AddSprite(model);
    SetOffsets(sprite, at.x, at.y + kIconSize);
    Quaternion turn;
    sprite->localRotation = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi);
    sprite->Place(780.0f, 490.0f, 40.0f);
    MakePlain(sprite);
    int index = count;
    count = index + 1;
    UiMesh* button = SceneMesh(index);
    button->SetScale(1.0f);
    button->flatSpecular = kColourOff;
    selected = index;
    screen_layout::LeavePanel();
}

// --- Action column ---------------------------------------------------------------------------------------------

HudActionColumn::HudActionColumn(Interface3D* ui, BattleScreen* newOwner) : UiWindow(ui), owner(newOwner) {
    // WideScreen (addon's handler): the panel offset from here on, the rectangle less the shift.
    screen_layout::EnterLeftPanel();
    const int shift = screen_layout::EdgeShift();
    rect = {-shift, 250, 40 - shift, 510};
    for (int y = 470; y > 230; y -= kStep) AddRegion()->SetRect(0, y, 40, y + kStep);
    ClearSceneObjects();
    actions.clear();
    count = 0;
    selected = -1;
    for (int i = 0; i < 6; ++i) regions[i]->SetTip(0);
    unit = nullptr;
    std::string text = g_clientWorld.texts->GetString("infoitem_16");
    labels.push_back(text);
    screen_layout::LeavePanel();        // addon's 10023430
}

void HudActionColumn::AddAction(std::string_view model, std::string_view image, int action) {
    const IconPlace at = IconAt(count, 0);
    CopyImage(objects[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y);
    UiMesh* sprite = AddSprite(model);
    SetOffsets(sprite, at.x, at.y + kIconSize);
    Quaternion turn;
    sprite->localRotation = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi);
    float y = kColumnBottom - (count * kStep);
    sprite->Place(20.0f - screen_layout::EdgeShift(), y, 40.0f);   // WideScreen
    MakePlain(sprite);
    int place = lib::Count(actions);
    actions.resize(place + 1);
    actions[place] = static_cast<std::uint32_t>(action);
    int index = count;
    count = index + 1;
    UiMesh* button = SceneMesh(index);
    button->SetScale(1.0f);
    button->flatSpecular = kColourOff;
    selected = index;
}
