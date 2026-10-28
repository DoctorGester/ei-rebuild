// The battle HUD's top panel (the message log's frame): its setup and the lines it adds
// to page 0 for picked-up items and money. The page display and layout are
// in ui_hud_panels.cpp. See ui.h and docs/notes/items_spells.md.
#include "ui.h"

#include "item_db_text.h"
#include "maths.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"

#include <concepts>

namespace {

constexpr float kPi = 3.1415927f;

float FloatFromBits(std::uint32_t bits) {
    float f;
    f = std::bit_cast<float>(bits);
    return f;
}
// A float argument would convert to an integer first: callers pass raw bits only.
template <std::floating_point T> float FloatFromBits(T) = delete;


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
    region->SetRect(0x181, 0, 0x19F, 0x14);   // the slide button
    region->SetTip(0x27D8, 1, 0);
    region = AddRegion();
    region->SetRect(0xB9, 0, 0xD7, 0x14);     // page 0
    region->SetTip(0x27D9, 1, 0x32);
    region = AddRegion();
    region->SetRect(0xD7, 0, 0xF5, 0x14);     // page 1
    region->SetTip(0x27DA, 1, 0x33);
    AddRegion()->SetRect(0xB4, 0, 0x26C, 100);

    CUiMesh* back = AddBlankSprite();
    back->SetRectangle(20.0f, 100.0f, 180.0f, 20.0f, 620.0f, 95.0f, 0, 0, 0x100, 0x100);
    back->texture_ = nullptr;
    back->flags_ |= 0x100;
    back->flatColour_ = 0x50000000;
    CUiMesh* bar = AddBlankSprite();
    bar->SetRectangle(20.0f, 100.0f, 180.0f, 0.0f, 620.0f, 20.0f, 0, 0, 0x100, 0x100);
    bar->texture_ = nullptr;
    bar->flags_ |= 0x100;
    bar->flatColour_ = 0xB4000000;

    CUiMesh* arrow = AddSprite("in25arrow");
    arrow->textureOffsetU_ = 0.22265625f;
    arrow->textureOffsetV_ = 0.140625f;
    Quaternion turn;
    arrow->localRotation_ = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi);
    arrow->Place(400.0f, -52.0f, FloatFromBits(0x4101B287));
    arrow->SetScale(0.8f);

    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 185.0f, 0.0f, 215.0f, 20.0f, 2, 0x80, 0x20, 0x94);
    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 215.0f, 0.0f, 245.0f, 20.0f, 2, 0x96, 0x20, 0xAA);
    AddBlankSprite()->SetFrame(18.0f, 80.0f, 180.0f, -5.0f, 620.0f, 100.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);

    page_ = page;
    ShowPage();
    Layout();
}

void CHudTopPanel::AddPickedUp(CItem* item, int crippled) {
    std::string name;
    std::string kind;
    g_clientWorld->texts_->FormatItemName(item, &name);
    const char* key = static_cast<char>(crippled) ? "combat_crippled"
                                                  : "combat_picked_up";
    g_clientWorld->texts_->GetString(key, &kind);
    std::string line = kind;
    line += " ";
    line += name;
    line += "\n";   // "\n"
    name = line;
    pageTexts_[0] += name;
    if (page_ == 0) owner_->MessageLog()->AddText(name);
}

void CHudTopPanel::AddMoney(int money) {
    if (money <= 0) return;
    std::string text;
    std::string picked;
    std::string format;
    g_clientWorld->texts_->GetString("combat_picked_up", &picked);
    g_clientWorld->texts_->GetString("format_money", &format);
    text = str::Format(format.c_str(), money);
    std::string line = picked;
    line += " ";
    line += text;
    line += "\n";   // "\n"
    text = line;
    pageTexts_[0] += text;
    if (page_ == 0) owner_->MessageLog()->AddText(text);
}
