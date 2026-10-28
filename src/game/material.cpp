// The material (CMaterial) and the material manager (CMaterialManager): material.h. See
// docs/notes/items_spells.md ("Materials").
#include "material.h"

#include "game_strings.h"
#include "mod_folders.h"
#include "world_data.h"   // TextPathString

#include "lib/crt.h"

#include <bit>
#include <cstring>
#include <new>
#include <string>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr const char* kMaterialClassName = "CMaterial";
constexpr const char* kManagerClassName = "CMaterialManager";
constexpr const char* kManagerFolder = "materials";
// The kinds Create enters, in its order, and their files.
constexpr MaterialKindFile kMaterialKindFiles[8] = /*00743BF8*/ {
    {0x11, "leather.mat"}, {0x12, "wood.mat"}, {0x13, "metal.mat"}, {0x14, "cloth.mat"},
    {0x15, "skin.mat"},    {0x16, "stone.mat"}, {0x17, "magic.mat"}, {0x19, "metal.mat"},
};

// The names, alpha and the two ints the constructor and Create reset (in their order).
void ResetFields(CMaterial* m) {
    m->name = game::NoneText();
    m->colors.diffuse.w = 1.0f;
    m->name2 = game::NoneText();
    m->spare90 = 0;
    m->name3 = game::NoneText();
    m->spare98 = 0;
}

} // namespace


CMaterial::CMaterial() {
    ClearFields();
    entries.Construct();
    ResetFields(this);
    entries.PopAll();
}

// The names go after the body (name3, name2, then name after the deque in the game).
CMaterial::~CMaterial() {
    entries.PopAll();
    entries.Destroy();
}

int CMaterial::Create() {
    ResetFields(this);
    entries.PopAll();
    return 1;
}

void CMaterial::Destroy() {
    entries.PopAll();
}

const char* CMaterial::GetName() { return name.c_str(); }
const char* CMaterial::GetClassName() { return kMaterialClassName; }
int CMaterial::GetClassId() { return 5; }

namespace {

// The table's colours for `key`; an absent key is entered with the default the game builds (its key copied
// over every dword of the colours).
MaterialColors& EnterKey(CMaterialManager::ColorTable& table, unsigned key) {
    if (auto found = table.find(key); found != table.end()) return found->second;
    std::uint32_t words[sizeof(MaterialColors) / 4];
    for (std::uint32_t& w : words) w = key;
    return table.emplace(key, std::bit_cast<MaterialColors>(words)).first->second;
}

} // namespace

CMaterialManager::CMaterialManager() {}

CMaterialManager::~CMaterialManager() = default;

int CMaterialManager::Create() {
    // Rebirth's ResOverride: the mod's archives open here if nothing opened them yet.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::LoadModArchives();
    if (!CBaseManager::Create()) return 0;
    const std::string folder = TheTextFolder();
    AddArchive((folder + "materials.res").c_str());

    for (const MaterialKindFile& kind : kMaterialKindFiles) {
        MaterialColors colors;
        std::memset(&colors, 0, sizeof(colors));
        void* data = GetFile(kind.file, nullptr);
        if (!data) {
            colors.diffuse = {0.3f, 0.3f, 0.3f, 1.0f};
            colors.ambient.x = colors.ambient.y = colors.ambient.z = 0.7f;
            colors.specular.x = colors.specular.y = colors.specular.z = 0.2f;
            colors.power = 5.0f;
        } else {
            std::memcpy(&colors, MaterialFileColors(data), sizeof(colors));
        }
        CloseFile(data);
        if (kind.kind == kMagicMaterialKind) colors.emissive.x = colors.emissive.y = colors.emissive.z = 1.0f;
        colors_[static_cast<unsigned>(kind.kind)] = colors;
    }
    return 1;
}

CEngineObject* CMaterialManager::NewObject(int type) {
    CMaterial* block = gamecrt::operator_new_as<CMaterial>();
    if (!block) return nullptr;
    auto* material = new (block) CMaterial();
    material->type_ = type;
    return material;
}

CMaterial* CMaterialManager::NewMaterial(int type) {
    auto* material = CreateObjectAs<CMaterial>(type, -1);
    // The game sets the flag before its null check, so a failed creation faults there.
    if (!material) return nullptr;
    material->flags_ |= 8;
    material->colors = EnterKey(colors_, static_cast<unsigned>(material->type_));
    return material;
}

int CMaterialManager::FillMaterial(CMaterial* material, unsigned key) {
    if (!material) return 0;
    material->colors = EnterKey(colors_, key);
    return 1;
}

const char* CMaterialManager::GetClassName() { return kManagerClassName; }
int CMaterialManager::GetClassId() { return 0x27; }
const char* CMaterialManager::GetFolderName() { return kManagerFolder; }
