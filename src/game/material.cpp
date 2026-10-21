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

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr const char* kMaterialClassName = "CMaterial";
constexpr const char* kManagerClassName = "CMaterialManager";
constexpr const char* kManagerFolder = "materials";
constexpr std::size_t kColorTableHint = 100;
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

// --- CMaterial -------------------------------------------------------------------------------------

CMaterial::CMaterial() {
    ClearFields();
    entries.Construct();
    ResetFields(this);
    entries.PopAll();
}

CMaterial* CMaterial::Construct() { return new (this) CMaterial(); }
EI_REPLACE_METHOD(0x0068DBC0, CMaterial::Construct);

// The names go after the body (name3, name2, then name after the deque in the original).
CMaterial::~CMaterial() {
    entries.PopAll();
    entries.Destroy();
}

void CMaterial::Destruct() { this->CMaterial::~CMaterial(); }
EI_REPLACE_METHOD(0x0068DE30, CMaterial::Destruct);

CMaterial* CMaterial::DeletingDestruct(unsigned flags) {
    this->CMaterial::~CMaterial();
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

const char* CMaterial::GetName() { return name; }
const char* CMaterial::GetClassName() { return kMaterialClassName; }
int CMaterial::GetClassId() { return 5; }

const char* __fastcall MaterialClassName(CMaterial* self) { return self->CMaterial::GetClassName(); }
int __fastcall MaterialClassId(CMaterial* self) { return self->CMaterial::GetClassId(); }
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

CMaterialManager::CMaterialManager() { colors_.InitBuckets(kColorTableHint); }

CMaterialManager* CMaterialManager::Construct() { return new (this) CMaterialManager(); }
EI_REPLACE_METHOD(0x0068E3F0, CMaterialManager::Construct);

CMaterialManager::~CMaterialManager() { colors_.Destroy(); }

void CMaterialManager::Destruct() { this->CMaterialManager::~CMaterialManager(); }
EI_REPLACE_METHOD(0x0068E540, CMaterialManager::Destruct);

CMaterialManager* CMaterialManager::DeletingDestruct(unsigned flags) {
    this->CMaterialManager::~CMaterialManager();
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

CEngineObject* CMaterialManager::NewObjectImpl(int type) {
    void* block = gamecrt::operator_new(sizeof(CMaterial));
    if (!block) return nullptr;
    auto* material = new (block) CMaterial();
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

CEngineObject* CMaterialManager::NewObject(int type) { return NewObjectImpl(type); }
const char* CMaterialManager::GetClassName() { return kManagerClassName; }
int CMaterialManager::GetClassId() { return 0x27; }
const char* CMaterialManager::GetFolderName() { return kManagerFolder; }

const char* __fastcall MaterialManagerFolder(CMaterialManager* self) { return self->CMaterialManager::GetFolderName(); }
const char* __fastcall MaterialManagerClassName(CMaterialManager* self) { return self->CMaterialManager::GetClassName(); }
int __fastcall MaterialManagerClassId(CMaterialManager* self) { return self->CMaterialManager::GetClassId(); }
EI_REPLACE(0x0068E4F0, MaterialManagerFolder);
EI_REPLACE(0x0068E500, MaterialManagerClassName);
EI_REPLACE(0x0068E510, MaterialManagerClassId);

namespace {
int __fastcall MaterialManagerCreateHook(CMaterialManager* self) { return self->CMaterialManager::Create(); }
} // namespace
EI_REPLACE(0x0068E640, MaterialManagerCreateHook);
EI_REPLACE_METHOD(0x0068E900, CMaterialManager::NewObjectImpl);
