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

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// A Direct3D material (D3DMATERIAL7): diffuse, ambient, specular and emissive colours (r, g, b, a)
// and the specular power. 0x44 bytes; copied as dwords.
struct MaterialColors {
    Vector4 diffuse;        // +0x00
    Vector4 ambient;        // +0x10
    Vector4 specular;       // +0x20
    Vector4 emissive;       // +0x30
    float power;            // +0x40
};
static_assert(sizeof(MaterialColors) == 0x44);

// An element of the material's deque (16 bytes): an emissive colour kept while the object's light
// is on (CObject3DClientSpecific::SetLit, 0051C9F0, pushes and pops them).
struct MaterialEntry {
    Vector4 emissive;
};
static_assert(sizeof(MaterialEntry) == 0x10);

// A material (class id 5, "CMaterial", 0x9C bytes; material.cpp), made by the material
// manager (*007AF740, CMaterialManager::NewMaterial) and by the scene loader for its helpers
// (00454E40 constructs the array of them, FakeUnitScene::helpers in scene_loader.h; the file's
// vectors are the ambient, diffuse and specular colours, the power and the diffuse alpha). It is
// also the material a figure part is drawn with (005A7A70). Names provisional. A real virtual
// class: the compiler makes its vtable (C19; game.exe's 00743BB4 is retired).
class CMaterial : public CEngineObject {
public:
    void SetDiffuse(const Vector3* value);
    void SetAmbient(const Vector3* value);
    void SetSpecular(const Vector3* value);

    // 0068DBC0. An engine object with empty names, alpha 1 and an empty deque (one node). The
    // colours stay as they were.
    CMaterial();
    // 0068DE30. The deque emptied and freed, the names, the engine object.
    ~CMaterial() override;
    // The deleting destructor (slot 0, 0068DD20) frees into the game's heap.
    static void operator delete(void* block) { gamecrt::operator_delete(block); }

    // 2: 0068DF90. Empty names, alpha 1, +0x90 / +0x98 0, the deque emptied; returns 1.
    int Create() override;
    // 3: 0068E040. Empties the deque.
    void Destroy() override;
    // 7: 0068DCF0 (short_functions.cpp). The name's text.
    const char* GetName() override;
    const char* GetClassName() override;   // 11: 0068DD00 "CMaterial"
    int GetClassId() override;             // 12: 0068DD10 (5)

    // The registrations of the constructor and destructors (C20 retires them): 0068DBC0 the
    // constructor in place (returns this), 0068DE30 the destructor, 0068DD20 (slot 0) the
    // destructor, then operator delete when bit 0 of `flags` is set (returns this).
    CMaterial* Construct();
    void Destruct();
    CMaterial* DeletingDestruct(unsigned flags);

    mfc::CString name;                      // +0x1C
    MaterialColors colors;                  // +0x20
    stlp::deque<MaterialEntry> entries;     // +0x64
    mfc::CString name2;                     // +0x8C (the scene file's second name)
    int spare90;                            // +0x90 0 after Create; nothing reads it
    mfc::CString name3;                     // +0x94 (the scene file's third name)
    int spare98;                            // +0x98 0 after Create; nothing reads it
};
static_assert(offsetof(CMaterial, name) == 0x1C && offsetof(CMaterial, colors) == 0x20 &&
              offsetof(CMaterial, entries) == 0x64 && offsetof(CMaterial, name2) == 0x8C &&
              offsetof(CMaterial, name3) == 0x94 && sizeof(CMaterial) == 0x9C);

// The material manager (vtable 00743C38, class id 0x27, "CMaterialManager", 0xD0 bytes; the world's
// *007AF740; material.cpp): a resource manager of CMaterial objects with a table of colours by key
// (filled by its Create, 0068E640, Rebirth-patched).
class CMaterialManager : public CResourceManager {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743C38);

    using ColorTable = stlp::hash_map<unsigned, MaterialColors, stlp::IntHash>;

    // 0068E3F0. CResourceManager's constructor body and an empty colour table (NextPrime(100) = 193 buckets).
    // Returns this.
    CMaterialManager* Construct();
    // 0068E540. The destructor body: the colour table freed, then CResourceManager's.
    void Destruct();
    // 0068E520. Slot 0.
    CMaterialManager* DeletingDestruct(unsigned flags);
    // 2: 0068E640 (A-H26; Rebirth-patched, hooked in the vanilla launch only). CBaseManager's
    // Create (0 when it fails); the archive "materials.res" of the text folder (007AF45C) added;
    // then the colours of each of the eight kinds of kMaterialKindFiles (0x11 leather, wood,
    // metal, cloth, skin, stone, 0x17 magic, 0x19 metal again) entered under the kind: the
    // file's colours (after its name: a 16-bit length and the text), or, without the file,
    // diffuse 0.3 (alpha 1), ambient 0.7, specular 0.2, power 5 and the rest 0; kind 0x17's
    // emissive colour is 1 either way. Returns 1.
    int Create() override;
    // 14: 0068E900. A new CMaterial with type_ = `type`; null when the allocation fails.
    CEngineObject* NewObject(int type) override;
    CEngineObject* NewObjectImpl(int type);   // body of the slot above (registered by member pointer)
    // 0068E970 (RET 4). A material of `type` (slot 23 with id -1, flags_ |= 8 before the null
    // check) given the table's colours for its type_ (an absent key is entered first, its colours
    // the key's bits in every dword).
    CMaterial* NewMaterial(int type);
    // 0068EAF0 (RET 8). The table's colours for `key` (entered as NewMaterial does when absent)
    // into `material`; 1, or 0 for a null material.
    int FillMaterial(CMaterial* material, unsigned key);

    ColorTable colors_;     // +0xBC
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

// 0068E4F0 "materials", 0068E500 "CMaterialManager", 0068E510 0x27 (slots 25, 11, 12).
const char* __fastcall MaterialManagerFolder(CMaterialManager* self);
const char* __fastcall MaterialManagerClassName(CMaterialManager* self);
int __fastcall MaterialManagerClassId(CMaterialManager* self);
const char* __fastcall MaterialClassName(CMaterial* self);
int __fastcall MaterialClassId(CMaterial* self);
