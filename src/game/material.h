#pragma once

#include "engine_object.h"
#include "resource_manager.h"

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/vecmath.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// A Direct3D material (D3DMATERIAL7): diffuse, ambient, specular and emissive colours (r, g, b, a)
// and the specular power. 0x44 bytes; copied as dwords.
struct MaterialColors {
    Vector4 diffuse;
    Vector4 ambient;
    Vector4 specular;
    Vector4 emissive;
    float power;
};
static_assert(sizeof(MaterialColors) == 0x44);

// An element of the material's deque (16 bytes): an emissive colour kept while the object's light
// is on (CObject3DClientSpecific::SetLit pushes and pops them).
struct MaterialEntry {
    Vector4 emissive;
};
static_assert(sizeof(MaterialEntry) == 0x10);

// A material (class id 5, "CMaterial"; material.cpp), made by the material manager (the world's
// pointer, CMaterialManager::NewMaterial) and by the scene loader for its helpers (it constructs the array of
// them, FakeUnitScene::helpers in scene_loader.h; the file's vectors are the ambient, diffuse and specular
// colours, the power and the diffuse alpha). It is also the material a figure part is drawn with. Names
// provisional. A real virtual class: the compiler makes its vtable.
class CMaterial : public CEngineObject {
public:
    // The colour of a figure part lit on the GPU (its colour arrays; the shader lights it): white,
    // with the alpha 3DfpFPU's LightMesh gave every colour (the diffuse alpha times 255, rounded).
    std::uint32_t GpuLitColour() const;

    CMaterial();
    ~CMaterial() override;

    // Empty names, alpha 1, +0x90 / +0x98 0, the deque emptied; returns 1.
    int Create() override;
    // Empties the deque.
    void Destroy() override;
    // The name's text (short_functions.cpp)
    const char* GetName() override;
    const char* GetClassName() override;   // 11: "CMaterial"
    int GetClassId() override;

    std::string name;
    MaterialColors colors;
    stlp::deque<MaterialEntry> entries;
    std::string name2;                      // (the scene file's second name)
    int spare90;                            // 0 after Create; nothing reads it
    std::string name3;                      // (the scene file's third name)
    int spare98;                            // 0 after Create; nothing reads it
};

// The material manager (class id 0x27, "CMaterialManager", 0xD0 bytes; the world's pointer;
// material.cpp): a resource manager of CMaterial objects with a table of colours by key (filled by
// its Create, Rebirth-patched). A real virtual class: the compiler makes its vtable.
class CMaterialManager : public CResourceManager {
public:
    using ColorTable = stlp::hash_map<unsigned, MaterialColors, stlp::IntHash>;

    CMaterialManager();
    ~CMaterialManager() override;
    const char* GetClassName() override;     // 11: "CMaterialManager"
    int GetClassId() override;
    const char* GetFolderName() override;    // 25: "materials"

    // Rebirth-patched. CBaseManager's Create (0 when it fails); the archive "materials.res" of the text
    // folder added; then the colours of each of the eight kinds of kMaterialKindFiles (0x11 leather, wood,
    // metal, cloth, skin, stone, 0x17 magic, 0x19 metal again) entered under the kind: the file's colours
    // (after its name: a 16-bit length and the text), or, without the file, diffuse 0.3 (alpha 1), ambient
    // 0.7, specular 0.2, power 5 and the rest 0; kind 0x17's emissive colour is 1 either way. Returns 1.
    int Create() override;
    // A new CMaterial with type_ = `type`; null when the allocation fails.
    CEngineObject* NewObject(int type) override;
    // A material of `type` (slot 23 with id -1, flags_ |= 8 before the null check) given the table's colours
    // for its type_ (an absent key is entered first, its colours the key's bits in every dword).
    CMaterial* NewMaterial(int type);
    // The table's colours for `key` (entered as NewMaterial does when absent)
    // into `material`; 1, or 0 for a null material.
    int FillMaterial(CMaterial* material, unsigned key);

    ColorTable colors_;
};
static_assert(offsetof(CMaterialManager, colors_) == 0xBC && sizeof(CMaterialManager) == 0xD0);

// A kind the manager's Create enters and its file (material.cpp's kMaterialKindFiles).
struct MaterialKindFile {
    int kind;
    const char* file;
};
constexpr int kMagicMaterialKind = 0x17;
// The colours a material file holds: after its name (a 16-bit length, then the text).
inline const void* MaterialFileColors(const void* data) {
    std::uint16_t nameLength;
    std::memcpy(&nameLength, data, sizeof(nameLength));
    return static_cast<const char*>(data) + nameLength + 2;
}

