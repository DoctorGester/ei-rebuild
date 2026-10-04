// The figure registry of a CFakeUnit scene (0044B980, called by LoadFakeUnitScene 00446C10): the
// scene's .reg file names, per material group (a mesh's face group named like a scene helper), the
// texture section (the helper's +0x8C name, matched case-insensitively against the file's
// sections), its "Main Texture Name" and the texture's "Dimentions"; a mapping section (X, Y,
// Width, Height) moves the group's texture coordinates into that rectangle of the texture (each
// coordinate once per mesh). Then the material type of the mesh's last group name (0044A2A0) is
// stored in the mesh (+0x184 / +0x188) and recorded, with the texture name, in the two caller
// arrays. Names provisional; see docs/notes/objects.md ("Scene loader").
#include "scene_loader.h"

#include "console.h"
#include "registry.h"

#include "../lib/crt.h"
#include "ei/hook.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace scene {
namespace {

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

// The unit's arrays: meshes (+0x544, 0x18C each) and helpers (+0x56C, 0x9C each).
constexpr std::size_t kMeshes = 0x544;
constexpr std::size_t kHelpers = 0x56C;
// Mesh fields used here (see scene_loader.h for the rest).
constexpr std::size_t kMaterialValue = 0x184;
constexpr std::size_t kMaterialType = 0x188;

// A CString passed by value: the caller copy-constructs it into the argument slot, the callee
// destroys it. Naked forwarders cannot take a class by value, so the slot is a raw pointer.
struct StringArg {
    char* data;
};
StringArg Copy(const mfc::CString& s) {
    StringArg arg;
    new (&arg) mfc::CString(s);
    return arg;
}
StringArg Copy(const char* text) {
    StringArg arg;
    new (&arg) mfc::CString(text);
    return arg;
}

// The registry, called through its game addresses (registry.cpp; the verify wrappers stay in the
// path). The string getter returns a CString by value, so it is a member function here.
struct Registry {
    CFileRegistry* Construct();                                                       // 00694520
    void Destroy();                                                                   // 00694540
    mfc::BOOL Open(const char* path);                                                 // 006945A0
    mfc::BOOL GetSectionNames(console::CStringArray* out);                            // 00694880
    int GetInt(const char* section, const char* key, int defaultValue);              // 00694BE0
    mfc::CString GetString(const char* section, const char* key, StringArg defaultValue);   // 00694D20
    void Close();                                                                     // 006946B0

    std::uint8_t storage[sizeof(CFileRegistry)];
};
__declspec(naked) CFileRegistry* Registry::Construct() EI_ORIGINAL(0x00694520)
__declspec(naked) void Registry::Destroy() EI_ORIGINAL(0x00694540)
__declspec(naked) mfc::BOOL Registry::Open(const char*) EI_ORIGINAL(0x006945A0)
__declspec(naked) mfc::BOOL Registry::GetSectionNames(console::CStringArray*) EI_ORIGINAL(0x00694880)
__declspec(naked) int Registry::GetInt(const char*, const char*, int) EI_ORIGINAL(0x00694BE0)
__declspec(naked) mfc::CString Registry::GetString(const char*, const char*, StringArg) EI_ORIGINAL(0x00694D20)
__declspec(naked) void Registry::Close() EI_ORIGINAL(0x006946B0)

// 00450750: the helper `index` of the unit's helper array (thiscall).
__declspec(naked) char* __fastcall HelperAt(VtArray*, void*, int /*index*/) EI_ORIGINAL(0x00450750)
// 00447F50: *out = the section of `sections` equal to `name` ignoring case, else `name` (consumed).
__declspec(naked) mfc::CString* __fastcall FindSection(mfc::CString* /*out*/, const console::CStringArray*, StringArg)
    EI_ORIGINAL(0x00447F50)
// 0044A2A0: the material type and value encoded in a group name; 1 when recognised.
__declspec(naked) int __fastcall ParseMaterial(const char* /*name*/, int* /*type*/, int* /*value*/) EI_ORIGINAL(0x0044A2A0)
// 00453A30: SetSize of the caller's (type, value) array (thiscall: size, growth or -1).
__declspec(naked) void __fastcall LinksSetSize(VtArray*, void*, int, int) EI_ORIGINAL(0x00453A30)
// The (type, name) array's elements: construct `count` at `p` (004553F0), destroy one (00454360,
// the deleting destructor with flags 0).
__declspec(naked) void __stdcall ConstructNamed(void* /*p*/, int /*count*/) EI_ORIGINAL(0x004553F0)
__declspec(naked) void __fastcall DestroyNamed(void*, void*, int /*flags*/) EI_ORIGINAL(0x00454360)

// The local CMap<DWORD, DWORD> of texture coordinates already moved (vtable 0073C16C): its
// GetAssocAt (00453C60), InitHashTable (00453B90) and NewAssoc (00453C00).
struct SeenMap {
    std::uintptr_t vtable;
    MapAssoc** table;
    std::uint32_t hashSize;
    int count;
    MapAssoc* freeList;
    mfc::CPlex* blocks;
    int blockSize;
};
static_assert(sizeof(SeenMap) == 0x1C);
__declspec(naked) MapAssoc* __fastcall MapGetAssocAt(SeenMap*, void*, const std::uint32_t*, std::uint32_t*)
    EI_ORIGINAL(0x00453C60)
__declspec(naked) void __fastcall MapInitHashTable(SeenMap*, void*, std::uint32_t, int) EI_ORIGINAL(0x00453B90)
__declspec(naked) MapAssoc* __fastcall MapNewAssoc(SeenMap*) EI_ORIGINAL(0x00453C00)

// map[key] as the original inlines it: the association (created with value 0 when missing).
MapAssoc* MapLookup(SeenMap& map, std::uint32_t key) {
    std::uint32_t hash;
    MapAssoc* assoc = MapGetAssocAt(&map, nullptr, &key, &hash);
    if (assoc) return assoc;
    if (!map.table) MapInitHashTable(&map, nullptr, map.hashSize, 1);
    assoc = MapNewAssoc(&map);
    assoc->hash = hash;
    assoc->key = key;
    assoc->next = map.table[hash];
    map.table[hash] = assoc;
    return assoc;
}

// map[key] = 1, the second inlined operator[] (its own bucket search, no GetAssocAt call).
void MapMark(SeenMap& map, std::uint32_t key) {
    std::uint32_t hash = (key >> 4) % map.hashSize;
    MapAssoc* assoc = nullptr;
    if (map.table) {
        for (MapAssoc* a = map.table[hash]; a; a = a->next) {
            if (a->key == key) {
                assoc = a;
                break;
            }
        }
    } else {
        MapInitHashTable(&map, nullptr, map.hashSize, 1);
    }
    if (!assoc) {
        assoc = MapNewAssoc(&map);
        assoc->hash = hash;
        assoc->key = key;
        assoc->next = map.table[hash];
        map.table[hash] = assoc;
    }
    assoc->value = 1;
}

// The map's destructor as inlined: RemoveAll (the table is deleted even when null) and the block
// chain freed.
void MapDestroy(SeenMap& map) {
    map.vtable = 0x0073C16C;
    gamecrt::operator_delete(map.table);
    map.table = nullptr;
    map.count = 0;
    map.freeList = nullptr;
    mfc::PlexFreeDataChain(map.blocks);
    map.blocks = nullptr;
}

// The texture rectangle a group's coordinates are mapped into: (x, y, w, h) of the mapping section
// and the texture's (width, height), as floats.
struct Mapping {
    float x, y, w, h;
    float width, height;
};

// u = (w / width) * u + x / width; v = 1 - ((h / height) * (1 - v) + y / height), each rounded once
// (the quotients and products stay on the FPU). __fastcall: the coordinate in ECX, the mapping in EDX.
__declspec(naked) void __fastcall MapTexCoord(float* /*uv*/, const Mapping* /*mapping*/) {
    __asm {
        push ecx
        fld dword ptr [edx + 8]
        fdiv dword ptr [edx + 10h]
        fmul dword ptr [ecx]
        fld dword ptr [edx]
        fdiv dword ptr [edx + 10h]
        faddp st(1), st
        fld dword ptr ds:[0073B84Ch]
        fsub dword ptr [ecx + 4]
        fld dword ptr [edx + 0Ch]
        fdiv dword ptr [edx + 14h]
        fmulp st(1), st
        fld dword ptr [edx + 4]
        fdiv dword ptr [edx + 14h]
        faddp st(1), st
        fstp dword ptr [esp]
        fstp dword ptr [ecx]
        fld dword ptr ds:[0073B84Ch]
        fsub dword ptr [esp]
        fstp dword ptr [ecx + 4]
        pop eax
        ret
    }
}

// FCOMP with -1.0 (0073B854), C3: equal (the values come from integers, never NaN).
bool IsMinusOne(float v) {
    std::uint32_t bits;
    std::memcpy(&bits, &v, 4);
    return bits == 0xBF800000u;
}

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// The (type, name) records of the caller's second array (8 bytes: the type, a CString).
struct NamedType {
    int type;
    mfc::CString name;
};
static_assert(sizeof(NamedType) == 8);

// CArray<NamedType>::SetSize as the original inlines it (elements constructed by 004553F0,
// destroyed by 00454360; storage from operator new).
void SetNamedSize(VtArray& a, int size) {
    ArrayBody& b = a.body;
    char* data = static_cast<char*>(b.data);
    if (size == 0) {
        if (data) {
            for (int i = 0; i < b.size; ++i) DestroyNamed(data + i * 8, nullptr, 0);
            gamecrt::operator_delete(data);
            b.data = nullptr;
        }
        b.max = 0;
        b.size = 0;
        return;
    }
    if (!data) {
        data = static_cast<char*>(gamecrt::operator_new(static_cast<std::size_t>(size) * 8));
        b.data = data;
        ConstructNamed(data, size);
        b.max = size;
        b.size = size;
        return;
    }
    if (size <= b.max) {
        if (b.size < size) {
            ConstructNamed(data + b.size * 8, size - b.size);
        } else if (b.size > size) {
            for (int i = size; i < b.size; ++i) DestroyNamed(data + i * 8, nullptr, 0);
        }
        b.size = size;
        return;
    }
    int grow = b.grow;
    if (grow == 0) {
        grow = b.size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 0x400) grow = 0x400;
    }
    int max = size < grow + b.max ? grow + b.max : size;
    char* fresh = static_cast<char*>(gamecrt::operator_new(static_cast<std::size_t>(max) * 8));
    std::memcpy(fresh, data, static_cast<std::size_t>(b.size) * 8);
    ConstructNamed(fresh + b.size * 8, size - b.size);
    gamecrt::operator_delete(data);
    b.data = fresh;
    b.size = size;
    b.max = max;
}

// The registry's values for one material group (the texture section and its mapping) and the
// group's faces' texture coordinates moved into the mapping, once each.
struct GroupStrings {
    mfc::CString& groupName;
    mfc::CString& mapping;
    mfc::CString& section;
};

void MapGroup(char* unit, Registry& reg, const console::CStringArray& sections, GroupStrings& s, char* meshes,
              std::size_t meshOffset, int group, int helper, SeenMap& seen) {
    char* helperRecord = HelperAt(&At<VtArray>(unit, kHelpers), nullptr, helper);
    alignas(mfc::CString) unsigned char found[sizeof(mfc::CString)];
    auto* foundString = reinterpret_cast<mfc::CString*>(found);
    FindSection(foundString, &sections, Copy(At<mfc::CString>(helperRecord, helper::kName2)));
    s.section = *foundString;
    foundString->~CString();
    s.mapping = reg.GetString(s.section, Text(0x007846DC), Copy(Text(0x007846F0)));   // "Main Texture Name", "none"

    Mapping m;
    m.width = static_cast<float>(reg.GetInt(Text(0x007846C8), Text(0x007846D4), 0x100));      // "Dimentions", "Width"
    m.height = static_cast<float>(reg.GetInt(Text(0x007846C8), Text(0x007846C0), 0x100));     // "Height"
    const char* mapping = s.mapping;
    if (reinterpret_cast<const int*>(mapping)[-2] == 0) {   // CStringData::nDataLength
        m.h = 0.0f;
        m.w = 0.0f;
        m.y = 0.0f;
        m.x = 0.0f;
    } else {
        m.x = static_cast<float>(reg.GetInt(s.mapping, Text(0x007846BC), -1));   // "X"
        m.y = static_cast<float>(reg.GetInt(s.mapping, Text(0x007846B8), -1));   // "Y"
        m.w = static_cast<float>(reg.GetInt(s.mapping, Text(0x007846D4), -1));   // "Width"
        m.h = static_cast<float>(reg.GetInt(s.mapping, Text(0x007846C0), -1));   // "Height"
        if (IsMinusOne(m.x) || IsMinusOne(m.y) || IsMinusOne(m.w) || IsMinusOne(m.h)) {
            mfc::CString message;
            message.Format(Text(0x0078466C), static_cast<const char*>(s.groupName), static_cast<const char*>(s.section));
            game::DebugLog(message);
            if (*reinterpret_cast<const std::uint32_t*>(0x007AF5C0) & 0x10000) {
                reinterpret_cast<int(__stdcall*)(const char*, unsigned, unsigned)>(0x0070AC94)(message, 0x30, 0);
            }
            m.h = 0.0f;
            m.w = 0.0f;
            m.y = 0.0f;
            m.x = 0.0f;
        }
    }

    for (int f = 0; f < At<FaceGroup*>(meshes + meshOffset, mesh::kGroups + 4)[group].faces.body.size; ++f) {
        const FaceGroup& g = At<FaceGroup*>(meshes + meshOffset, mesh::kGroups + 4)[group];
        std::uint32_t face = static_cast<const std::uint32_t*>(g.faces.body.data)[f];
        for (int k = 0; k < 3; ++k) {
            char* mesh = At<char*>(unit, kMeshes + 4) + meshOffset;
            std::uint32_t t = At<MeshFace*>(mesh, mesh::kFaces)[face].vertex2[k];
            if (MapLookup(seen, t)->value != 0) continue;
            MapTexCoord(At<float*>(mesh, mesh::kTexCoords) + t * 2, &m);
            MapMark(seen, t);
        }
    }
}

// The material type of the mesh's last group name (0044A2A0) into the mesh (+0x188 type, +0x184
// value) and, once each, into `links` (type, value) and `names` (type, texture name).
void RecordMaterial(char* unit, std::size_t meshOffset, const mfc::CString& groupName, const mfc::CString& mapping,
                    VtArray* links, VtArray* names) {
    mfc::CString texture;
    int type;
    int value;
    if (ParseMaterial(groupName, &type, &value) != 1) {
        game::DebugLog(Text(0x00784648));   // Undefined texture/material types.
        return;
    }
    int textureType = type;
    texture = mapping;
    At<int>(At<char*>(unit, kMeshes + 4) + meshOffset, kMaterialType) = type;
    At<int>(At<char*>(unit, kMeshes + 4) + meshOffset, kMaterialValue) = value;

    auto* pairs = static_cast<int*>(links->body.data);
    int count = links->body.size;
    int i = 0;
    for (; i < count; ++i) {
        if (pairs[i * 2 + 1] == value && pairs[i * 2] == type) break;
    }
    if (i == count) {
        LinksSetSize(links, nullptr, count + 1, -1);
        pairs = static_cast<int*>(links->body.data);
        pairs[count * 2] = type;
        pairs[count * 2 + 1] = value;
    }

    int j = 0;
    for (; j < names->body.size; ++j) {
        const NamedType& n = static_cast<const NamedType*>(names->body.data)[j];
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(n.name)),
                            reinterpret_cast<const unsigned char*>(static_cast<const char*>(texture))) == 0 &&
            n.type == textureType) {
            break;
        }
    }
    if (j == names->body.size) {
        int old = names->body.size;
        SetNamedSize(*names, old + 1);
        NamedType& n = static_cast<NamedType*>(names->body.data)[old];
        n.type = textureType;
        n.name = texture;
    }
}

} // namespace

// 0044B980 (__fastcall: the unit in ECX, the .reg path in EDX; RET 8). `links` collects (type,
// value) pairs, `names` (type, texture name) records, each once. Returns 1, 0 when the file has
// no sections, or what FatalError returns when it cannot be opened.
int __fastcall LoadFigureRegistry(char* unit, const mfc::CString* path, VtArray* links, VtArray* names) {
    mfc::CString groupName;
    mfc::CString mapping;
    mfc::CString section;
    console::CStringArray sections;
    Registry reg;
    reg.Construct();
    if (!reg.Open(*path)) {
        int result = game::FatalError(Text(0x007846F8), static_cast<const char*>(*path));   // Can't open .reg file
        reg.Destroy();
        return result;
    }
    if (!reg.GetSectionNames(&sections)) {
        reg.Destroy();
        return 0;
    }
    GroupStrings strings{groupName, mapping, section};

    for (int m = 0; m < At<int>(unit, kMeshes + 8); ++m) {
        SeenMap seen{0x0073C16C, nullptr, 17, 0, nullptr, nullptr, 10};
        std::size_t meshOffset = static_cast<std::size_t>(m) * mesh::kSize;
        for (int g = 0; g < At<int>(At<char*>(unit, kMeshes + 4) + meshOffset, mesh::kGroups + 8); ++g) {
            char* meshes = At<char*>(unit, kMeshes + 4);
            groupName = At<FaceGroup*>(meshes + meshOffset, mesh::kGroups + 4)[g].name;
            for (int h = 0; h < At<int>(unit, kHelpers + 8); ++h) {
                char* helperRecord = At<char*>(unit, kHelpers + 4) + h * helper::kSize;
                if (gamecrt::mbscmp(At<const unsigned char*>(helperRecord, helper::kName),
                                    reinterpret_cast<const unsigned char*>(static_cast<const char*>(groupName))) != 0) {
                    continue;
                }
                MapGroup(unit, reg, sections, strings, meshes, meshOffset, g, h, seen);
                break;
            }
        }

        RecordMaterial(unit, meshOffset, groupName, mapping, links, names);
        MapDestroy(seen);
    }

    reg.Close();
    reg.Destroy();
    return 1;
}
EI_REPLACE(0x0044B980, LoadFigureRegistry);

} // namespace scene
