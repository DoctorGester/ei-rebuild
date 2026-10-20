// Script object references and groups (sets of object references). See script.h.
#include "script.h"
#include "game_arrays.h"
#include "unit.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc_array.h"
#include "originals_library.h"
#include "originals_world.h"

#include <cstdint>

namespace {

// The world's object registry (global at 007AFF60); FindObject looks an object up by its id.
struct ObjectRegistry {
    CEngineObject* FindObject(int id);   // 00593AA0
};
CEngineObject* ObjectRegistry::FindObject(int id) { return WorldFindObjectOriginal(this, nullptr, static_cast<unsigned>(id)); }
ObjectRegistry* Registry() { return reinterpret_cast<ObjectRegistry*>(0x007AFF60); }

// The game's CRT.
void __cdecl GameQsort(void* a0, std::size_t a1, std::size_t a2, int(__cdecl* a3)(const void*, const void*)) { GameQsortOriginal(a0, a1, a2, a3); }

constexpr std::uint32_t kDeleted = 1;     // CEngineObject::flags_: removed from the world

std::uintptr_t Address(const CEngineObject* object) { return reinterpret_cast<std::uintptr_t>(object); }

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

bool g_byId = false;   // ScriptGroup::OrderById

// 004B2D80 (the game's: by object address, unsigned; ours: ScriptGroup::After).
int __cdecl CompareMembers(const void* a, const void* b) {
    const CEngineObject* x = static_cast<const ScriptObjectRef*>(a)->object;
    const CEngineObject* y = static_cast<const ScriptObjectRef*>(b)->object;
    if (x == y) return 0;
    return ScriptGroup::After(x, y) ? 1 : -1;
}

} // namespace

// ---- Member order (SV7b1) -------------------------------------------------------------------------

bool ScriptGroup::IdAfter(const CEngineObject* x, const CEngineObject* y) {
    if (!x || !y) return x != nullptr;
    return x->id_ > y->id_;
}

bool ScriptGroup::After(const CEngineObject* x, const CEngineObject* y) {
    if (g_byId || ei::Standalone()) return IdAfter(x, y);
    return Address(x) > Address(y);
}

void ScriptGroup::OrderById() { g_byId = true; }

// ---- ScriptObjectRef ----------------------------------------------------------------------------

void ScriptObjectRef::Reset() { Clear(this); }
EI_REPLACE_METHOD(0x004B24B0, ScriptObjectRef::Reset);
EI_REPLACE_METHOD(0x004B5AE0, ScriptObjectRef::Reset);

void ScriptObjectRef::Assign(CEngineObject* newObject) { Store(this, newObject); }
EI_REPLACE_METHOD(0x004B24D0, ScriptObjectRef::Assign);

CEngineObject* ScriptObjectRef::Construct(const ScriptObjectRef& other) {
    CEngineObject* o = other.object;
    object = o;
    AddRef(o);
    return o;
}
EI_REPLACE_METHOD(0x004B2500, ScriptObjectRef::Construct);

CEngineObject* ScriptObjectRef::AsUnit() const {
    CEngineObject* o = object;
    if (o && o->GetClassId() == kClassUnit) return o;
    return nullptr;
}
EI_REPLACE_METHOD(0x004B2530, ScriptObjectRef::AsUnit);

void* ScriptObjectRef::GetUnitPerception() const {
    CEngineObject* o = object;
    if (o && o->GetClassId() == kClassUnit) return static_cast<CUnit*>(o)->controller;
    return nullptr;
}
EI_REPLACE_METHOD(0x004B2550, ScriptObjectRef::GetUnitPerception);

void ScriptObjectRef::ReleaseIfDeleted() {
    if (object && (object->flags_ & kDeleted)) {
        object->Release();
        object = nullptr;
    }
}
EI_REPLACE_METHOD(0x004B2570, ScriptObjectRef::ReleaseIfDeleted);

void ScriptObjectRef::Load(ScriptStream* stream) {
    int id;
    stream->Read(&id, 4);
    if (id == -1) {
        Clear(this);
        return;
    }
    Store(this, Registry()->FindObject(id));
}
EI_REPLACE_METHOD(0x004B2590, ScriptObjectRef::Load);

void ScriptObjectRef::Save(ScriptStream* stream) const {
    int id = -1;
    if (object) id = object->id_;
    stream->Write(&id, 4);
}
EI_REPLACE_METHOD(0x004B25F0, ScriptObjectRef::Save);

// ---- ScriptGroup --------------------------------------------------------------------------------

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
        data = static_cast<ScriptObjectRef*>(gamecrt::operator_new(newSize * 4));
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
    auto* fresh = static_cast<ScriptObjectRef*>(gamecrt::operator_new(newMax * 4));
    for (int i = 0; i < size; ++i) fresh[i] = data[i];
    for (int i = size; i < newSize; ++i) fresh[i].object = nullptr;
    gamecrt::operator_delete(data);
    data = fresh;
    size = newSize;
    maxSize = newMax;
}

// The merges walk both arrays in member order (After). Each source element is read before
// the destination slot is released, like the original (it matters only if `this` is a or b).
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
EI_REPLACE_METHOD(0x004B2620, ScriptGroup::Union);

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
EI_REPLACE_METHOD(0x004B2920, ScriptGroup::Difference);

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
EI_REPLACE_METHOD(0x004B2B60, ScriptGroup::Intersection);

void ScriptGroup::Sort() { GameQsort(data, size, 4, CompareMembers); }
EI_REPLACE_METHOD(0x004B2D60, ScriptGroup::Sort);

int ScriptGroup::IndexOf(const ScriptObjectRef& ref) const {
    CEngineObject* o = ref.object;
    for (int i = 0; i < size; ++i)
        if (data[i].object == o) return i;
    return -1;
}
EI_REPLACE_METHOD(0x004B2DA0, ScriptGroup::IndexOf);

void ScriptGroup::Remove(CEngineObject* object) {
    int i = 0;
    for (; i < size; ++i)
        if (data[i].object == object) break;
    if (i >= size) return;
    // RemoveAt(i, 1).
    int after = size - i - 1;
    Clear(&data[i]);
    if (after != 0) GameMemmove(&data[i], &data[i + 1], after * 4);
    --size;
}
EI_REPLACE_METHOD(0x004B2E60, ScriptGroup::Remove);

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
    if (low < oldSize) GameMemmove(&d[low + 1], &d[low], (oldSize - low) * 4);
    d[low].object = nullptr;
    ++object->refCount_;
    d[low].object = object;
}
EI_REPLACE_METHOD(0x004B2EF0, ScriptGroup::Insert);

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
            if (after != 0) GameMemmove(&data[i], &data[i + 1], after * 4);
            --size;
        }
    } while (i < size);
}
EI_REPLACE_METHOD(0x004B3110, ScriptGroup::RemoveDeleted);

void ScriptGroup::InsertAll(const mfc::CTypedPtrList<CEngineObject>* list) {
    for (auto* node = list->head; node;) {
        auto* current = node;
        node = node->next;
        Insert(current->data);
    }
}
EI_REPLACE_METHOD(0x004B31D0, ScriptGroup::InsertAll);

void ScriptGroup::CopyToList(mfc::CTypedPtrList<CEngineObject>* list) const {
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
            node->data = object;
        }
        if (list->tail) list->tail->next = node;
        else list->head = node;
        list->tail = node;
    }
}
EI_REPLACE_METHOD(0x004B3200, ScriptGroup::CopyToList);

BOOL ScriptGroup::Contains(CEngineObject* object) const {
    for (int i = 0; i < size; ++i)
        if (data[i].object == object) return 1;
    return 0;
}
EI_REPLACE_METHOD(0x004B3330, ScriptGroup::Contains);

void ScriptGroup::Load(ScriptStream* stream) {
    int count;
    stream->Read(&count, 4);
    Resize(count);
    for (int i = 0; i < count; ++i) {
        ScriptObjectRef* slot = &data[i];
        int id;
        stream->Read(&id, 4);
        if (id == -1) Clear(slot);
        else Store(slot, Registry()->FindObject(id));
    }
    GameQsort(data, size, 4, CompareMembers);
}
EI_REPLACE_METHOD(0x004B3360, ScriptGroup::Load);
