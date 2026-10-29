// Script object references and groups (sets of object references). See script.h.
#include "script.h"
#include "unit.h"
#include "combat.h"
#include "world.h"

#include "game_singletons.h"

#include <algorithm>
#include <cstdint>
#include <list>

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
    const std::size_t aSize = a.size(), bSize = b.size();
    resize(aSize + bSize);
    std::size_t out = 0, i = 0, j = 0;
    while (i < aSize) {
        if (j >= bSize) {
            while (i < aSize) {
                EngineObject* x = a[i++].object;
                (*this)[out++].object = x;
            }
            break;
        }
        EngineObject* x = a[i].object;
        EngineObject* y = b[j].object;
        if (x == y) {
            (*this)[out++].object = x;
            ++i;
            ++j;
        } else if (After(x, y)) {
            (*this)[out++].object = y;
            ++j;
        } else {
            (*this)[out++].object = x;
            ++i;
        }
    }
    while (j < bSize) {
        EngineObject* y = b[j++].object;
        (*this)[out++].object = y;
    }
    resize(out);
}

void ScriptGroup::Difference(const ScriptGroup& a, const ScriptGroup& b) {
    const std::size_t aSize = a.size(), bSize = b.size();
    resize(aSize);
    std::size_t out = 0, i = 0, j = 0;
    while (i < aSize) {
        if (j >= bSize) {
            while (i < aSize) {
                EngineObject* x = a[i++].object;
                (*this)[out++].object = x;
            }
            break;
        }
        EngineObject* x = a[i].object;
        EngineObject* y = b[j].object;
        if (x == y) {
            ++i;
            ++j;
        } else if (After(x, y)) {
            ++j;
        } else {
            (*this)[out++].object = x;
            ++i;
        }
    }
    resize(out);
}

void ScriptGroup::Intersection(const ScriptGroup& a, const ScriptGroup& b) {
    const std::size_t aSize = a.size(), bSize = b.size();
    resize(aSize > bSize ? aSize : bSize);
    std::size_t out = 0, i = 0, j = 0;
    while (i < aSize && j < bSize) {
        EngineObject* x = a[i].object;
        EngineObject* y = b[j].object;
        if (x == y) {
            (*this)[out++].object = x;
            ++i;
            ++j;
        } else if (After(x, y)) {
            ++j;
        } else {
            ++i;
        }
    }
    resize(out);
}

void ScriptGroup::Sort() { std::sort(begin(), end(), MemberBefore); }

int ScriptGroup::IndexOf(const ScriptObjectRef& ref) const {
    EngineObject* o = ref.object;
    for (std::size_t i = 0; i < size(); ++i)
        if ((*this)[i].object == o) return static_cast<int>(i);
    return -1;
}

void ScriptGroup::Remove(EngineObject* object) {
    auto at = std::find_if(begin(), end(), [object](const ScriptObjectRef& ref) { return ref.object == object; });
    if (at != end()) erase(at);
}

// A binary search for the place in member order; nothing when the object is there.
void ScriptGroup::Insert(EngineObject* object) {
    if (!object) return;
    int low = 0;
    int high = static_cast<int>(size()) - 1;
    while (high - low >= 0) {
        int middle = (high + low) / 2;
        EngineObject* current = (*this)[middle].object;
        if (object == current) return;
        if (After(object, current)) low = middle + 1;
        else high = middle - 1;
    }
    insert(begin() + low, ScriptObjectRef{object});
}

void ScriptGroup::RemoveDeleted() {
    std::size_t i = 0;
    while (i < size()) {
        ScriptObjectRef& slot = (*this)[i];
        slot.ReleaseIfDeleted();
        if (slot.object) ++i;
        else erase(begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void ScriptGroup::InsertAll(const std::list<HeldRef<CUnit>>* list) {
    for (CUnit* unit : *list) Insert(unit);
}

void ScriptGroup::CopyToList(std::list<HeldRef<CUnit>>* list) const {
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
    resize(static_cast<std::size_t>(count));
    for (ScriptObjectRef& slot : *this) slot.Load(stream);
    Sort();
}
