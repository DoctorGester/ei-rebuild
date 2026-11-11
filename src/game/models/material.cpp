// The material (Material) and the material manager (MaterialManager): material.h. See
// docs/notes/items_spells.md ("Materials").
#include "game/models/material.h"

#include "game/core/engine_globals.h"
#include "game/mods/mod_folders.h"

#include "game/core/asset_text.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/rounding.h"
#include "game/data/resource.h"
#include "lib/toml.h"

#include <algorithm>
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

std::string_view Material::GetName() { return name; }
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
        MaterialFile file;
        MaterialColors& lit = file.colors;
        if (!file.Read(FindRecord(kind.file))) {
            lit.diffuse = {0.3f, 0.3f, 0.3f, 1.0f};
            lit.ambient = {0.7f, 0.7f, 0.7f, lit.ambient.w};
            lit.specular = {0.2f, 0.2f, 0.2f, lit.specular.w};
            lit.power = 5.0f;
        }
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

bool MaterialFile::Read(const RecordFile& file) {
    if (!file.Any()) return false;
    if (file.found) {
        // The name's length and text, then the colours.
        std::string bytes = file.original;
        bytes.resize(std::max<std::size_t>(bytes.size(), 2));
        const std::size_t nameLength = lib::Load<std::uint16_t>(bytes.data());
        bytes.resize(std::max(bytes.size(), 2 + nameLength + sizeof(MaterialColors)));
        unreadName = text::FromAssetText(std::string_view(bytes).substr(2, nameLength));
        colors = lib::Load<MaterialColors>(bytes.data() + 2 + nameLength);
    }
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    return true;
}

namespace {

constexpr const char* kColorKeys[4] = {"diffuse", "ambient", "specular", "emissive"};

Vector4& ColorOf(MaterialColors& colors, int i) {
    Vector4* all[4] = {&colors.diffuse, &colors.ambient, &colors.specular, &colors.emissive};
    return *all[i];
}

} // namespace

void MaterialFile::MergeToml(const toml::Value& root, const std::string& path) {
    for (int i = 0; i < root.Size(); ++i) {
        const std::string_view key = root.KeyAt(i);
        const toml::Value value = root.ValueAt(i);
        bool known = key == "name" || key == "power" || IsReplaceKey(key, value);
        if (key == "name" && toml::Read(value, &unreadName)) unreadName = text::ToAssetText(unreadName);
        if (key == "power") toml::Read(value, &colors.power);
        for (int c = 0; c < 4; ++c) {
            if (key != kColorKeys[c]) continue;
            Vector4& color = ColorOf(colors, c);
            color = {};
            toml::Read(value.At(0), &color.x);
            toml::Read(value.At(1), &color.y);
            toml::Read(value.At(2), &color.z);
            toml::Read(value.At(3), &color.w);
            known = true;
        }
        if (!known) game::Warning("materials: %s line %d: unknown key %s", path, value.Line(), std::string(key));
    }
}

std::string MaterialFile::ToToml() const {
    toml::Writer writer;
    writer.Pair("name", toml::String(text::FromAssetText(unreadName)));
    MaterialColors copy = colors;
    for (int c = 0; c < 4; ++c) {
        const Vector4& color = ColorOf(copy, c);
        writer.Pair(kColorKeys[c], toml::Array({toml::Float(color.x), toml::Float(color.y), toml::Float(color.z),
                                                toml::Float(color.w)}));
    }
    writer.Pair("power", toml::Float(colors.power));
    return writer.text;
}

std::uint32_t Material::GpuLitColour() const {
    const float alpha = colors.diffuse.w * 255.0f;
    return static_cast<std::uint32_t>(lib::RoundEven(alpha)) << 24 | 0x00FFFFFFu;
}
