// The battle screen's weapon bar, quick items, spell column and action column: the four constructors, the
// functions that add a slot's sprite and the weapon bar's highlight. Rebirth patches all nine for its
// WideScreen layout, reproduced behind the flag (ui_widescreen.h): the weapon bar and the action column at
// the screen's left edge, the quick items and the spell column at its right (the panel offset while some of
// them run, layout constants moved by EdgeShift). See ui.h, docs/notes/ui.md ("Battle screen") and
// docs/notes/rebirth.md ("WideScreen").
#include "ui.h"

#include "item_db_text.h"
#include "ui_widescreen.h"

#include <cstdint>
#include <string>
#include "lib/str.h"
#include "lib/vecmath.h"
#include "game_singletons.h"

#include <vector>

namespace {

float Pi() { return 3.1415927f /*007423F8*/; }
// A texel as a texture offset: 1 / 256.
float TexelStep() { return 0.00390625f /*0073EEB8*/; }
// The layout y the columns' bottom button sits at (490).
float ColumnBottom() { return 490.0f /*00742948*/; }

constexpr std::uint32_t kPlainFlag = 0x100;        // CUiMesh::flags_: drawn in flatColour_
constexpr std::uint32_t kWhite = 0xFFFFFFFF;
constexpr std::uint32_t kColourOn = 0xFF282828;    // the weapon bar's highlighted slot
constexpr std::uint32_t kColourOff = 0xFF000000;
constexpr int kStep = 40;                          // the weapon slots' width, the columns' button height
constexpr int kIconSize = 0x40;                    // the items', spells' and actions' pictures
constexpr int kWeaponWidth = 0x2A, kWeaponHeight = 0x80;
constexpr int kMaxQuickItems = 8;

CUiMesh* Sprite(CUiWindow* window, int index) { return window->SceneMesh(index); }

// The tooltips' labels: the texts "infoitem_<id>" appended in order.
template <int N>
void AddLabels(std::vector<std::string>* labels, const int (&ids)[N]) {
    CTextManager* texts = TheTextManager();
    std::string text;
    for (int id : ids) {
        text = str::Format("infoitem_%d", id);
        texts->GetString(text.c_str(), &text);
        labels->push_back(text);
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

CHudWeaponBar::CHudWeaponBar(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {
    // WideScreen: the rectangle and the first region from 80 - shift; the
    // regions still run up to x 240, so the bar has more of them (no tips beyond the fourth).
    const int shift = widescreen::EdgeShift();
    rect_ = {80 - shift, 520, 240 - shift, 600};
    for (int x = 80 - shift; x < 240; x += kStep) AddRegion()->SetRect(x, 520, x + kStep, 600);
    ClearSceneObjects();
    selected_ = -1;
    count_ = 0;
    for (int i = 0; i < 4; ++i) regions_[i]->SetTip(0, 1, 0);
    unit_ = nullptr;
    const int ids[] = {15, 20, 10, 5};
    AddLabels(&labels_, ids);
}

void CHudWeaponBar::Highlight(int index, int on) {
    widescreen::EnterLeftPanel();   // addon's 100233ED; 10023406 at both returns
    if (index < 0 || index >= count_) {
        widescreen::LeavePanel();
        return;
    }
    CUiMesh* sprite = Sprite(this, index);
    const int left = (index * 5 + 10) * 8;
    const int right = (index * 5 + 15) * 8;
    if (on) {
        float x = static_cast<float>(index) * 40.0f /*0073D72C*/ + 100.0f /*0073BAB0*/;
        sprite->Place(x, 540.0f, 12.0f);
        sprite->SetScale(0.65f);
        sprite->flatSpecular_ = kColourOn;
        regions_[index]->SetRect(left, 495, right, 600);
        widescreen::LeavePanel();
        return;
    }
    // The more weapons, the larger each: 1.2 per slot in use, 24 deep at scale 1.
    float scale = 1.0f /*0073B84C*/;
    for (int i = count_; i > 0; --i) scale *= 1.2f /*0073CDF4*/;
    float depth = scale * 24.0f /*0073CE28*/;
    float x = static_cast<float>(index) * 40.0f /*0073D72C*/ + 100.0f /*0073BAB0*/;
    sprite->Place(x, 560.0f, depth);
    sprite->SetScale(scale);
    sprite->flatSpecular_ = kColourOff;
    regions_[index]->SetRect(left, 520, right, 600);
    widescreen::LeavePanel();
}

void CHudWeaponBar::AddWeapon(const char* model, const char* image) {
    // Slots 0 and 1 share the texture's first column, 2 and 3 the second; odd slots the lower
    // half. (Left unset for a count over 3, which SetUnit never reaches.)
    const int x = count_ >= 2 ? kWeaponWidth : 0;
    const int y = (count_ & 1) ? kWeaponHeight : 0;
    CopyImage(objects_[0], image, 0x80, 0x80, kWeaponWidth, kWeaponHeight, x, y, 0, 0);
    CUiMesh* sprite = AddSprite(model);
    if (sprite) {
        // Turned about z, then y, then z again: pi * -0.05, pi * -1/3 and pi * 0.75.
        Quaternion first, second, third;
        first.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, Pi() * (-0.05f /*00742804*/));
        second.SetAxisAngle(Vector3{0.0f, 1.0f, 0.0f}, Pi() * (-0.33333334f /*00742800*/));
        Quaternion both = ProductFirst(first, second);
        third.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, Pi() * 0.75f /*0073C524*/);
        sprite->localRotation_ = ProductSecond(both, third);
    } else {
        sprite = AddSprite(nullptr);
        // 80 and 120; WideScreen: both less the shift (addon points the FADDs at its two floats
        // written over).
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

// --- Quick items -----------------------------------------------------------------------------------------------

// WideScreen: addon enters the right panels before the window's constructor (10023477); the window's reads no
// panel offset, so here it is the body's first step.
CHudQuickItems::CHudQuickItems(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {
    widescreen::EnterRightPanel();   // addon's 10023477; 1002348E at the end
    const int shift = widescreen::EdgeShift();
    rect_ = {520 + shift, 500, 720 + shift, 600};   // WideScreen
    // The bottom row (the unit's items), then the top row (the player's), each from the right.
    for (int x = 670; x > 470; x -= 50) AddRegion()->SetRect(x, 550, x + 50, 600);
    for (int x = 670; x > 470; x -= 50) AddRegion()->SetRect(x, 500, x + 50, 550);
    ClearSceneObjects();
    shownCount_ = 0;
    selected_ = -1;
    for (int i = 0; i < 8; ++i) {
        regions_[i]->SetTip(0, 1, 0);
        regionItem_[i] = -1;
    }
    entries_.clear();
    unit_ = nullptr;
    const int ids[] = {7, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28};
    AddLabels(&labels_, ids);
    widescreen::LeavePanel();
}

void CHudQuickItems::AddItem(const char* model, const char* image) {
    if (shownCount_ >= kMaxQuickItems) return;
    const IconPlace at = IconAt(shownCount_, 0x80);
    CopyImage(objects_[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y, 0, 0);
    CUiMesh* sprite = AddSprite(model);
    if (sprite) {
        Quaternion turn;
        sprite->localRotation_ = *turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, Pi());
    } else {
        sprite = AddSprite(nullptr);
        const float shift = static_cast<float>(widescreen::EdgeShift());   // WideScreen
        sprite->SetRectangle(20.0f, 20.0f, 680.0f + shift, 560.0f, 720.0f + shift, 600.0f, 0, 0, kIconSize, kIconSize);
    }
    SetOffsets(sprite, at.x, at.y + kIconSize);
    MakePlain(sprite);
    ++shownCount_;
}

// --- Spell column ----------------------------------------------------------------------------------------------

CHudSpellColumn::CHudSpellColumn(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {
    const int shift = widescreen::EdgeShift();
    rect_ = {760 + shift, 190, 800 + shift, 510};   // WideScreen
    ClearSceneObjects();
    ClearRegions();
    count_ = 0;
    selected_ = -1;
    unit_ = nullptr;
    const int ids[] = {7, 18, 8, 9, 10, 11, 12, 13, 21, 22, 26, 27, 28};
    AddLabels(&labels_, ids);
}

void CHudSpellColumn::AddSpell(const char* model, const char* image) {
    widescreen::EnterRightPanel();   // addon's 1002344D; 10023464 at the end
    // What is there moves up a step; the new button takes the bottom place.
    for (int i = 0; i < std::ssize(regions_); ++i) {
        int top = (i - count_) * kStep;
        regions_[i]->SetRect(760, top + 470, 800, top + 510);
    }
    AddRegion()->SetRect(760, 470, 800, 510);
    for (int i = 0; i < std::ssize(sceneObjects_); ++i) {
        float y = ColumnBottom() - static_cast<float>((count_ - i) * kStep);
        Sprite(this, i)->Place(780.0f, y, 40.0f);
    }
    const IconPlace at = IconAt(count_, 0x80);
    CopyImage(objects_[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y, 0, 0);
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

// --- Action column ---------------------------------------------------------------------------------------------

CHudActionColumn::CHudActionColumn(CInterface3D* ui, CBattleScreen* owner) : CUiWindow(ui), owner_(owner) {
    // WideScreen (addon's handler): the panel offset from here on, the rectangle less the shift.
    widescreen::EnterLeftPanel();
    const int shift = widescreen::EdgeShift();
    rect_ = {-shift, 250, 40 - shift, 510};
    for (int y = 470; y > 230; y -= kStep) AddRegion()->SetRect(0, y, 40, y + kStep);
    ClearSceneObjects();
    actions_.clear();
    count_ = 0;
    selected_ = -1;
    for (int i = 0; i < 6; ++i) regions_[i]->SetTip(0, 1, 0);
    unit_ = nullptr;
    std::string text;
    TheTextManager()->GetString("infoitem_16", &text);
    labels_.push_back(text);
    widescreen::LeavePanel();        // addon's 10023430
}

void CHudActionColumn::AddAction(const char* model, const char* image, int action) {
    const IconPlace at = IconAt(count_, 0);
    CopyImage(objects_[0], image, kIconSize, kIconSize, kIconSize, kIconSize, at.x, at.y, 0, 0);
    CUiMesh* sprite = AddSprite(model);
    SetOffsets(sprite, at.x, at.y + kIconSize);
    Quaternion turn;
    sprite->localRotation_ = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, Pi());
    float y = ColumnBottom() - static_cast<float>(count_ * kStep);
    sprite->Place(20.0f - static_cast<float>(widescreen::EdgeShift()), y, 40.0f);   // WideScreen
    MakePlain(sprite);
    int place = std::ssize(actions_);
    actions_.resize(place + 1);
    actions_[place] = static_cast<std::uint32_t>(action);
    int index = count_;
    count_ = index + 1;
    CUiMesh* button = Sprite(this, index);
    button->SetScale(1.0f);
    button->flatSpecular_ = kColourOff;
    selected_ = index;
}
