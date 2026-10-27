// The material (CMaterial) and the material manager (CMaterialManager): material.h. A-H24; see
// docs/notes/items_spells.md ("Materials").
#include "material.h"
#include "scene_loader.h"   // scene::InitHelper

#include "game_strings.h"
#include "mod_folders.h"
#include "world_data.h"   // TextPathString

#include "ei/hook.h"
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
EI_REWRITTEN_SPECIAL(0x0068DBC0, CMaterial::CMaterial);

// A fake unit's helper built in its array's storage (scene_loader.h's HelperElements).
void scene::InitHelper(CMaterial* p) { new (p) CMaterial(); }

// The names go after the body (name3, name2, then name after the deque in the original).
CMaterial::~CMaterial() {
    entries.PopAll();
    entries.Destroy();
}
EI_REWRITTEN_SPECIAL(0x0068DE30, CMaterial::~CMaterial);
EI_REWRITTEN_SPECIAL(0x0068DD20, CMaterial::~CMaterial);   // deleting

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

EI_REWRITTEN(0x0068DD00, CMaterial::GetClassName);
EI_REWRITTEN(0x0068DD10, CMaterial::GetClassId);

EI_REWRITTEN(0x0068DF90, CMaterial::Create);
EI_REWRITTEN(0x0068E040, CMaterial::Destroy);

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
    auto* node = stlp::allocate_as<Node>();
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
EI_REWRITTEN_SPECIAL(0x0068E3F0, CMaterialManager::CMaterialManager);

CMaterialManager::~CMaterialManager() { colors_.Destroy(); }
EI_REWRITTEN_SPECIAL(0x0068E540, CMaterialManager::~CMaterialManager);
EI_REWRITTEN_SPECIAL(0x0068E520, CMaterialManager::~CMaterialManager);

int CMaterialManager::Create() {
    // Rebirth's ResOverride: the mod's archives open here if nothing opened them yet.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) modfolders::LoadModArchives();
    if (!CBaseManager::Create()) return 0;
    const mfc::CString folder(TheTextFolder());
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

CEngineObject* CMaterialManager::NewObject(int type) {
    CMaterial* block = gamecrt::operator_new_as<CMaterial>();
    if (!block) return nullptr;
    auto* material = new (block) CMaterial();
    material->type_ = type;
    return material;
}

CMaterial* CMaterialManager::NewMaterial(int type) {
    auto* material = CreateObjectAs<CMaterial>(type, -1);
    // The original sets the flag before its null check, so a failed creation faults there.
    if (!material) return nullptr;
    material->flags_ |= 8;
    material->colors = EnterKey(colors_, static_cast<unsigned>(material->type_))->value;
    return material;
}
EI_REWRITTEN(0x0068E970, CMaterialManager::NewMaterial);

int CMaterialManager::FillMaterial(CMaterial* material, unsigned key) {
    if (!material) return 0;
    material->colors = EnterKey(colors_, key)->value;
    return 1;
}
EI_REWRITTEN(0x0068EAF0, CMaterialManager::FillMaterial);

const char* CMaterialManager::GetClassName() { return kManagerClassName; }
int CMaterialManager::GetClassId() { return 0x27; }
const char* CMaterialManager::GetFolderName() { return kManagerFolder; }

EI_REWRITTEN(0x0068E4F0, CMaterialManager::GetFolderName);
EI_REWRITTEN(0x0068E500, CMaterialManager::GetClassName);
EI_REWRITTEN(0x0068E510, CMaterialManager::GetClassId);

EI_REWRITTEN(0x0068E640, CMaterialManager::Create);
EI_REWRITTEN(0x0068E900, CMaterialManager::NewObject);
