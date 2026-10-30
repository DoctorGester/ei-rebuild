// The material (Material) and the material manager (CMaterialManager): material.h. See
// docs/notes/items_spells.md ("Materials").
#include "material.h"

#include "game_strings.h"
#include "mod_folders.h"
#include "world_data.h"   // TextPathString

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/rounding.h"

#include <bit>
#include <cstring>
#include <new>
#include <string>

namespace {

constexpr const char* kMaterialClassName = "CMaterial";
constexpr const char* kManagerClassName = "CMaterialManager";
constexpr const char* kManagerFolder = "materials";
// The kinds Create enters, in its order, and their files.
constexpr MaterialKindFile kMaterialKindFiles[8] = /*00743BF8*/ {
    {0x11, "leather.mat"}, {0x12, "wood.mat"}, {0x13, "metal.mat"}, {0x14, "cloth.mat"},
    {0x15, "skin.mat"},    {0x16, "stone.mat"}, {0x17, "magic.mat"}, {0x19, "metal.mat"},
};

// The names and alpha the constructor and Create reset (in their order).
void ResetFields(Material* m) {
    m->name = game::NoneText();
    m->colors.diffuse.w = 1.0f;
    m->name2 = game::NoneText();
    m->name3 = game::NoneText();
}

// The table's colours for `key`; an absent key is entered with the default the game builds (its key copied
// over every dword of the colours).
MaterialColors& EnterKey(std::unordered_map<unsigned, MaterialColors>& table, unsigned key) {
    if (auto found = table.find(key); found != table.end()) return found->second;
    std::uint32_t words[sizeof(MaterialColors) / 4];
    for (std::uint32_t& w : words) w = key;
    return table.emplace(key, std::bit_cast<MaterialColors>(words)).first->second;
}

} // namespace


Material::Material() {
    ResetFields(this);
}

void Material::Create() {
    ResetFields(this);
    entries.clear();
}

void Material::Destroy() {
    entries.clear();
}

const char* Material::GetName() { return name.c_str(); }
const char* Material::ClassName() { return kMaterialClassName; }
ClassId Material::GetClassId() { return ClassId::Material; }

CMaterialManager::CMaterialManager() {}

CMaterialManager::~CMaterialManager() = default;

void CMaterialManager::Create() {
    // Rebirth's ResOverride: the mod's archives open here if nothing opened them yet.
    if (ei::rebirth::Enabled(ei::rebirth::Patch::ResOverride)) modfolders::LoadModArchives();
    BaseManager::Create();
    const std::string folder = g_textFolder;
    AddArchive((folder + "materials.res"));

    for (const MaterialKindFile& kind : kMaterialKindFiles) {
        MaterialColors lit;
        std::memset(&lit, 0, sizeof(lit));
        void* data = GetFile(kind.file, nullptr);
        if (!data) {
            lit.diffuse = {0.3f, 0.3f, 0.3f, 1.0f};
            lit.ambient.x = lit.ambient.y = lit.ambient.z = 0.7f;
            lit.specular.x = lit.specular.y = lit.specular.z = 0.2f;
            lit.power = 5.0f;
        } else {
            lib::LoadBytes(lit, MaterialFileColors(data));
        }
        CloseFile(data);
        if (kind.kind == kMagicMaterialKind) lit.emissive.x = lit.emissive.y = lit.emissive.z = 1.0f;
        colors[static_cast<unsigned>(kind.kind)] = lit;
    }
}

EngineObject* CMaterialManager::NewObject(int objectType) {
    auto* material = new Material();
    material->type = objectType;
    return material;
}

Material* CMaterialManager::NewMaterial(int materialType) {
    auto* material = CreateObjectAs<Material>(materialType, -1);
    // The game sets the flag before its null check, so a failed creation faults there.
    if (!material) return nullptr;
    material->flags |= MaterialFlag::Keyed;
    material->colors = EnterKey(colors, static_cast<unsigned>(material->type));
    return material;
}

int CMaterialManager::FillMaterial(Material* material, unsigned key) {
    if (!material) return 0;
    material->colors = EnterKey(colors, key);
    return 1;
}

const char* CMaterialManager::ClassName() { return kManagerClassName; }
ClassId CMaterialManager::GetClassId() { return ClassId::MaterialManager; }
const char* CMaterialManager::GetFolderName() { return kManagerFolder; }

std::uint32_t Material::GpuLitColour() const {
    const float alpha = colors.diffuse.w * 255.0f;
    return static_cast<std::uint32_t>(lib::RoundEven(alpha)) << 24 | 0x00FFFFFFu;
}
