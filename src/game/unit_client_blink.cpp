// The blink of the party units' faces (CUnitClientSpecific, unit_client_models.h; A-U3): the face
// texture holds the open and the closed eyes, which the original copies over the eye rectangle of
// the unit's skin texture with DirectDraw blits down the mip chain.
//
// No texture has a DirectDraw surface (dx_surface.h; the skins are decoded by the new renderer):
// the copy does nothing (ticket R11d), and the blink's state is kept as the original keeps it.
#include "unit_client_models.h"

#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "texture.h"
#include "unit.h"
#include "unit_db.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <windows.h>

#include <cstdint>

namespace {

constexpr float kEyeUnit = 0.00390625f;    // 0073EEB8: 1/256 of the skin's width
constexpr int kSkinUnits = 0x100;

// _ftol: truncation through a 64-bit integer, of which the low half is kept.
int Truncate(double value) { return static_cast<int>(static_cast<std::int64_t>(value)); }


// The skin texture: the first texture of the unit's client half (its material), or null.
CTexture* SkinOf(CUnit* unit) {
    auto* half = static_cast<CObject3DClientSpecific*>(unit->Object3D()->attached);
    if (!half->partSets[0]) return nullptr;
    return half->partSets[0]->texture;
}

// The eye rectangle's numbers shared by both: the skin's scale (stored as a float) and the
// face's offset `t` (in the original's mixed units, kept as it computes it).
struct EyeFrame {
    float scale;
    double offset;
    int at[2];
};

EyeFrame Frame(const CTexture* skin, const CTexture* face, const UnitModelRecord* model) {
    EyeFrame frame;
    float scale = static_cast<float>(x87::FromUnsigned(skin->width_) * kEyeUnit);
    frame.scale = scale;
    int eyeHeight = static_cast<int>(model->eyeHeight);
    frame.offset = (x87::FromUnsigned(face->height_) / frame.scale - (eyeHeight + eyeHeight)) / frame.scale;
    frame.at[0] = Truncate(static_cast<int>(model->eyeLeft) * static_cast<double>(frame.scale));
    int fromBottom = kSkinUnits - static_cast<int>(model->eyeBottom) - eyeHeight;
    frame.at[1] = Truncate(fromBottom * static_cast<double>(frame.scale));
    return frame;
}

} // namespace

// 00533980 (__fastcall, RET 8). The original blits `from` of `source` to `at` of `target`, then the
// same halved on each next level of both mip chains (DDSCAPS_TEXTURE | DDSCAPS_MIPMAP attachments,
// whose references are never released) until either has no further level. The textures have no
// DirectDraw surface: nothing is copied.
void __fastcall BlitMipChain(CTexture* /*target*/, const int* /*at*/, CTexture* /*source*/, const RECT* /*from*/) {}
EI_REPLACE(0x00533980, BlitMipChain);

void CUnitClientSpecific::BlinkClose(bool force) {
    if (!faceTexture) return;
    if (!force && eyesClosed == 1) return;
    CTexture* skin = SkinOf(unit);
    if (!skin) return;
    auto* model = reinterpret_cast<const UnitModelRecord*>(unit->GetPrototypeModel());
    auto* face = static_cast<CTexture*>(faceTexture);
    EyeFrame frame = Frame(skin, face, model);
    double scale = frame.scale;
    int eyeHeight = static_cast<int>(model->eyeHeight);
    RECT closed = {0, Truncate(frame.offset), Truncate(static_cast<int>(model->eyeWidth) * scale),
                   Truncate((eyeHeight + frame.offset) * scale)};
    BlitMipChain(skin, frame.at, face, &closed);
    eyesClosed = 1;
}
EI_REPLACE_METHOD(0x00538ED0, CUnitClientSpecific::BlinkClose);

void CUnitClientSpecific::BlinkOpen(bool force) {
    if (!faceTexture) return;
    if (!force && eyesClosed == 0) return;
    CTexture* skin = SkinOf(unit);
    if (!skin) return;
    auto* model = reinterpret_cast<const UnitModelRecord*>(unit->GetPrototypeModel());
    auto* face = static_cast<CTexture*>(faceTexture);
    EyeFrame frame = Frame(skin, face, model);
    double scale = frame.scale;
    int eyeHeight = static_cast<int>(model->eyeHeight);
    RECT open = {0, Truncate((eyeHeight + frame.offset) * scale), Truncate(static_cast<int>(model->eyeWidth) * scale),
                 Truncate((eyeHeight + eyeHeight + frame.offset) * scale)};
    BlitMipChain(skin, frame.at, face, &open);
    eyesClosed = 0;
}
EI_REPLACE_METHOD(0x00539010, CUnitClientSpecific::BlinkOpen);
