// The battle HUD's party portraits, per frame (HudPortraits::Update): each portrait's
// health and mana bars, the red flash after a hit, the face picture (normal, hit, low mana) and
// model (by the unit's build), the face model's easing towards its rest orientation with an
// occasional random nod, shake or turn, and the marker under the selected unit's portrait.
// See docs/notes/ui.md.
#include "ui.h"

#include "camera.h"
#include "game_singletons.h"
#include "maths.h"
#include "net_classes.h"
#include "net_messages.h"
#include "plugin_fixes.h"
#include "random.h"
#include "ui_float.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "ei/rebirth.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <format>
#include <string>

namespace {

bool GamePaused() { return g_clientWorld.paused != 0; }

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
// The scales the original multiplies random words with (about 2^-32, -0.3 * 2^-32 and 2^-31).
constexpr float kRandomScale32 = 2.3283064e-10f;
constexpr float kRandomDelayScale = -6.9849196e-11f;
constexpr float kRandomScale31 = 4.656613e-10f;
constexpr float kWobbleRate = 0.33333334f;    // wobbles per second
constexpr float kTwelfth = 0.083333336f;
constexpr float kWobbleSpan = 0.2f;
constexpr float kWobbleFrequency = 5.0f;
constexpr float kWobbleAmplitude = 0.1f;
constexpr float kMarkerRise = 60.0f;
constexpr float kMarkerDepth = 10.0f;

// A bar: its right edge (x of vertices 2 and 3) moves with the fraction, its texture follows.
void SetBar(EngineObject* object, float fraction, std::uint32_t v0, std::uint32_t v1) {
    UiMesh* bar = AsUiMesh(object);
    float u = ((fraction * kBarTexels + kBarStart) * kTexel) + kHalfTexel;
    float right = fraction - 0.5f;
    bar->vertices[0].x()[2] = right;
    bar->vertices[0].x()[3] = right;
    bar->texCoords[2].u = u;
    bar->texCoords[2].v = FloatFromBits(v0);
    bar->texCoords[3].u = u;
    bar->texCoords[3].v = FloatFromBits(v1);
}

// The original inlines three quaternion products, each with its own operand order; every
// component is rounded once. The wobble target: q1 (about y) times q2 (about x).
Quaternion TargetProduct(const Quaternion& q1, const Quaternion& q2) {
    Quaternion r;
    r.w = q1.w * q2.w - DotUnrounded<Sum3::ZYX>(q1.x, q1.y, q1.z, q2.x, q2.y, q2.z);
    r.x = ((q2.y * q1.z - q1.y * q2.z) + q1.w * q2.x) + q1.x * q2.w;
    r.y = ((q1.x * q2.z - q2.x * q1.z) + q1.w * q2.y) + q1.y * q2.w;
    r.z = ((q1.y * q2.x - q1.x * q2.y) + q2.w * q1.z) + q2.z * q1.w;
    return r;
}

// The face's rest orientation from the slot's two parts (a, b).
Quaternion RestProduct(const Quaternion& a, const Quaternion& b) {
    Quaternion r;
    r.w = a.w * b.w - DotUnrounded<Sum3::ZYX>(a.x, a.y, a.z, b.x, b.y, b.z);
    r.x = ((a.y * b.z - a.z * b.y) + a.x * b.w) + b.x * a.w;
    r.y = ((a.z * b.x - a.x * b.z) + a.y * b.w) + a.w * b.y;
    r.z = ((a.x * b.y - b.x * a.y) + a.z * b.w) + b.z * a.w;
    return r;
}

// The eased face orientation f with the wobble w applied.
Quaternion WobbleProduct(const Quaternion& f, const Quaternion& w) {
    Quaternion r;
    r.w = f.w * w.w - DotUnrounded<Sum3::ZYX>(f.x, f.y, f.z, w.x, w.y, w.z);
    r.x = ((w.z * f.y - f.z * w.y) + w.x * f.w) + f.x * w.w;
    r.y = ((f.z * w.x - w.z * f.x) + w.y * f.w) + f.y * w.w;
    r.z = ((w.y * f.x - w.x * f.y) + w.z * f.w) + f.z * w.w;
    return r;
}

float RandomTilt(std::uint32_t r) {
    return (r * kRandomScale31 - 1.0f) * kHudQuarterTurn * kTwelfth;
}

Quaternion AxisAngle(float x, float y, float z, float angle) {
    Vector3 axis = {x, y, z};
    Quaternion q;
    q.SetAxisAngle(axis, angle);
    return q;
}

} // namespace

void HudPortraits::Wobble(int index, int kind) {
    slots[index].wobbleTime = g_clientRandom.Next() * kRandomDelayScale;
    slots[index].wobbleKind = kind;
}

void HudPortraits::Update() {
    float dt = GamePaused() ? 0.0f : UiContainer::FrameTime();
    SceneMesh(0)->visible = false;    // the selection marker

    for (int i = 0; i < std::ssize(owner->party); ++i) {
        if (i >= count) continue;
        // Objects 5i+1 .. 5i+5, read afresh each time (replacing the face model may move the array).
        auto object = [this, i](int k) { return sceneObjects[i * 5 + k]; };
        HudPortraitSlot& slot = slots[i];
        Unit* unit = owner->party[i];

        // Health bar (object 5i+2) and mana bar (5i+3).
        int hitPoints = unit->packedVitals.hitPoints < 0 ? 0 : unit->packedVitals.hitPoints;
        float health = unit->packedVitals.maxHitPoints > 0
                            ? static_cast<float>(hitPoints) / static_cast<float>(unit->packedVitals.maxHitPoints)
                            : 0.0f;
        SetBar(object(2), health, kHealthV0, kHealthV1);
        std::uint16_t maxMana = unit->packedVitals.maxMana;
        float mana = 0.0f;
        if (maxMana != 0) {
            float current = static_cast<float>(unit->packedVitals.mana);   // exact (16 bits)
            float positive = 0.0f < current ? current : 0.0f;
            mana = positive / static_cast<int>(maxMana);
        }
        SetBar(object(3), mana, kManaV0, kManaV1);

        // A drop in hit points flashes the portrait (object 5i+5) and shows the hit picture.
        if (slot.lastHitPoints > hitPoints) {
            slot.flash = 1.0f;
            slot.lastHitPoints = hitPoints;
            slot.faceTimer = 0.0f;
            slot.faceMode = 2;
        } else if (slot.flash > 0.0f) {
            slot.flash = slot.flash - dt;
        }
        slot.flash = MaxKeepB(slot.flash, 0.0f);
        EngineObject* flash = object(5);
        if (slot.flash > 0.0f) {
            AsUiMesh(flash)->visible = true;
            int alpha = static_cast<int>(slot.flash * kFlashAlpha);
            std::uint32_t colour = static_cast<std::uint32_t>(alpha) << 24 | 0xFF0000u;
            AsUiMesh(flash)->flatColour = colour;
        } else {
            AsUiMesh(flash)->visible = false;
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
        EI_NAN_AUDIT(slot.build, build);
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
            ReplaceSceneObject(object(1), name);
        }

        // Now and then (a third of the time per second) a wobble starts after a random delay:
        // a turn towards a random target.
        Quaternion wobble = {1.0f, 0.0f, 0.0f, 0.0f};
        if (slot.wobbleKind == 0 && dt > 0.0f) {
            std::uint32_t roll = g_clientRandom.Next();
            if (dt * kWobbleRate > roll * kRandomScale32) {
                slot.wobbleTime = g_clientRandom.Next() * kRandomDelayScale;
                slot.wobbleKind = 3;
                std::uint32_t r1 = g_clientRandom.Next();
                std::uint32_t r2 = g_clientRandom.Next();
                Quaternion q1 = AxisAngle(0.0f, 1.0f, 0.0f, RandomTilt(r1));
                Quaternion q2 = AxisAngle(1.0f, 0.0f, 0.0f, RandomTilt(r2));
                slot.wobbleTarget = TargetProduct(q1, q2);
                slot.wobbleSpeed = g_clientRandom.Next() * kRandomScale31 + 1.0f;
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
                                           kHudQuarterTurn * std::sin(t * kWobbleFrequency) * kWobbleAmplitude);
                    else
                        slot.wobbleKind = 0;
                    break;
                case 2:   // shake
                    if ((kHudFullTurn * kWobbleSpan) > t)
                        wobble = AxisAngle(0.0f, 1.0f, 0.0f,
                                           kHudQuarterTurn * std::sin(t * kWobbleFrequency) * kWobbleAmplitude);
                    else
                        slot.wobbleKind = 0;
                    break;
                case 3:   // turn towards the target and back
                    if (kHudFullTurn / slot.wobbleSpeed > t) {
                        float w = 0.5f - std::cos(t * slot.wobbleSpeed) * 0.5f;
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
        Quaternion rest = RestProduct(slot.restA, slot.restB);
        Quaternion previous = slot.current;
        Quaternion face;
        EI_NAN_AUDIT(dt);
        if (dt < 0.5f)
            face.Slerp(dt + dt, previous, rest);
        else
            face = rest;
        slot.current = face;
        if (slot.wobbleKind != 0) face = WobbleProduct(face, wobble);
        AsUiMesh(object(1))->localRotation = face;

        // The selection marker (object 0) sits above the selected unit's portrait (object 5i+4).
        if (g_camera.targetId == unit->id) {
            SceneMesh(0)->visible = true;
            const UiMesh::Placement placed = AsUiMesh(object(4))->GetPlacement();
            SceneMesh(0)->Place(placed.x, placed.y - kMarkerRise, kMarkerDepth);
        }
    }
}

// --- Ui hud portrait setup ---------------------------------------------------------------------------

// The battle HUD's party portraits, built when the party changes: HudPortraits::Rebuild
// drops the old sprites and regions and adds one portrait per party unit (and, in a multiplayer
// client, per player without a live unit) through AddPortrait; ShowFace copies
// a face picture into the portrait's quarter of a face texture. See ui.h and docs/notes/items_spells.md.

namespace {

constexpr float kSeventh = 0.14285715f;         // 1 / 7

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
        Unit* unit = owner->party[i];
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
