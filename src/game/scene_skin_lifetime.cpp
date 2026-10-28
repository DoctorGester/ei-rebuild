// The fake unit's skin (SceneSkin, CFakeUnit + 0x4E0): its constructor, destructor, Create and
// Clear. Layout: scene_loader.h; the bones: figure.h.
#include "scene_loader.h"

#include "figure.h"

#include "lib/crt.h"

#include <memory>
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
    spareArray = {};
    clock[0] = 0;
    owner = 0;
    root = nullptr;
    clock[1] = kClockUnsetHigh;
    name.clear();
    names.clear();
    spare38 = 0;
    lib::FreeStorage(spareArray);
}

SceneSkin::~SceneSkin() {
    if (DropRoot(this)) bones.clear();
    owner = 0;
    names.clear();
}

int SceneSkin::Create() {
    if (DropRoot(this)) {
        lib::FreeStorage(bones);
    }
    clock[0] = 0;
    clock[1] = 0;
    name = kNoneText;
    names.clear();
    spare4C = 0;
    return 1;
}

void SceneSkin::Clear() {
    if (DropRoot(this)) bones.clear();
    owner = 0;
    names.clear();
}

void SceneSkin::SetOwner(std::uint32_t value) { owner = value; }
