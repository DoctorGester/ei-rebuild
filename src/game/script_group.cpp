// Script object references and groups (sets of object references). See script.h.
#include "script.h"
#include "unit.h"
#include "combat.h"
#include "world.h"

#include "game_singletons.h"

#include <algorithm>
#include <cstdint>

namespace {

// The server world's object by id.
EngineObject* FindObject(int id) { return g_serverWorld.FindObject(static_cast<unsigned>(id)); }


// The members' order (the game's qsort by object address, unsigned; ours ScriptGroup::After, by id). The live
// objects' ids are unique, so only copies of one reference are equal and any sort gives the game's qsort's order.
bool MemberBefore(const ScriptObjectRef& a, const ScriptObjectRef& b) { return ScriptGroup::After(b.object, a.object); }

} // namespace

// ---- Member order -------------------------------------------------------------------------

bool ScriptGroup::After(const EngineObject* x, const EngineObject* y) {
    if (!x || !y) return x != nullptr;
    return x->id > y->id;
}

CUnit* ScriptObjectRef::AsUnit() const {
    return UnitOrNull(object);
}

UnitServer* ScriptObjectRef::GetUnitPerception() const {
    CUnit* unit = UnitOrNull(object);
    return unit ? unit->ServerController() : nullptr;
}

void ScriptObjectRef::ReleaseIfDeleted() {
    if (object && (object->flags & MapObjectFlag::Removed)) object = nullptr;
}

void ScriptObjectRef::Load(ByteStream* stream) {
    int id;
    stream->ReadValue(id);
    object = id == -1 ? nullptr : FindObject(id);
}

void ScriptObjectRef::Save(ByteStream* stream) const {
    int id = -1;
    if (object) id = object->id;
    stream->WriteValue(id);
}

// The merges walk both groups in member order (After) and write the result from the front of `this`, which is
// sized first. Each source element is read before the destination slot is written (it matters only if `this`
// is a or b).
void ScriptGroup::Union(const ScriptGroup& a, const ScriptGroup& b) {
    const std::size_t aSize = a.members.size(), bSize = b.members.size();
    members.resize(aSize + bSize);
    std::size_t out = 0, i = 0, j = 0;
    while (i < aSize) {
        if (j >= bSize) {
            while (i < aSize) {
                EngineObject* x = a.members[i++].object;
                members[out++].object = x;
            }
            break;
        }
        EngineObject* x = a.members[i].object;
        EngineObject* y = b.members[j].object;
        if (x == y) {
            members[out++].object = x;
            ++i;
            ++j;
        } else if (After(x, y)) {
            members[out++].object = y;
            ++j;
        } else {
            members[out++].object = x;
            ++i;
        }
    }
    while (j < bSize) {
        EngineObject* y = b.members[j++].object;
        members[out++].object = y;
    }
    members.resize(out);
}

void ScriptGroup::Difference(const ScriptGroup& a, const ScriptGroup& b) {
    const std::size_t aSize = a.members.size(), bSize = b.members.size();
    members.resize(aSize);
    std::size_t out = 0, i = 0, j = 0;
    while (i < aSize) {
        if (j >= bSize) {
            while (i < aSize) {
                EngineObject* x = a.members[i++].object;
                members[out++].object = x;
            }
            break;
        }
        EngineObject* x = a.members[i].object;
        EngineObject* y = b.members[j].object;
        if (x == y) {
            ++i;
            ++j;
        } else if (After(x, y)) {
            ++j;
        } else {
            members[out++].object = x;
            ++i;
        }
    }
    members.resize(out);
}

void ScriptGroup::Intersection(const ScriptGroup& a, const ScriptGroup& b) {
    const std::size_t aSize = a.members.size(), bSize = b.members.size();
    members.resize(aSize > bSize ? aSize : bSize);
    std::size_t out = 0, i = 0, j = 0;
    while (i < aSize && j < bSize) {
        EngineObject* x = a.members[i].object;
        EngineObject* y = b.members[j].object;
        if (x == y) {
            members[out++].object = x;
            ++i;
            ++j;
        } else if (After(x, y)) {
            ++j;
        } else {
            ++i;
        }
    }
    members.resize(out);
}

void ScriptGroup::Sort() { std::sort(members.begin(), members.end(), MemberBefore); }

void ScriptGroup::Remove(EngineObject* object) {
    auto at = std::find_if(members.begin(), members.end(), [object](const ScriptObjectRef& ref) { return ref.object == object; });
    if (at != members.end()) members.erase(at);
}

// A binary search for the place in member order; nothing when the object is there.
void ScriptGroup::Insert(EngineObject* object) {
    if (!object) return;
    int low = 0;
    int high = static_cast<int>(size()) - 1;
    while (high - low >= 0) {
        int middle = (high + low) / 2;
        EngineObject* current = members[middle].object;
        if (object == current) return;
        if (After(object, current)) low = middle + 1;
        else high = middle - 1;
    }
    members.insert(members.begin() + low, ScriptObjectRef{object});
}

void ScriptGroup::RemoveDeleted() {
    std::size_t i = 0;
    while (i < size()) {
        ScriptObjectRef& slot = members[i];
        slot.ReleaseIfDeleted();
        if (slot.object) ++i;
        else members.erase(members.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void ScriptGroup::InsertAll(const std::vector<HeldRef<CUnit>>* list) {
    for (CUnit* unit : *list) Insert(unit);
}

void ScriptGroup::CopyToList(std::vector<HeldRef<CUnit>>* list) const {
    list->clear();
    for (const ScriptObjectRef& ref : *this) list->push_back(static_cast<CUnit*>(ref.object.Get()));
}

bool ScriptGroup::Contains(EngineObject* object) const {
    for (const ScriptObjectRef& ref : *this)
        if (ref.object == object) return true;
    return false;
}

void ScriptGroup::Load(ByteStream* stream) {
    int count;
    stream->ReadValue(count);
    members.resize(static_cast<std::size_t>(count));
    for (ScriptObjectRef& slot : members) slot.Load(stream);
    Sort();
}
