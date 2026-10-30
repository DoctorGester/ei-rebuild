// The blink of the party units' faces (UnitClientSpecific, unit_client_models.h): the face
// texture ("<model>mouth") holds the open and the closed state, which is copied over a
// rectangle of the unit's skin texture with DirectDraw blits down the mip chain.
// No texture has a DirectDraw surface (surface.h; the skins are decoded by the new renderer):
// the new renderer draws the face's rectangle over the skin's in its shader instead
// (render::TexturePatch, docs/notes/units.md); the skin's texture is not changed.
#include "unit_client_models.h"

#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "texture.h"
#include "unit.h"
#include "unit_db.h"

#include "lib/pc24.h"
#include "render/render.h"

#include <cstdint>

namespace {

constexpr float kEyeUnit = 0.00390625f;    // 1/256 of the skin's width
constexpr int kSkinUnits = 0x100;

// _ftol: truncation through a 64-bit integer, of which the low half is kept.
int Truncate(float value) { return static_cast<int>(static_cast<std::int64_t>(value)); }


// The skin texture: the first texture of the unit's client half (its material), or null.
CTexture* SkinOf(CUnit* unit) {
    auto* half = unit->Object3D()->ClientSpecific();
    if (!half->partSets[0]) return nullptr;
    return half->partSets[0]->texture;
}

// The eye rectangle's numbers shared by both: the skin's scale and the face's offset `t` (in mixed units). The
// data's ints enter the operations exactly (pc24's helpers round the result once).
struct EyeFrame {
    float scale;
    float offset;
    int at[2];
};

EyeFrame Frame(const CTexture* skin, const CTexture* face, const UnitModelRecord* model) {
    EyeFrame frame;
    frame.scale = static_cast<float>(static_cast<double>(skin->width) * kEyeUnit);
    int eyeHeight = static_cast<int>(model->eyeHeight);
    frame.offset = pc24::Sub(pc24::Div(static_cast<double>(face->height), frame.scale),
                             static_cast<double>(eyeHeight + eyeHeight)) / frame.scale;
    frame.at[0] = Truncate(pc24::Mul(static_cast<double>(static_cast<int>(model->eyeLeft)), frame.scale));
    int fromBottom = kSkinUnits - static_cast<int>(model->eyeBottom) - eyeHeight;
    frame.at[1] = Truncate(pc24::Mul(static_cast<double>(fromBottom), frame.scale));
    return frame;
}

} // namespace

// The game blitted `from` of `source` to the rectangle of its size at `at` of `target`, then on each next level of
// both mip chains with each of the eight edges halved (SAR), until either had no further level. Ours hands the
// first level's rectangles to the renderer, which shows them over the skin in its shader on every level.
void BlitMipChain(CTexture* target, const int* at, CTexture* source, const Rect* from) {
    const int to[4] = {at[0], at[1], at[0] + (from->right - from->left), at[1] + (from->bottom - from->top)};
    const int rectangle[4] = {from->left, from->top, from->right, from->bottom};
    render::TexturePatch(target->surface, to, source->surface, rectangle);
}

void UnitClientSpecific::BlinkClose(bool force) {
    if (!faceTexture) return;
    if (!force && eyesClosed == 1) return;
    CTexture* skin = SkinOf(unit);
    if (!skin) return;
    auto* model = unit->GetPrototypeModel();
    CTexture* face = faceTexture;
    EyeFrame frame = Frame(skin, face, model);
    float scale = frame.scale;
    int eyeHeight = static_cast<int>(model->eyeHeight);
    Rect closed = {0, Truncate(frame.offset), Truncate(pc24::Mul(static_cast<double>(static_cast<int>(model->eyeWidth)), scale)),
                   Truncate(pc24::Add(static_cast<double>(eyeHeight), frame.offset) * scale)};
    BlitMipChain(skin, frame.at, face, &closed);
    eyesClosed = 1;
}

void UnitClientSpecific::BlinkOpen(bool force) {
    if (!faceTexture) return;
    if (!force && eyesClosed == 0) return;
    CTexture* skin = SkinOf(unit);
    if (!skin) return;
    auto* model = unit->GetPrototypeModel();
    CTexture* face = faceTexture;
    EyeFrame frame = Frame(skin, face, model);
    float scale = frame.scale;
    int eyeHeight = static_cast<int>(model->eyeHeight);
    Rect open = {0, Truncate(pc24::Add(static_cast<double>(eyeHeight), frame.offset) * scale),
                 Truncate(pc24::Mul(static_cast<double>(static_cast<int>(model->eyeWidth)), scale)),
                 Truncate(pc24::Add(static_cast<double>(eyeHeight + eyeHeight), frame.offset) * scale)};
    BlitMipChain(skin, frame.at, face, &open);
    eyesClosed = 0;
}
