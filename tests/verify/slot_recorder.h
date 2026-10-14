#pragma once

// A stand-in copy of an object's vtable for the length of one call: the entries (and the one before
// the table) are copied and chosen slots replaced by recorders, which note the call and forward it
// to the real slot, so everything still happens once. A recorder made inside a recorded call (a
// nested check) keeps its own record and puts the outer one back when it ends; read Call() before
// the recorder goes out of scope. Used by checks that a function calls a virtual slot of another
// object with given arguments.

#include <cstdint>
#include <cstring>

namespace ei::verify {

struct SlotCall {
    int slot = -1;
    std::uintptr_t args[3] = {};
    int calls = 0;
};

class SlotRecorder {
public:
    static constexpr int kSlots = 64;

    explicit SlotRecorder(void* object) : object_(object), outerReal_(s_real), outerCall_(s_call) {
        real_ = *static_cast<void***>(object);
        std::memcpy(table_, real_ - 1, sizeof(table_));
        *static_cast<void***>(object) = table_ + 1;
        s_real = real_;
        s_call = SlotCall{};
    }
    ~SlotRecorder() {
        if (*static_cast<void***>(object_) == table_ + 1) *static_cast<void***>(object_) = real_;
        s_real = outerReal_;
        s_call = outerCall_;
    }
    SlotRecorder(const SlotRecorder&) = delete;
    SlotRecorder& operator=(const SlotRecorder&) = delete;

    // Records slot `Slot` taking `Args` dword arguments (0-3; thiscall, the callee pops them).
    template <int Slot, int Args>
    void Record() {
        table_[1 + Slot] = reinterpret_cast<void*>(Recorder<Slot, Args>::Get());
    }
    static const SlotCall& Call() { return s_call; }
    // The real vtable of the object the innermost recorder stands in for (null when none).
    static void** RealVtable() { return s_real; }

private:
    template <int Slot, int Args>
    struct Recorder;
    template <int Slot>
    struct Recorder<Slot, 0> {
        static void __fastcall Fn(void* self, void*) {
            Note(Slot, 0, 0, 0);
            reinterpret_cast<void(__thiscall*)(void*)>(s_real[Slot])(self);
        }
        static auto Get() { return &Fn; }
    };
    template <int Slot>
    struct Recorder<Slot, 1> {
        static void __fastcall Fn(void* self, void*, std::uintptr_t a) {
            Note(Slot, a, 0, 0);
            reinterpret_cast<void(__thiscall*)(void*, std::uintptr_t)>(s_real[Slot])(self, a);
        }
        static auto Get() { return &Fn; }
    };
    template <int Slot>
    struct Recorder<Slot, 2> {
        static void __fastcall Fn(void* self, void*, std::uintptr_t a, std::uintptr_t b) {
            Note(Slot, a, b, 0);
            reinterpret_cast<void(__thiscall*)(void*, std::uintptr_t, std::uintptr_t)>(s_real[Slot])(self, a, b);
        }
        static auto Get() { return &Fn; }
    };
    template <int Slot>
    struct Recorder<Slot, 3> {
        static void __fastcall Fn(void* self, void*, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c) {
            Note(Slot, a, b, c);
            reinterpret_cast<void(__thiscall*)(void*, std::uintptr_t, std::uintptr_t, std::uintptr_t)>(s_real[Slot])(
                self, a, b, c);
        }
        static auto Get() { return &Fn; }
    };
    static void Note(int slot, std::uintptr_t a, std::uintptr_t b, std::uintptr_t c) {
        s_call.slot = slot;
        s_call.args[0] = a;
        s_call.args[1] = b;
        s_call.args[2] = c;
        ++s_call.calls;
    }

    void* object_;
    void** outerReal_;
    SlotCall outerCall_;
    void** real_;
    void* table_[kSlots + 1];
    static inline void** s_real = nullptr;
    static inline SlotCall s_call;
};

} // namespace ei::verify
