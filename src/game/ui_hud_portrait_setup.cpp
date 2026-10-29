// The battle HUD's party portraits, built when the party changes: CHudPortraits::Rebuild
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
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <cstdint>

namespace {

constexpr float kTexel = 0.00390625f;           // 1 / 256
constexpr float kSeventh = 0.14285715f;         // 1 / 7
constexpr float kThin = 0.2f;
constexpr float kFat = 0.8f;

// True when the two texts are equal (strcmp(a, b) == 0).
bool SameText(std::string_view a, std::string_view b) { return a == b; }

// The face picture's sex letter: "m" for "unhuma", "f" for "unhufe", else `sex` stays as it is.
// EI_Plugin's Fixes: "f" when the fifth character of the name is 'f', else "m".
void SexOf(const std::string& figure, std::string& sex) {
    const bool fixes = pluginfixes::Enabled();
    if (fixes ? !pluginfixes::ModelIsFemale(figure) : SameText(figure, "unhuma"))
        sex = "m";
    else if (fixes || SameText(figure, "unhufe"))
        sex = "f";
}

CUiMesh* Sprite(CEngineObject* object) { return AsUiMesh(object); }

} // namespace

void CHudPortraits::ShowFace(int index, int state) {
    int u;
    int v;
    switch (index % 4) {
    case 0: u = 0; v = 0; break;
    case 1: u = 0x80; v = 0; break;
    case 2: u = 0; v = 0x80; break;
    case 3: u = 0x80; v = 0x80; break;
    default: u = state; v = state; break;   // a negative index (not reached)
    }
    std::string name = slots_[index].faceName_;
    if (state == 1)
        name += "a";
    else if (state == 2)
        name += "c";
    else if (state == 3)
        name += "b";
    const int texture = index / 4 + 1;
    if (static_cast<char>(CopyImage(objects_[texture], name, 0x80, 0x80, 0x80, 0x80, u, v)) == 0)
        CopyImage(objects_[texture], slots_[index].faceName_, 0x80, 0x80, 0x80, 0x80, u, v);
}

void CHudPortraits::AddPortrait(const std::string& model, std::string_view picture, int wide, int hasPlayer) {
    const bool player = static_cast<char>(hasPlayer) != 0;
    // EI_Plugin's NPlayers: max(players, 7) portraits to a row instead of 6.
    const int perRow = ei::rebirth::Enabled(ei::rebirth::Patch::EiPluginNPlayers)
                           ? (ei::rebirth::PluginNPlayers() < 7 ? 7 : ei::rebirth::PluginNPlayers())
                           : 6;
    const int column = count_ % perRow;
    int u = 0;
    int v = 0;
    switch (count_ % 4) {
    case 0: CreateTexture(0x100, 0x100, 0x20); break;   // a new face texture every fourth portrait
    case 1: u = 0x80; break;
    case 2: v = 0x80; break;
    case 3: u = 0x80; v = 0x80; break;
    default: break;   // a negative count (not reached; the original leaves u, v uninitialised)
    }
    CopyImage(objects_[std::ssize(objects_) - 1], picture, 0x80, 0x80, 0x80, 0x80, u, v);
    slots_.emplace_back();   // identity orientations, empty strings
    slots_[count_].hasPlayer_ = player;
    CUiMesh* face = AddSprite(model);

    int x;
    int width;
    if (IsMultiplayerClient()) {
        width = static_cast<char>(wide) != 0 ? 0x38 : 0x28;
        int players = static_cast<int>(net::g_gameClient->players_.size());
        x = 400 - static_cast<int>(static_cast<unsigned>(width + players * 40 - 40) >> 1);
        if (column > 0) {
            x += width + column * 40 - 40;
            width = 40;
        }
    } else {
        width = 0x38;
        x = column * 0x38 - std::ssize(owner_->party_) * 0x38 / 2 + 400;
    }
    const int right = x + width;

    if (face) {
        face->Place(static_cast<float>(static_cast<double>(x + right) * 0.5f), 548.0f, 7.0f);
        face->SetScale(width < 0x32 ? 0.22f : 0.3f);
        Quaternion turn;
        slots_[count_].restA_ = *turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, kPi);
        face->localRotation_ = slots_[count_].restA_;
    } else {
        face = AddBlankSprite();
        face->SetRectangle(7.0f, 100.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 580.0f, 0, 0, 0x80,
                           0x80);
        slots_[count_].restA_ = kHudRestRotation;
    }
    HudPortraitSlot& slot = slots_[count_];
    slot.current_ = slot.restA_;
    slots_[count_].wobbleKind_ = 0;
    if (player) {
        face->tint_[0] = 0.5f;
        face->tint_[1] = 0.5f;
        face->tint_[2] = 0.5f;
    }
    face->textureOffsetU_ = static_cast<float>(u) * kTexel;
    face->textureOffsetV_ = static_cast<float>(v + 0x80) * kTexel;
    face->texture_ = objects_[std::ssize(objects_) - 1];

    // The health and mana bars (hidden for a player), the frame and the hit flash.
    const float barScale = static_cast<float>(right - x - 6);
    CUiMesh* health = AddBlankSprite();
    health->SetRectangle(7.0f, barScale, static_cast<float>(x + 3), 587.0f, static_cast<float>(right - 3), 592.0f, 9, 9,
                         0x49, 0x16);
    health->texture_ = objects_[0];
    if (player) health->visible_ = 0;
    CUiMesh* mana = AddBlankSprite();
    mana->SetRectangle(7.0f, barScale, static_cast<float>(x + 3), 592.0f, static_cast<float>(right - 3), 597.0f, 9, 0x23,
                       0x49, 0x30);
    mana->texture_ = objects_[0];
    if (player) mana->visible_ = 0;
    CUiMesh* frame = AddBlankSprite();
    frame->SetFrame(20.0f, 80.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 600.0f, 3.0f, 0x1B, 0x60, 0x5A,
                    0x65);
    frame->texture_ = objects_[0];
    CUiMesh* flash = AddBlankSprite();
    flash->SetRectangle(22.0f, 80.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 600.0f, 0, 0, 0x100,
                        0x100);
    flash->texture_ = nullptr;
    flash->flags_ |= 0x100;
    flash->visible_ = 0;

    Sprite(sceneObjects_[column * 5 + 4])->visible_ = 0;   // the column's frame
    slots_[column].restB_ = kHudRestRotation;
    ++count_;
    AddRegion()->SetRect(x, 510, right, 600);
}

void CHudPortraits::Rebuild(int wide) {
    ClearSceneObjects();
    ClearRegions();
    while (std::ssize(objects_) > 1) RemoveObjectAt(1);
    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);   // the marker
    count_ = 0;

    std::string build;     // "th", "me" or "fa"
    std::string sex;       // kept from one portrait to the next when the figure is neither
    std::string picture;
    std::string model;
    std::string tip;
    slots_.clear();

    for (int i = 0; i < std::ssize(owner_->party_); ++i) {
        CUnit* unit = owner_->party_[i];
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
        model = str::Format("infa%s%s%dface", figure, build,
                     unit->GetPrototype()->hair + 1);
        picture = str::Format("face%s%02d", sex, skin);
        AddPortrait(model, picture, wide, 0);

        if (owner_->party_[i]->flags_ & 0x10) {
            Sprite(sceneObjects_[i * 5 + 4])->visible_ = 1;
            Quaternion turn;
            slots_[i].restB_ = *turn.SetAxisAngle(Vector3{-1.0f, 0.0f, 0.0f}, kHudQuarterTurn * kSeventh);
        }
        slots_[i].flash_ = 0.0f;
        slots_[i].lastHitPoints_ = owner_->party_[i]->packedVitals.hitPoints;
        slots_[i].faceName_ = picture;
        slots_[i].faceState_ = 0;
        slots_[i].modelPrefix_ = str::Format("infa%s", unit->name);
        const int hair = unit->GetPrototype()->hair;
        slots_[i].modelSuffix_ = str::Format("%dface", hair + 1);
        slots_[i].build_ = figureBuild;
        slots_[i].playerId_ = -1;
        owner_->UnitName(owner_->party_[i], &tip);
        const int tipId = 0x37 + i;
        regions_[i]->SetTipString(tip, tipId < 0x3A && !IsMultiplayerClient() ? tipId : 0);
    }

    if (!IsMultiplayerClient()) return;
    for (auto node = net::g_gameClient->players_.begin(); node != net::g_gameClient->players_.end(); ++node) {
        auto* player = node->Get();
        CEngineObject* unit = nullptr;
        if (player->id_ >= 0) unit = g_clientWorld->FindUnit(player->id_);
        if (player->state_ == 0 && player->id_ >= 0 && unit) continue;

        UnitPrototypeRecord* prototype;
        {
            prototype = unitdb::PrototypeNamed(std::string(player->text_));
            // EI_Plugin's Fixes: an unknown prototype becomes the first one that is no creature.
            if (!prototype && pluginfixes::Enabled())
                prototype = pluginfixes::FallbackPrototype();
        }
        const UnitModelRecord* figure = unitdb::ModelNamed(std::string(prototype->modelName));
        SexOf(figure->figureName, sex);
        build = "me";
        model = str::Format("infa%s%s%dface", figure->figureName, build, prototype->hair + 1);
        picture = str::Format("face%s%02d", sex, prototype->skin);
        AddPortrait(model, picture, wide, 1);
        HudPortraitSlot& slot = slots_[count_ - 1];
        slot.flash_ = 0.0f;
        slot.lastHitPoints_ = 0;
        slots_[count_ - 1].faceName_ = picture;
        slots_[count_ - 1].faceState_ = 0;
        slots_[count_ - 1].playerId_ = player->serial_;
        regions_[count_ - 1]->SetTipString(player->name_.data());
    }
}

// EI_Plugin.asi patches both: NPlayers' row length in AddPortrait (the sites for more than 7 players are not
// reproduced) and Fixes in Rebuild (the party's and the players' sex, the prototype fallback).
