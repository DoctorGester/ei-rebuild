// The battle HUD's party portraits, built when the party changes: HudPortraits::Rebuild
// drops the old sprites and regions and adds one portrait per party unit (and, in a multiplayer
// client, per player without a live unit) through AddPortrait; ShowFace copies
// a face picture into the portrait's quarter of a face texture. See ui.h and docs/notes/items_spells.md.
#include "ui.h"
#include "plugin_fixes.h"

#include "maths.h"
#include "net_classes.h"
#include "net_messages.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/rebirth.h"
#include <string>
#include "game_singletons.h"
#include "lib/containers.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <format>

namespace {

constexpr float kTexel = 0.00390625f;           // 1 / 256
constexpr float kSeventh = 0.14285715f;         // 1 / 7
constexpr float kThin = 0.2f;
constexpr float kFat = 0.8f;

// The face picture's sex letter: "m" for "unhuma", "f" for "unhufe", else `sex` stays as it is.
// EI_Plugin's Fixes: "f" when the fifth character of the name is 'f', else "m".
void SexOf(const std::string& figure, std::string& sex) {
    const bool fixes = pluginfixes::Enabled();
    if (fixes ? !pluginfixes::ModelIsFemale(figure) : figure == "unhuma")
        sex = "m";
    else if (fixes || figure == "unhufe")
        sex = "f";
}

} // namespace

void HudPortraits::ShowFace(int index, int state) {
    int u;
    int v;
    switch (index % 4) {
    case 0: u = 0; v = 0; break;
    case 1: u = 0x80; v = 0; break;
    case 2: u = 0; v = 0x80; break;
    case 3: u = 0x80; v = 0x80; break;
    default: u = state; v = state; break;   // a negative index (not reached)
    }
    std::string name = slots[index].faceName;
    if (state == 1)
        name += "a";
    else if (state == 2)
        name += "c";
    else if (state == 3)
        name += "b";
    const int texture = index / 4 + 1;
    if (!CopyImage(objects[texture], name, 0x80, 0x80, 0x80, 0x80, u, v))
        CopyImage(objects[texture], slots[index].faceName, 0x80, 0x80, 0x80, 0x80, u, v);
}

void HudPortraits::AddPortrait(const std::string& model, std::string_view picture, bool wide, bool hasPlayer) {
    // EI_Plugin's NPlayers: max(players, 7) portraits to a row instead of 6.
    const int perRow = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers)
                           ? (ei::rebirth::PluginNPlayers() < 7 ? 7 : ei::rebirth::PluginNPlayers())
                           : 6;
    const int column = count % perRow;
    int u = 0;
    int v = 0;
    switch (count % 4) {
    case 0: CreateTexture(0x100, 0x100, TextureKind::Blank1555); break;   // a new face texture every fourth portrait
    case 1: u = 0x80; break;
    case 2: v = 0x80; break;
    case 3: u = 0x80; v = 0x80; break;
    default: break;   // a negative count (not reached; the original leaves u, v uninitialised)
    }
    CopyImage(objects[std::ssize(objects) - 1], picture, 0x80, 0x80, 0x80, 0x80, u, v);
    slots.emplace_back();    // identity orientations, empty strings
    slots[count].hasPlayer = hasPlayer;
    UiMesh* face = AddSprite(model);

    int x;
    int width;
    if (IsMultiplayerClient()) {
        width = wide ? 0x38 : 0x28;
        int players = static_cast<int>(net::g_gameClient->players.size());
        x = 400 - static_cast<int>(static_cast<unsigned>(width + players * 40 - 40) >> 1);
        if (column > 0) {
            x += width + column * 40 - 40;
            width = 40;
        }
    } else {
        width = 0x38;
        x = column * 0x38 - lib::Count(owner->party) * 0x38 / 2 + 400;
    }
    const int right = x + width;

    if (face) {
        face->Place((x + right) * 0.5f, 548.0f, 7.0f);
        face->SetScale(width < 0x32 ? 0.22f : 0.3f);
        Quaternion turn;
        slots[count].restA = *turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, kPi);
        face->localRotation = slots[count].restA;
    } else {
        face = AddBlankSprite();
        face->SetRectangle(7.0f, 100.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 580.0f, 0, 0, 0x80,
                           0x80);
        slots[count].restA = kHudRestRotation;
    }
    HudPortraitSlot& slot = slots[count];
    slot.current = slot.restA;
    slots[count].wobbleKind = 0;
    if (hasPlayer) {
        face->tint[0] = 0.5f;
        face->tint[1] = 0.5f;
        face->tint[2] = 0.5f;
    }
    face->textureOffsetU = u * kTexel;
    face->textureOffsetV = (v + 0x80) * kTexel;
    face->texture = objects[std::ssize(objects) - 1];

    // The health and mana bars (hidden for a player), the frame and the hit flash.
    const float barScale = static_cast<float>(right - x - 6);
    UiMesh* health = AddBlankSprite();
    health->SetRectangle(7.0f, barScale, static_cast<float>(x + 3), 587.0f, static_cast<float>(right - 3), 592.0f, 9, 9,
                         0x49, 0x16);
    health->texture = objects[0];
    if (hasPlayer) health->visible = false;
    UiMesh* mana = AddBlankSprite();
    mana->SetRectangle(7.0f, barScale, static_cast<float>(x + 3), 592.0f, static_cast<float>(right - 3), 597.0f, 9, 0x23,
                       0x49, 0x30);
    mana->texture = objects[0];
    if (hasPlayer) mana->visible = false;
    UiMesh* frame = AddBlankSprite();
    frame->SetFrame(20.0f, 80.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 600.0f, 3.0f, 0x1B, 0x60, 0x5A,
                    0x65);
    frame->texture = objects[0];
    UiMesh* flash = AddBlankSprite();
    flash->SetRectangle(22.0f, 80.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 600.0f, 0, 0, 0x100,
                        0x100);
    flash->texture = nullptr;
    flash->flags |= FigureMeshFlag::Flat;
    flash->visible = false;

    AsUiMesh(sceneObjects[column * 5 + 4])->visible = false;     // the column's frame
    slots[column].restB = kHudRestRotation;
    ++count;
    AddRegion()->SetRect(x, 510, right, 600);
}

void HudPortraits::Rebuild(bool wide) {
    ClearSceneObjects();
    ClearRegions();
    while (std::ssize(objects) > 1) RemoveObjectAt(1);
    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);   // the marker
    count = 0;

    std::string build;     // "th", "me" or "fa"
    std::string sex;       // kept from one portrait to the next when the figure is neither
    std::string picture;
    std::string model;
    std::string tip;
    slots.clear();

    for (int i = 0; i < std::ssize(owner->party); ++i) {
        CUnit* unit = owner->party[i];
        const int skin = unit->GetPrototype()->skin;
        SexOf(unit->name, sex);
        const float figureBuild = unit->scale.y;
        build = "me";
        EI_NAN_AUDIT(figureBuild);
        if (figureBuild < kThin)
            build = "th";
        else if (figureBuild > kFat)
            build = "fa";
        const char* figure = unit->name.c_str();
        model = std::format("infa{}{}{}face", figure, build,
                     unit->GetPrototype()->hair + 1);
        picture = std::format("face{}{:02}", sex, skin);
        AddPortrait(model, picture, wide, false);

        if (owner->party[i]->flags & MapObjectFlag::Selected) {
            AsUiMesh(sceneObjects[i * 5 + 4])->visible = true;
            Quaternion turn;
            slots[i].restB = *turn.SetAxisAngle(Vector3{-1.0f, 0.0f, 0.0f}, kHudQuarterTurn * kSeventh);
        }
        slots[i].flash = 0.0f;
        slots[i].lastHitPoints = owner->party[i]->packedVitals.hitPoints;
        slots[i].faceName = picture;
        slots[i].faceState = 0;
        slots[i].modelPrefix = std::format("infa{}", unit->name);
        const int hair = unit->GetPrototype()->hair;
        slots[i].modelSuffix = std::format("{}face", hair + 1);
        slots[i].build = figureBuild;
        slots[i].playerId = -1;
        const int tipId = 0x37 + i;
        regions[i]->SetTipString(owner->UnitName(owner->party[i]), tipId < 0x3A && !IsMultiplayerClient() ? tipId : 0);
    }

    if (!IsMultiplayerClient()) return;
    for (auto node = net::g_gameClient->players.begin(); node != net::g_gameClient->players.end(); ++node) {
        auto* player = node->Get();
        EngineObject* unit = nullptr;
        if (player->id >= 0) unit = g_clientWorld.FindUnit(player->id);
        if (player->state == 0 && player->id >= 0 && unit) continue;

        UnitPrototypeRecord* prototype;
        {
            prototype = unitdb::PrototypeNamed(std::string(player->text));
            // EI_Plugin's Fixes: an unknown prototype becomes the first one that is no creature.
            if (!prototype && pluginfixes::Enabled())
                prototype = pluginfixes::FallbackPrototype();
        }
        const UnitModelRecord* figure = unitdb::ModelNamed(std::string(prototype->modelName));
        SexOf(figure->figureName, sex);
        build = "me";
        model = std::format("infa{}{}{}face", figure->figureName, build, prototype->hair + 1);
        picture = std::format("face{}{:02}", sex, prototype->skin);
        AddPortrait(model, picture, wide, true);
        HudPortraitSlot& slot = slots[count - 1];
        slot.flash = 0.0f;
        slot.lastHitPoints = 0;
        slots[count - 1].faceName = picture;
        slots[count - 1].faceState = 0;
        slots[count - 1].playerId = player->serial;
        regions[count - 1]->SetTipString(player->name.data());
    }
}

// EI_Plugin.asi patches both: NPlayers' row length in AddPortrait (the sites for more than 7 players are not
// reproduced) and Fixes in Rebuild (the party's and the players' sex, the prototype fallback).
