// The material (Material) and the material manager (MaterialManager): material.h. See
// docs/notes/items_spells.md ("Materials").
#include "game/models/material.h"

#include "game/app/singletons.h"
#include "game/mods/mod_folders.h"

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
// The kinds Create enters, in its order, and their files.
constexpr MaterialKindFile kMaterialKindFiles[8] = /*00743BF8*/ {
    {0x11, "leather.mat"}, {0x12, "wood.mat"}, {0x13, "metal.mat"}, {0x14, "cloth.mat"},
    {0x15, "skin.mat"},    {0x16, "stone.mat"}, {0x17, "magic.mat"}, {0x19, "metal.mat"},
};

// The names and alpha the constructor and Create reset (in their order).
void ResetFields(Material* m) {
    m->name = "none";
    m->colors.diffuse.w = 1.0f;
    m->name2 = "none";
    m->name3 = "none";
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

MaterialManager::MaterialManager() {}

MaterialManager::~MaterialManager() = default;

void MaterialManager::Create() {
    // Rebirth's ResOverride: the mod's archives open here if nothing opened them yet.
    if (modfolders::Active()) modfolders::LoadModArchives();
    BaseManager::Create();
    const std::string folder = g_textFolder;
    AddArchive((folder + "materials.res"));

    for (const MaterialKindFile& kind : kMaterialKindFiles) {
        MaterialColors lit;
        std::memset(&lit, 0, sizeof(lit));
        void* data = GetFile(kind.file, nullptr);
        if (!data) {
            lit.diffuse = {0.3f, 0.3f, 0.3f, 1.0f};
            lit.ambient = {0.7f, 0.7f, 0.7f, lit.ambient.w};
            lit.specular = {0.2f, 0.2f, 0.2f, lit.specular.w};
            lit.power = 5.0f;
        } else {
            lit = lib::Load<MaterialColors>(MaterialFileColors(data));
        }
        CloseFile(data);
        if (kind.kind == kMagicMaterialKind) lit.emissive = {1.0f, 1.0f, 1.0f, lit.emissive.w};
        colors[static_cast<unsigned>(kind.kind)] = lit;
    }
}

EngineObject* MaterialManager::NewObject(int objectType) {
    auto* material = new Material();
    material->type = objectType;
    return material;
}

Material* MaterialManager::NewMaterial(int materialType) {
    auto* material = CreateObjectAs<Material>(materialType, -1);
    // The game sets the flag before its null check, so a failed creation faults there.
    if (!material) return nullptr;
    material->flags |= MaterialFlag::Keyed;
    material->colors = EnterKey(colors, static_cast<unsigned>(material->type));
    return material;
}

int MaterialManager::FillMaterial(Material* material, unsigned key) {
    if (!material) return 0;
    material->colors = EnterKey(colors, key);
    return 1;
}

const char* MaterialManager::ClassName() { return kManagerClassName; }
ClassId MaterialManager::GetClassId() { return ClassId::MaterialManager; }

std::uint32_t Material::GpuLitColour() const {
    const float alpha = colors.diffuse.w * 255.0f;
    return static_cast<std::uint32_t>(lib::RoundEven(alpha)) << 24 | 0x00FFFFFFu;
}
