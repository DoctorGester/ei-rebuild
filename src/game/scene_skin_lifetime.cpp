// The fake unit's skin (SceneSkin, CFakeUnit + 0x4E0, 0x50 bytes; A-U12): its constructor,
// destructor, Create and Clear. Layout: scene_loader.h; the bones: figure.h.
#include "scene_loader.h"

#include "figure.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <new>

using scene::SceneSkin;

namespace {
constexpr std::uint32_t kClockUnsetHigh = 0xBFF00000u;   // the clock as a double: -1.0
constexpr const char* kNoneText = "none";

// The root bone destroyed (its children with it) and deleted. True when there was one.
bool DropRoot(SceneSkin* skin) {
    CAnimationNode* root = skin->root;
    if (!root) return false;
    root->DestructBody();
    gamecrt::operator_delete(root);
    skin->root = nullptr;
    return true;
}
} // namespace

SceneSkin::SceneSkin() {
    bones = {};
    names.Construct();
    spareArray = {};
    clock[0] = 0;
    owner = 0;
    root = nullptr;
    clock[1] = kClockUnsetHigh;
    name.Empty();
    names.SetSize(0, -1);
    spare38 = 0;
    if (spareArray.data) {
        lib::FreeAligned32(spareArray.data);
        spareArray.data = nullptr;
    }
    spareArray.capacity = 0;
    spareArray.size = 0;
}

SceneSkin* SceneSkin::Construct() { return new (this) SceneSkin(); }
EI_REPLACE_METHOD(0x00508700, SceneSkin::Construct);

SceneSkin::~SceneSkin() {
    if (DropRoot(this)) bones.SetSize(0, -1);
    owner = 0;
    names.SetSize(0, -1);
    if (spareArray.data) lib::FreeAligned32(spareArray.data);
    names.Destruct();
    if (bones.data) lib::FreeAligned32(bones.data);
}

void SceneSkin::Destruct() { this->~SceneSkin(); }
EI_REPLACE_METHOD(0x005087B0, SceneSkin::Destruct);

int SceneSkin::Create() {
    if (DropRoot(this)) {
        if (bones.data) {
            lib::FreeAligned32(bones.data);
            bones.data = nullptr;
        }
        bones.capacity = 0;
        bones.size = 0;
    }
    clock[0] = 0;
    clock[1] = 0;
    name = kNoneText;
    names.SetSize(0, -1);
    spare4C = 0;
    return 1;
}
EI_REPLACE_METHOD(0x00508870, SceneSkin::Create);

void SceneSkin::Clear() {
    if (DropRoot(this)) bones.SetSize(0, -1);
    owner = 0;
    names.SetSize(0, -1);
}
EI_REPLACE_METHOD(0x005088E0, SceneSkin::Clear);

void SceneSkin::SetOwner(std::uint32_t value) { owner = value; }
EI_REPLACE_METHOD(0x00508930, SceneSkin::SetOwner);
