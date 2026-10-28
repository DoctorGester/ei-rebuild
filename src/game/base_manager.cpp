// CBaseManager and the object list iterator. See base_manager.h and docs/notes/core.md.
#include "base_manager.h"
#include "hash_tables_400.h"

#include "core_streams.h"

#include "lib/crt.h"

#include <list>
#include <type_traits>

#undef GetClassName

namespace {

using ObjectMap = CBaseManager::ObjectMap;
using SpareMap = CBaseManager::SpareMap;

// Id written after the last object by SaveObjects; an object with this id is followed by a 1 byte.
constexpr int kEndOfObjects = -0x521;
const char* OutOfIdsMessage() { return "CBaseManager::RegisterInstant.\n Can't find first free key.\n"; }

// --- Helpers ------------------------------------------------------------------------------------

// Create's reset of a table: emptied, its highest key zeroed, sized for `size` elements, the
// highest key zeroed again.
template <typename Map>
void ResetTable(Map& table, int& maxKey, std::size_t size) {
    table.clear();
    maxKey = 0;
    table.reserve(size);
    maxKey = 0;
}

// The objects table's walk as Destroy does it: from begin, until the end or an entry without an object.
template <typename F>
void ForEachObject(ObjectMap& objects, F f) {
    for (auto& [id, object] : objects) {
        if (!object) return;
        f(object);
    }
}

// CPtrList::AddTail: the new node.
std::list<CEngineObject*>::iterator AddTail(std::list<CEngineObject*>* list, CEngineObject* object) {
    list->push_back(object);
    return std::prev(list->end());
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
            int r = libcrt::Rand() << 15;
            r += libcrt::Rand();
            int candidate = r % range + low;
            if (candidate == 0) continue;
            if (!m->objects_.contains(static_cast<unsigned>(candidate))) {
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
    auto found = m->positions_.find(id);
    if (found == m->positions_.end()) return;
    std::list<CEngineObject*>::iterator node = found->second;
    m->positions_.erase(found);
    if (node != std::list<CEngineObject*>::iterator{}) m->objectList_.erase(node);
}

} // namespace

// --- Construction and destruction ---------------------------------------------------------------

CBaseManager::CBaseManager() {
    ClearFields();
    maxId_ = 0;
    maxPositionId_ = 0;
    maxSpareType_ = 0;
    maxSpareCountType_ = 0;
    idLow_ = 0;
    context_ = nullptr;
    idHigh_ = 2000000000;
    reservedA_ = -1;
    reservedB_ = -1;
    lastId_ = -1;
}

CBaseManager::~CBaseManager() {
    CBaseManager::Destroy();
    objectList_.clear();
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
    std::list<int> ids;
    ForEachObject(objects_, [&ids](CEngineObject* o) {
        ids.push_back(o->id_);
        o->Destroy();
    });
    for (const auto id : ids) {
        if (auto found = objects_.find(static_cast<unsigned>(id)); found != objects_.end()) found->second->Release();
    }
    ids.clear();
    objectList_.clear();
    ForEachObject(objects_, [](CEngineObject* o) { delete o; });
    objects_.clear();
    maxId_ = 0;
    positions_.clear();
    maxPositionId_ = 0;
    FreeSpares();
    spares_.clear();
    maxSpareType_ = 0;
    spareCounts_.clear();
    maxSpareCountType_ = 0;
    context_ = nullptr;
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
    objects_.clear();
    objectList_.clear();
    maxPositionId_ = 0;
    positions_.clear();
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
        if (auto found = spares_.find(type); found != spares_.end()) {
            std::list<CEngineObject*>* spares = found->second;
            if (!spares->empty()) {
                object = spares->front();
                spares->pop_front();
            }
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
        if (!objects_.contains(static_cast<unsigned>(chosen))) {   // Lookup
            hash400::Entry(positions_, maxPositionId_, chosen) = AddTail(&objectList_, object);
        }
        hash400::SetAt(objects_, maxId_, chosen, object);
        object->id_ = chosen;
        lastId_ = chosen;
    } else {
        if (!objects_.contains(static_cast<unsigned>(id))) {
            std::list<CEngineObject*>::iterator position = AddTail(&objectList_, object);
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
    auto node = objects_.find(id);
    return node != objects_.end() ? node->second : nullptr;
}

void CBaseManager::SetContext(CManagerContext* context) { context_ = context; }

CEngineObject* CBaseManager::Unregister(int id) {
    auto found = objects_.find(static_cast<unsigned>(id));
    if (found == objects_.end()) return nullptr;
    CEngineObject* object = found->second;
    if ((found = objects_.find(static_cast<unsigned>(id))) != objects_.end()) objects_.erase(found);
    ForgetPosition(this, id);
    return object;
}

int CBaseManager::RegisterInstant(CEngineObject* object) {
    int id = NextFreeId(this);
    if (!objects_.contains(static_cast<unsigned>(id))) {
        std::list<CEngineObject*>::iterator position = AddTail(&objectList_, object);
        if (id > maxPositionId_) maxPositionId_ = id;
        positions_[id] = position;
    }
    if (id > maxId_) maxId_ = id;
    objects_[static_cast<unsigned>(id)] = object;
    object->id_ = id;
    lastId_ = id;
    object->SetOwner(this);
    return id;
}

int CBaseManager::RemoveObject(CEngineObject* object) {
    int id = object->id_;
    int type = object->type_;
    auto found = objects_.find(static_cast<unsigned>(id));
    if (found != objects_.end() && found->second == object) {
        if (objects_.contains(static_cast<unsigned>(id))) {   // Lookup
            hash400::RemoveKey(objects_, static_cast<unsigned>(id));
            if (auto position = hash400::RemoveKey(positions_, id); position != std::list<CEngineObject*>::iterator{})
                objectList_.erase(position);
        }
    }
    if (auto spare = spares_.find(type); spare != spares_.end()) {
        AddTail(spare->second, object);
        object->id_ = -1;
        int count = hash400::Entry(spareCounts_, maxSpareCountType_, type) + 1;
        if (type > maxSpareCountType_) maxSpareCountType_ = type;
        spareCounts_[type] = count;
        return 0;
    }
    auto* list = new std::list<CEngineObject*>;
    if (type > maxSpareType_) maxSpareType_ = type;
    spares_[type] = list;
    AddTail(list, object);
    object->id_ = -1;
    if (type > maxSpareCountType_) maxSpareCountType_ = type;
    int count = spareCounts_[type] + 1;
    if (type > maxSpareCountType_) maxSpareCountType_ = type;
    spareCounts_[type] = count;
    return 0;
}

// Each type's list deletes its objects, is emptied and deleted; then the spares and spare counts tables are
// emptied.
void CBaseManager::FreeSpares() {
    for (auto& [type, list] : spares_) {
        if (!list) break;
        for (CEngineObject* object : *list) delete object;
        delete list;
    }
    maxSpareType_ = 0;
    spares_.clear();
    maxSpareCountType_ = 0;
    spareCounts_.clear();
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
    int count = static_cast<int>(objectList_.size());
    out->Write(&count, 4);
    for (CEngineObject* object : objectList_) {
        int value = object->id_;
        out->Write(&value, 4);
        value = object->type_;
        out->Write(&value, 4);
        value = static_cast<int>(object->prototypeId_);
        out->Write(&value, 4);
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
    for (CEngineObject* object : objectList_) {
        int id = object->id_;
        out->Write(&id, 4);
        if (object->id_ == kEndOfObjects) {
            char one = 1;
            out->Write(&one, 1);
        }
        object->Save(out);
    }
    int mark = kEndOfObjects;
    out->Write(&mark, 4);
    char zero = 0;
    out->Write(&zero, 1);
}

void CBaseManager::ReleaseUnused() {
    for (std::list<CEngineObject*>::iterator node = objectList_.begin(); node != objectList_.end();) {
        CEngineObject* object = *node;
        ++node;
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
    position = manager->objectList_.begin();
    return Next();
}

CEngineObject* ObjectListIterator::Next() {
    if (!manager || position == manager->objectList_.end()) return nullptr;
    return *position++;
}
