// The battle HUD's top panel (the message log's frame): its setup and the lines it adds
// to page 0 for picked-up items and money. The page display and layout are
// in ui_hud_panels.cpp. See ui.h and docs/notes/items_spells.md.
#include "ui.h"
#include "ui_float.h"

#include "text_manager.h"
#include "maths.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"
#include "world.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <concepts>

void HudTopPanel::Setup(int newPage, bool shown) {
    rect.left = 0xB4;
    offset = shown ? 0x5F : 0;
    rect.top = 0;
    slide = shown ? 1.0f : 0.0f;
    rect.right = 0x26C;
    rect.bottom = 100;
    sliding = true;
    travel = 0x5F;
    slideDirection = shown ? 1.0f : -1.0f;

    UiRegion* region = AddRegion();
    region->SetRect(0x181, 0, 0x19F, 0x14);   // the slide button
    region->SetTip(0x27D8);
    region = AddRegion();
    region->SetRect(0xB9, 0, 0xD7, 0x14);     // page 0
    region->SetTip(0x27D9, 0x32);
    region = AddRegion();
    region->SetRect(0xD7, 0, 0xF5, 0x14);     // page 1
    region->SetTip(0x27DA, 0x33);
    AddRegion()->SetRect(0xB4, 0, 0x26C, 100);

    UiMesh* back = AddBlankSprite();
    back->SetRectangle(20.0f, 100.0f, 180.0f, 20.0f, 620.0f, 95.0f, 0, 0, 0x100, 0x100);
    back->texture = nullptr;
    back->flags |= FigureMeshFlag::Flat;
    back->flatColour = 0x50000000;
    UiMesh* bar = AddBlankSprite();
    bar->SetRectangle(20.0f, 100.0f, 180.0f, 0.0f, 620.0f, 20.0f, 0, 0, 0x100, 0x100);
    bar->texture = nullptr;
    bar->flags |= FigureMeshFlag::Flat;
    bar->flatColour = 0xB4000000;

    UiMesh* arrow = AddSprite("in25arrow");
    arrow->textureOffsetU = 0.22265625f;
    arrow->textureOffsetV = 0.140625f;
    Quaternion turn;
    arrow->localRotation = *turn.SetAxisAngle(Vector3{0.0f, 0.0f, 1.0f}, kPi);
    arrow->Place(400.0f, -52.0f, FloatFromBits(0x4101B287));
    arrow->SetScale(0.8f);

    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 185.0f, 0.0f, 215.0f, 20.0f, 2, 0x80, 0x20, 0x94);
    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 215.0f, 0.0f, 245.0f, 20.0f, 2, 0x96, 0x20, 0xAA);
    AddBlankSprite()->SetFrame(18.0f, 80.0f, 180.0f, -5.0f, 620.0f, 100.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);

    page = newPage;
    ShowPage();
    Layout();
}

void HudTopPanel::AddPickedUp(Item* item, bool crippled) {
    std::string name;
    std::string kind;
    name = g_clientWorld.texts->FormatItemName(item);
    const char* key = crippled ? "combat_crippled" : "combat_picked_up";
    kind = g_clientWorld.texts->GetString(key);
    std::string line = kind;
    line += " ";
    line += name;
    line += "\n";   // "\n"
    name = line;
    pageTexts[0] += name;
    if (page == 0) owner->messageLog->AddText(name);
}

void HudTopPanel::AddMoney(int money) {
    if (money <= 0) return;
    std::string text;
    std::string picked;
    std::string format;
    picked = g_clientWorld.texts->GetString("combat_picked_up");
    format = g_clientWorld.texts->GetString("format_money");
    text = str::Format(format, money);
    std::string line = picked;
    line += " ";
    line += text;
    line += "\n";   // "\n"
    text = line;
    pageTexts[0] += text;
    if (page == 0) owner->messageLog->AddText(text);
}
