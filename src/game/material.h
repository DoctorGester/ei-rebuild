#pragma once

#include "engine_object.h"
#include "resource_manager.h"

#include "lib/bit_flags.h"
#include "lib/bytes.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// A material's lighting colours: diffuse, ambient, specular and emissive (r, g, b, a) and the specular power.
// 0x44 bytes.
struct MaterialColors {
    Vector4 diffuse;
    Vector4 ambient;
    Vector4 specular;
    Vector4 emissive;
    float power;
};
static_assert(sizeof(MaterialColors) == 0x44);

// An element of the material's entries (16 bytes): an emissive colour kept while the object's light
// is on (Object3DClientSpecific::SetLit pushes and pops them).
struct MaterialEntry {
    Vector4 emissive;
};

// The bits of EngineObject::flags on a material.
enum class MaterialFlag : std::uint32_t {
    Keyed = 0x8,            // set by CMaterialManager::NewMaterial (its colours from the table by type); nothing reads it
    KeepSpecular = 0x10,    // a figure part drawn with it keeps its speculars (draw flag 8; the stun / light pulses set it)
};
template <> inline constexpr bool lib::kBitFlags<MaterialFlag> = true;

// A material (class id 5, "Material"; material.cpp), made by the material manager (the world's
// pointer, CMaterialManager::NewMaterial; the file's vectors are the ambient, diffuse and specular
// colours, the power and the diffuse alpha). It is also the material a figure part is drawn with. Names
// provisional. A real virtual class: the compiler makes its vtable.
class Material : public EngineObject {
public:
    // The colour of a figure part lit on the GPU (its colour arrays; the shader lights it): white,
    // with the alpha 3DfpFPU's LightMesh gave every colour (the diffuse alpha times 255, rounded).
    std::uint32_t GpuLitColour() const;

    Material();
    ~Material() override = default;

    // Empty names, alpha 1, +0x90 / +0x98 0, the entries emptied; returns 1.
    void Create() override;
    // Empties the entries.
    void Destroy() override;
    // The name's text
    const char* GetName() override;
    const char* ClassName() override;   // 11: "Material"
    ClassId GetClassId() override;

    std::string name;
    MaterialColors colors;
    std::vector<MaterialEntry> entries;
    std::string name2;                      // (the scene file's second name)
    std::string name3;                      // (the scene file's third name)
};

// The material manager (class id 0x27, "CMaterialManager", 0xD0 bytes; the world's pointer;
// material.cpp): a resource manager of Material objects with a table of colours by key (filled by
// its Create, Rebirth-patched). A real virtual class: the compiler makes its vtable.
class CMaterialManager : public ResourceManager {
public:

    CMaterialManager();
    ~CMaterialManager() override;
    const char* ClassName() override;   // 11: "CMaterialManager"
    ClassId GetClassId() override;
    const char* GetFolderName() override;    // 25: "materials"

    // Rebirth-patched. BaseManager's Create (0 when it fails); the archive "materials.res" of the text
    // folder added; then the colours of each of the eight kinds of kMaterialKindFiles (0x11 leather, wood,
    // metal, cloth, skin, stone, 0x17 magic, 0x19 metal again) entered under the kind: the file's colours
    // (after its name: a 16-bit length and the text), or, without the file, diffuse 0.3 (alpha 1), ambient
    // 0.7, specular 0.2, power 5 and the rest 0; kind 0x17's emissive colour is 1 either way. Returns 1.
    void Create() override;
    // A new Material with type = `type`; null when the allocation fails.
    EngineObject* NewObject(int type) override;
    // A material of `type` (created with id -1, MaterialFlag::Keyed set before the null check) given the table's colours
    // for its type (an absent key is entered first, its colours the key's bits in every dword).
    Material* NewMaterial(int type);
    // The table's colours for `key` (entered as NewMaterial does when absent)
    // into `material`; 1, or 0 for a null material.
    int FillMaterial(Material* material, unsigned key);

    std::unordered_map<unsigned, MaterialColors> colors;
};

// A kind the manager's Create enters and its file (material.cpp's kMaterialKindFiles).
struct MaterialKindFile {
    int kind;
    const char* file;
};
constexpr int kMagicMaterialKind = 0x17;
// The colours a material file holds: after its name (a 16-bit length, then the text).
inline const void* MaterialFileColors(const void* data) {
    std::uint16_t nameLength;
    lib::LoadBytes(nameLength, data);
    return static_cast<const char*>(data) + nameLength + 2;
}

