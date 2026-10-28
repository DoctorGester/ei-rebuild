// Script object references and groups (sets of object references). See script.h.
#include "script.h"
#include "game_arrays.h"
#include "unit.h"
#include "combat.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstdint>
#include <cstring>

namespace {

// The server world's object by id.
CEngineObject* FindObject(int id) { return TheServerWorld()->FindObject(static_cast<unsigned>(id)); }

// The game's CRT.
void __cdecl GameQsort(void* a0, std::size_t a1, std::size_t a2, int(__cdecl* a3)(const void*, const void*)) { libcrt::Qsort(a0, a1, a2, a3); }

constexpr std::uint32_t kDeleted = 1;     // CEngineObject::flags_: removed from the world

void AddRef(CEngineObject* object) {
    if (object) ++object->refCount_;
}

// Release and clear one slot, as the inlined ScriptObjectRef destructor does.
void Clear(ScriptObjectRef* slot) {
    if (slot->object) slot->object->Release();
    slot->object = nullptr;
}

// Store `object` in `slot` after releasing what it held (Reset, then add a reference).
void Store(ScriptObjectRef* slot, CEngineObject* object) {
    Clear(slot);
    AddRef(object);
    slot->object = object;
}


// The game's order: by object address, unsigned (ours: ScriptGroup::After).
int __cdecl CompareMembers(const void* a, const void* b) {
    const CEngineObject* x = static_cast<const ScriptObjectRef*>(a)->object;
    const CEngineObject* y = static_cast<const ScriptObjectRef*>(b)->object;
    if (x == y) return 0;
    return ScriptGroup::After(x, y) ? 1 : -1;
}

} // namespace

// ---- Member order -------------------------------------------------------------------------

bool ScriptGroup::After(const CEngineObject* x, const CEngineObject* y) {
    if (!x || !y) return x != nullptr;
    return x->id_ > y->id_;
}


void ScriptObjectRef::Reset() { Clear(this); }

void ScriptObjectRef::Assign(CEngineObject* newObject) { Store(this, newObject); }

CEngineObject* ScriptObjectRef::Construct(const ScriptObjectRef& other) {
    CEngineObject* o = other.object;
    object = o;
    AddRef(o);
    return o;
}

CUnit* ScriptObjectRef::AsUnit() const {
    return UnitOrNull(object);
}

CUnitServer* ScriptObjectRef::GetUnitPerception() const {
    CUnit* unit = UnitOrNull(object);
    return unit ? unit->ServerController() : nullptr;
}

void ScriptObjectRef::ReleaseIfDeleted() {
    if (object && (object->flags_ & kDeleted)) {
        object->Release();
        object = nullptr;
    }
}

void ScriptObjectRef::Load(ScriptStream* stream) {
    int id;
    stream->Read(&id, 4);
    if (id == -1) {
        Clear(this);
        return;
    }
    Store(this, FindObject(id));
}

void ScriptObjectRef::Save(ScriptStream* stream) const {
    int id = -1;
    if (object) id = object->id_;
    stream->Write(&id, 4);
}

void ScriptGroup::SetSize(int newSize, int newGrowBy) { mfc::impl::ArrayAs<gamearrays::ObjectRefs>(*this).SetSize(newSize, newGrowBy); }

void ScriptGroup::Resize(int newSize) {
    if (newSize == 0) {
        if (data) {
            for (int i = 0; i < size; ++i) Clear(&data[i]);
            gamecrt::operator_delete(data);
            data = nullptr;
        }
        maxSize = 0;
        size = 0;
        return;
    }
    if (!data) {
        data = gamecrt::operator_new_array<ScriptObjectRef>(newSize);
        for (int i = 0; i < newSize; ++i) data[i].object = nullptr;
        maxSize = newSize;
        size = newSize;
        return;
    }
    if (newSize <= maxSize) {
        if (size < newSize) {
            for (int i = size; i < newSize; ++i) data[i].object = nullptr;
        } else if (newSize < size) {
            for (int i = newSize; i < size; ++i) Clear(&data[i]);
        }
        size = newSize;
        return;
    }
    int grow = growBy;
    if (grow == 0) {
        grow = size / 8;
        if (grow < 4) grow = 4;
        else if (grow > 0x400) grow = 0x400;
    }
    int newMax = maxSize + grow;
    if (newSize >= newMax) newMax = newSize;
    auto* fresh = gamecrt::operator_new_array<ScriptObjectRef>(newMax);
    for (int i = 0; i < size; ++i) fresh[i] = data[i];
    for (int i = size; i < newSize; ++i) fresh[i].object = nullptr;
    gamecrt::operator_delete(data);
    data = fresh;
    size = newSize;
    maxSize = newMax;
}

// The merges walk both arrays in member order (After). Each source element is read before
// the destination slot is released (it matters only if `this` is a or b).
void ScriptGroup::Union(const ScriptGroup& a, const ScriptGroup& b) {
    int aSize = a.size, bSize = b.size;
    Resize(aSize + bSize);
    int out = 0, i = 0, j = 0;
    if (aSize > 0) {
        do {
            if (j >= bSize) {
                while (i < aSize) {
                    CEngineObject* x = a.data[i++].object;
                    Store(&data[out++], x);
                }
                break;
            }
            CEngineObject* x = a.data[i].object;
            CEngineObject* y = b.data[j].object;
            if (x == y) {
                Store(&data[out++], x);
                ++i;
                ++j;
            } else if (After(x, y)) {
                Store(&data[out++], y);
                ++j;
            } else {
                Store(&data[out++], x);
                ++i;
            }
        } while (i < aSize);
    }
    while (j < bSize) {
        CEngineObject* y = b.data[j++].object;
        Store(&data[out++], y);
    }
    SetSize(out, -1);
}

void ScriptGroup::Difference(const ScriptGroup& a, const ScriptGroup& b) {
    int aSize = a.size, bSize = b.size;
    Resize(aSize);
    int out = 0, i = 0, j = 0;
    if (aSize > 0) {
        do {
            if (j >= bSize) {
                while (i < aSize) {
                    CEngineObject* x = a.data[i++].object;
                    Store(&data[out++], x);
                }
                break;
            }
            CEngineObject* x = a.data[i].object;
            CEngineObject* y = b.data[j].object;
            if (x == y) {
                ++i;
                ++j;
            } else if (After(x, y)) {
                ++j;
            } else {
                Store(&data[out++], x);
                ++i;
            }
        } while (i < aSize);
    }
    SetSize(out, -1);
}

void ScriptGroup::Intersection(const ScriptGroup& a, const ScriptGroup& b) {
    int aSize = a.size, bSize = b.size;
    Resize(aSize > bSize ? aSize : bSize);
    int out = 0, i = 0, j = 0;
    if (aSize > 0) {
        do {
            if (j >= bSize) break;
            CEngineObject* x = a.data[i].object;
            CEngineObject* y = b.data[j].object;
            if (x == y) {
                Store(&data[out++], x);
                ++i;
                ++j;
            } else if (After(x, y)) {
                ++j;
            } else {
                ++i;
            }
        } while (i < aSize);
    }
    SetSize(out, -1);
}

void ScriptGroup::Sort() { GameQsort(data, size, 4, CompareMembers); }

int ScriptGroup::IndexOf(const ScriptObjectRef& ref) const {
    CEngineObject* o = ref.object;
    for (int i = 0; i < size; ++i)
        if (data[i].object == o) return i;
    return -1;
}

void ScriptGroup::Remove(CEngineObject* object) {
    int i = 0;
    for (; i < size; ++i)
        if (data[i].object == object) break;
    if (i >= size) return;
    // RemoveAt(i, 1).
    int after = size - i - 1;
    Clear(&data[i]);
    if (after != 0) std::memmove(&data[i], &data[i + 1], after * 4);
    --size;
}

void ScriptGroup::Insert(CEngineObject* object) {
    int oldSize = size;
    int low = 0;
    int high = oldSize - 1;
    if (!object) return;
    if (high >= 0) {
        do {
            int middle = (high + low) / 2;
            CEngineObject* current = data[middle].object;
            if (object == current) return;
            if (After(object, current)) low = middle + 1;
            else high = middle - 1;
        } while (high - low >= 0);
    }
    // InsertAt(low, object).
    Resize(oldSize + 1);
    ScriptObjectRef* d = data;
    if (low < oldSize) std::memmove(&d[low + 1], &d[low], (oldSize - low) * 4);
    d[low].object = nullptr;
    ++object->refCount_;
    d[low].object = object;
}

void ScriptGroup::RemoveDeleted() {
    int i = 0;
    if (size <= 0) return;
    do {
        ScriptObjectRef* slot = &data[i];
        if (slot->object && (slot->object->flags_ & kDeleted)) {
            slot->object->Release();
            slot->object = nullptr;
        }
        if (data[i].object) {
            ++i;
        } else {
            int after = size - i - 1;
            Clear(&data[i]);
            if (after != 0) std::memmove(&data[i], &data[i + 1], after * 4);
            --size;
        }
    } while (i < size);
}

void ScriptGroup::InsertAll(const mfc::CTypedPtrList<CUnit>* list) {
    for (auto* node = list->head; node;) {
        auto* current = node;
        node = node->next;
        Insert(current->data);
    }
}

void ScriptGroup::CopyToList(mfc::CTypedPtrList<CUnit>* list) const {
    for (auto* node = list->head; node; node = node->next) {
        if (node->data) node->data->Release();
        node->data = nullptr;
    }
    list->RemoveAll();
    for (int i = 0; i < size; ++i) {
        CEngineObject* object = data[i].object;
        auto* node = list->NewNode(list->tail, nullptr);
        if (object != node->data) {   // a new node's element is null: nothing to release
            AddRef(object);
            node->data = static_cast<CUnit*>(object);
        }
        if (list->tail) list->tail->next = node;
        else list->head = node;
        list->tail = node;
    }
}

BOOL ScriptGroup::Contains(CEngineObject* object) const {
    for (int i = 0; i < size; ++i)
        if (data[i].object == object) return 1;
    return 0;
}

void ScriptGroup::Load(ScriptStream* stream) {
    int count;
    stream->Read(&count, 4);
    Resize(count);
    for (int i = 0; i < count; ++i) {
        ScriptObjectRef* slot = &data[i];
        int id;
        stream->Read(&id, 4);
        if (id == -1) Clear(slot);
        else Store(slot, FindObject(id));
    }
    GameQsort(data, size, 4, CompareMembers);
}
