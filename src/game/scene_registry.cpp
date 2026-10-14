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
#include "originals_library.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace scene {
namespace {

// The unit's arrays: meshes (SceneMesh) and helpers (SceneHelper); FakeUnitScene, scene_loader.h.
SceneMesh& MeshAt(FakeUnitScene* unit, int index) { return Typed<MeshArray>(unit->meshes).data[index]; }

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
struct Registry : RegistryOriginals {
    CFileRegistry* Construct();                                                       // 00694520
    int GetInt(const char* section, const char* key, int defaultValue);              // 00694BE0
    void Close();                                                                     // 006946B0

    std::uint8_t storage[sizeof(CFileRegistry)];
};
__declspec(naked) CFileRegistry* Registry::Construct() EI_ORIGINAL(0x00694520)
__declspec(naked) int Registry::GetInt(const char*, const char*, int) EI_ORIGINAL(0x00694BE0)
__declspec(naked) void Registry::Close() EI_ORIGINAL(0x006946B0)

// 00450750: the helper `index` of the unit's helper array (thiscall).
__declspec(naked) SceneHelper* __fastcall HelperAt(VtArray*, void*, int /*index*/) EI_ORIGINAL(0x00450750)
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

// The local CMap<DWORD, DWORD> of texture coordinates already moved (vtable 0073C16C; the
// DwordMap layout): its GetAssocAt (00453C60), InitHashTable (00453B90) and NewAssoc (00453C00).
using SeenMap = DwordMap;
void* SeenMapVtable() { return reinterpret_cast<void*>(0x0073C16C); }
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
    map.vtable = SeenMapVtable();
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

// CArray<NamedType> (elements constructed by 004553F0, destroyed one by one by 00454360).
struct NamedElements {
    static void Construct(NamedType* elements, int count) { ConstructNamed(elements, count); }
    static void Destruct(NamedType* elements, int count) {
        for (int i = 0; i < count; ++i) DestroyNamed(&elements[i], nullptr, 0);
    }
};
using NamedArray = mfc::CArray<NamedType, NamedElements>;
void SetNamedSize(VtArray& a, int size) { Typed<NamedArray>(a).SetSize(size); }

// The registry's values for one material group (the texture section and its mapping) and the
// group's faces' texture coordinates moved into the mapping, once each.
struct GroupStrings {
    mfc::CString& groupName;
    mfc::CString& mapping;
    mfc::CString& section;
};

void MapGroup(FakeUnitScene* unit, Registry& reg, const console::CStringArray& sections, GroupStrings& s, int meshIndex,
              int group, int helper, SeenMap& seen) {
    SceneHelper* helperRecord = HelperAt(&unit->helpers, nullptr, helper);
    alignas(mfc::CString) unsigned char found[sizeof(mfc::CString)];
    auto* foundString = reinterpret_cast<mfc::CString*>(found);
    FindSection(foundString, &sections, Copy(helperRecord->name2));
    s.section = *foundString;
    foundString->~CString();
    s.mapping = reg.GetString(s.section, Text(0x007846DC), Copy(Text(0x007846F0)));   // "Main Texture Name", "none"

    Mapping m;
    m.width = static_cast<float>(reg.GetInt(Text(0x007846C8), Text(0x007846D4), 0x100));      // "Dimentions", "Width"
    m.height = static_cast<float>(reg.GetInt(Text(0x007846C8), Text(0x007846C0), 0x100));     // "Height"
    if (s.mapping.GetLength() == 0) {
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

    for (int f = 0; f < MeshAt(unit, meshIndex).groups.data[group].faces.size; ++f) {
        const FaceGroup& g = MeshAt(unit, meshIndex).groups.data[group];
        std::uint32_t face = g.faces.data[f];
        for (int k = 0; k < 3; ++k) {
            SceneMesh& mesh = MeshAt(unit, meshIndex);
            std::uint32_t t = mesh.faces.data[face].vertex2[k];
            if (MapLookup(seen, t)->value != 0) continue;
            MapTexCoord(reinterpret_cast<float*>(&mesh.texCoords.data[t]), &m);   // (u, v) as floats
            MapMark(seen, t);
        }
    }
}

// The material type of the mesh's last group name (0044A2A0) into the mesh (+0x188 type, +0x184
// value) and, once each, into `links` (type, value) and `names` (type, texture name).
void RecordMaterial(FakeUnitScene* unit, int meshIndex, const mfc::CString& groupName, const mfc::CString& mapping,
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
    MeshAt(unit, meshIndex).materialType = type;
    MeshAt(unit, meshIndex).materialValue = value;

    auto* pairs = reinterpret_cast<int*>(links->data);
    int count = links->size;
    int i = 0;
    for (; i < count; ++i) {
        if (pairs[i * 2 + 1] == value && pairs[i * 2] == type) break;
    }
    if (i == count) {
        LinksSetSize(links, nullptr, count + 1, -1);
        pairs = reinterpret_cast<int*>(links->data);
        pairs[count * 2] = type;
        pairs[count * 2 + 1] = value;
    }

    int j = 0;
    for (; j < names->size; ++j) {
        const NamedType& n = reinterpret_cast<const NamedType*>(names->data)[j];
        if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(n.name)),
                            reinterpret_cast<const unsigned char*>(static_cast<const char*>(texture))) == 0 &&
            n.type == textureType) {
            break;
        }
    }
    if (j == names->size) {
        int old = names->size;
        SetNamedSize(*names, old + 1);
        NamedType& n = reinterpret_cast<NamedType*>(names->data)[old];
        n.type = textureType;
        n.name = texture;
    }
}

} // namespace

// 0044B980 (__fastcall: the unit in ECX, the .reg path in EDX; RET 8). `links` collects (type,
// value) pairs, `names` (type, texture name) records, each once. Returns 1, 0 when the file has
// no sections, or what FatalError returns when it cannot be opened.
int __fastcall LoadFigureRegistry(FakeUnitScene* unit, const mfc::CString* path, VtArray* links, VtArray* names) {
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

    for (int m = 0; m < unit->meshes.size; ++m) {
        SeenMap seen{SeenMapVtable(), nullptr, 17, 0, nullptr, nullptr, 10};
        for (int g = 0; g < MeshAt(unit, m).groups.size; ++g) {
            groupName = MeshAt(unit, m).groups.data[g].name;
            for (int h = 0; h < unit->helpers.size; ++h) {
                const SceneHelper& helperRecord = Typed<HelperArray>(unit->helpers).data[h];
                if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(helperRecord.name)),
                                    reinterpret_cast<const unsigned char*>(static_cast<const char*>(groupName))) != 0) {
                    continue;
                }
                MapGroup(unit, reg, sections, strings, m, g, h, seen);
                break;
            }
        }

        RecordMaterial(unit, m, groupName, mapping, links, names);
        MapDestroy(seen);
    }

    reg.Close();
    reg.Destroy();
    return 1;
}
EI_REPLACE(0x0044B980, LoadFigureRegistry);

} // namespace scene
