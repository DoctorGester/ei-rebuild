// Checks for the windows' model previews (ui_model.cpp): 005FC1D0 ReleaseObject, 005FC2F0 Draw,
// 005FC3B0 Play, 005FC3D0 Stop, 005FC4C0 ShowPart, 005FC4F0 ShowAllParts. All run ours only (they
// release, animate and draw the shared object). Reached by the Esc menu (ui_screens, ui_hud: its
// Build plays animation 1, hovering a stone plays and stops them, its slot 20 draws the model), the
// save / load and options windows (ShowPart / ShowAllParts in their Builds, Draw while sliding in)
// and every window destroyed with a model (ReleaseObject). 005FC1F0 Load (A-I18, ours only: it makes an
// object): a referenced object with flag 0x4000000 at (0, 0, 3) holding parts; reached by the Esc menu,
// options and save / load windows (ui_screens, ui_options, ui_saveload).
#include "verify.h"

#include "../../src/game/map_object_figures.h"
#include "../../src/game/ui.h"

#include <vector>

namespace {
void* s_unused;

constexpr std::uint32_t kHidden = 0x80;

std::uint32_t Flags(void* part) { return reinterpret_cast<CEngineObject*>(part)->flags_; }

std::vector<std::uint32_t> PartFlags(CObject3D* object) {
    std::vector<std::uint32_t> flags;
    for (int i = 0; i < object->parts.size; ++i)
        flags.push_back(object->parts.data[i] ? Flags(object->parts.data[i]) : 0);
    return flags;
}
} // namespace

static void __fastcall ReleaseObjectVerify(CUiModel* self) {
    self->CUiModel::ReleaseObject();
    ei::verify::Check("CUiModel::ReleaseObject", true, self->object_ == nullptr, "");
}
EI_VERIFY(0x005FC1D0, ReleaseObjectVerify, 0, s_unused);

// The pass's colour is put back and flag 0x800 is clear again; the object stays.
static void __fastcall ModelDrawVerify(CUiModel* self, void*, UiDrawPass* pass) {
    std::uint32_t colour = pass->colour_;
    CTransformObject* object = self->object_;
    self->CUiModel::Draw(pass);
    bool ok = pass->colour_ == colour && self->object_ == object && !(self->Figure()->flags_ & 0x800);
    ei::verify::Check("CUiModel::Draw", true, ok, "colour %08X -> %08X", colour, pass->colour_);
}
EI_VERIFY(0x005FC2F0, ModelDrawVerify, 0, s_unused);

static void __fastcall PlayVerify(CUiModel* self, void*, int animation) {
    CTransformObject* object = self->object_;
    self->CUiModel::Play(animation);
    ei::verify::Check("CUiModel::Play", true, self->object_ == object, "animation %d", animation);
}
EI_VERIFY(0x005FC3B0, PlayVerify, 0, s_unused);

static void __fastcall StopVerify(CUiModel* self) {
    CTransformObject* object = self->object_;
    self->CUiModel::Stop();
    ei::verify::Check("CUiModel::Stop", true, self->object_ == object, "");
}
EI_VERIFY(0x005FC3D0, StopVerify, 0, s_unused);

// Only the named part's hidden bit changes (to !show); a missing name changes nothing.
static void __fastcall ShowPartVerify(CUiModel* self, void*, const char* name, int show) {
    CObject3D* object = self->Figure();
    std::vector<std::uint32_t> before = PartFlags(object);
    FigurePart* part = object->FindPart(name);
    self->CUiModel::ShowPart(name, show);
    std::vector<std::uint32_t> after = PartFlags(object);
    bool ok = before.size() == after.size();
    for (std::size_t i = 0; ok && i < before.size(); ++i) {
        std::uint32_t expected = before[i];
        if (part && object->parts.data[i] == part)
            expected = static_cast<std::uint8_t>(show) ? expected & ~kHidden : expected | kHidden;
        ok = after[i] == expected;
    }
    if (part) ok &= ((Flags(part) & kHidden) == 0) == (static_cast<std::uint8_t>(show) != 0);
    ei::verify::Check("CUiModel::ShowPart", true, ok, "name %s show %d found %d", name, show, part != nullptr);
}
EI_VERIFY(0x005FC4C0, ShowPartVerify, 0, s_unused);

// Every part's hidden bit becomes !show, the other bits stay.
static void __fastcall ShowAllPartsVerify(CUiModel* self, void*, int show) {
    CObject3D* object = self->Figure();
    std::vector<std::uint32_t> before = PartFlags(object);
    self->CUiModel::ShowAllParts(show);
    std::vector<std::uint32_t> after = PartFlags(object);
    bool ok = before.size() == after.size();
    for (std::size_t i = 0; ok && i < before.size(); ++i) {
        if (!object->parts.data[i]) continue;
        std::uint32_t expected = static_cast<std::uint8_t>(show) ? before[i] & ~kHidden : before[i] | kHidden;
        ok = after[i] == expected;
    }
    ei::verify::Check("CUiModel::ShowAllParts", true, ok, "parts %d show %d", object->parts.size, show);
}
EI_VERIFY(0x005FC4F0, ShowAllPartsVerify, 0, s_unused);

static void __fastcall LoadVerify(CUiModel* self, void*, const char* name, const char* from, const char* to) {
    self->CUiModel::Load(name, from, to);
    CObject3D* object = self->Figure();
    bool ok = object && object->refCount_ >= 1 && (object->flags_ & 0x4000000) && object->position.x == 0.0f &&
              object->position.y == 0.0f && object->position.z == 3.0f && object->parts.size > 0;
    ei::verify::Check("CUiModel::Load", true, ok, "model %s object %p parts %d", name ? name : "(null)",
                      static_cast<void*>(object), object ? object->parts.size : -1);
}
EI_VERIFY(0x005FC1F0, LoadVerify, 0, s_unused);
