// Checks for the client world's set-up (src/game/world_client_setup.cpp). Both run once at every
// start (00456440: SetUpGraphics; the client's Setup 00561E50: LoadLights), so every scenario
// reaches them.
#include "verify.h"

#include "../../src/game/sky_object.h"   // before windows.h (GetClassName)
#include "../../src/game/model.h"
#include "../../src/game/texture.h"
#include "../../src/game/world.h"

#include <cstring>

namespace {
void* s_unused;

constexpr int kTableFloats = 3 * 24 * 4;
}

// 00563930 LoadLights: PUSH -1; PUSH 0x722538 (7 bytes). Differential: the three tables saved,
// the original reads the file into them, its tables recorded and the old ones put back, then ours;
// every float compared within a few ULPs (the steps keep an unrounded sum).
static int(__fastcall* s_loadLights)(CWorldClient*, void*, const char*);
static int __fastcall LoadLightsVerify(CWorldClient* self, void*, const char* file) {
    static float before[kTableFloats], theirs[kTableFloats];
    std::memcpy(before, self->sunlight_, sizeof(before));
    int expected = s_loadLights(self, nullptr, file);
    std::memcpy(theirs, self->sunlight_, sizeof(theirs));
    std::memcpy(self->sunlight_, before, sizeof(before));
    int actual = self->LoadLights(file);
    const auto* ours = reinterpret_cast<const float*>(self->sunlight_);
    int first = -1;
    for (int i = 0; i < kTableFloats && first < 0; ++i)
        if (!ei::verify::NearlyEqual(theirs[i], ours[i], 4, 1e-6f)) first = i;
    ei::verify::Check("CWorldClient::LoadLights", true, expected == actual && first < 0,
                      "%s: result %d vs %d, first difference at float %d", file, expected, actual, first);
    return actual;
}
EI_VERIFY(0x00563930, LoadLightsVerify, 7, s_loadLights);

// 00563710 SetUpGraphics: makes objects, so ours only: the digit sprites exist with the loaded
// "numbers" texture (when the manager has it) and the figure loader is set and referenced.
static void __fastcall SetUpGraphicsVerify(CWorldClient* self, void*, void* device) {
    self->SetUpGraphics(device);
    bool ok = self->numbers_ && self->figureLoader_ && self->figureLoader_->refCount_ >= 1 &&
              (!self->numbers_->texture || (self->numbers_->texture->flags_ & 0x40000));
    ei::verify::Check("CWorldClient::SetUpGraphics", true, ok, "numbers %p loader %p", self->numbers_,
                      self->figureLoader_);
}
EI_VERIFY(0x00563710, SetUpGraphicsVerify, 0, s_unused);

// 00562F70 LoadSky: makes objects, so ours only (every start, from SetUpGraphics). Checked: the sky
// exists; when it got its object, that is a CSkyObject (made as the figure factory's class 0x44;
// its own GetClassId is 0x20) named "nask0" with flag 0x10, and the sky has a material.
static void __fastcall LoadSkyVerify(CWorldClient* self) {
    const bool had = self->skyDome_ != nullptr;
    self->LoadSky();
    CSky* sky = self->skyDome_;
    bool ok = sky != nullptr;
    CSkyObject* object = sky ? sky->object : nullptr;
    int classId = -1;
    const char* name = "";
    unsigned flags = 0;
    if (ok && !had && object) {
        classId = object->GetClassId();
        name = object->name_ ? static_cast<const char*>(object->name_) : "(null)";
        flags = object->flags_;
        ok = classId == 0x20 && std::strcmp(name, "nask0") == 0 && (flags & 0x10) && sky->material != nullptr;
    }
    ei::verify::Check("CWorldClient::LoadSky", true, ok, "sky %p object %p class 0x%X name \"%s\" flags 0x%X material %p",
                      static_cast<void*>(sky), static_cast<void*>(object), classId, name, flags,
                      sky ? static_cast<void*>(sky->material) : nullptr);
}
EI_VERIFY(0x00562F70, LoadSkyVerify, 0, s_unused);
