// The material (CMaterial) and the material manager (CMaterialManager): material.h. A-H24; see
// docs/notes/items_spells.md ("Materials").
#include "material.h"

#include "game_strings.h"
#include "mod_folders.h"
#include "world_data.h"   // TextPathString

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>
#include <new>

namespace {

constexpr std::uintptr_t kMaterialClassName = std::uintptr_t(0x00795A60);   // "CMaterial"
constexpr std::uintptr_t kManagerClassName = std::uintptr_t(0x00795ACC);    // "CMaterialManager"
constexpr std::uintptr_t kManagerFolder = std::uintptr_t(0x00795AC0);       // "materials"
constexpr std::size_t kColorTableHint = 100;

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

// --- CMaterial -------------------------------------------------------------------------------------

CMaterial* CMaterial::Construct() {
    ei::SetVtable(this, CEngineObject::kVtableAddress);
    ClearFields();
    new (&name) mfc::CString();
    entries.Construct();
    new (&name2) mfc::CString();
    new (&name3) mfc::CString();
    ei::SetVtable(this, kVtableAddress);
    ResetFields(this);
    entries.PopAll();
    return this;
}
EI_REPLACE_METHOD(0x0068DBC0, CMaterial::Construct);

void CMaterial::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    entries.PopAll();
    name3.~CString();
    name2.~CString();
    entries.Destroy();
    name.~CString();
    DestructBase();
}
EI_REPLACE_METHOD(0x0068DE30, CMaterial::Destruct);

CMaterial* CMaterial::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0068DD20, CMaterial::DeletingDestruct);

int CMaterial::Create() {
    ResetFields(this);
    entries.PopAll();
    return 1;
}

void CMaterial::Destroy() {
    entries.PopAll();
}

const char* __fastcall MaterialClassName(CMaterial*) { return reinterpret_cast<const char*>(kMaterialClassName); }
int __fastcall MaterialClassId(CMaterial*) { return 5; }
EI_REPLACE(0x0068DD00, MaterialClassName);
EI_REPLACE(0x0068DD10, MaterialClassId);

namespace {
int __fastcall MaterialCreateHook(CMaterial* self) { return self->CMaterial::Create(); }
void __fastcall MaterialDestroyHook(CMaterial* self) { self->CMaterial::Destroy(); }
} // namespace
EI_REPLACE(0x0068DF90, MaterialCreateHook);
EI_REPLACE(0x0068E040, MaterialDestroyHook);

// --- CMaterialManager ------------------------------------------------------------------------------

namespace {

// The table's entry for `key`: the table grown for one more entry, then the key's chain searched;
// an absent key is entered at the head of its bucket with the default pair the original builds (its
// key copied over every dword of the colours).
CMaterialManager::ColorTable::Node* EnterKey(CMaterialManager::ColorTable& table, unsigned key) {
    using Node = CMaterialManager::ColorTable::Node;
    table.Resize(table.Size() + 1);
    std::size_t bucket = key % table.BucketCount();
    for (Node* n = table.Bucket(bucket); n; n = n->next)
        if (n->key == key) return n;
    auto* node = static_cast<Node*>(stlp::allocate(sizeof(Node)));
    node->next = nullptr;
    node->key = key;
    std::uint32_t words[sizeof(MaterialColors) / 4];
    for (std::uint32_t& w : words) w = key;
    std::memcpy(&node->value, words, sizeof(words));
    table.LinkAtHead(bucket, node);
    return node;
}

} // namespace

CMaterialManager* CMaterialManager::Construct() {
    CResourceManager::Construct();
    colors_.InitBuckets(kColorTableHint);
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x0068E3F0, CMaterialManager::Construct);

void CMaterialManager::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    colors_.Destroy();
    CResourceManager::Destruct();
}
EI_REPLACE_METHOD(0x0068E540, CMaterialManager::Destruct);

CMaterialManager* CMaterialManager::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0068E520, CMaterialManager::DeletingDestruct);

int CMaterialManager::Create() {
    // Rebirth's ResOverride (0068E648): the mod's archives open here if nothing opened them yet.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::LoadModArchives();
    if (!CBaseManager::Create()) return 0;
    const mfc::CString folder(*static_cast<const mfc::CString*>(worlddata::TextPathString()));
    AddArchive(folder + "materials.res");

    const MaterialKindFile* const kinds = MaterialKindFileTable();
    for (int i = 0; i < kMaterialKindFiles; ++i) {
        MaterialColors colors;
        std::memset(&colors, 0, sizeof(colors));
        void* data = GetFile(kinds[i].file, nullptr);
        if (!data) {
            colors.diffuse = {0.3f, 0.3f, 0.3f, 1.0f};
            colors.ambient.x = colors.ambient.y = colors.ambient.z = 0.7f;
            colors.specular.x = colors.specular.y = colors.specular.z = 0.2f;
            colors.power = 5.0f;
        } else {
            std::memcpy(&colors, MaterialFileColors(data), sizeof(colors));
        }
        CloseFile(data);
        if (kinds[i].kind == kMagicMaterialKind) colors.emissive.x = colors.emissive.y = colors.emissive.z = 1.0f;
        colors_[static_cast<unsigned>(kinds[i].kind)] = colors;
    }
    return 1;
}

CEngineObject* CMaterialManager::NewObjectImpl(int type) {
    void* block = gamecrt::operator_new(sizeof(CMaterial));
    if (!block) return nullptr;
    CMaterial* material = static_cast<CMaterial*>(block)->Construct();
    if (!material) return nullptr;
    material->type_ = type;
    return material;
}

CMaterial* CMaterialManager::NewMaterial(int type) {
    auto* material = static_cast<CMaterial*>(CreateObject(type, -1));
    // The original sets the flag before its null check, so a failed creation faults there.
    if (!material) return nullptr;
    material->flags_ |= 8;
    material->colors = EnterKey(colors_, static_cast<unsigned>(material->type_))->value;
    return material;
}
EI_REPLACE_METHOD(0x0068E970, CMaterialManager::NewMaterial);

int CMaterialManager::FillMaterial(CMaterial* material, unsigned key) {
    if (!material) return 0;
    material->colors = EnterKey(colors_, key)->value;
    return 1;
}
EI_REPLACE_METHOD(0x0068EAF0, CMaterialManager::FillMaterial);

const char* __fastcall MaterialManagerFolder(CMaterialManager*) { return reinterpret_cast<const char*>(kManagerFolder); }
const char* __fastcall MaterialManagerClassName(CMaterialManager*) {
    return reinterpret_cast<const char*>(kManagerClassName);
}
int __fastcall MaterialManagerClassId(CMaterialManager*) { return 0x27; }
EI_REPLACE(0x0068E4F0, MaterialManagerFolder);
EI_REPLACE(0x0068E500, MaterialManagerClassName);
EI_REPLACE(0x0068E510, MaterialManagerClassId);

namespace {
int __fastcall MaterialManagerCreateHook(CMaterialManager* self) { return self->CMaterialManager::Create(); }
} // namespace
EI_REPLACE(0x0068E640, MaterialManagerCreateHook);
EI_REPLACE_METHOD(0x0068E900, CMaterialManager::NewObjectImpl);
