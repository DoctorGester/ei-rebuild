// The battle screen's weapon bar, quick items, spell column and action column: the four
// constructors (0063BAB0, 0063C800, 0063E2A0, 0063F300), the functions that add a slot's sprite
// (0063BCC0, 0063CAE0, 0063E4B0, 0063F500) and the weapon bar's highlight (0063C050). Rebirth
// patches all nine for its WideScreen layout, reproduced behind the flag (W3, ui_widescreen.h):
// the weapon bar and the action column at the screen's left edge, the quick items and the spell
// column at its right (the panel offset while some of them run, layout constants moved by
// EdgeShift). Rewritten callers keep their forwarders (the battle screen's Create, the panels'
// SetUnit). See ui.h, docs/notes/ui.md ("Battle screen") and docs/notes/rebirth.md ("WideScreen").
#include "ui.h"
#include "game_strings.h"

#include "item_db_text.h"
#include "ui_widescreen.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "originals_library.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>

namespace {

void** Vtable(std::uintptr_t address) { return ei::GamePointer<void*>(address); }

float Pi() { return 3.1415927f /*007423F8*/; }
// A texel as a texture offset: 1 / 256 (0073EEB8).
float TexelStep() { return 0.00390625f /*0073EEB8*/; }
// The layout y the columns' bottom button sits at (00742948, 490).
float ColumnBottom() { return 490.0f /*00742948*/; }

constexpr std::uintptr_t kQuickEntriesVtable = std::uintptr_t(0x007428A0);
constexpr std::uintptr_t kDwordArrayVtable = std::uintptr_t(0x0073C3D0);

constexpr std::uint32_t kPlainFlag = 0x100;        // CUiMesh::flags_: drawn in flatColour_
constexpr std::uint32_t kWhite = 0xFFFFFFFF;
constexpr std::uint32_t kColourOn = 0xFF282828;    // the weapon bar's highlighted slot
constexpr std::uint32_t kColourOff = 0xFF000000;
constexpr int kStep = 40;                          // the weapon slots' width, the columns' button height
constexpr int kIconSize = 0x40;                    // the items', spells' and actions' pictures
constexpr int kWeaponWidth = 0x2A, kWeaponHeight = 0x80;
constexpr int kMaxQuickItems = 8;

CUiMesh* Sprite(CUiWindow* window, int index) { return reinterpret_cast<CUiMesh*>(window->sceneObjects_.data[index]); }

// The tooltips' labels: the texts "infoitem_<id>" appended in order.
template <int N>
void AddLabels(mfc::CStringArray* labels, const int (&ids)[N]) {
    CTextManager* texts = TheTextManager();
    mfc::CString text;
    for (int id : ids) {
        text.Format("infoitem_%d", id);
        texts->GetString(text, &text);
        labels->SetAtGrow(labels->size, text);
    }
}

// Where the n-th 64 x 64 picture goes in a panel's texture: four down a column, the columns from
// `firstColumn`.
struct IconPlace {
    int x;
    int y;
};
IconPlace IconAt(int n, int firstColumn) { return {firstColumn + (n / 4) * kIconSize, (n % 4) * kIconSize}; }

void SetOffsets(CUiMesh* sprite, int x, int bottom) {
    sprite->textureOffsetU_ = static_cast<float>(x) * TexelStep();
    sprite->textureOffsetV_ = static_cast<float>(bottom) * TexelStep();
}

void MakePlain(CUiMesh* sprite) {
    sprite->flatColour_ = kWhite;
    sprite->flags_ |= kPlainFlag;
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

CHudWeaponBar* CHudWeaponBar::Construct(CInterface3D* ui, CBattleScreen* owner) {
    CUiWindow::Construct(ui);
    owner_ = owner;
    auto* labels = reinterpret_cast<mfc::CStringArray*>(&labelsVtable_);
    labels->Construct();
    vtable_ = Vtable(0x0074279C);
    inputVtable_ = Vtable(0x00742768);
    // WideScreen: the rectangle and the first region from 80 - shift (0063BAFC, 0063BB2B); the
    // regions still run up to x 240, so the bar has more of them (no tips beyond the fourth).
    const int shift = widescreen::EdgeShift();
    rect_ = {80 - shift, 520, 240 - shift, 600};
    for (int x = 80 - shift; x < 240; x += kStep) AddRegion()->CallSetRect(x, 520, x + kStep, 600);
    ClearSceneObjects();
    selected_ = -1;
    count_ = 0;
    for (int i = 0; i < 4; ++i) regions_.data[i]->SetTip(0, 1, 0);
    unit_ = nullptr;
    const int ids[] = {15, 20, 10, 5};
    AddLabels(labels, ids);
    return this;
}
EI_REPLACE_METHOD(0x0063BAB0, CHudWeaponBar::Construct);

void CHudWeaponBar::HighlightVanilla(int index, int on) {
    widescreen::EnterLeftPanel();   // addon's 100233ED; 10023406 at both returns
    if (index < 0 || index >= count_) {
        widescreen::LeavePanel();
        return;
    }
    CUiMesh* sprite = Sprite(this, index);
    const int left = (index * 5 + 10) * 8;
    const int right = (index * 5 + 15) * 8;
    if (on) {
        float x = static_cast<float>(index) * 40.0f /*0073D72C*/ + 100.0f /*0073BAB0*/;   // 40, 100
        sprite->Place(x, 540.0f, 12.0f);
        sprite->SetScale(0.65f);
        sprite->flatSpecular_ = kColourOn;
        regions_.data[index]->CallSetRect(left, 495, right, 600);
        widescreen::LeavePanel();
        return;
    }
    // The more weapons, the larger each: 1.2 (0073CDF4) per slot in use, 24 (0073CE28) deep at scale 1.
    float scale = 1.0f /*0073B84C*/;
    for (int i = count_; i > 0; --i) scale *= 1.2f /*0073CDF4*/;
    float depth = scale * 24.0f /*0073CE28*/;
    float x = static_cast<float>(index) * 40.0f /*0073D72C*/ + 100.0f /*0073BAB0*/;
    sprite->Place(x, 560.0f, depth);
    sprite->SetScale(scale);
    sprite->flatSpecular_ = kColourOff;
    regions_.data[index]->CallSetRect(left, 520, right, 600);
    widescreen::LeavePanel();
}
EI_REPLACE_METHOD(0x0063C050, CHudWeaponBar::HighlightVanilla);

void CHudWeaponBar::AddWeapon(const char* model, const char* image) {
    // Slots 0 and 1 share the texture's first column, 2 and 3 the second; odd slots the lower
    // half. (The original leaves both unset for a count over 3, which SetUnit never reaches.)
    const int x = count_ >= 2 ? kWeaponWidth : 0;
    const int y = (count_ & 1) ? kWeaponHeight : 0;
    CopyImage(objects_.data[0], image, 0x80, 0x80, kWeaponWidth, kWeaponHeight, x, y, 0, 0);
    CUiMesh* sprite = AddSprite(model);
    if (sprite) {
        // Turned about z, then y, then z again: pi * -0.05 (00742804), pi * -1/3 (00742800) and
        // pi * 0.75 (0073C524).
        Quaternion first, second, third;
        first.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, Pi() * (-0.05f /*00742804*/));
        second.SetAxisAngle(Vector3{0.0f, 1.0f, 0.0f}, Pi() * (-0.33333334f /*00742800*/));
        Quaternion both = ProductFirst(first, second);
        third.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, Pi() * 0.75f /*0073C524*/);
        sprite->localRotation_ = ProductSecond(both, third);
    } else {
        sprite = AddSprite(nullptr);
        // 80 and 120; WideScreen: both less the shift (addon points the FADDs at its two floats
        // written over 005FC9F4, 0063BF71 / 0063BF80).
        float offset = static_cast<float>(count_ * kStep);
        const float shift = static_cast<float>(widescreen::EdgeShift());
        sprite->SetRectangle(12.0f, 40.0f, offset + (80.0f /*00741F1C*/ - shift), 520.0f,
                             offset + (120.0f /*00740094*/ - shift), 600.0f, 0, 0, kWeaponWidth, kWeaponHeight);
    }
    SetOffsets(sprite, x, y + kWeaponHeight);
    MakePlain(sprite);
    int index = count_;
    count_ = index + 1;
    Highlight(index, 0);
}
EI_REPLACE_METHOD(0x0063BCC0, CHudWeaponBar::AddWeapon);

// --- Quick items -----------------------------------------------------------------------------------------------

CHudQuickItems* CHudQuickItems::Construct(CInterface3D* ui, CBattleScreen* owner) {
    widescreen::EnterRightPanel();   // addon's 10023477; 1002348E at the end
    CUiWindow::Construct(ui);
    owner_ = owner;
    auto* entries = reinterpret_cast<mfc::CArray<Entry>*>(&entriesVtable_);
    entries->Construct(kQuickEntriesVtable);
    auto* labels = reinterpret_cast<mfc::CStringArray*>(&labelsVtable_);
    labels->Construct();
    vtable_ = Vtable(0x0074283C);
    inputVtable_ = Vtable(0x00742808);
    const int shift = widescreen::EdgeShift();
    rect_ = {520 + shift, 500, 720 + shift, 600};   // WideScreen: 0063C880 / 0063C894
    // The bottom row (the unit's items), then the top row (the player's), each from the right.
    for (int x = 670; x > 470; x -= 50) AddRegion()->CallSetRect(x, 550, x + 50, 600);
    for (int x = 670; x > 470; x -= 50) AddRegion()->CallSetRect(x, 500, x + 50, 550);
    ClearSceneObjects();
    shownCount_ = 0;
    selected_ = -1;
    for (int i = 0; i < 8; ++i) {
        regions_.data[i]->SetTip(0, 1, 0);
        regionItem_[i] = -1;
    }
    entries->SetSize(0, -1);
    unit_ = nullptr;
    const int ids[] = {7, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28};
    AddLabels(labels, ids);
    widescreen::LeavePanel();
    return this;
}
EI_REPLACE_METHOD(0x0063C800, CHudQuickItems::Construct);

void CHudQuickItems::AddItem(const char* model, const char* image) {
    if (shownCount_ >= kMaxQuickItems) return;
    const IconPlace at = IconAt(shownCount_, 0x80);
    CopyImage(objects_.data[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y, 0, 0);
    CUiMesh* sprite = AddSprite(model);
    if (sprite) {
        Quaternion turn;
        sprite->localRotation_ = *turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, Pi());
    } else {
        sprite = AddSprite(nullptr);
        const float shift = static_cast<float>(widescreen::EdgeShift());   // WideScreen: 0063CC13 / 0063CC07
        sprite->SetRectangle(20.0f, 20.0f, 680.0f + shift, 560.0f, 720.0f + shift, 600.0f, 0, 0, kIconSize, kIconSize);
    }
    SetOffsets(sprite, at.x, at.y + kIconSize);
    MakePlain(sprite);
    ++shownCount_;
}
EI_REPLACE_METHOD(0x0063CAE0, CHudQuickItems::AddItem);

// --- Spell column ----------------------------------------------------------------------------------------------

CHudSpellColumn* CHudSpellColumn::Construct(CInterface3D* ui, CBattleScreen* owner) {
    CUiWindow::Construct(ui);
    owner_ = owner;
    auto* labels = reinterpret_cast<mfc::CStringArray*>(&labelsVtable_);
    labels->Construct();
    vtable_ = Vtable(0x007428E4);
    inputVtable_ = Vtable(0x007428B0);
    const int shift = widescreen::EdgeShift();
    rect_ = {760 + shift, 190, 800 + shift, 510};   // WideScreen: 0063E306 / 0063E31A
    ClearSceneObjects();
    ClearRegions();
    count_ = 0;
    selected_ = -1;
    unit_ = nullptr;
    const int ids[] = {7, 18, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28};
    AddLabels(labels, ids);
    return this;
}
EI_REPLACE_METHOD(0x0063E2A0, CHudSpellColumn::Construct);

void CHudSpellColumn::AddSpell(const char* model, const char* image) {
    widescreen::EnterRightPanel();   // addon's 1002344D; 10023464 at the end
    // What is there moves up a step; the new button takes the bottom place.
    for (int i = 0; i < regions_.size; ++i) {
        int top = (i - count_) * kStep;
        regions_.data[i]->CallSetRect(760, top + 470, 800, top + 510);
    }
    AddRegion()->CallSetRect(760, 470, 800, 510);
    for (int i = 0; i < sceneObjects_.size; ++i) {
        float y = ColumnBottom() - static_cast<float>((count_ - i) * kStep);
        Sprite(this, i)->Place(780.0f, y, 40.0f);
    }
    const IconPlace at = IconAt(count_, 0x80);
    CopyImage(objects_.data[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y, 0, 0);
    CUiMesh* sprite = AddSprite(model);
    SetOffsets(sprite, at.x, at.y + kIconSize);
    Quaternion turn;
    sprite->localRotation_ = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, Pi());
    sprite->Place(780.0f, 490.0f, 40.0f);
    MakePlain(sprite);
    int index = count_;
    count_ = index + 1;
    CUiMesh* button = Sprite(this, index);
    button->SetScale(1.0f);
    button->flatSpecular_ = kColourOff;
    selected_ = index;
    widescreen::LeavePanel();
}
EI_REPLACE_METHOD(0x0063E4B0, CHudSpellColumn::AddSpell);

// --- Action column ---------------------------------------------------------------------------------------------

CHudActionColumn* CHudActionColumn::Construct(CInterface3D* ui, CBattleScreen* owner) {
    CUiWindow::Construct(ui);
    owner_ = owner;
    actions_.Construct(kDwordArrayVtable);
    auto* labels = reinterpret_cast<mfc::CStringArray*>(&labelsVtable_);
    labels->Construct();
    vtable_ = Vtable(0x00742980);
    inputVtable_ = Vtable(0x0074294C);
    // WideScreen (addon's 10023416 at 0063F378, 0063F38E): the panel offset from here on, the
    // rectangle less the shift.
    widescreen::EnterLeftPanel();
    const int shift = widescreen::EdgeShift();
    rect_ = {-shift, 250, 40 - shift, 510};
    for (int y = 470; y > 230; y -= kStep) AddRegion()->CallSetRect(0, y, 40, y + kStep);
    ClearSceneObjects();
    actions_.SetSize(0, -1);
    count_ = 0;
    selected_ = -1;
    for (int i = 0; i < 6; ++i) regions_.data[i]->SetTip(0, 1, 0);
    unit_ = nullptr;
    mfc::CString text;
    TheTextManager()->GetString("infoitem_16", &text);
    labels->SetAtGrow(labels->size, text);
    widescreen::LeavePanel();        // addon's 10023430
    return this;
}
EI_REPLACE_METHOD(0x0063F300, CHudActionColumn::Construct);

void CHudActionColumn::AddAction(const char* model, const char* image, int action) {
    const IconPlace at = IconAt(count_, 0);
    CopyImage(objects_.data[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y, 0, 0);
    CUiMesh* sprite = AddSprite(model);
    SetOffsets(sprite, at.x, at.y + kIconSize);
    Quaternion turn;
    sprite->localRotation_ = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, Pi());
    float y = ColumnBottom() - static_cast<float>(count_ * kStep);
    sprite->Place(20.0f - static_cast<float>(widescreen::EdgeShift()), y, 40.0f);   // WideScreen: 0063F633
    MakePlain(sprite);
    int place = actions_.size;
    actions_.SetSize(place + 1, -1);
    actions_.data[place] = static_cast<std::uint32_t>(action);
    int index = count_;
    count_ = index + 1;
    CUiMesh* button = Sprite(this, index);
    button->SetScale(1.0f);
    button->flatSpecular_ = kColourOff;
    selected_ = index;
}
EI_REPLACE_METHOD(0x0063F500, CHudActionColumn::AddAction);
