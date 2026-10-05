// Checks for the windows' per-frame base slots (ui_window.cpp, ui_container.cpp): 005FEB90 DrawBase,
// 005FECC0 RedrawBase, 005FED00 RedrawTips, 00601040 RedrawContainer, 00601090 RedrawChildTips,
// 006010E0 UpdateBase, 00600270 FrameTime. Every frame of every scenario reaches them (the screens'
// slots 20, 22, 23 and 24 end in these).
//
// The dispatchers run once (ours). The objects they call through a vtable (a container's children,
// a window's surfaces) get a stand-in copy of their vtable for the duration: its dispatched slot
// records the call and forwards it to the real one, so everything still happens exactly once. The
// recorded calls must be the ones the original makes, in order (the shown surfaces' slot 0, then
// the enabled children's slot 22 or 23, or every child's slot 24). Stand-ins are taken off only
// objects still listed afterwards (a child could have been deleted); a table left on an object
// that was removed but not deleted stays valid (the tables are never freed) and keeps forwarding.
#include "verify.h"

#include "../../src/game/originals_render.h"
#include "../../src/game/ui.h"

#include <cstdint>
#include <vector>

namespace {

constexpr int kTableSlots = 96;

// A stand-in vtable: entries[0] holds the real vtable, the table starts at entries[1].
struct StandInTable {
    void* entries[kTableSlots + 1];
};
std::vector<StandInTable*> s_freeTables;

class Recorder;
std::vector<Recorder*> s_active;

struct Call {
    void* object;
    int slot;
    bool operator==(const Call& o) const { return object == o.object && slot == o.slot; }
};

class Recorder {
public:
    Recorder() { s_active.push_back(this); }
    ~Recorder() {
        s_active.pop_back();
        for (Installed& i : installed_)
            if (i.table) s_freeTables.push_back(i.table);
    }
    Recorder(const Recorder&) = delete;

    // Replaces `slot` of `object`'s vtable (copied up to `slots` entries) by the recorder.
    void Install(void* object, int slot, void* recorder, int slots) {
        for (Installed& i : installed_)
            if (i.object == object) return;
        StandInTable* table;
        if (!s_freeTables.empty()) {
            table = s_freeTables.back();
            s_freeTables.pop_back();
        } else {
            table = new StandInTable;
        }
        void** real = *static_cast<void***>(object);
        table->entries[0] = real;
        for (int k = 0; k < slots; ++k) table->entries[k + 1] = real[k];
        table->entries[slot + 1] = recorder;
        *static_cast<void***>(object) = &table->entries[1];
        installed_.push_back(Installed{object, real, table});
    }
    // Puts the real vtables back on the objects `stillListed` accepts; the others keep their table.
    template <typename Listed>
    void Remove(Listed stillListed) {
        for (Installed& i : installed_) {
            if (!stillListed(i.object)) {
                i.table = nullptr;   // left in place, never reused
                continue;
            }
            *static_cast<void***>(i.object) = i.real;
        }
    }
    bool Owns(void* object) const {
        for (const Installed& i : installed_)
            if (i.object == object) return true;
        return false;
    }

    std::vector<Call> calls;
    int depth = 0;

private:
    struct Installed {
        void* object;
        void** real;
        StandInTable* table;
    };
    std::vector<Installed> installed_;
};

// The stand-in slot: recorded by the innermost recorder that installed it (unless that recorder is
// already inside one of its forwarded calls), then the real slot.
template <int Slot>
void __fastcall Forward(void* self) {
    void** table = *static_cast<void***>(self);
    auto** real = static_cast<void**>(table[-1]);
    Recorder* owner = nullptr;
    for (auto it = s_active.rbegin(); it != s_active.rend() && !owner; ++it)
        if ((*it)->Owns(self)) owner = *it;
    if (owner && owner->depth == 0) owner->calls.push_back(Call{self, Slot});
    if (owner) ++owner->depth;
    using Fn = void(__thiscall*)(void*);
    reinterpret_cast<Fn>(real[Slot])(self);
    if (owner) --owner->depth;
}

// A container's children with a stand-in on `slot`; the expected calls are the children it should
// reach (all, or the enabled ones), judged before the call.
void InstallChildren(Recorder& r, CUiContainer* self, int slot, void* forward, bool enabledOnly,
                     std::vector<Call>& expected) {
    for (int i = 0; i < self->children_.size_; ++i) {
        CUiWindow* child = self->children_.data_[i];
        if (!enabledOnly || child->enabled_) expected.push_back(Call{child, slot});
        r.Install(child, slot, forward, kTableSlots);
    }
}

// The window's shown surfaces with a stand-in on slot 0.
void InstallSurfaces(Recorder& r, CUiWindow* self, std::vector<Call>& expected) {
    for (int i = 0; i < self->surfaces_.size_; ++i) {
        CUiSurface* surface = self->surfaces_.data_[i];
        if (surface->Shown()) expected.push_back(Call{surface, 0});
        r.Install(surface, 0, reinterpret_cast<void*>(&Forward<0>), 16);
    }
}

bool Listed(CUiWindow* self, void* object) {
    for (int i = 0; i < self->surfaces_.size_; ++i)
        if (self->surfaces_.data_[i] == object) return true;
    return false;
}
bool ListedChild(CUiContainer* self, void* object) {
    for (int i = 0; i < self->children_.size_; ++i)
        if (self->children_.data_[i] == object) return true;
    return false;
}

void Compare(const char* name, const std::vector<Call>& expected, const std::vector<Call>& actual) {
    bool ok = expected == actual;
    ei::verify::Check(name, true, ok, "expected %u calls, got %u", static_cast<unsigned>(expected.size()),
                      static_cast<unsigned>(actual.size()));
}

void* s_unused;

} // namespace

static void __fastcall RedrawBaseVerify(CUiWindow* self) {
    std::vector<Call> expected;
    {
        Recorder r;
        InstallSurfaces(r, self, expected);
        self->CUiWindow::RedrawBase();
        r.Remove([&](void* o) { return Listed(self, o); });
        Compare("CUiWindow::RedrawBase", expected, r.calls);
    }
}
EI_VERIFY(0x005FECC0, RedrawBaseVerify, 0, s_unused);

static void __fastcall UpdateBaseVerify(CUiContainer* self) {
    std::vector<Call> expected;
    {
        Recorder r;
        InstallChildren(r, self, 24, reinterpret_cast<void*>(&Forward<24>), false, expected);
        self->CUiContainer::UpdateBase();
        r.Remove([&](void* o) { return ListedChild(self, o); });
        Compare("CUiContainer::UpdateBase", expected, r.calls);
    }
}
EI_VERIFY(0x006010E0, UpdateBaseVerify, 0, s_unused);

static void __fastcall RedrawContainerVerify(CUiContainer* self) {
    std::vector<Call> expected;
    {
        Recorder r;
        if (self->interface_->drawing_) {
            InstallSurfaces(r, self, expected);
            InstallChildren(r, self, 22, reinterpret_cast<void*>(&Forward<22>), true, expected);
        }
        self->CUiContainer::RedrawContainer();
        r.Remove([&](void* o) { return Listed(self, o) || ListedChild(self, o); });
        Compare("CUiContainer::RedrawContainer", expected, r.calls);
    }
}
EI_VERIFY(0x00601040, RedrawContainerVerify, 0, s_unused);

static void __fastcall RedrawChildTipsVerify(CUiContainer* self) {
    std::vector<Call> expected;
    {
        Recorder r;
        if (self->interface_->drawing_)
            InstallChildren(r, self, 23, reinterpret_cast<void*>(&Forward<23>), true, expected);
        self->CUiContainer::RedrawChildTips();
        r.Remove([&](void* o) { return ListedChild(self, o); });
        Compare("CUiContainer::RedrawChildTips", expected, r.calls);
    }
}
EI_VERIFY(0x00601090, RedrawChildTipsVerify, 0, s_unused);

// 005FEB90 draws (ours only): returns 1, the lists it walks are unchanged.
static BOOL __fastcall DrawBaseVerify(CUiWindow* self, void*, int pass) {
    int objects = self->sceneObjects_.size_;
    int models = self->models_.size_;
    BOOL result = self->CUiWindow::DrawBase(pass);
    bool ok = result == 1 && self->sceneObjects_.size_ == objects && self->models_.size_ == models;
    ei::verify::Check("CUiWindow::DrawBase", true, ok, "objects %d models %d", objects, models);
    return result;
}
EI_VERIFY(0x005FEB90, DrawBaseVerify, 0, s_unused);

// 00600270 (ours only: its first instructions are a 3-byte SUB and a CALL, nothing to run the
// original from): the display's milliseconds (slot 6, the last frame's, unchanged within the call)
// times 0.001f.
static double __fastcall FrameTimeVerify(CUiContainer* self) {
    double result = self->CUiContainer::FrameTime();
    void* display = DisplayObject();
    using Fn = std::uint32_t(__thiscall*)(void*);
    std::uint32_t ms = reinterpret_cast<Fn>((*static_cast<void***>(display))[6])(display);
    double expected = static_cast<double>(static_cast<std::int64_t>(ms)) * static_cast<double>(0.001f);
    ei::verify::CheckNear("CUiContainer::FrameTime", expected, result, "ms %u", ms);
    return result;
}
EI_VERIFY(0x00600270, FrameTimeVerify, 0, s_unused);
