// CBaseManager (00402B20-004048F0) and the object list iterator. See base_manager.h and
// docs/notes/core.md.
#include "base_manager.h"
#include "hash_tables_400.h"

#include "core_streams.h"

#include "ei/hook.h"
#include "ei/vcall.h"   // ei::SetVtable
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "lib/mfc_list.h"
#include "lib/mfc_slots.h"

#include <type_traits>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

using ObjectMap = CBaseManager::ObjectMap;
using ObjectList = CBaseManager::ObjectList;
using ObjectNode = ObjectList::Node;
using PositionMap = CBaseManager::PositionMap;
using SpareMap = CBaseManager::SpareMap;
using SpareCountMap = CBaseManager::SpareCountMap;

// Destroy's list<int> of ids: the sentinel node of a circular list.
using IdList = stlp::list<int>;
using IdNode = IdList::Node;

// The manager's object lists (objectList_ and the spare lists) are CTypedPtrLists with this vtable.
// Id written after the last object by SaveObjects; an object with this id is followed by a 1 byte.
constexpr int kEndOfObjects = -0x521;
// 00783C00: the fatal "out of ids" message ("CBaseManager::RegisterInstant ...").
const char* OutOfIdsMessage() { return "CBaseManager::RegisterInstant.\n Can't find first free key.\n"; }

// --- Helpers ------------------------------------------------------------------------------------

// The positions and spare counts tables are built by hand (the hashtable constructor inlined):
// functors, empty bucket vector, count 0, 100 buckets; then the table's highest key is zeroed. The
// original copies the three empty functor bytes from an uninitialised stack byte; they are never
// read, so they are zeroed here.
template <typename Map>
void BuildTable(Map& table, int& maxKey) {
    table.Construct(100);
    maxKey = 0;
}

// Create's reset of a table: emptied, its highest key zeroed, sized for `size` elements, the
// highest key zeroed again.
template <typename Map>
void ResetTable(Map& table, int& maxKey, std::size_t size) {
    table.Clear();
    maxKey = 0;
    table.Resize(size);
    maxKey = 0;
}

// The objects table's walk as Destroy does it: from begin, until the end or an entry without an
// object.
template <typename F>
void ForEachObject(ObjectMap& objects, F f) {
    for (ObjectMap::Node* node = objects.First(); node; node = objects.After(node)) {
        CEngineObject* object = node->value;
        if (!object) return;
        f(object);
    }
}

// CPtrList::AddTail as the manager's code inlines it, with the list's own NewNode.
ObjectNode* AddTail(ObjectList* list, CEngineObject* object) {
    ObjectNode* node = list->NewNode(list->tail, nullptr);
    node->data = object;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
    return node;
}

// The id tables' operator[] that keeps the highest key (00406AA0, hash_tables_400.cpp).
template <typename Map>
typename Map::Node* InsertKey(Map& table, int& maxKey, int key) {
    if (key > maxKey) maxKey = key;
    return table.FindOrInsert({static_cast<decltype(Map::Pair::key)>(key), {}});
}

// The id of an object registered without one: maxId_ + 1 while that stays inside [idLow_,
// idHigh_), else a random one in that range that `find` does not find (100000 tries; then the
// fatal "out of ids" message and -1).
int NextFreeId(CBaseManager* m) {
    int limit = m->idHigh_;
    int current = m->maxId_;
    int low = m->idLow_;
    int id;
    if (current < limit && current >= low) {
        id = current + 1;
    } else {
        int range = limit - low;
        id = -1;
        for (int tries = 0; tries < 100000; ++tries) {
            int r = gamecrt::rand() << 15;
            r += gamecrt::rand();
            int candidate = r % range + low;
            if (candidate == 0) continue;
            if (!m->objects_.Find(static_cast<unsigned>(candidate))) {
                id = candidate;
                break;
            }
        }
    }
    if (id == -1) game::FatalError(OutOfIdsMessage());
    return id;
}

// The positions entry of `id` (the id's node in the object list) erased and the node removed from
// the list (inlined RemoveAt).
void ForgetPosition(CBaseManager* m, int id) {
    PositionMap::Node* found = m->positions_.Find(id);
    if (!found) return;
    ObjectNode* node = found->value;
    m->positions_.Erase(found);
    if (node) m->objectList_.RemoveAt(node);
}

} // namespace

// --- Construction and destruction ---------------------------------------------------------------

// The containers have no destructors (the bodies free them), and their default constructors only
// null the hash maps' bucket vectors, which SetUpBaseManager builds anyway: the C++ constructor and
// destructor below build and free them as the bodies do.
static_assert(std::is_trivially_destructible_v<ObjectMap> && std::is_trivially_destructible_v<PositionMap> &&
              std::is_trivially_destructible_v<SpareMap> && std::is_trivially_destructible_v<SpareCountMap>);
static_assert(std::is_trivially_default_constructible_v<ObjectList> && std::is_trivially_destructible_v<ObjectList>);

CBaseManager* CBaseManager::ConstructBase() {
    ei::SetVtable(this, CEngineObject::kVtableAddress);   // the inlined CEngineObject constructor
    SetUpBaseManager();
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REWRITTEN(0x00402B20, CBaseManager::ConstructBase);

CBaseManager::CBaseManager() { SetUpBaseManager(); }

void CBaseManager::SetUpBaseManager() {
    ClearFields();
    objects_.Construct(100);
    maxId_ = 0;
    objectList_.vtable = mfc::impl::TempListClass::Vtable();   // CPtrList(10)
    objectList_.count = 0;
    objectList_.free = nullptr;
    objectList_.tail = nullptr;
    objectList_.head = nullptr;
    objectList_.blocks = nullptr;
    objectList_.blockSize = 10;
    BuildTable(positions_, maxPositionId_);
    spares_.Construct(100);
    maxSpareType_ = 0;
    BuildTable(spareCounts_, maxSpareCountType_);
    idLow_ = 0;
    context_ = nullptr;
    idHigh_ = 2000000000;
    reservedA_ = -1;
    reservedB_ = -1;
    lastId_ = -1;
}

void CBaseManager::DestructBody() {
    ei::SetVtable(this, kVtableAddress);
    TearDownBaseManager();
    DestructBase();
}
EI_REWRITTEN(0x00402DF0, CBaseManager::DestructBody);

CBaseManager::~CBaseManager() { TearDownBaseManager(); }

void CBaseManager::TearDownBaseManager() {
    CBaseManager::Destroy();
    spareCounts_.Destroy();
    spares_.Destroy();
    positions_.Destroy();
    objectList_.vtable = mfc::impl::TempListClass::Vtable();   // ~CPtrList
    objectList_.RemoveAll();
    objects_.Destroy();
}

CBaseManager* CBaseManager::DeletingDestruct(unsigned flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00402DD0, CBaseManager::DeletingDestruct);

// --- Slots 2, 3, 13, 15 -------------------------------------------------------------------------

int CBaseManager::Create() {
    idLow_ = 0;
    idHigh_ = 2000000000;
    ResetTable(objects_, maxId_, 0x400);
    ResetTable(positions_, maxPositionId_, 0x400);
    ResetTable(spares_, maxSpareType_, 0x100);
    ResetTable(spareCounts_, maxSpareCountType_, 0x100);
    reservedA_ = 0;
    reservedB_ = 300;
    return 1;
}

// Every object gets a reference, then its own Destroy (slot 3); then every object still registered
// under the ids collected is released (slot 4); the object list is emptied; every object left is
// deleted (slot 0); then the tables are cleared.
void CBaseManager::Destroy() {
    ForEachObject(objects_, [](CEngineObject* o) { ++o->refCount_; });
    IdList ids;
    ids.Construct();
    ForEachObject(objects_, [&ids](CEngineObject* o) {
        IdList::Insert(ids.End(), o->id_);
        o->Destroy();
    });
    for (IdNode* n = ids.First(); n != ids.End(); n = n->next) {
        if (ObjectMap::Node* found = objects_.Find(static_cast<unsigned>(n->data))) found->value->Release();
    }
    ids.Clear();
    objectList_.RemoveAll();
    ForEachObject(objects_, [](CEngineObject* o) { delete o; });
    objects_.Clear();
    maxId_ = 0;
    positions_.Clear();
    maxPositionId_ = 0;
    FreeSpares();
    spares_.Clear();
    maxSpareType_ = 0;
    spareCounts_.Clear();
    maxSpareCountType_ = 0;
    context_ = nullptr;
    ids.Release();
}

BOOL CBaseManager::InitImpl(void* context) {
    context_ = context;
    if (Create()) return 1;
    context_ = nullptr;
    return 0;
}

// Every object gets a reference, then is destroyed (slot 3), then released (slot 4; objects nothing
// else holds go back to the spares). Then the objects table, the object list and the positions are
// emptied.
void CBaseManager::DestroyAll() {
    ForEachObject(objects_, [](CEngineObject* o) { ++o->refCount_; });
    ForEachObject(objects_, [](CEngineObject* o) { o->Destroy(); });
    ForEachObject(objects_, [](CEngineObject* o) { o->Release(); });
    maxId_ = 0;
    objects_.Clear();
    objectList_.RemoveAll();
    maxPositionId_ = 0;
    positions_.Clear();
}

// --- Slots 16-22: no archives -------------------------------------------------------------------
// 18 (00402D80) and 22 (00402DC0) are shorter than the hook's jump and stay original.

BOOL CBaseManager::AddArchiveImpl(const char*) { return 0; }
BOOL CBaseManager::RemoveArchiveImpl(const char*) { return 0; }
BOOL CBaseManager::RemoveAllArchives() { return 0; }
void* CBaseManager::GetFileImpl(const char*, std::uint32_t*) { return nullptr; }
bool CBaseManager::CloseFileImpl(void*) { return false; }
BOOL CBaseManager::HasFileImpl(const char*) { return 0; }
void CBaseManager::FreeAllFiles() {}

// --- Slots 23, 24: objects in and out -----------------------------------------------------------

CEngineObject* CBaseManager::CreateObjectImpl(int type, int id) {
    CEngineObject* object = nullptr;
    if (InsertKey(spareCounts_, maxSpareCountType_, type)->value > 0) {
        if (SpareMap::Node* found = spares_.Find(type)) {
            ObjectList* spares = found->value;
            if (spares->count != 0) object = spares->RemoveHead();
        }
    }
    if (!object) {
        object = NewObject(type);
        if (!object) return nullptr;
    }
    object->flags_ = 0;
    object->type_ = type;
    if (id == -1) {
        int chosen = NextFreeId(this);
        if (!objects_.Find(static_cast<unsigned>(chosen))) {   // Lookup (00404F80)
            objectList_.AddTail(object);
            ObjectNode* tail = objectList_.tail;
            InsertKey(positions_, maxPositionId_, chosen)->value = tail;
        }
        InsertKey(objects_, maxId_, chosen)->value = object;   // SetAt (00404E80)
        object->id_ = chosen;
        lastId_ = chosen;
    } else {
        if (!objects_.Find(static_cast<unsigned>(id))) {
            ObjectNode* position = AddTail(&objectList_, object);
            InsertKey(positions_, maxPositionId_, id)->value = position;
        }
        if (id > maxId_) maxId_ = id;
        objects_[static_cast<unsigned>(id)] = object;
        object->id_ = id;
        lastId_ = id;
    }
    object->SetOwner(this);
    object->Create();
    return object;
}

int CBaseManager::ReleaseObjectImpl(CEngineObject* object) {
    if (!object || object->refCount_ != 0) return 0;
    ForgetPosition(this, object->id_);
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

// The slots whose bodies are registered by member pointer.
void CBaseManager::Load(CStream* stream) { LoadImpl(stream); }
void CBaseManager::Save(CStream* stream) { SaveImpl(stream); }
BOOL CBaseManager::Init(void* context) { return InitImpl(context); }
BOOL CBaseManager::AddArchive(const char* path) { return AddArchiveImpl(path); }
BOOL CBaseManager::RemoveArchive(const char* path) { return RemoveArchiveImpl(path); }
void* CBaseManager::GetFile(const char* name, std::uint32_t* size) { return GetFileImpl(name, size); }
bool CBaseManager::CloseFile(void* data) { return CloseFileImpl(data); }
BOOL CBaseManager::HasFile(const char* name) { return HasFileImpl(name); }
CEngineObject* CBaseManager::CreateObject(int type, int id) { return CreateObjectImpl(type, id); }
int CBaseManager::ReleaseObject(CEngineObject* object) { return ReleaseObjectImpl(object); }

namespace {
// The virtual slots' entry points (rewrite-guide rule 4).
int __fastcall CreateHook(CBaseManager* self) { return self->CBaseManager::Create(); }
void __fastcall DestroyHook(CBaseManager* self) { self->CBaseManager::Destroy(); }
void __fastcall DestroyAllHook(CBaseManager* self) { self->CBaseManager::DestroyAll(); }
} // namespace
EI_REPLACE(0x00403020, CreateHook);
EI_REPLACE(0x004031E0, DestroyHook);
// The compiler's jump thunks to it (functions.csv "thunk" rows; S16a: the standalone image has no
// code at them): the same replacement, so their calls skip its check.
EI_REPLACE(0x0066C100, DestroyHook);
EI_REPLACE(0x0067E160, DestroyHook);
EI_REPLACE_METHOD(0x004046E0, CBaseManager::LoadImpl);
EI_REPLACE_METHOD(0x00404760, CBaseManager::SaveImpl);
EI_REPLACE_METHOD(0x004031B0, CBaseManager::InitImpl);
EI_REPLACE(0x004034E0, DestroyAllHook);
EI_REPLACE_METHOD(0x00402D60, CBaseManager::AddArchiveImpl);
EI_REPLACE_METHOD(0x00402D70, CBaseManager::RemoveArchiveImpl);
EI_REPLACE_METHOD(0x00402D90, CBaseManager::GetFileImpl);
EI_REPLACE_METHOD(0x00402DA0, CBaseManager::CloseFileImpl);
EI_REPLACE_METHOD(0x00402DB0, CBaseManager::HasFileImpl);
EI_REPLACE_METHOD(0x00403730, CBaseManager::CreateObjectImpl);
EI_REPLACE_METHOD(0x00403AE0, CBaseManager::ReleaseObjectImpl);

// --- Registration (non-virtual) -----------------------------------------------------------------

CEngineObject* CBaseManager::FindObject(unsigned id) const {
    auto* node = objects_.Find(id);
    return node ? node->value : nullptr;
}
EI_REPLACE_METHOD(0x00403F40, CBaseManager::FindObject);

void CBaseManager::SetContext(void* context) { context_ = context; }
EI_REWRITTEN(0x00403720, CBaseManager::SetContext);

CEngineObject* CBaseManager::Unregister(int id) {
    ObjectMap::Node* found = objects_.Find(static_cast<unsigned>(id));
    if (!found) return nullptr;
    CEngineObject* object = found->value;
    if ((found = objects_.Find(static_cast<unsigned>(id))) != nullptr) objects_.Erase(found);
    ForgetPosition(this, id);
    return object;
}
EI_REWRITTEN(0x004039B0, CBaseManager::Unregister);

int CBaseManager::RegisterInstant(CEngineObject* object) {
    int id = NextFreeId(this);
    if (!objects_.Find(static_cast<unsigned>(id))) {
        ObjectNode* position = AddTail(&objectList_, object);
        if (id > maxPositionId_) maxPositionId_ = id;
        positions_[id] = position;
    }
    if (id > maxId_) maxId_ = id;
    objects_.FindOrInsert({static_cast<unsigned>(id), nullptr})->value = object;
    object->id_ = id;
    lastId_ = id;
    object->SetOwner(this);
    return id;
}
EI_REWRITTEN(0x00403BD0, CBaseManager::RegisterInstant);

int CBaseManager::RemoveObject(CEngineObject* object) {
    int id = object->id_;
    int type = object->type_;
    ObjectMap::Node* found = objects_.Find(static_cast<unsigned>(id));
    if (found && found->value == object) {
        if (objects_.Find(static_cast<unsigned>(id))) {   // Lookup (00404F80)
            hash400::ObjectsRemoveKey(objects_, static_cast<unsigned>(id));
            if (auto* position = hash400::PositionsRemoveKey(positions_, static_cast<unsigned>(id)))
                objectList_.RemoveAt(position);
        }
    }
    if (SpareMap::Node* spare = spares_.Find(type)) {
        AddTail(spare->value, object);
        object->id_ = -1;
        int count = InsertKey(spareCounts_, maxSpareCountType_, type)->value + 1;
        if (type > maxSpareCountType_) maxSpareCountType_ = type;
        spareCounts_[type] = count;
        return 0;
    }
    // The first spare of this type: a new list (CPtrList(10)).
    auto* list = static_cast<ObjectList*>(gamecrt::operator_new(sizeof(ObjectList)));
    if (list) {
        list->count = 0;
        list->free = nullptr;
        list->tail = nullptr;
        list->head = nullptr;
        list->blocks = nullptr;
        list->vtable = mfc::impl::TempListClass::Vtable();
        list->blockSize = 10;
    }
    if (type > maxSpareType_) maxSpareType_ = type;
    spares_[type] = list;
    AddTail(list, object);
    object->id_ = -1;
    if (type > maxSpareCountType_) maxSpareCountType_ = type;
    int count = spareCounts_[type] + 1;
    if (type > maxSpareCountType_) maxSpareCountType_ = type;
    spareCounts_.FindOrInsert({type, 0})->value = count;
    return 0;
}
EI_REWRITTEN(0x00404040, CBaseManager::RemoveObject);

// Each type's list deletes its objects (slot 0), is emptied (RemoveAll, inlined) and deleted (its
// slot 1); then the spares and spare counts tables are emptied.
void CBaseManager::FreeSpares() {
    SpareMap::Node* node = spares_.First();
    while (node) {
        ObjectList* list = node->value;
        if (!list) break;
        for (ObjectNode* n = list->head; n;) {
            ObjectNode* next = n->next;
            delete n->data;
            n = next;
        }
        list->RemoveAll();
        reinterpret_cast<mfc::CObjectVirtuals*>(list)->DeletingDestructor(1u);   // its CObject slot 1
        node = spares_.After(node);
    }
    maxSpareType_ = 0;
    spares_.Clear();
    maxSpareCountType_ = 0;
    spareCounts_.Clear();
}
EI_REWRITTEN(0x00404410, CBaseManager::FreeSpares);

// --- Saving and loading -------------------------------------------------------------------------

// The spares are freed; then `count` records {id, type, prototypeId_} each create an object (slot 23).
void CBaseManager::LoadImpl(CStream* in) {
    FreeSpares();
    int count;
    in->Read(&count, 4);
    for (int i = 0; i < count; ++i) {
        int id, type, extra;
        in->Read(&id, 4);
        in->Read(&type, 4);
        in->Read(&extra, 4);
        CEngineObject* object = CreateObject(type, id);
        object->prototypeId_ = static_cast<std::uint32_t>(extra);
    }
}

// The object list's count, then {id, type, prototypeId_} per object.
void CBaseManager::SaveImpl(CStream* out) {
    int count = objectList_.count;
    out->Write(&count, 4);
    for (ObjectNode* node = objectList_.head; node;) {
        ObjectNode* next = node->next;
        CEngineObject* object = node->data;
        int value = object->id_;
        out->Write(&value, 4);
        value = object->type_;
        out->Write(&value, 4);
        value = static_cast<int>(object->prototypeId_);
        out->Write(&value, 4);
        node = next;
    }
}

// Records {id, object data (slot 9)} until the end mark followed by a 0 byte (with a 1 byte the
// mark is an object id after all); stops at an id not registered (FindObject inlined).
void CBaseManager::LoadObjects(CStream* in) {
    for (;;) {
        int id;
        in->Read(&id, 4);
        if (id == kEndOfObjects) {
            char more;
            in->Read(&more, 1);
            if (!more) return;
        }
        CEngineObject* object = FindObject(static_cast<unsigned>(id));
        if (!object) return;
        object->Load(in);
    }
}
EI_REWRITTEN(0x004047E0, CBaseManager::LoadObjects);

// {id, [1 byte if the id is the end mark], object data (slot 10)} per object in list order, then
// the end mark and a 0 byte.
void CBaseManager::SaveObjects(CStream* out) {
    for (ObjectNode* node = objectList_.head; node;) {
        ObjectNode* next = node->next;
        CEngineObject* object = node->data;
        int id = object->id_;
        out->Write(&id, 4);
        if (object->id_ == kEndOfObjects) {
            char one = 1;
            out->Write(&one, 1);
        }
        object->Save(out);
        node = next;
    }
    int mark = kEndOfObjects;
    out->Write(&mark, 4);
    char zero = 0;
    out->Write(&zero, 1);
}
EI_REWRITTEN(0x00404860, CBaseManager::SaveObjects);

void CBaseManager::ReleaseUnused() {
    for (ObjectNode* node = objectList_.head; node;) {
        CEngineObject* object = node->data;
        node = node->next;
        if (object->refCount_ == 0) {
            object->refCount_ = 1;
            object->Release();
        }
    }
}
EI_REWRITTEN(0x004048F0, CBaseManager::ReleaseUnused);

// --- ObjectListIterator -------------------------------------------------------------------------

// The reference moves from the previous manager to `list`; returns the list's first object (the
// position then names the next node), or null.
CEngineObject* ObjectListIterator::First(CBaseManager* list) {
    if (manager) --manager->refCount_;
    manager = list;
    if (list) ++list->refCount_;
    ObjectNode* node = manager->objectList_.head;
    position = node;
    if (!node) return nullptr;
    position = node->next;
    return node->data;
}
EI_REWRITTEN(0x00404920, ObjectListIterator::First);

// The object at the position (moving it on), or null at the end.
CEngineObject* ObjectListIterator::Next() {
    ObjectNode* node = position;
    if (!node) return nullptr;
    position = node->next;
    return node->data;
}
EI_REWRITTEN(0x004BAA30, ObjectListIterator::Next);
EI_REWRITTEN(0x00404960, ObjectListIterator::Next);
