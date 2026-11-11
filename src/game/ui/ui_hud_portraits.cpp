// The battle HUD's party portraits (HudPortraits, ui_hud.h): their mouse handlers (selecting a unit, or picking a
// player for a kick or ban), the portraits built for the party and the players, and per frame each portrait's health
// and mana bars, the red flash after a hit, the face picture (normal, hit, low mana) and model (by the unit's build),
// the face model's easing towards its rest orientation with an occasional random nod, shake or turn, and the marker
// under the selected unit's portrait. See docs/notes/ui.md, section 7.
#include "game/ui/ui_hud.h"
#include "engine/scene/camera.h"
#include "game/app/singletons.h"
#include "game/net/net_session.h"
#include "game/net/net_messages.h"
#include "game/players/faces.h"
#include "engine/core/random.h"
#include "game/ui/screen_layout.h"
#include "game/ui/ui.h"
#include "game/ui/ui_battle.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_float.h"
#include "game/ui/ui_hud_pieces.h"
#include "game/ui/ui_mesh.h"
#include "game/units/unit.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <format>
#include <iterator>
#include <list>
#include <string>

namespace {
constexpr int kOriginalRow = 6;   // portraits to a row for up to 6 players
constexpr int kWideRow = 7;       // the most players whose faces keep the full width
// Bar texture coordinate for a fill fraction: ((f * 64 + 9) / 256 + 1/512), stored.
constexpr float kBarTexels = 64.0f;
constexpr float kBarStart = 9.0f;
constexpr float kTexel = 0.00390625f;
constexpr float kHalfTexel = 0.001953125f;
constexpr std::uint32_t kHealthV0 = 0x3F768000;   // the bars' v coordinates
constexpr std::uint32_t kHealthV1 = 0x3F698000;
constexpr std::uint32_t kManaV0 = 0x3F5C8000;
constexpr std::uint32_t kManaV1 = 0x3F4F8000;
constexpr float kFlashAlpha = 200.0f;
constexpr float kHitPictureTime = 2.0f;       // face mode 2
constexpr float kMode1PictureTime = 4.0f;     // face mode 1
constexpr float kLowMana = 0.25f;
constexpr float kThin = 0.2f;
constexpr float kFat = 0.8f;
constexpr float kWobbleRate = 0.33333334f;    // wobbles per second
constexpr float kTwelfth = 0.083333336f;
constexpr float kWobbleSpan = 0.2f;
constexpr float kWobbleFrequency = 5.0f;
constexpr float kWobbleAmplitude = 0.1f;
constexpr float kMarkerRise = 60.0f;
constexpr float kMarkerDepth = 10.0f;

// A bar: its right edge (x of vertices 2 and 3) moves with the fraction, its texture follows.
void SetBar(UiMesh* object, float fraction, std::uint32_t v0, std::uint32_t v1) {
    UiMesh* bar = object;
    float u = ((fraction * kBarTexels + kBarStart) * kTexel) + kHalfTexel;
    float right = fraction - 0.5f;
    bar->vertices[0].x()[2] = right;
    bar->vertices[0].x()[3] = right;
    bar->texCoords[2].u = u;
    bar->texCoords[2].v = FloatFromBits(v0);
    bar->texCoords[3].u = u;
    bar->texCoords[3].v = FloatFromBits(v1);
}

float RandomTilt(std::uint32_t r) {
    return (ScaleWord(r, 2.0f) - 1.0f) * kHudQuarterTurn * kTwelfth;
}

Quaternion AxisAngle(float x, float y, float z, float angle) {
    Vector3 axis = {x, y, z};
    Quaternion q = Quaternion::AxisAngle(axis, angle);
    return q;
}

constexpr float kSeventh = 0.14285715f;         // 1 / 7

// The face picture's sex letter: "f" when the fifth character of the name is 'f', else "m" (EI_Plugin's Fixes, always
// on; the original's "m" for "unhuma", "f" for "unhufe", else none).
void SexOf(const std::string& figure, std::string& sex) { sex = pluginfixes::ModelIsFemale(figure) ? "f" : "m"; }
} // namespace

// --- Portraits -------------------------------------------------------------------------------------------------------

HudPortraits::HudPortraits(BattleScreen* newOwner) : owner(newOwner) {
    rect = Rect{250, 440, 550, 600};
    AddBlankSprite()->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);
}

// The portraits read the portrait mode of the battle screen they belong to (the game reads the interface's active
// screen: the battle screen while they get input).
void HudPortraits::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { LeftDown(event); }, [&] { LeftUp(event); }, [&] { MouseMove(event); });
}

void HudPortraits::LeftDown(UiEvent& event) {
    const int mode = owner->portraitMode;
    if (mode == 0) {
        // Any portrait region takes the event; only a party member's takes the mouse.
        if (const int region = RegionIndex(regions, HitRegion(event)); region >= 0 && region < std::ssize(owner->party))
            CaptureMouse();
        return;
    }
    const int region = RegionIndex(regions, RegionUnder(event.at));
    if (region < 0 || region >= count) return;
    // Picking a player for a server command: the player id of the slot, or of the session
    // player whose unit is the portrait's unit.
    int playerId = -1;
    if (slots[region].hasPlayer) {
        playerId = slots[region].playerId;
    } else {
        int unitId = owner->party[region]->id;
        // The session's player records: id the player's unit, serial its id.
        for (auto node = net::g_gameClient->players.begin(); node != net::g_gameClient->players.end(); ++node) {
            auto* player = node->Get();
            if (player->id == unitId) {
                playerId = player->serial;
                break;
            }
        }
    }
    char text[128];
    std::snprintf(text, sizeof(text), "%d", playerId);
    // ClientSession::SendCommand: kick / ban by serial.
    if (mode == 1)
        net::g_gameClient->SendCommand(net::CommandKind::KickBySerial, text);
    else if (mode == 2)
        net::g_gameClient->SendCommand(net::CommandKind::BanBySerial, text);
    owner->portraitMode = 0;
}

void HudPortraits::LeftUp(UiEvent& event) {
    if (owner->portraitMode != 0) return;
    const int region = RegionIndex(regions, PressedRegion(event));
    // Rebirth's FixSpellSelfCasting patches SelectUnit.
    if (region >= 0 && region < std::ssize(owner->party)) owner->SelectUnit(owner->party[region], {});
}

void HudPortraits::MouseMove(UiEvent& event) {
    if (owner->portraitMode == 0) {
        const int region = RegionIndex(regions, HitRegion(event));
        if (region >= 0 && region < std::ssize(owner->party)) owner->HoverUnit(owner->party[region], {});
        return;
    }
    const int region = RegionIndex(regions, RegionUnder(event.at));
    if (region >= 0 && region < count) g_cursor.SetShape(CursorShape::Default);
}

void HudPortraits::Wobble(int index, int kind) {
    slots[index].wobbleTime = g_clientRandom.Below(-0.3f);
    slots[index].wobbleKind = kind;
}

void HudPortraits::Update() {
    float dt = GamePaused() ? 0.0f : UiContainer::FrameTime();
    marker->visible = false;

    for (int i = 0; i < std::ssize(owner->party); ++i) {
        if (i >= count) continue;
        HudPortraitSlot& slot = slots[i];
        Unit* unit = owner->party[i];

        // Health bar and mana bar.
        int hitPoints = unit->packedVitals.hitPoints < 0 ? 0 : unit->packedVitals.hitPoints;
        float health = unit->packedVitals.maxHitPoints > 0
                            ? static_cast<float>(hitPoints) / static_cast<float>(unit->packedVitals.maxHitPoints)
                            : 0.0f;
        SetBar(slot.healthBar, health, kHealthV0, kHealthV1);
        std::uint16_t maxMana = unit->packedVitals.maxMana;
        float mana = 0.0f;
        if (maxMana != 0) {
            float current = static_cast<float>(unit->packedVitals.mana);   // exact (16 bits)
            float positive = 0.0f < current ? current : 0.0f;
            mana = positive / static_cast<int>(maxMana);
        }
        SetBar(slot.manaBar, mana, kManaV0, kManaV1);

        // A drop in hit points flashes the portrait and shows the hit picture.
        if (slot.lastHitPoints > hitPoints) {
            slot.flash = 1.0f;
            slot.lastHitPoints = hitPoints;
            slot.faceTimer = 0.0f;
            slot.faceMode = 2;
        } else if (slot.flash > 0.0f) {
            slot.flash -= dt;
        }
        slot.flash = MaxKeepB(slot.flash, 0.0f);
        UiMesh* flash = slot.hitFlash;
        if (slot.flash > 0.0f) {
            flash->visible = true;
            int alpha = static_cast<int>(slot.flash * kFlashAlpha);
            std::uint32_t colour = static_cast<std::uint32_t>(alpha) << 24 | 0xFF0000u;
            flash->flatColour = colour;
        } else {
            flash->visible = false;
        }

        // The face picture: a timed one (hit, mode 1) while it lasts, else low mana or normal.
        int state = 0;
        if (slot.faceMode != 0) {
            slot.faceTimer = dt + slot.faceTimer;
            if (slot.faceMode == 1) {
                if (slot.faceTimer > kMode1PictureTime) slot.faceMode = 0;
            } else if (slot.faceMode == 2) {
                if (slot.faceTimer > kHitPictureTime) slot.faceMode = 0;
            }
            state = slot.faceMode;
        }
        if (state == 0) {
            std::uint16_t maximum = unit->packedVitals.maxMana;
            if (maximum == 0 ||
                static_cast<float>(static_cast<int>(unit->packedVitals.mana)) / static_cast<float>(static_cast<int>(maximum)) <
                    kLowMana)   // integers: never NaN
                state = 3;
        }
        if (state != slot.faceState) {
            slot.faceState = state;
            ShowFace(i, state);
        }

        // The face model follows the unit's build (its scale's y): thin, medium or fat.
        float build = unit->scale.y;
        if (slot.build != build) {
            slot.build = build;
            std::string name("me");
            if (build < kThin)
                name = "th";
            else if (build > kFat)
                name = "fa";
            {
                const std::string& prefix = slot.modelPrefix;
                const std::string& suffix = slot.modelSuffix;
                std::string withPrefix = prefix + name;
                std::string full = withPrefix + suffix;
                name = full;
            }
            if (UiMesh* made = ReplaceSceneObject(slot.face, name)) slot.face = made;
        }

        // Now and then (a third of the time per second) a wobble starts after a random delay:
        // a turn towards a random target.
        Quaternion wobble = {1.0f, 0.0f, 0.0f, 0.0f};
        if (slot.wobbleKind == 0 && dt > 0.0f) {
            std::uint32_t roll = g_clientRandom.Next();
            if (dt * kWobbleRate > ScaleWord(roll, 1.0f)) {
                slot.wobbleTime = g_clientRandom.Below(-0.3f);
                slot.wobbleKind = 3;
                std::uint32_t r1 = g_clientRandom.Next();
                std::uint32_t r2 = g_clientRandom.Next();
                Quaternion q1 = AxisAngle(0.0f, 1.0f, 0.0f, RandomTilt(r1));
                Quaternion q2 = AxisAngle(1.0f, 0.0f, 0.0f, RandomTilt(r2));
                slot.wobbleTarget = q2 * q1;
                slot.wobbleSpeed = g_clientRandom.Below(2.0f) + 1.0f;
            }
        }
        if (slot.wobbleKind != 0) {
            slot.wobbleTime = dt + slot.wobbleTime;
            if (slot.wobbleTime >= 0.0f) {
                float t = slot.wobbleTime;
                switch (slot.wobbleKind) {
                case 1:   // nod
                    if ((kPi * kWobbleSpan) > t)
                        wobble = AxisAngle(1.0f, 0.0f, 0.0f,
                                           kHudQuarterTurn * lib::Sin(t * kWobbleFrequency) * kWobbleAmplitude);
                    else
                        slot.wobbleKind = 0;
                    break;
                case 2:   // shake
                    if ((kHudFullTurn * kWobbleSpan) > t)
                        wobble = AxisAngle(0.0f, 1.0f, 0.0f,
                                           kHudQuarterTurn * lib::Sin(t * kWobbleFrequency) * kWobbleAmplitude);
                    else
                        slot.wobbleKind = 0;
                    break;
                case 3:   // turn towards the target and back
                    if (kHudFullTurn / slot.wobbleSpeed > t) {
                        float w = 0.5f - lib::Cos(t * slot.wobbleSpeed) * 0.5f;
                        wobble.Slerp(w, kHudRestRotation, slot.wobbleTarget);
                    } else {
                        slot.wobbleKind = 0;
                    }
                    break;
                default:
                    break;
                }
            }
        }

        // The face eases towards its rest orientation (fully when the frame is long).
        Quaternion rest = slot.restA * slot.restB;
        Quaternion previous = slot.current;
        Quaternion face;
        if (dt < 0.5f)
            face.Slerp(dt + dt, previous, rest);
        else
            face = rest;
        slot.current = face;
        if (slot.wobbleKind != 0) face = face * wobble;
        slot.face->localRotation = face;

        // The marker sits above the camera target's portrait.
        if (g_camera.targetId == unit->id) {
            marker->visible = true;
            const UiMesh::Placement placed = slot.frame->GetPlacement();
            marker->Place(placed.x, placed.y - kMarkerRise, kMarkerDepth);
        }
    }
}

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
    // EI_Plugin's NPlayers, for a network game's player limit (ClientSession::PlayerLimit): max(players, 6) portraits
    // to a row; above 7 the faces are 20 wide at a pitch of 20 instead of 40 and the face model sits at depth 9.1875
    // instead of 7 (EI_Plugin's six extra sites).
    const int players = IsMultiplayerClient() ? net::g_gameClient->PlayerLimit() : 0;
    const int perRow = players > kOriginalRow ? players : kOriginalRow;
    const bool crowded = players > kWideRow;
    const int pitch = crowded ? 20 : 40;
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
        width = crowded ? pitch : wide ? 0x38 : 0x28;
        const int present = static_cast<int>(net::g_gameClient->players.size()) + owner->companionPortraits;
        x = 400 - static_cast<int>(static_cast<unsigned>(width + present * pitch - pitch) >> 1);
        if (column > 0) {
            x += width + column * pitch - pitch;
            width = pitch;
        }
    } else {
        width = 0x38;
        x = column * 0x38 - lib::Count(owner->party) * 0x38 / 2 + 400;
    }
    const int right = x + width;

    if (face) {
        face->Place((x + right) * 0.5f, 548.0f, crowded ? 9.1875f : 7.0f);
        face->SetScale(width < 0x32 ? 0.22f : 0.3f);
        slots[count].restA = Quaternion::AxisAngle(Vector3{1.0f, 0.0f, 0.0f}, kPi);
        face->localRotation = slots[count].restA;
    } else {
        face = AddBlankSprite();
        face->SetRectangle(7.0f, kSpriteScale, static_cast<float>(x), 510.0f, static_cast<float>(right), 580.0f, 0, 0, 0x80,
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
    slot.face = face;
    slot.healthBar = health;
    slot.manaBar = mana;
    slot.frame = frame;
    slot.hitFlash = flash;

    slots[column].frame->visible = false;     // the column's frame
    slots[column].restB = kHudRestRotation;
    ++count;
    AddRegion()->SetRect(x, 510, right, 600);
}

void HudPortraits::Rebuild(bool wide) {
    ClearSceneObjects();
    ClearRegions();
    while (std::ssize(objects) > 1) RemoveObjectAt(1);
    marker = AddBlankSprite();
    marker->SetRectangle(10.0f, 80.0f, 0.0f, 0.0f, 20.0f, 20.0f, 0x22, 0x96, 0x36, 0xAA);
    count = 0;

    std::string build;     // "th", "me" or "fa"
    std::string sex;       // kept from one portrait to the next when the figure is neither
    std::string picture;
    std::string model;
    std::string tip;
    slots.clear();

    for (int i = 0; i < std::ssize(owner->party); ++i) {
        Unit* unit = owner->party[i];
        const int skin = unit->GetPrototype()->skin;
        SexOf(unit->name, sex);
        const float figureBuild = unit->scale.y;
        build = "me";
        if (figureBuild < kThin)
            build = "th";
        else if (figureBuild > kFat)
            build = "fa";
        const std::string& figure = unit->name;
        model = std::format("infa{}{}{}face", figure, build,
                     unit->GetPrototype()->hair + 1);
        picture = std::format("face{}{:02}", sex, skin);
        AddPortrait(model, picture, wide, false);

        if (owner->party[i]->flags & MapObjectFlag::Selected) {
            slots[i].frame->visible = true;
            slots[i].restB = Quaternion::AxisAngle(Vector3{-1.0f, 0.0f, 0.0f}, kHudQuarterTurn * kSeventh);
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
        if (player->state == net::PlayerStatus::InZone && player->id >= 0 && unit) continue;

        UnitPrototypeRecord* prototype;
        {
            prototype = g_unitDatabase.prototypes.FindByName(std::string(player->text));
            // EI_Plugin's Fixes (always on): an unknown prototype becomes the first one that is no creature.
            if (!prototype) prototype = pluginfixes::FallbackPrototype();
        }
        const UnitModelRecord* figure = g_unitDatabase.models.FindByName(std::string(prototype->modelName));
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
