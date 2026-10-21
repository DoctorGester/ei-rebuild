// The battle HUD's top panel (the message log's frame): its setup (00649AF0) and the lines it adds
// to page 0 for picked-up items (0064A4E0) and money (0064A630). The page display and layout are
// in ui_hud_panels.cpp. See ui.h and docs/notes/items_spells.md.
#include "ui.h"
#include "game_strings.h"

#include "item_db_text.h"
#include "maths.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_ui.h"

#include <concepts>
#include <cstdint>
#include <cstring>

namespace {

float Pi() { return *reinterpret_cast<const volatile float*>(0x007423F8); }

float FloatFromBits(std::uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatFromBits(T) = delete;

// The message log, the battle screen's child window 9.
void* MessageLog(CBattleScreen* screen) { return screen->children_.data[9]; }

} // namespace

void CHudTopPanel::Build(int page, int shown) {
    const bool in = static_cast<char>(shown) != 0;
    rect_.left = 0xB4;
    offset_ = in ? 0x5F : 0;
    rect_.top = 0;
    slide_ = in ? 1.0f : 0.0f;
    rect_.right = 0x26C;
    rect_.bottom = 100;
    sliding_ = 1;
    travel_ = 0x5F;
    slideDirection_ = in ? 1.0f : -1.0f;

    CUiRegion* region = AddRegion();
    region->CallSetRect(0x181, 0, 0x19F, 0x14);   // the slide button
    region->SetTip(0x27D8, 1, 0);
    region = AddRegion();
    region->CallSetRect(0xB9, 0, 0xD7, 0x14);     // page 0
    region->SetTip(0x27D9, 1, 0x32);
    region = AddRegion();
    region->CallSetRect(0xD7, 0, 0xF5, 0x14);     // page 1
    region->SetTip(0x27DA, 1, 0x33);
    AddRegion()->CallSetRect(0xB4, 0, 0x26C, 100);

    CUiMesh* back = AddSprite(nullptr);
    back->SetRectangle(20.0f, 100.0f, 180.0f, 20.0f, 620.0f, 95.0f, 0, 0, 0x100, 0x100);
    back->texture_ = nullptr;
    back->flags_ |= 0x100;
    back->flatColour_ = 0x50000000;
    CUiMesh* bar = AddSprite(nullptr);
    bar->SetRectangle(20.0f, 100.0f, 180.0f, 0.0f, 620.0f, 20.0f, 0, 0, 0x100, 0x100);
    bar->texture_ = nullptr;
    bar->flags_ |= 0x100;
    bar->flatColour_ = 0xB4000000;

    CUiMesh* arrow = AddSprite("in25arrow");
    arrow->textureOffsetU_ = 0.22265625f;
    arrow->textureOffsetV_ = 0.140625f;
    Quaternion turn;
    arrow->localRotation_ = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, Pi());
    arrow->Place(400.0f, -52.0f, FloatFromBits(0x4101B287));
    arrow->SetScale(0.8f);

    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 185.0f, 0.0f, 215.0f, 20.0f, 2, 0x80, 0x20, 0x94);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 215.0f, 0.0f, 245.0f, 20.0f, 2, 0x96, 0x20, 0xAA);
    AddSprite(nullptr)->SetFrame(18.0f, 80.0f, 180.0f, -5.0f, 620.0f, 100.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);

    page_ = page;
    ShowPage();
    Layout();
}
EI_REPLACE_METHOD(0x00649AF0, CHudTopPanel::Build);

void CHudTopPanel::AddPickedUp(CItem* item, int crippled) {
    mfc::CString name;
    mfc::CString kind;
    GameTexts()->FormatItemName(item, &name);
    const char* key = static_cast<char>(crippled) ? "combat_crippled"
                                                  : "combat_picked_up";
    GameTexts()->GetString(key, &kind);
    mfc::CString line(kind);
    line += " ";
    line += name;
    line += "\n";   // "\n"
    name = line;
    PageText(0) += name;
    if (page_ == 0) static_cast<CUiScrollText*>(MessageLog(owner_))->AddText(name);
}
EI_REPLACE_METHOD(0x0064A4E0, CHudTopPanel::AddPickedUp);

void CHudTopPanel::AddMoney(int money) {
    if (money <= 0) return;
    mfc::CString text;
    mfc::CString picked;
    mfc::CString format;
    GameTexts()->GetString("combat_picked_up", &picked);
    GameTexts()->GetString("format_money", &format);
    text.Format(format, money);
    mfc::CString line(picked);
    line += " ";
    line += text;
    line += "\n";   // "\n"
    text = line;
    PageText(0) += text;
    if (page_ == 0) static_cast<CUiScrollText*>(MessageLog(owner_))->AddText(text);
}
EI_REPLACE_METHOD(0x0064A630, CHudTopPanel::AddMoney);
