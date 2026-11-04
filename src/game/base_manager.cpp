// BaseManager and the object list iterator. See base_manager.h and docs/notes/core.md.
#include "base_manager.h"

#include "core_streams.h"
#include "hash_tables_400.h"

#include "lib/crt.h"

#include <list>
#include <type_traits>

namespace {

using ObjectMap = BaseManager::ObjectMap;
using SpareMap = BaseManager::SpareMap;

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

// Appends to the list: the new node.
std::list<EngineObject*>::iterator AddTail(std::list<EngineObject*>* list, EngineObject* object) {
    list->push_back(object);
    return std::prev(list->end());
}

// The id of an object registered without one: maxId + 1 while that stays inside [idLow,
// idHigh), else a random one in that range that `find` does not find (100000 tries; then the
// fatal "out of ids" message and -1).
int NextFreeId(BaseManager* m) {
    int limit = m->idHigh;
    int current = m->maxId;
    int low = m->idLow;
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
            if (!m->objects.contains(static_cast<unsigned>(candidate))) {
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
void ForgetPosition(BaseManager* m, int id) {
    auto found = m->positions.find(id);
    if (found == m->positions.end()) return;
    std::list<EngineObject*>::iterator node = found->second;
    m->positions.erase(found);
    if (node != std::list<EngineObject*>::iterator{}) m->objectList.erase(node);
}

} // namespace

// --- Construction and destruction ---------------------------------------------------------------

BaseManager::BaseManager() {
    maxId = 0;
    maxPositionId = 0;
    maxSpareType = 0;
    maxSpareCountType = 0;
    idLow = 0;
    context = nullptr;
    idHigh = 2000000000;
    reservedA = -1;
    reservedB = -1;
    lastId = -1;
}

BaseManager::~BaseManager() {
    BaseManager::Destroy();
    objectList.clear();
}

// --- Create and destroy -------------------------------------------------------------------------

void BaseManager::Create() {
    idLow = 0;
    idHigh = 2000000000;
    ResetTable(objects, maxId, 0x400);
    ResetTable(positions, maxPositionId, 0x400);
    ResetTable(spares, maxSpareType, 0x100);
    ResetTable(spareCounts, maxSpareCountType, 0x100);
    reservedA = 0;
    reservedB = 300;
}

// Every object gets a reference, then its own Destroy; then every object still registered under the ids
// collected is released; the object list is emptied; every object left is deleted; then the tables are cleared.
void BaseManager::Destroy() {
    ForEachObject(objects, [](EngineObject* o) { o->AddRef(); });
    std::vector<int> ids;
    ForEachObject(objects, [&ids](EngineObject* o) {
        ids.push_back(o->id);
        o->Destroy();
    });
    for (const auto objectId : ids) {
        if (auto found = objects.find(static_cast<unsigned>(objectId)); found != objects.end()) found->second->Release();
    }
    ids.clear();
    objectList.clear();
    ForEachObject(objects, [](EngineObject* o) { delete o; });
    objects.clear();
    maxId = 0;
    positions.clear();
    maxPositionId = 0;
    FreeSpares();
    spares.clear();
    maxSpareType = 0;
    spareCounts.clear();
    maxSpareCountType = 0;
    context = nullptr;
}

void BaseManager::Init(ManagerContext* newContext) {
    context = newContext;
    Create();
}

// Every object gets a reference, then is destroyed, then released (objects nothing else holds go back to the
// spares). Then the objects table, the object list and the positions are emptied.
void BaseManager::DestroyAll() {
    ForEachObject(objects, [](EngineObject* o) { o->AddRef(); });
    ForEachObject(objects, [](EngineObject* o) { o->Destroy(); });
    ForEachObject(objects, [](EngineObject* o) { o->Release(); });
    maxId = 0;
    objects.clear();
    objectList.clear();
    maxPositionId = 0;
    positions.clear();
}

// --- No archives --------------------------------------------------------------------------------

bool BaseManager::AddArchive(const std::string&) { return false; }
bool BaseManager::RemoveArchive(const std::string&) { return false; }
bool BaseManager::RemoveAllArchives() { return false; }
void* BaseManager::GetFile(std::string_view, std::uint32_t*) { return nullptr; }
bool BaseManager::CloseFile(void*) { return false; }
bool BaseManager::HasFile(std::string_view) { return false; }
void BaseManager::FreeAllFiles() {}

// --- Objects in and out -------------------------------------------------------------------------

EngineObject* BaseManager::CreateObject(int newType, int newId) {
    EngineObject* object = nullptr;
    if (hash400::Entry(spareCounts, maxSpareCountType, newType) > 0) {
        if (auto found = spares.find(newType); found != spares.end() && !found->second.empty()) {
            object = found->second.front();
            found->second.pop_front();
        }
    }
    if (!object) {
        object = NewObject(newType);
        if (!object) return nullptr;
    }
    object->flags = 0;
    object->type = newType;
    if (newId == -1) {
        int chosen = NextFreeId(this);
        if (!objects.contains(static_cast<unsigned>(chosen))) {    // Lookup
            hash400::Entry(positions, maxPositionId, chosen) = AddTail(&objectList, object);
        }
        hash400::SetAt(objects, maxId, chosen, object);
        object->id = chosen;
        lastId = chosen;
    } else {
        if (!objects.contains(static_cast<unsigned>(newId))) {
            std::list<EngineObject*>::iterator position = AddTail(&objectList, object);
            hash400::Entry(positions, maxPositionId, newId) = position;
        }
        if (newId > maxId) maxId = newId;
        objects[static_cast<unsigned>(newId)] = object;
        object->id = newId;
        lastId = newId;
    }
    object->SetOwner(this);
    object->Create();
    return object;
}

int BaseManager::ReleaseObject(EngineObject* object) {
    if (!object || object->refCount != 0) return 0;
    ForgetPosition(this, object->id);
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

// --- Registration (non-virtual) -----------------------------------------------------------------

EngineObject* BaseManager::FindObject(unsigned objectId) const {
    auto node = objects.find(objectId);
    return node != objects.end() ? node->second : nullptr;
}

void BaseManager::SetContext(ManagerContext* newContext) { context = newContext; }

EngineObject* BaseManager::Unregister(int objectId) {
    auto found = objects.find(static_cast<unsigned>(objectId));
    if (found == objects.end()) return nullptr;
    EngineObject* object = found->second;
    if ((found = objects.find(static_cast<unsigned>(objectId))) != objects.end()) objects.erase(found);
    ForgetPosition(this, objectId);
    return object;
}

int BaseManager::RegisterInstant(EngineObject* object) {
    int objectId = NextFreeId(this);
    if (!objects.contains(static_cast<unsigned>(objectId))) {
        std::list<EngineObject*>::iterator position = AddTail(&objectList, object);
        if (objectId > maxPositionId) maxPositionId = objectId;
        positions[objectId] = position;
    }
    if (objectId > maxId) maxId = objectId;
    objects[static_cast<unsigned>(objectId)] = object;
    object->id = objectId;
    lastId = objectId;
    object->SetOwner(this);
    return objectId;
}

int BaseManager::RemoveObject(EngineObject* object) {
    int objectId = object->id;
    int objectType = object->type;
    auto found = objects.find(static_cast<unsigned>(objectId));
    if (found != objects.end() && found->second == object) {
        if (objects.contains(static_cast<unsigned>(objectId))) {    // Lookup
            hash400::RemoveKey(objects, static_cast<unsigned>(objectId));
            if (auto position = hash400::RemoveKey(positions, objectId); position != std::list<EngineObject*>::iterator{})
                objectList.erase(position);
        }
    }
    hash400::Entry(spares, maxSpareType, objectType).push_back(object);
    object->id = -1;
    int count = hash400::Entry(spareCounts, maxSpareCountType, objectType) + 1;
    spareCounts[objectType] = count;
    return 0;
}

// Each type's spares are deleted; then the spares and spare counts tables are emptied.
void BaseManager::FreeSpares() {
    for (auto& [objectType, list] : spares)
        for (EngineObject* object : list) delete object;
    maxSpareType = 0;
    spares.clear();
    maxSpareCountType = 0;
    spareCounts.clear();
}

// --- Saving and loading -------------------------------------------------------------------------

// The spares are freed; then `count` records {id, type, prototypeId} each create an object (CreateObject).
void BaseManager::Load(ByteStream* in) {
    FreeSpares();
    int count;
    in->ReadValue(count);
    for (int i = 0; i < count; ++i) {
        int objectId, objectType, extra;
        in->ReadValue(objectId);
        in->ReadValue(objectType);
        in->ReadValue(extra);
        EngineObject* object = CreateObject(objectType, objectId);
        object->prototypeId = static_cast<std::uint32_t>(extra);
    }
}

void BaseManager::Save(ByteStream* out) {
    int count = static_cast<int>(objectList.size());
    out->WriteValue(count);
    for (EngineObject* object : objectList) {
        int value = object->id;
        out->WriteValue(value);
        value = object->type;
        out->WriteValue(value);
        value = static_cast<int>(object->prototypeId);
        out->WriteValue(value);
    }
}

// Records {id, the object's Load data} until the end mark followed by a 0 byte (with a 1 byte the
// mark is an object id after all); stops at an id not registered (FindObject inlined).
void BaseManager::LoadObjects(ByteStream* in) {
    for (;;) {
        int objectId;
        in->ReadValue(objectId);
        if (objectId == kEndOfObjects) {
            char more;
            in->ReadValue(more);
            if (!more) return;
        }
        EngineObject* object = FindObject(static_cast<unsigned>(objectId));
        if (!object) return;
        object->Load(in);
    }
}

// {id, [1 byte if the id is the end mark], the object's Save data} per object in list order, then
// the end mark and a 0 byte.
void BaseManager::SaveObjects(ByteStream* out) {
    for (EngineObject* object : objectList) {
        int objectId = object->id;
        out->WriteValue(objectId);
        if (object->id == kEndOfObjects) {
            char one = 1;
            out->WriteValue(one);
        }
        object->Save(out);
    }
    int mark = kEndOfObjects;
    out->WriteValue(mark);
    char zero = 0;
    out->WriteValue(zero);
}

void BaseManager::ReleaseUnused() {
    for (std::list<EngineObject*>::iterator node = objectList.begin(); node != objectList.end();) {
        EngineObject* object = *node;
        ++node;
        if (object->refCount == 0) {
            object->refCount = 1;
            object->Release();
        }
    }
}

// --- ObjectListIterator -------------------------------------------------------------------------

// The reference moves from the previous manager to `list`; returns the list's first object (the
// position then names the next node), or null.
EngineObject* ObjectListIterator::First(BaseManager* list) {
    if (manager) manager->ForgetReference();
    manager = list;
    if (list) list->AddRef();
    position = manager->objectList.begin();
    return Next();
}

EngineObject* ObjectListIterator::Next() {
    if (!manager || position == manager->objectList.end()) return nullptr;
    return *position++;
}
