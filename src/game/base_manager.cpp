// CBaseManager and the object list iterator. See base_manager.h and docs/notes/core.md.
#include "base_manager.h"
#include "hash_tables_400.h"

#include "core_streams.h"

#include "lib/crt.h"
#include "lib/mfc_list.h"
#include "lib/mfc_slots.h"

#include <type_traits>

#undef GetClassName

namespace {

using ObjectMap = CBaseManager::ObjectMap;
using ObjectList = CBaseManager::ObjectList;
using ObjectNode = ObjectList::Node;
using PositionMap = CBaseManager::PositionMap;
using SpareMap = CBaseManager::SpareMap;
using SpareCountMap = CBaseManager::SpareCountMap;

using IdList = stlp::list<int>;
using IdNode = IdList::Node;

// Id written after the last object by SaveObjects; an object with this id is followed by a 1 byte.
constexpr int kEndOfObjects = -0x521;
const char* OutOfIdsMessage() { return "CBaseManager::RegisterInstant.\n Can't find first free key.\n"; }

// --- Helpers ------------------------------------------------------------------------------------

// The positions and spare counts tables built by hand (the hashtable constructor inlined). The game copies the
// three empty functor bytes from an uninitialised stack byte; they are never read, so they are zeroed here.
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

// The objects table's walk as Destroy does it: from begin, until the end or an entry without an object.
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

// The containers have no destructors (the destructor frees them), and their default constructors only
// null the hash maps' bucket vectors, which the constructor builds anyway.
static_assert(std::is_trivially_destructible_v<ObjectMap> && std::is_trivially_destructible_v<PositionMap> &&
              std::is_trivially_destructible_v<SpareMap> && std::is_trivially_destructible_v<SpareCountMap>);
static_assert(std::is_trivially_default_constructible_v<ObjectList> && std::is_trivially_destructible_v<ObjectList>);

CBaseManager::CBaseManager() {
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

CBaseManager::~CBaseManager() {
    CBaseManager::Destroy();
    spareCounts_.Destroy();
    spares_.Destroy();
    positions_.Destroy();
    objectList_.vtable = mfc::impl::TempListClass::Vtable();   // ~CPtrList
    objectList_.RemoveAll();
    objects_.Destroy();
}

// --- Create and destroy -------------------------------------------------------------------------

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

// Every object gets a reference, then its own Destroy; then every object still registered under the ids
// collected is released; the object list is emptied; every object left is deleted; then the tables are cleared.
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

BOOL CBaseManager::Init(CManagerContext* context) {
    context_ = context;
    if (Create()) return 1;
    context_ = nullptr;
    return 0;
}

// Every object gets a reference, then is destroyed, then released (objects nothing else holds go back to the
// spares). Then the objects table, the object list and the positions are emptied.
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

// --- No archives --------------------------------------------------------------------------------

BOOL CBaseManager::AddArchive(const char*) { return 0; }
BOOL CBaseManager::RemoveArchive(const char*) { return 0; }
BOOL CBaseManager::RemoveAllArchives() { return 0; }
void* CBaseManager::GetFile(const char*, std::uint32_t*) { return nullptr; }
bool CBaseManager::CloseFile(void*) { return false; }
BOOL CBaseManager::HasFile(const char*) { return 0; }
void CBaseManager::FreeAllFiles() {}

// --- Objects in and out -------------------------------------------------------------------------

CEngineObject* CBaseManager::CreateObject(int type, int id) {
    CEngineObject* object = nullptr;
    if (hash400::Entry(spareCounts_, maxSpareCountType_, type) > 0) {
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
        if (!objects_.Find(static_cast<unsigned>(chosen))) {   // Lookup
            objectList_.AddTail(object);
            ObjectNode* tail = objectList_.tail;
            hash400::Entry(positions_, maxPositionId_, chosen) = tail;
        }
        hash400::SetAt(objects_, maxId_, chosen, object);
        object->id_ = chosen;
        lastId_ = chosen;
    } else {
        if (!objects_.Find(static_cast<unsigned>(id))) {
            ObjectNode* position = AddTail(&objectList_, object);
            hash400::Entry(positions_, maxPositionId_, id) = position;
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

int CBaseManager::ReleaseObject(CEngineObject* object) {
    if (!object || object->refCount_ != 0) return 0;
    ForgetPosition(this, object->id_);
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

// --- Registration (non-virtual) -----------------------------------------------------------------

CEngineObject* CBaseManager::FindObject(unsigned id) const {
    auto* node = objects_.Find(id);
    return node ? node->value : nullptr;
}

void CBaseManager::SetContext(CManagerContext* context) { context_ = context; }

CEngineObject* CBaseManager::Unregister(int id) {
    ObjectMap::Node* found = objects_.Find(static_cast<unsigned>(id));
    if (!found) return nullptr;
    CEngineObject* object = found->value;
    if ((found = objects_.Find(static_cast<unsigned>(id))) != nullptr) objects_.Erase(found);
    ForgetPosition(this, id);
    return object;
}

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

int CBaseManager::RemoveObject(CEngineObject* object) {
    int id = object->id_;
    int type = object->type_;
    ObjectMap::Node* found = objects_.Find(static_cast<unsigned>(id));
    if (found && found->value == object) {
        if (objects_.Find(static_cast<unsigned>(id))) {   // Lookup
            hash400::RemoveKey(objects_, static_cast<unsigned>(id));
            if (auto* position = hash400::RemoveKey(positions_, id))
                objectList_.RemoveAt(position);
        }
    }
    if (SpareMap::Node* spare = spares_.Find(type)) {
        AddTail(spare->value, object);
        object->id_ = -1;
        int count = hash400::Entry(spareCounts_, maxSpareCountType_, type) + 1;
        if (type > maxSpareCountType_) maxSpareCountType_ = type;
        spareCounts_[type] = count;
        return 0;
    }
    auto* list = gamecrt::operator_new_as<ObjectList>();
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

// Each type's list deletes its objects, is emptied and deleted; then the spares and spare counts tables are
// emptied.
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
        reinterpret_cast<mfc::CObjectVirtuals*>(list)->DeletingDestructor(1u);
        node = spares_.After(node);
    }
    maxSpareType_ = 0;
    spares_.Clear();
    maxSpareCountType_ = 0;
    spareCounts_.Clear();
}

// --- Saving and loading -------------------------------------------------------------------------

// The spares are freed; then `count` records {id, type, prototypeId_} each create an object (CreateObject).
void CBaseManager::Load(CStream* in) {
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

void CBaseManager::Save(CStream* out) {
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

// Records {id, the object's Load data} until the end mark followed by a 0 byte (with a 1 byte the
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

// {id, [1 byte if the id is the end mark], the object's Save data} per object in list order, then
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

CEngineObject* ObjectListIterator::Next() {
    ObjectNode* node = position;
    if (!node) return nullptr;
    position = node->next;
    return node->data;
}
