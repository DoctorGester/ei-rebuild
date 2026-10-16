// The battle HUD's party portraits, built when the party changes: CHudPortraits::Rebuild (006407E0)
// drops the old sprites and regions and adds one portrait per party unit (and, in a multiplayer
// client, per player without a live unit) through AddPortrait (00640060); ShowFace (00640670) copies
// a face picture into the portrait's quarter of a face texture. See ui.h and docs/notes/items_spells.md.
#include "ui.h"
#include "game_strings.h"
#include "plugin_fixes.h"

#include "maths.h"
#include "net_classes.h"
#include "net_messages.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "originals_world.h"

#include <cstdint>

namespace {

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }
float Half() { return GameFloat(0x0073B86C); }            // 0.5
float Texel() { return GameFloat(0x0073EEB8); }           // 1 / 256
float Seventh() { return GameFloat(0x0073CDCC); }         // 1 / 7
float Pi() { return GameFloat(0x007423F8); }
float Thin() { return GameFloat(0x0073BAD4); }            // 0.2
float Fat() { return GameFloat(0x0073CCA8); }             // 0.8
float WobbleAngle() { return GameFloat(0x007C8144); }     // a global angle (ui_hud_portraits.cpp)
const Quaternion& Identity() { return *reinterpret_cast<const Quaternion*>(0x007C8150); }
void* UnitPrototypes() { return reinterpret_cast<void*>(0x007AF364); }     // hash_map<string, UnitPrototypeRecord*>
void* FigurePrototypes() { return reinterpret_cast<void*>(0x007AF3A4); }   // hash_map<string, FigurePrototypeRecord*>
void* ClientWorld() { return reinterpret_cast<void*>(0x007AF5B8); }

// The slots' STLport vector (CHudPortraits +0x158): 0064AB20 appends a default slot (identity
// orientations, empty strings; plain RET), 0064AD40 erases [first, last) (RET 8, returns first).
__declspec(naked) void __fastcall SlotsPushDefault(HudPortraitSlot** /*vector*/) EI_ORIGINAL(0x0064AB20)
__declspec(naked) HudPortraitSlot* __fastcall SlotsErase(HudPortraitSlot** /*vector*/, void*, HudPortraitSlot* /*first*/,
                                                         HudPortraitSlot* /*last*/) EI_ORIGINAL(0x0064AD40)

// pair<const string, T*> of the name maps; find_or_insert returns the stored pair.
struct NamePair {
    stlp::string name;
    void* value;
};

// The inlined strcmp(a, b) == 0.
bool SameText(const char* a, const char* b) {
    for (;; ++a, ++b) {
        if (*a != *b) return false;
        if (*a == 0) return true;
    }
}

// The face picture's sex letter: "m" for "unhuma", "f" for "unhufe", else `sex` stays as it is.
// EI_Plugin's Fixes (006408E6, 00640D2F): "f" when the fifth character of the name is 'f', else "m".
void SexOf(const char* figure, mfc::CString& sex) {
    const bool fixes = pluginfixes::Enabled();
    if (fixes ? !pluginfixes::ModelIsFemale(figure) : SameText(figure, game::Text(0x007908C8)))   // "unhuma"
        sex = game::Text(0x00792EAC);                     // "m"
    else if (fixes || SameText(figure, game::Text(0x00792EA4)))     // "unhufe"
        sex = game::Text(0x0078BBD8);                     // "f"
}

// A client player record in the client's list (stlp list node: next, prev, value).
struct PlayerNode {
    PlayerNode* next;
    PlayerNode* prev;
    net::PlayerMessage* player;
};
PlayerNode* PlayersHead() { return static_cast<PlayerNode*>(net::GameClient()->players_); }

CUiMesh* Sprite(CEngineObject* object) { return reinterpret_cast<CUiMesh*>(object); }

} // namespace

__declspec(naked) void CHudPortraits::AddPortraitAt(const char*, const char*, int, int) EI_ORIGINAL(0x00640060)

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
    mfc::CString name(slots_[index].FaceName());
    if (state == 1)
        name += game::Text(0x00794594);   // "a"
    else if (state == 2)
        name += game::Text(0x0079254C);   // "c"
    else if (state == 3)
        name += game::Text(0x00792558);   // "b"
    const int texture = index / 4 + 1;
    if (static_cast<char>(CopyImage(objects_.data[texture], name, 0x80, 0x80, 0x80, 0x80, u, v, 0, 0)) == 0)
        CopyImage(objects_.data[texture], slots_[index].FaceName(), 0x80, 0x80, 0x80, 0x80, u, v, 0, 0);
}

void CHudPortraits::AddPortrait(const char* model, const char* picture, int wide, int hasPlayer) {
    const bool player = static_cast<char>(hasPlayer) != 0;
    // EI_Plugin's NPlayers (00640069): max(players, 7) portraits to a row instead of 6.
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
    CopyImage(objects_.data[objects_.size - 1], picture, 0x80, 0x80, 0x80, 0x80, u, v, 0, 0);
    SlotsPushDefault(&slots_);
    slots_[count_].hasPlayer_ = player;
    CUiMesh* face = AddSprite(model);

    int x;
    int width;
    if (IsMultiplayerClient()) {
        width = static_cast<char>(wide) != 0 ? 0x38 : 0x28;
        int players = 0;
        for (PlayerNode* node = PlayersHead()->next; node != PlayersHead(); node = node->next) ++players;
        x = 400 - static_cast<int>(static_cast<unsigned>(width + players * 40 - 40) >> 1);
        if (column > 0) {
            x += width + column * 40 - 40;
            width = 40;
        }
    } else {
        width = 0x38;
        x = column * 0x38 - owner_->party_.size * 0x38 / 2 + 400;
    }
    const int right = x + width;

    if (face) {
        face->Place(static_cast<float>(static_cast<double>(x + right) * Half()), 548.0f, 7.0f);
        face->SetScale(width < 0x32 ? 0.22f : 0.3f);
        Quaternion turn;
        slots_[count_].restA_ = *turn.SetAxisAngle(Vector3{1.0f, 0.0f, 0.0f}, Pi());
        face->localRotation_ = slots_[count_].restA_;
    } else {
        face = AddSprite(nullptr);
        face->SetRectangle(7.0f, 100.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 580.0f, 0, 0, 0x80,
                           0x80);
        slots_[count_].restA_ = Identity();
    }
    HudPortraitSlot& slot = slots_[count_];
    slot.current_ = slot.restA_;
    slots_[count_].wobbleKind_ = 0;
    if (player) {
        face->tint_[0] = 0.5f;
        face->tint_[1] = 0.5f;
        face->tint_[2] = 0.5f;
    }
    face->textureOffsetU_ = static_cast<float>(u) * Texel();
    face->textureOffsetV_ = static_cast<float>(v + 0x80) * Texel();
    face->texture_ = objects_.data[objects_.size - 1];

    // The health and mana bars (hidden for a player), the frame and the hit flash.
    const float barScale = static_cast<float>(right - x - 6);
    CUiMesh* health = AddSprite(nullptr);
    health->SetRectangle(7.0f, barScale, static_cast<float>(x + 3), 587.0f, static_cast<float>(right - 3), 592.0f, 9, 9,
                         0x49, 0x16);
    health->texture_ = objects_.data[0];
    if (player) health->visible_ = 0;
    CUiMesh* mana = AddSprite(nullptr);
    mana->SetRectangle(7.0f, barScale, static_cast<float>(x + 3), 592.0f, static_cast<float>(right - 3), 597.0f, 9, 0x23,
                       0x49, 0x30);
    mana->texture_ = objects_.data[0];
    if (player) mana->visible_ = 0;
    CUiMesh* frame = AddSprite(nullptr);
    frame->SetFrame(20.0f, 80.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 600.0f, 3.0f, 0x1B, 0x60, 0x5A,
                    0x65);
    frame->texture_ = objects_.data[0];
    CUiMesh* flash = AddSprite(nullptr);
    flash->SetRectangle(22.0f, 80.0f, static_cast<float>(x), 510.0f, static_cast<float>(right), 600.0f, 0, 0, 0x100,
                        0x100);
    flash->texture_ = nullptr;
    flash->flags_ |= 0x100;
    flash->visible_ = 0;

    Sprite(sceneObjects_.data[column * 5 + 4])->visible_ = 0;   // the column's frame
    slots_[column].restB_ = Identity();
    ++count_;
    AddRegion()->CallSetRect(x, 510, right, 600);
}

void CHudPortraits::Rebuild(int wide) {
    ClearSceneObjects();
    ClearRegions();
    while (objects_.size > 1) RemoveObjectAt(1);
    AddSprite(nullptr)->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);   // the marker
    count_ = 0;

    mfc::CString build;     // "th", "me" or "fa"
    mfc::CString sex;       // kept from one portrait to the next when the figure is neither
    mfc::CString picture;
    mfc::CString model;
    mfc::CString tip;
    SlotsErase(&slots_, nullptr, slots_, slotsEnd_);

    for (int i = 0; i < owner_->party_.size; ++i) {
        CUnit* unit = owner_->party_[i];
        const int skin = reinterpret_cast<UnitPrototypeRecord*>(unit->GetPrototype())->skin;
        SexOf(unit->name, sex);
        const float figureBuild = unit->scale.y;
        build = game::Text(0x007844A8);                              // "me"
        if (!(figureBuild >= Thin()))
            build = game::Text(0x007945AC);                          // "th"
        else if (figureBuild > Fat())
            build = game::Text(0x007945A8);                          // "fa"
        const char* figure = unit->name;
        model.Format(game::Text(0x007935AC), figure, static_cast<const char*>(build),   // "infa%s%s%dface"
                     reinterpret_cast<UnitPrototypeRecord*>(unit->GetPrototype())->hair + 1);
        picture.Format(game::Text(0x00792E88), static_cast<const char*>(sex), skin);     // "face%s%02d"
        AddPortraitAt(model, picture, wide, 0);

        if (owner_->party_[i]->flags_ & 0x10) {
            Sprite(sceneObjects_.data[i * 5 + 4])->visible_ = 1;
            Quaternion turn;
            slots_[i].restB_ = *turn.SetAxisAngle(Vector3{-1.0f, 0.0f, 0.0f}, WobbleAngle() * Seventh());
        }
        slots_[i].flash_ = 0.0f;
        slots_[i].lastHitPoints_ = owner_->party_[i]->packedVitals.hitPoints;
        slots_[i].FaceName() = picture;
        slots_[i].faceState_ = 0;
        slots_[i].ModelPrefix().Format(game::Text(0x007945A0), unit->name);   // "infa%s"
        const int hair = reinterpret_cast<UnitPrototypeRecord*>(unit->GetPrototype())->hair;
        slots_[i].ModelSuffix().Format(game::Text(0x00794598), hair + 1);             // "%dface"
        slots_[i].build_ = figureBuild;
        slots_[i].playerId_ = -1;
        owner_->UnitName(owner_->party_[i], &tip);
        const int tipId = 0x37 + i;
        regions_.data[i]->SetTipString(tip, 1, tipId < 0x3A && !IsMultiplayerClient() ? tipId : 0);
    }

    if (!IsMultiplayerClient()) return;
    for (PlayerNode* node = PlayersHead()->next; node != PlayersHead(); node = node->next) {
        net::PlayerMessage* player = node->player;
        CEngineObject* unit = nullptr;
        if (player->id_ >= 0) unit = WorldFindUnit(ClientWorld(), nullptr, player->id_);
        if (player->state_ == 0 && player->id_ >= 0 && unit) continue;

        UnitPrototypeRecord* prototype;
        {
            NamePair key{stlp::string(player->text_.start, player->text_.finish), nullptr};
            prototype = static_cast<UnitPrototypeRecord*>(
                static_cast<NamePair*>(NameMapInsertOriginal(UnitPrototypes(), nullptr, &key))->value);
            // EI_Plugin's Fixes (00640C97): an unknown prototype becomes the first one that is no creature.
            if (!prototype && pluginfixes::Enabled())
                prototype = reinterpret_cast<UnitPrototypeRecord*>(pluginfixes::FallbackPrototype());
        }
        FigurePrototypeRecord* figure;
        {
            NamePair key{stlp::string(static_cast<const char*>(prototype->modelName)), nullptr};
            figure = static_cast<FigurePrototypeRecord*>(
                static_cast<NamePair*>(FigureMapInsertOriginal(FigurePrototypes(), nullptr, &key))->value);
        }
        SexOf(figure->model, sex);
        build = game::Text(0x007844A8);                              // "me"
        model.Format(game::Text(0x007935AC), figure->model, static_cast<const char*>(build), prototype->hair + 1);
        picture.Format(game::Text(0x00792E88), static_cast<const char*>(sex), prototype->skin);
        AddPortraitAt(model, picture, wide, 1);
        HudPortraitSlot& slot = slots_[count_ - 1];
        slot.flash_ = 0.0f;
        slot.lastHitPoints_ = 0;
        slots_[count_ - 1].FaceName() = picture;
        slots_[count_ - 1].faceState_ = 0;
        slots_[count_ - 1].playerId_ = player->serial_;
        regions_.data[count_ - 1]->SetTipString(player->name_.start, 1, 0);
    }
}

EI_REPLACE_METHOD(0x00640670, CHudPortraits::ShowFace);
EI_REPLACE_METHOD(0x00640060, CHudPortraits::AddPortrait);
EI_REPLACE_METHOD(0x006407E0, CHudPortraits::Rebuild);
// EI_Plugin.asi patches both in a Rebirth launch: NPlayers' row length in AddPortrait (00640069; the sites
// for more than 7 players are not reproduced, the installer skips the function then) and Fixes in Rebuild
// (the party's and the players' sex at 006408E6 / 00640D2F, the prototype fallback at 00640C97).
