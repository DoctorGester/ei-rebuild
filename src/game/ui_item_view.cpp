// The item screens' model view and the description panel's drawing. See
// ui_item_info.h and docs/notes/ui.md ("Item descriptions").
#include "app.h"
#include "core_refcount.h"
#include "texture.h"
#include "ui_item_info.h"
#include "plugin_fixes.h"

#include "surface.h"
#include "item.h"
#include "random.h"
#include "spell.h"
#include "unit_db.h"

#include "ei/rebirth.h"
#include "lib/vecmath.h"
#include "maths.h"
#include "lib/nan_audit.h"
#include "lib/crt.h"
#include "platform/clock.h"
#include "render/render.h"
#include "render/render_state.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"
#include "world.h"

#include <cmath>
#include <bit>
#include <format>

namespace {

constexpr float kFullTurn = /*007C7D20*/ 6.2831855f;   // a static initialiser: pi + pi


int ScaledFtol(float value, float scale) { return static_cast<int>(value * scale); }

std::uint32_t PackColour(const float* rgb) {
    std::uint32_t r = static_cast<std::uint32_t>(ScaledFtol(rgb[0], 255.0f));
    std::uint32_t g = static_cast<std::uint32_t>(ScaledFtol(rgb[1], 255.0f));
    std::uint32_t b = static_cast<std::uint32_t>(ScaledFtol(rgb[2], 255.0f));
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// The cached object for `name` (its use time refreshed), or a new one put in the cache, which then drops its
// least recently used entries beyond its limit. Null when it cannot be made. `make` is the object's maker.
template <typename Object>
Object* CachedObject(ItemObjectCache<Object>* cache, std::string_view name, Object* (*make)(std::string_view)) {
    auto node = cache->map.find(std::string(name));
    if (node != cache->map.end()) {
        node->second.lastUse = platform::Milliseconds();
        return node->second.object;
    }
    Object* object = make(name);
    if (!object) return nullptr;
    std::uint32_t time = platform::Milliseconds();
    {
        std::string key(name);
        ItemCacheEntry<Object>& value = cache->map[key];
        value.object = object;
        value.lastUse = time;
    }
    TrimItemCache(cache);
    return object;
}

// A new turning state with a random glow phase: word * 2 pi (as float) * 2^-32 in that order, stored as
// float.
float RandomPhase(std::uint32_t random) {
    return random * kFullTurn * 2.3283064e-10f;
}

// The spell glow: the phase advances by dt * 5 and wraps at 2 pi (_CIfmod); its sine scales the school's
// colour (kSchoolGlow) into 0xFFrrggbb.
// The spell schools' glow colours (red, green, blue and an unused 1 each).
constexpr float kSchoolGlow[8][4] = /*00740358*/ {
    {1.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 0.0f, 1.0f},
    {1.0f, 1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f, 1.0f}};

std::uint32_t Glow(float dt, ItemTurn* turn, const float* colour) {
    const float advanced = dt * 5.0f + turn->glow;
    const float pi = 3.1415927f;
    const float phase = std::fmod(advanced, pi + pi);
    turn->glow = phase;
    const float strength = (std::sin(phase) + 1.0f) * 0.15f;
    const auto channel = [&](int i) { return static_cast<std::uint32_t>(strength * colour[i] * 255.0f); };
    const std::uint32_t r = channel(0);
    const std::uint32_t g = channel(1);
    const std::uint32_t b = channel(2);
    return (((r | 0xFFFFFF00u) << 8) | g) << 8 | b;
}

// The turn: on the hovered item the speed grows by 8 per second; elsewhere it brakes towards the
// next full turn (4 * (2 pi - angle)), or stops below 0.1. At most 5. The angle advances by
// dt * speed and wraps at 2 pi.
void Turn(ItemTurn* turn, float dt, bool hovered) {
    EI_NAN_AUDIT(turn->speed);
    if (hovered) {
        turn->speed = 8.0f * dt + turn->speed;
    } else if (turn->speed < 0.1f) {
        turn->angle = 0.0f;
        turn->speed = 0.0f;
    } else {
        float pi = 3.1415927f;
        turn->speed = (pi + pi - turn->angle) * 4.0f;
    }
    if (turn->speed > 5.0f) turn->speed = 5.0f;
    // Wraps while above 2 pi, not when equal or unordered.
    float angle = dt * turn->speed + turn->angle;
    turn->angle = angle;
    while (angle > kFullTurn) {
        angle = turn->angle - kFullTurn;
        turn->angle = angle;
    }
}

Quaternion HalfTurn(float ax, float ay, float az) {
    const Vector3 axis = {ax, ay, az};
    Quaternion q;
    q.SetAxisAngle(axis, 3.1415927f);
    return q;
}

// The two products the original game inlines (a * b), each with its own order of the w sum.
Quaternion ProductA(const Quaternion& a, const Quaternion& b) {   // weapons
    Quaternion r;
    r.w = a.w * b.w - DotUnrounded<Sum3::XYZ>(a.x, a.y, a.z, b.x, b.y, b.z);
    r.x = ((b.y * a.z - a.y * b.z) + a.w * b.x) + a.x * b.w;
    r.y = ((a.x * b.z - b.x * a.z) + a.w * b.y) + a.y * b.w;
    r.z = ((a.y * b.x - a.x * b.y) + b.w * a.z) + a.w * b.z;
    return r;
}
Quaternion ProductB(const Quaternion& a, const Quaternion& b) {   // other items
    Quaternion r;
    r.w = a.w * b.w - DotUnrounded<Sum3::ZYX>(a.x, a.y, a.z, b.x, b.y, b.z);
    r.x = ((b.y * a.z - b.z * a.y) + a.w * b.x) + b.w * a.x;
    r.y = ((b.z * a.x - b.x * a.z) + a.w * b.y) + b.w * a.y;
    r.z = ((b.x * a.y - b.y * a.x) + a.z * b.w) + a.w * b.z;
    return r;
}

bool IsRecordLoot(const Item* item, LootRecordType recordType) {
    if (item->Kind() != ItemKind::Loot) return false;
    const auto* loot = AsLoot(item);
    return loot->kind == LootKind::Record && loot->recordType == recordType;
}

bool LiesFlat(Item* item) {
    if (item->Kind() == ItemKind::Scroll) return true;
    return IsRecordLoot(item, LootRecordType::Spell) || IsRecordLoot(item, LootRecordType::Modifier);
}

void SetShade(UiMesh* sprite, float grey) {
    sprite->tint[0] = grey;
    sprite->tint[1] = grey;
    sprite->tint[2] = grey;
}

constexpr std::uint32_t kPnt3 = kMmpFormatPnt3;

// An item picture's file as the renderer takes it: `width` x `height` pixels right after the header (the extra block
// not skipped, as the original read them), premultiplied 32-bit or "PNT3" packed. Valid while `file` and the file are.
render::TextureImage PictureOf(const std::string& file, const MmpHeader* image, std::uint32_t fileBytes, int width,
                               int height) {
    const std::uint32_t header = static_cast<std::uint32_t>(sizeof(MmpHeader));
    return render::TextureImage{file.c_str(), image + 1, fileBytes > header ? fileBytes - header : 0, width, height,
                                image->format == kPnt3 ? render::ImageLayout::Pnt3 : render::ImageLayout::Argb};
}

} // namespace

void ItemScreen::DrawItem(SceneRenderer* pass, const ItemCell& cell) {
    Item* item = cell.item;
    UiMesh* model = CachedObject(&models, item->PictureName(), ItemModel);
    CTexture* texture = CachedObject(&textures, item->FigureName(), ItemFigureTexture);
    if (!model || !texture) return;

    model->texture = texture;
    model->Place(cell.x, cell.y, cell.depth);
    model->SetScale(0.48157462f * cell.size * cell.depth * 0.0009375f);
    model->material = blackMaterial;

    if (ItemTurnOf(item) == nullptr) {
        auto* turn = new ItemTurn();
        turn->glow = RandomPhase(g_clientRandom.Next());
        item->link.Assign(turn);
    }

    if (cell.mode == 9) {
        // Greyed: half bright, lying still, scaled to fit the cell by the model's larger extent.
        SetShade(model, 0.5f);
        model->localRotation = LiesFlat(item) ? HalfTurn(0.0f, 0.0f, 1.0f) : HalfTurn(1.0f, 0.0f, 0.0f);
        // The larger of the box's x and y extents: y when x is below it (or NaN), selected by their bits.
        const Vector3& extents = model->box.bounds.boxMax;
        EI_NAN_AUDIT(extents.y, extents.x);
        float extent = extents.x < extents.y ? extents.y : extents.x;
        float half = 0.5f;
        float y = model->box.bounds.center.y * cell.size * half + cell.y;
        float x = cell.x - model->box.bounds.center.x * cell.size * half;
        model->Place(x, y, cell.depth);
        model->SetScale(0.48157462f * cell.size * cell.depth *
                                        0.00125f / extent);
    } else {
        SetShade(model, 1.0f);
        float dt = FrameTime();
        ItemTurn* turn = ItemTurnOf(item);
        ItemKind kind = item->Kind();
        CSpell* spell = item->spell;
        if (spell && (kind == ItemKind::Weapon || kind == ItemKind::Armor)) {
            int school = spell->GetPrototype()->school;
            if (school < 8)
                model->flatSpecular = Glow(dt, turn, kSchoolGlow[school]);
            else
                turn->glow = 0.0f;
        }
        Turn(turn, dt, item == hoveredItem);

        if (LiesFlat(item)) {
            model->localRotation = HalfTurn(0.0f, 0.0f, 1.0f);
        } else if (kind == ItemKind::Weapon) {
            // Weapons turn about (1, 1, 0) after a half turn about x.
            Quaternion a = HalfTurn(1.0f, 0.0f, 0.0f);
            const Vector3 axis = {1.0f, 1.0f, 0.0f};
            Quaternion b;
            b.SetAxisAngle(axis, turn->angle);
            model->localRotation = ProductA(a, b);
        } else if (IsRecordLoot(item, LootRecordType::Weapon) || IsRecordLoot(item, LootRecordType::Armor) || IsRecordLoot(item, LootRecordType::Quick)) {
            model->localRotation = HalfTurn(1.0f, 0.0f, 0.0f);    // blueprints lie still
        } else {
            // Others turn about y after a half turn about x.
            Quaternion a = HalfTurn(1.0f, 0.0f, 0.0f);
            const Vector3 axis = {0.0f, 1.0f, 0.0f};
            Quaternion b;
            b.SetAxisAngle(axis, turn->angle);
            model->localRotation = ProductB(a, b);
        }

        float frameDepth = cell.depth * 1.3f;
        UiMesh* frame = AddBlankSprite();
        float half = 35.0f;
        frame->SetRectangle(frameDepth, 100.0f, cell.x - half, cell.y - half,
                            cell.x + half, cell.y + half, 0x9D, 0x9D, 0xE3, 0xE3);
        frame->texture = objects[2];
        frame->Draw(pass);
        RemoveSceneObject(frame);
    }

    model->flatColour = PackColour(model->tint);
    model->flags |= FigureMeshFlag::Flat;
    if (item->spell) render::g_renderState.specular = true;
    model->Draw(pass);
    if (item->spell) render::g_renderState.specular = false;

    CSpell* spell = item->spell;
    if (item->Kind() == ItemKind::Scroll && spell && spell->GetModifierCount() > 0) {
        const UiMesh::Placement placed = model->GetPlacement();
        const float px = placed.x, py = placed.y, pz = placed.depth;
        float scale = model->scaleMatrix.m[0];
        UiMesh* mark = AddBlankSprite();
        mark->SetRectangle(pz * 0.9f, 100.0f, 0.0f, 0.0f, 35.0f, 35.0f, 0, 0,
                           0x20, 0x20);
        mark->texture = objects[1];
        mark->SetScale(scale);
        SetShade(mark, cell.mode == 9 ? 0.5f : 1.0f);
        float step = scale * 550.0f;
        for (int i = 0; i < spell->GetModifierCount(); ++i) {
            int code = spell->modifiers[i] - 1;    // the modifier's index (stored as index + 1)
            float cellSize = 0.125f;
            mark->textureOffsetU = (code & 7) * cellSize;
            mark->textureOffsetV = (-(code >> 3)) * cellSize;
            float d = step / pz;
            float dx;
            if (i == 0 || i == 4)
                dx = 0.0f;
            else if (i == 5 || i == 6 || i == 7)
                dx = -d;
            else
                dx = d;
            float dy;
            if (i == 2 || i == 6)
                dy = 0.0f;
            else if (i == 0 || i == 1 || i == 7)
                dy = -d;
            else
                dy = d;
            float depth = pz * 0.9f;
            mark->Place(dx + px, dy + py, depth);
            mark->Draw(pass);
        }
        RemoveSceneObject(mark);
    }

    if (cell.mode != 9 && !CanEquip(item, cell.mode)) {
        float depth = cell.depth * 1.25f;
        if (LiesFlat(item)) depth = cell.depth * 0.8f;
        UiMesh* shade = AddBlankSprite();
        float half = 35.0f;
        shade->SetRectangle(depth, 100.0f, cell.x - half, cell.y - half,
                            cell.x + half, cell.y + half, 0, 0, 0x100, 0x100);
        shade->texture = nullptr;
        shade->flags |= FigureMeshFlag::Flat;
        shade->flatColour = 0x82804040;
        shade->Draw(pass);
        RemoveSceneObject(shade);
    }
}

void ItemInfoPanel::Draw(SceneRenderer* pass) {
    if (Item* item = FirstItem()) {
        const ItemCell cell{item, x + 100.0f,
                            y + 200.0f, 3.0f, 120.0f, 9};
        owner->DrawItem(pass, cell);
    }
}

// --- A hero's face -----------------------------------------------------------------------------

namespace {

// A hero face's model "infa<model>me<hair + 1>face" and texture "face<m|f><skin, two digits>".
struct HeroFaceNames {
    std::string model;
    std::string texture;
};

HeroFaceNames FaceNames(const char* prototype) {
    UnitPrototypeRecord* unit;
    {
        std::string key(prototype);
        unit = g_unitDatabase.prototypes.byName[key];   // an unknown name gets a null entry
    }
    UnitModelRecord* figure;
    {
        std::string key(unit->modelName);
        figure = unitdb::ModelNamed(key);
    }
    std::string sex;
    const std::string& figureModel = figure->figureName;
    const bool fixes = pluginfixes::Enabled();   // EI_Plugin's Fixes: the model's fifth character
    if (fixes ? !pluginfixes::ModelIsFemale(figureModel) : figureModel == "unhuma")
        sex = "m";
    else if (fixes || figureModel == "unhufe")
        sex = "f";
    return {std::format("infa{}me{}face", figureModel, unit->hair + 1), std::format("face{}{:02}", sex, unit->skin)};
}

} // namespace

void ItemScreen::DrawHeroFace(SceneRenderer* pass, const HeroFaceCell& cell) {
    const HeroFaceNames names = FaceNames(cell.prototype);
    UiMesh* model = CachedObject(&models, names.model, ItemModel);
    CTexture* texture = CachedObject(&textures, names.texture, ItemFigureTexture);
    if (!model || !texture) return;

    model->texture = texture;
    model->Place(cell.x, cell.y, cell.depth);
    model->SetScale(cell.size * cell.depth * 0.48157462f * 0.0009375f);
    model->material = blackMaterial;
    SetShade(model, 1.0f);
    Quaternion turn = HalfTurn(1.0f, 0.0f, 0.0f);
    const Vector3 axis = {-1.0f, 0.0f, 0.0f};
    Quaternion tilt;
    tilt.SetAxisAngle(axis, (cell.y - 300.0f) * 0.001f);
    Quaternion orientation;
    model->localRotation = *MultiplyQuaternions(&orientation, &tilt, &turn);

    float frameDepth = cell.depth * 1.3f;
    UiMesh* frame = AddBlankSprite();
    float half = 35.0f;
    frame->SetRectangle(frameDepth, 100.0f, cell.x - half, cell.y - half,
                        cell.x + half, cell.y + half, 0x9D, 0x9D, 0xE3, 0xE3);
    frame->texture = objects[2];
    frame->Draw(pass);
    RemoveSceneObject(frame);
    model->Draw(pass);
}

// --- An item's figure texture -------------------------------------------------------------

CTexture* ItemFigureTexture(std::string_view name) {
    CTextureManager* manager = g_clientWorld.textureManager;
    CTexture* texture = manager->NewTexture(0x100, 0x100, TextureKind::Blank1555);
    if (!texture) return nullptr;
    texture->AddRef();
    const std::string file = std::string(name) + ".mmp";
    std::uint32_t size = 0;
    const auto* image = static_cast<const MmpHeader*>(manager->GetFile(file, &size));
    if (!image) {
        // No picture: the whole texture white.
        render::DrawIntoTexture(texture->surface, nullptr, 0, 0, 0x100, 0x100);
    } else {
        // The picture at the texture's bottom left.
        const int width = static_cast<int>(image->width), height = static_cast<int>(image->height);
        const render::TextureImage picture = PictureOf(file, image, size, width, height);
        render::DrawIntoTexture(texture->surface, &picture, 0, 0x100 - height, width, height);
    }
    manager->FreeAllFiles();
    return texture;
}

// --- An image into part of a texture -------------------------------------------------------

bool UiWindow::CopyImage(CTexture* texture, std::string_view name, int imageWidth, int imageHeight, int width,
                         int height, int x, int y) {
    CTextureManager* manager = g_clientWorld.textureManager;
    const std::string file = std::string(name) + ".mmp";
    std::uint32_t size = 0;
    const auto* image = static_cast<const MmpHeader*>(manager->GetFile(file, &size));
    if (!image) {
        render::DrawIntoTexture(texture->surface, nullptr, x, y, width, height);    // white
    } else {
        const render::TextureImage picture = PictureOf(file, image, size, imageWidth, imageHeight);
        render::DrawIntoTexture(texture->surface, &picture, x, y, width, height);
    }
    manager->FreeAllFiles();
    return image != nullptr;
}
