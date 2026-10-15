// Checks for the STLport vector<T> instantiations (src/lib/stlp_vector.cpp).
//
// The base constructor, the allocator proxy's constructor and get_allocator compare with the
// original (run on a scratch object). A free (~_Vector_base, deallocate) of a pool-sized block runs
// the original first when its first instructions can be copied, takes the block off its free list
// again and then runs ours, which must put the same block on top of the same free list; the others
// (a register-argument deallocate, a short first block, or a block above 128 bytes that goes to
// operator delete) run ours only and check the free list. The members that change a plain-data
// vector (ChangeCheck) run the original, undo it and run ours: the same block, pointers, elements
// and result.
#include "verify.h"

#include "stlp_list_verify.h"

#include "../../src/lib/stlp_vector.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <vector>

namespace {

void* s_unused;

using stlp_verify::FreeList;
using stlp_verify::Name;
using stlp::AllocProxy;
using stlp::AnyVector;

using Vector1 = stlp::VectorAbi<char>;
using Vector4 = stlp::VectorAbi<std::uint32_t>;
using Vector8 = stlp::VectorAbi<stlp::Opaque<8>>;
using Vector12 = stlp::VectorAbi<stlp::Opaque<12>>;
using Vector16 = stlp::VectorAbi<stlp::Opaque<16>>;
using Vector20 = stlp::VectorAbi<stlp::Opaque<20>>;
using Vector24 = stlp::VectorAbi<stlp::Opaque<24>>;
using Vector32 = stlp::VectorAbi<stlp::Opaque<32>>;
using Vector36 = stlp::VectorAbi<stlp::Opaque<36>>;
using Vector40 = stlp::VectorAbi<stlp::Opaque<40>>;
using Vector48 = stlp::VectorAbi<stlp::Opaque<48>>;
using Vector60 = stlp::VectorAbi<stlp::Opaque<60>>;
using Vector96 = stlp::VectorAbi<stlp::Opaque<96>>;
using Vector288 = stlp::VectorAbi<stlp::Opaque<288>>;

// The three words of a vector as the checks compare them.
struct Words {
    std::uint32_t w[3];
};

template <std::uintptr_t A>
struct ConstructBaseCheck {
    using Fn = AnyVector*(__fastcall*)(AnyVector*, void*, const void*);
    static inline Fn original = nullptr;

    static AnyVector* __fastcall Verify(AnyVector* self, void*, const void* allocator) {
        Words theirs;
        std::memset(&theirs, 0xCD, sizeof(theirs));
        AnyVector* scratch = reinterpret_cast<AnyVector*>(&theirs);
        bool returned = original(scratch, nullptr, allocator) == scratch;
        AnyVector* result = stlp::VectorConstructBase(self, nullptr, allocator);
        Words ours;
        std::memcpy(&ours, self, sizeof(ours));
        bool ok = returned && result == self && std::memcmp(&ours, &theirs, sizeof(Words)) == 0;
        char detail[96] = "";
        if (!ok)
            std::snprintf(detail, sizeof(detail), "ours %08X %08X %08X, original's %08X %08X %08X", ours.w[0],
                          ours.w[1], ours.w[2], theirs.w[0], theirs.w[1], theirs.w[2]);
        ei::verify::Report(Name<A>("stlp::_Vector_base::_Vector_base"), ok, detail);
        return result;
    }
};

template <std::uintptr_t A>
struct ProxyConstructCheck {
    using Fn = AllocProxy*(__fastcall*)(AllocProxy*, void*, const void*, void* const*);
    static inline Fn original = nullptr;

    static AllocProxy* __fastcall Verify(AllocProxy* self, void*, const void* allocator, void* const* data) {
        AllocProxy theirs;
        std::memset(&theirs, 0xCD, sizeof(theirs));
        bool returned = original(&theirs, nullptr, allocator, data) == &theirs;
        AllocProxy* result = stlp::AllocProxyConstruct(self, nullptr, allocator, data);
        bool ok = returned && result == self && self->data == theirs.data;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p", self->data, theirs.data);
        ei::verify::Report(Name<A>("stlp::_STLP_alloc_proxy::_STLP_alloc_proxy"), ok, detail);
        return result;
    }
};

template <std::uintptr_t A>
struct GetAllocatorCheck {
    using Fn = std::uint8_t*(__fastcall*)(const AnyVector*, void*, std::uint8_t*);
    static inline Fn original = nullptr;

    static std::uint8_t* __fastcall Verify(const AnyVector* self, void*, std::uint8_t* out) {
        std::uint8_t theirs = 0xCD;
        bool returned = original(self, nullptr, &theirs) == &theirs;
        std::uint8_t* result = stlp::VectorGetAllocator(self, nullptr, out);
        int expected = theirs | (returned ? 0 : 0x100);
        int actual = *out | (result == out ? 0 : 0x100);
        ei::verify::Check(Name<A>("stlp::_Vector_base::get_allocator"), expected, actual, "vector %p", self);
        return result;
    }
};

// A free of `p` (`bytes` long) by `theirs` (when `runOriginal`) and then by `ours`. A pool-sized block
// must end on top of its free list with the old top under it; the original's is taken off again
// before ours runs.
template <typename Theirs, typename Ours>
void CheckFree(const char* name, void* p, std::size_t bytes, bool runOriginal, Theirs theirs, Ours ours) {
    void** list = p ? FreeList(bytes) : nullptr;
    void* top = list ? *list : nullptr;
    bool ok = true;
    char detail[96] = "";
    if (list && runOriginal) {
        theirs();
        ok = *list == p && *static_cast<void**>(p) == top;
        if (!ok) std::snprintf(detail, sizeof(detail), "the original left %p on top, not %p", *list, p);
        *list = top;
    }
    ours();
    if (ok && list && !(*list == p && *static_cast<void**>(p) == top)) {
        ok = false;
        std::snprintf(detail, sizeof(detail), "%p (%u bytes) is not on top of its free list", p,
                      static_cast<unsigned>(bytes));
    }
    ei::verify::Report(name, ok, detail);
}

template <std::uintptr_t A, typename Abi>
struct FreeCheck {
    using Vector = typename Abi::Vector;
    using T = typename Vector::value_type;
    static inline void* original = nullptr;

    static void __fastcall DestroyBase(Vector* self) {
        T* start = self->begin();
        std::size_t bytes = static_cast<std::size_t>(self->EndOfStorage() - start) * sizeof(T);
        CheckFree(
            Name<A>("stlp::_Vector_base::~_Vector_base"), start, bytes, original != nullptr,
            [&] { reinterpret_cast<void(__fastcall*)(Vector*)>(original)(self); }, [&] { Abi::DestroyBase(self); });
    }
    static void __fastcall Deallocate(const AllocProxy* proxy, void*, T* p, std::size_t n) {
        using Fn = void(__fastcall*)(const AllocProxy*, void*, T*, std::size_t);
        CheckFree(
            Name<A>("stlp::_STLP_alloc_proxy::deallocate"), p, n * sizeof(T), original != nullptr,
            [&] { reinterpret_cast<Fn>(original)(proxy, nullptr, p, n); }, [&] { Abi::Deallocate(proxy, nullptr, p, n); });
    }
    static void __fastcall DeallocateFast(T* p, std::size_t n) {
        CheckFree(Name<A>("stlp::_STLP_alloc_proxy::deallocate"), p, n * sizeof(T), false, [] {},
                  [&] { Abi::DeallocateFast(p, n); });
    }
};
template <std::uintptr_t A> using Free1 = FreeCheck<A, Vector1>;
template <std::uintptr_t A> using Free4 = FreeCheck<A, Vector4>;
template <std::uintptr_t A> using Free8 = FreeCheck<A, Vector8>;
template <std::uintptr_t A> using Free12 = FreeCheck<A, Vector12>;
template <std::uintptr_t A> using Free16 = FreeCheck<A, Vector16>;
template <std::uintptr_t A> using Free20 = FreeCheck<A, Vector20>;
template <std::uintptr_t A> using Free24 = FreeCheck<A, Vector24>;
template <std::uintptr_t A> using Free32 = FreeCheck<A, Vector32>;
template <std::uintptr_t A> using Free36 = FreeCheck<A, Vector36>;
template <std::uintptr_t A> using Free40 = FreeCheck<A, Vector40>;
template <std::uintptr_t A> using Free48 = FreeCheck<A, Vector48>;
template <std::uintptr_t A> using Free96 = FreeCheck<A, Vector96>;
template <std::uintptr_t A> using Free288 = FreeCheck<A, Vector288>;

// A vector's three pointers and its elements, to compare the original's result with ours.
template <typename T>
struct State {
    T* start;
    T* finish;
    T* eos;
    std::vector<unsigned char> bytes;   // [start, finish)

    static State Of(stlp::vector<T>& v) {
        State s{v.Start(), v.Finish(), v.EndOfStorage(), {}};
        const auto* p = reinterpret_cast<const unsigned char*>(s.start);
        s.bytes.assign(p, p + (s.finish - s.start) * sizeof(T));
        return s;
    }
    std::size_t CapacityBytes() const { return static_cast<std::size_t>(eos - start) * sizeof(T); }
};

// A member that changes a plain-data vector in place or moves it to a new block. When the old block
// is pool-sized (or absent) the original runs first; its result is recorded and undone (a new block
// back to the pool, the old one off its free list again, the old bytes and pointers restored), then
// ours runs and must give the same pointers (the same new block when that is pool-sized), elements
// and result. Otherwise ours runs alone and the pointers must stay ordered.
template <typename T, typename Theirs, typename Ours>
void CheckChange(const char* name, stlp::vector<T>* self, bool canRunOriginal, Theirs theirs, Ours ours) {
    State<T> before = State<T>::Of(*self);
    std::size_t oldBytes = before.CapacityBytes();
    std::vector<unsigned char> storage(reinterpret_cast<unsigned char*>(before.start),
                                       reinterpret_cast<unsigned char*>(before.start) + oldBytes);
    char detail[128] = "";
    if (!canRunOriginal || oldBytes > 128) {
        ours();
        bool ok = self->Start() <= self->Finish() && self->Finish() <= self->EndOfStorage();
        if (!ok) std::snprintf(detail, sizeof(detail), "pointers %p %p %p", self->Start(), self->Finish(), self->EndOfStorage());
        ei::verify::Report(name, ok, detail);
        return;
    }
    std::uintptr_t theirResult = theirs();
    State<T> after = State<T>::Of(*self);
    if (after.start != before.start) {
        if (before.start) {
            void** list = FreeList(oldBytes);
            if (*list != before.start) {
                std::snprintf(detail, sizeof(detail), "the original did not free %p to the top of its list", before.start);
                ei::verify::Report(name, false, detail);
                return;   // the original's result stays
            }
            *list = *reinterpret_cast<void**>(before.start);
        }
        stlp::deallocate(after.start, after.CapacityBytes());
    }
    if (oldBytes) std::memcpy(before.start, storage.data(), oldBytes);
    self->Start() = before.start;
    self->Finish() = before.finish;
    self->EndOfStorage() = before.eos;
    std::uintptr_t ourResult = ours();
    State<T> mine = State<T>::Of(*self);
    bool sameBlock = after.start == before.start || after.CapacityBytes() <= 128;
    bool ok = mine.finish - mine.start == after.finish - after.start && mine.eos - mine.start == after.eos - after.start &&
              (!sameBlock || mine.start == after.start) && mine.bytes == after.bytes && ourResult == theirResult;
    if (!ok)
        std::snprintf(detail, sizeof(detail), "ours %p +%d/%d result %08X, original's %p +%d/%d result %08X", mine.start,
                      static_cast<int>(mine.finish - mine.start), static_cast<int>(mine.eos - mine.start),
                      static_cast<unsigned>(ourResult), after.start, static_cast<int>(after.finish - after.start),
                      static_cast<int>(after.eos - after.start), static_cast<unsigned>(theirResult));
    ei::verify::Report(name, ok, detail);
}

template <std::uintptr_t A, typename Abi>
struct ChangeCheck {
    using Vector = typename Abi::Vector;
    using T = typename Vector::value_type;
    static inline void* original = nullptr;

    template <typename Fn, typename... Args>
    static std::uintptr_t Theirs(Args... args) {
        if constexpr (std::is_void_v<decltype(reinterpret_cast<Fn>(original)(args...))>) {
            reinterpret_cast<Fn>(original)(args...);
            return 0;
        } else {
            return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Fn>(original)(args...));
        }
    }

    static void __fastcall InsertOverflow(Vector* self, void*, T* pos, const T* x, std::size_t n) {
        using Fn = void(__fastcall*)(Vector*, void*, T*, const T*, std::size_t);
        CheckChange(
            Name<A>("stlp::vector::_M_insert_overflow"), self, original != nullptr,
            [&] { return Theirs<Fn>(self, nullptr, pos, x, n); },
            [&] { return Abi::InsertOverflow(self, nullptr, pos, x, n), std::uintptr_t(0); });
    }
    static void __fastcall FillInsert(Vector* self, void*, T* pos, std::size_t n, const T* x) {
        using Fn = void(__fastcall*)(Vector*, void*, T*, std::size_t, const T*);
        CheckChange(
            Name<A>("stlp::vector::_M_fill_insert"), self, original != nullptr,
            [&] { return Theirs<Fn>(self, nullptr, pos, n, x); },
            [&] { return Abi::FillInsert(self, nullptr, pos, n, x), std::uintptr_t(0); });
    }
    static void __fastcall RangeInsert(Vector* self, void*, T* pos, const T* first, const T* last, const void* tag) {
        using Fn = void(__fastcall*)(Vector*, void*, T*, const T*, const T*, const void*);
        CheckChange(
            Name<A>("stlp::vector::_M_range_insert"), self, original != nullptr,
            [&] { return Theirs<Fn>(self, nullptr, pos, first, last, tag); },
            [&] { return Abi::RangeInsert(self, nullptr, pos, first, last, tag), std::uintptr_t(0); });
    }
    static T* __fastcall EraseRange(Vector* self, void*, T* first, T* last) {
        using Fn = T*(__fastcall*)(Vector*, void*, T*, T*);
        T* result = nullptr;
        CheckChange(
            Name<A>("stlp::vector::erase"), self, original != nullptr, [&] { return Theirs<Fn>(self, nullptr, first, last); },
            [&] {
                result = Abi::EraseRange(self, nullptr, first, last);
                return reinterpret_cast<std::uintptr_t>(result);
            });
        return result;
    }
    static void __fastcall Clear(Vector* self) {
        using Fn = void(__fastcall*)(Vector*);
        CheckChange(
            Name<A>("stlp::vector::clear"), self, original != nullptr, [&] { return Theirs<Fn>(self); },
            [&] { return Abi::Clear(self), std::uintptr_t(0); });
    }
    static void __fastcall Resize(Vector* self, void*, std::size_t n, const T* x) {
        using Fn = void(__fastcall*)(Vector*, void*, std::size_t, const T*);
        CheckChange(
            Name<A>("stlp::vector::resize"), self, original != nullptr, [&] { return Theirs<Fn>(self, nullptr, n, x); },
            [&] { return Abi::Resize(self, nullptr, n, x), std::uintptr_t(0); });
    }
    static void __fastcall Reserve(Vector* self, void*, std::size_t n) {
        using Fn = void(__fastcall*)(Vector*, void*, std::size_t);
        CheckChange(
            Name<A>("stlp::vector::reserve"), self, original != nullptr, [&] { return Theirs<Fn>(self, nullptr, n); },
            [&] { return Abi::Reserve(self, nullptr, n), std::uintptr_t(0); });
    }
    static void __fastcall PushBack(Vector* self, void*, const T* x) {
        using Fn = void(__fastcall*)(Vector*, void*, const T*);
        CheckChange(
            Name<A>("stlp::vector::push_back"), self, original != nullptr, [&] { return Theirs<Fn>(self, nullptr, x); },
            [&] { return Abi::PushBack(self, nullptr, x), std::uintptr_t(0); });
    }
    static Vector* __fastcall Assign(Vector* self, void*, const Vector* x) {
        using Fn = Vector*(__fastcall*)(Vector*, void*, const Vector*);
        CheckChange(
            Name<A>("stlp::vector::operator="), self, original != nullptr, [&] { return Theirs<Fn>(self, nullptr, x); },
            [&] { return reinterpret_cast<std::uintptr_t>(Abi::Assign(self, nullptr, x)); });
        return self;
    }

    // A new block: the original's is given back to the pool, then ours must take the same one
    // (when pool-sized) and hold the same bytes.
    template <typename Theirs, typename Ours>
    static T* CheckBlock(const char* name, std::size_t n, Theirs theirs, Ours ours) {
        T* expected = theirs();
        std::vector<unsigned char> bytes(reinterpret_cast<unsigned char*>(expected),
                                         reinterpret_cast<unsigned char*>(expected) + n * sizeof(T));
        stlp::deallocate(expected, n * sizeof(T));
        T* block = ours();
        bool ok = (n * sizeof(T) > 128 || block == expected) &&
                  std::memcmp(block, bytes.data(), bytes.size()) == 0;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p, %u elements", block, expected, static_cast<unsigned>(n));
        ei::verify::Report(name, ok, detail);
        return block;
    }
    static T* __fastcall AllocateAndCopy(Vector* self, void*, std::size_t n, const T* first, const T* last) {
        using Fn = T*(__fastcall*)(Vector*, void*, std::size_t, const T*, const T*);
        return CheckBlock(
            Name<A>("stlp::vector::_M_allocate_and_copy"), n,
            [&] { return reinterpret_cast<Fn>(original)(self, nullptr, n, first, last); },
            [&] { return Abi::AllocateAndCopy(self, nullptr, n, first, last); });
    }
    static void __fastcall ConstructWithCapacity(Vector* self, void*, std::size_t n, const void* allocator) {
        using Fn = void(__fastcall*)(Vector*, void*, std::size_t, const void*);
        Vector scratch;
        reinterpret_cast<Fn>(original)(&scratch, nullptr, n, allocator);
        T* expected = scratch.Start();
        bool theirsOk = scratch.Finish() == expected && scratch.Capacity() == n;
        stlp::deallocate(expected, n * sizeof(T));
        scratch.Start() = scratch.Finish() = scratch.EndOfStorage() = nullptr;
        Abi::ConstructWithCapacity(self, nullptr, n, allocator);
        bool ok = theirsOk && (n * sizeof(T) > 128 || self->Start() == expected) && self->Finish() == self->Start() &&
                  self->Capacity() == n;
        char detail[64] = "";
        if (!ok) std::snprintf(detail, sizeof(detail), "ours %p, original's %p, %u elements", self->Start(), expected, static_cast<unsigned>(n));
        ei::verify::Report(Name<A>("stlp::_Vector_base::_Vector_base(n)"), ok, detail);
    }

    // The scalar deleting destructor: ours only (with flag 1 it frees the object itself); the
    // storage must be on top of its free list.
    static Vector* __fastcall DeleteBase(Vector* self, void*, unsigned flags) {
        T* start = self->Start();
        std::size_t bytes = self->Capacity() * sizeof(T);
        void** list = start ? FreeList(bytes) : nullptr;
        Vector* result = Abi::DeleteBase(self, nullptr, flags);
        bool ok = result == self && (!list || *list == start);
        ei::verify::Report(Name<A>("stlp::_Vector_base::scalar deleting destructor"), ok,
                           ok ? "" : "the storage is not on top of its free list");
        return result;
    }
};
template <std::uintptr_t A> using Change1 = ChangeCheck<A, Vector1>;
template <std::uintptr_t A> using Change4 = ChangeCheck<A, Vector4>;
template <std::uintptr_t A> using Change8 = ChangeCheck<A, Vector8>;
template <std::uintptr_t A> using Change12 = ChangeCheck<A, Vector12>;
template <std::uintptr_t A> using Change60 = ChangeCheck<A, Vector60>;
template <std::uintptr_t A> using Change16 = ChangeCheck<A, Vector16>;
template <std::uintptr_t A> using Change24 = ChangeCheck<A, Vector24>;

} // namespace

// _Vector_base(const allocator_type&).
EI_VERIFY(0x00406240, ConstructBaseCheck<0x00406240>::Verify, 6, ConstructBaseCheck<0x00406240>::original);
EI_VERIFY(0x00406530, ConstructBaseCheck<0x00406530>::Verify, 6, ConstructBaseCheck<0x00406530>::original);
EI_VERIFY(0x0040ADE0, ConstructBaseCheck<0x0040ADE0>::Verify, 6, ConstructBaseCheck<0x0040ADE0>::original);
EI_VERIFY(0x0040AE50, ConstructBaseCheck<0x0040AE50>::Verify, 6, ConstructBaseCheck<0x0040AE50>::original);
EI_VERIFY(0x0041FF90, ConstructBaseCheck<0x0041FF90>::Verify, 6, ConstructBaseCheck<0x0041FF90>::original);
EI_VERIFY(0x0041FFF0, ConstructBaseCheck<0x0041FFF0>::Verify, 6, ConstructBaseCheck<0x0041FFF0>::original);
EI_VERIFY(0x004217E0, ConstructBaseCheck<0x004217E0>::Verify, 6, ConstructBaseCheck<0x004217E0>::original);
EI_VERIFY(0x00424B40, ConstructBaseCheck<0x00424B40>::Verify, 6, ConstructBaseCheck<0x00424B40>::original);
EI_VERIFY(0x0042D310, ConstructBaseCheck<0x0042D310>::Verify, 6, ConstructBaseCheck<0x0042D310>::original);
EI_VERIFY(0x00438CE0, ConstructBaseCheck<0x00438CE0>::Verify, 6, ConstructBaseCheck<0x00438CE0>::original);
EI_VERIFY(0x00439E10, ConstructBaseCheck<0x00439E10>::Verify, 6, ConstructBaseCheck<0x00439E10>::original);
EI_VERIFY(0x004E2BA0, ConstructBaseCheck<0x004E2BA0>::Verify, 6, ConstructBaseCheck<0x004E2BA0>::original);
EI_VERIFY(0x0053B440, ConstructBaseCheck<0x0053B440>::Verify, 6, ConstructBaseCheck<0x0053B440>::original);
EI_VERIFY(0x0053B5E0, ConstructBaseCheck<0x0053B5E0>::Verify, 6, ConstructBaseCheck<0x0053B5E0>::original);
EI_VERIFY(0x0053B780, ConstructBaseCheck<0x0053B780>::Verify, 6, ConstructBaseCheck<0x0053B780>::original);
EI_VERIFY(0x0053B980, ConstructBaseCheck<0x0053B980>::Verify, 6, ConstructBaseCheck<0x0053B980>::original);
EI_VERIFY(0x0053BB80, ConstructBaseCheck<0x0053BB80>::Verify, 6, ConstructBaseCheck<0x0053BB80>::original);
EI_VERIFY(0x0053F4F0, ConstructBaseCheck<0x0053F4F0>::Verify, 6, ConstructBaseCheck<0x0053F4F0>::original);
EI_VERIFY(0x0055A3E0, ConstructBaseCheck<0x0055A3E0>::Verify, 6, ConstructBaseCheck<0x0055A3E0>::original);
EI_VERIFY(0x0056F570, ConstructBaseCheck<0x0056F570>::Verify, 6, ConstructBaseCheck<0x0056F570>::original);
EI_VERIFY(0x00584720, ConstructBaseCheck<0x00584720>::Verify, 6, ConstructBaseCheck<0x00584720>::original);
EI_VERIFY(0x00598940, ConstructBaseCheck<0x00598940>::Verify, 6, ConstructBaseCheck<0x00598940>::original);
EI_VERIFY(0x005F8F10, ConstructBaseCheck<0x005F8F10>::Verify, 6, ConstructBaseCheck<0x005F8F10>::original);
EI_VERIFY(0x005F9540, ConstructBaseCheck<0x005F9540>::Verify, 6, ConstructBaseCheck<0x005F9540>::original);
EI_VERIFY(0x0062B550, ConstructBaseCheck<0x0062B550>::Verify, 6, ConstructBaseCheck<0x0062B550>::original);
EI_VERIFY(0x0062B570, ConstructBaseCheck<0x0062B570>::Verify, 6, ConstructBaseCheck<0x0062B570>::original);
EI_VERIFY(0x0062B5E0, ConstructBaseCheck<0x0062B5E0>::Verify, 6, ConstructBaseCheck<0x0062B5E0>::original);
EI_VERIFY(0x006342A0, ConstructBaseCheck<0x006342A0>::Verify, 6, ConstructBaseCheck<0x006342A0>::original);
EI_VERIFY(0x006AFB00, ConstructBaseCheck<0x006AFB00>::Verify, 6, ConstructBaseCheck<0x006AFB00>::original);

// _STLP_alloc_proxy(const allocator_type&, const pointer&).
EI_VERIFY(0x00406260, ProxyConstructCheck<0x00406260>::Verify, 6, ProxyConstructCheck<0x00406260>::original);
EI_VERIFY(0x00406550, ProxyConstructCheck<0x00406550>::Verify, 6, ProxyConstructCheck<0x00406550>::original);
EI_VERIFY(0x00425080, ProxyConstructCheck<0x00425080>::Verify, 6, ProxyConstructCheck<0x00425080>::original);
EI_VERIFY(0x004263D0, ProxyConstructCheck<0x004263D0>::Verify, 6, ProxyConstructCheck<0x004263D0>::original);
EI_VERIFY(0x0043A980, ProxyConstructCheck<0x0043A980>::Verify, 6, ProxyConstructCheck<0x0043A980>::original);
EI_VERIFY(0x005AD640, ProxyConstructCheck<0x005AD640>::Verify, 6, ProxyConstructCheck<0x005AD640>::original);
EI_VERIFY(0x005FA0C0, ProxyConstructCheck<0x005FA0C0>::Verify, 6, ProxyConstructCheck<0x005FA0C0>::original);
EI_VERIFY(0x005FA340, ProxyConstructCheck<0x005FA340>::Verify, 6, ProxyConstructCheck<0x005FA340>::original);
EI_VERIFY(0x00619750, ProxyConstructCheck<0x00619750>::Verify, 6, ProxyConstructCheck<0x00619750>::original);

// get_allocator().
EI_VERIFY(0x00421EB0, GetAllocatorCheck<0x00421EB0>::Verify, 5, GetAllocatorCheck<0x00421EB0>::original);
EI_VERIFY(0x00438CC0, GetAllocatorCheck<0x00438CC0>::Verify, 5, GetAllocatorCheck<0x00438CC0>::original);

// ~_Vector_base().
EI_VERIFY(0x0040C720, Free1<0x0040C720>::DestroyBase, 5, Free1<0x0040C720>::original);
EI_VERIFY(0x0040C770, Free1<0x0040C770>::DestroyBase, 5, Free1<0x0040C770>::original);
EI_VERIFY(0x00425040, Free1<0x00425040>::DestroyBase, 5, Free1<0x00425040>::original);
EI_VERIFY(0x0042D2C0, Free1<0x0042D2C0>::DestroyBase, 5, Free1<0x0042D2C0>::original);
EI_VERIFY(0x004DEAB0, Free1<0x004DEAB0>::DestroyBase, 5, Free1<0x004DEAB0>::original);
EI_VERIFY(0x00598D80, Free1<0x00598D80>::DestroyBase, 5, Free1<0x00598D80>::original);
EI_VERIFY(0x006B72E0, Free1<0x006B72E0>::DestroyBase, 5, Free1<0x006B72E0>::original);
EI_VERIFY(0x00405BF0, Free4<0x00405BF0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x00405DB0, Free4<0x00405DB0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x00405E00, Free4<0x00405E00>::DestroyBase, 0, s_unused);
EI_VERIFY(0x00405E50, Free4<0x00405E50>::DestroyBase, 0, s_unused);
EI_VERIFY(0x0043A2B0, Free4<0x0043A2B0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x0043AAB0, Free4<0x0043AAB0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x004AEDA0, Free4<0x004AEDA0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x004AEF50, Free4<0x004AEF50>::DestroyBase, 0, s_unused);
EI_VERIFY(0x004AF100, Free4<0x004AF100>::DestroyBase, 0, s_unused);
EI_VERIFY(0x004E2A20, Free4<0x004E2A20>::DestroyBase, 0, s_unused);
EI_VERIFY(0x005150D0, Free4<0x005150D0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x00520EB0, Free4<0x00520EB0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x0053B930, Free4<0x0053B930>::DestroyBase, 0, s_unused);
EI_VERIFY(0x0053BE70, Free4<0x0053BE70>::DestroyBase, 0, s_unused);
EI_VERIFY(0x0053BF00, Free4<0x0053BF00>::DestroyBase, 0, s_unused);
EI_VERIFY(0x005843A0, Free4<0x005843A0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x00584450, Free4<0x00584450>::DestroyBase, 0, s_unused);
EI_VERIFY(0x005F9880, Free4<0x005F9880>::DestroyBase, 0, s_unused);
EI_VERIFY(0x00609F50, Free4<0x00609F50>::DestroyBase, 0, s_unused);
EI_VERIFY(0x006BC4D0, Free4<0x006BC4D0>::DestroyBase, 0, s_unused);
EI_VERIFY(0x006BC510, Free4<0x006BC510>::DestroyBase, 0, s_unused);
EI_VERIFY(0x0056F590, Free12<0x0056F590>::DestroyBase, 5, Free12<0x0056F590>::original);
EI_VERIFY(0x0053B600, Free96<0x0053B600>::DestroyBase, 5, Free96<0x0053B600>::original);
EI_VERIFY(0x0053B460, Free288<0x0053B460>::DestroyBase, 5, Free288<0x0053B460>::original);

// _STLP_alloc_proxy::deallocate(p, n) (RET 8).
EI_VERIFY(0x004251B0, Free4<0x004251B0>::Deallocate, 6, Free4<0x004251B0>::original);
EI_VERIFY(0x00425210, Free4<0x00425210>::Deallocate, 6, Free4<0x00425210>::original);
EI_VERIFY(0x0043A300, Free4<0x0043A300>::Deallocate, 6, Free4<0x0043A300>::original);
EI_VERIFY(0x00520F20, Free4<0x00520F20>::Deallocate, 6, Free4<0x00520F20>::original);
EI_VERIFY(0x00404D10, Free12<0x00404D10>::Deallocate, 6, Free12<0x00404D10>::original);
EI_VERIFY(0x00407DA0, Free12<0x00407DA0>::Deallocate, 6, Free12<0x00407DA0>::original);
EI_VERIFY(0x00408020, Free12<0x00408020>::Deallocate, 6, Free12<0x00408020>::original);
EI_VERIFY(0x0040B560, Free12<0x0040B560>::Deallocate, 6, Free12<0x0040B560>::original);
EI_VERIFY(0x0040B5A0, Free12<0x0040B5A0>::Deallocate, 6, Free12<0x0040B5A0>::original);
EI_VERIFY(0x00422390, Free12<0x00422390>::Deallocate, 6, Free12<0x00422390>::original);
EI_VERIFY(0x00422790, Free12<0x00422790>::Deallocate, 6, Free12<0x00422790>::original);
EI_VERIFY(0x00425470, Free12<0x00425470>::Deallocate, 6, Free12<0x00425470>::original);
EI_VERIFY(0x004256D0, Free12<0x004256D0>::Deallocate, 6, Free12<0x004256D0>::original);
EI_VERIFY(0x004257A0, Free12<0x004257A0>::Deallocate, 6, Free12<0x004257A0>::original);
EI_VERIFY(0x004258F0, Free12<0x004258F0>::Deallocate, 6, Free12<0x004258F0>::original);
EI_VERIFY(0x00425A40, Free12<0x00425A40>::Deallocate, 6, Free12<0x00425A40>::original);
EI_VERIFY(0x00425D60, Free12<0x00425D60>::Deallocate, 6, Free12<0x00425D60>::original);
EI_VERIFY(0x00425E30, Free12<0x00425E30>::Deallocate, 6, Free12<0x00425E30>::original);
EI_VERIFY(0x00425F00, Free12<0x00425F00>::Deallocate, 6, Free12<0x00425F00>::original);
EI_VERIFY(0x00426050, Free12<0x00426050>::Deallocate, 6, Free12<0x00426050>::original);
EI_VERIFY(0x00426120, Free12<0x00426120>::Deallocate, 6, Free12<0x00426120>::original);
EI_VERIFY(0x004261F0, Free12<0x004261F0>::Deallocate, 6, Free12<0x004261F0>::original);
EI_VERIFY(0x00426330, Free12<0x00426330>::Deallocate, 6, Free12<0x00426330>::original);
EI_VERIFY(0x00439E60, Free12<0x00439E60>::Deallocate, 6, Free12<0x00439E60>::original);
EI_VERIFY(0x0043E0A0, Free12<0x0043E0A0>::Deallocate, 6, Free12<0x0043E0A0>::original);
EI_VERIFY(0x0053BF40, Free12<0x0053BF40>::Deallocate, 6, Free12<0x0053BF40>::original);
EI_VERIFY(0x0056F700, Free12<0x0056F700>::Deallocate, 6, Free12<0x0056F700>::original);
EI_VERIFY(0x00425AB0, Free16<0x00425AB0>::Deallocate, 6, Free16<0x00425AB0>::original);
EI_VERIFY(0x00426260, Free16<0x00426260>::Deallocate, 6, Free16<0x00426260>::original);
EI_VERIFY(0x0043A410, Free20<0x0043A410>::Deallocate, 6, Free20<0x0043A410>::original);
EI_VERIFY(0x004648D0, Free20<0x004648D0>::Deallocate, 6, Free20<0x004648D0>::original);
EI_VERIFY(0x0053CA60, Free20<0x0053CA60>::Deallocate, 6, Free20<0x0053CA60>::original);
EI_VERIFY(0x0053CAA0, Free20<0x0053CAA0>::Deallocate, 6, Free20<0x0053CAA0>::original);
EI_VERIFY(0x0053CAE0, Free20<0x0053CAE0>::Deallocate, 6, Free20<0x0053CAE0>::original);
EI_VERIFY(0x005AE150, Free20<0x005AE150>::Deallocate, 6, Free20<0x005AE150>::original);
EI_VERIFY(0x0040ACE0, Free24<0x0040ACE0>::Deallocate, 6, Free24<0x0040ACE0>::original);
EI_VERIFY(0x0043A480, Free24<0x0043A480>::Deallocate, 6, Free24<0x0043A480>::original);
EI_VERIFY(0x0043A4E0, Free24<0x0043A4E0>::Deallocate, 6, Free24<0x0043A4E0>::original);
EI_VERIFY(0x006135F0, Free24<0x006135F0>::Deallocate, 6, Free24<0x006135F0>::original);
EI_VERIFY(0x0056F840, Free32<0x0056F840>::Deallocate, 6, Free32<0x0056F840>::original);
EI_VERIFY(0x00411E20, Free36<0x00411E20>::Deallocate, 6, Free36<0x00411E20>::original);

// _STLP_alloc_proxy::deallocate(p, n) (ECX p, EDX n).
EI_VERIFY(0x00406590, Free4<0x00406590>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00411EA0, Free4<0x00411EA0>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00426490, Free4<0x00426490>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x004268A0, Free4<0x004268A0>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x004AA7D0, Free4<0x004AA7D0>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x004AA810, Free4<0x004AA810>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00520F60, Free4<0x00520F60>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00584A90, Free4<0x00584A90>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x0069C990, Free4<0x0069C990>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x005FA7E0, Free8<0x005FA7E0>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x006344C0, Free8<0x006344C0>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00426860, Free12<0x00426860>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00426920, Free12<0x00426920>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00426960, Free12<0x00426960>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x0043AB30, Free12<0x0043AB30>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00461710, Free12<0x00461710>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x0047A650, Free12<0x0047A650>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00498C40, Free12<0x00498C40>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x0056FB00, Free12<0x0056FB00>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x0056FB40, Free12<0x0056FB40>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x0068C640, Free12<0x0068C640>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x0043B180, Free20<0x0043B180>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x004FC080, Free20<0x004FC080>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00411B30, Free24<0x00411B30>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x005FA920, Free36<0x005FA920>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x005FA960, Free36<0x005FA960>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x004452D0, Free40<0x004452D0>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x00479530, Free48<0x00479530>::DeallocateFast, 0, s_unused);
EI_VERIFY(0x0043AAF0, Free288<0x0043AAF0>::DeallocateFast, 0, s_unused);

// Plain-data members (ChangeCheck): against the original where the first instructions can be
// copied; the deleting destructors ours only.
EI_VERIFY(0x00421F60, Change4<0x00421F60>::ConstructWithCapacity, 7, Change4<0x00421F60>::original);
EI_VERIFY(0x00438D00, Change4<0x00438D00>::ConstructWithCapacity, 7, Change4<0x00438D00>::original);
EI_VERIFY(0x00464DF0, Change1<0x00464DF0>::DeleteBase, 0, s_unused);
EI_VERIFY(0x0053D970, Change1<0x0053D970>::DeleteBase, 0, s_unused);
EI_VERIFY(0x0053D990, Change1<0x0053D990>::DeleteBase, 0, s_unused);
EI_VERIFY(0x0053D9B0, Change1<0x0053D9B0>::DeleteBase, 0, s_unused);
EI_VERIFY(0x0069DB00, Change16<0x0069DB00>::AllocateAndCopy, 5, Change16<0x0069DB00>::original);
EI_VERIFY(0x0043B6C0, Change4<0x0043B6C0>::InsertOverflow, 5, Change4<0x0043B6C0>::original);
EI_VERIFY(0x0043B850, Change4<0x0043B850>::InsertOverflow, 5, Change4<0x0043B850>::original);
EI_VERIFY(0x0043D5F0, Change4<0x0043D5F0>::InsertOverflow, 5, Change4<0x0043D5F0>::original);
EI_VERIFY(0x004E2FA0, Change4<0x004E2FA0>::InsertOverflow, 5, Change4<0x004E2FA0>::original);
EI_VERIFY(0x00571B00, Change4<0x00571B00>::InsertOverflow, 5, Change4<0x00571B00>::original);
EI_VERIFY(0x00587700, Change4<0x00587700>::InsertOverflow, 5, Change4<0x00587700>::original);
EI_VERIFY(0x005FA9A0, Change4<0x005FA9A0>::InsertOverflow, 5, Change4<0x005FA9A0>::original);
EI_VERIFY(0x0069D470, Change4<0x0069D470>::InsertOverflow, 5, Change4<0x0069D470>::original);
EI_VERIFY(0x006ADD50, Change4<0x006ADD50>::InsertOverflow, 5, Change4<0x006ADD50>::original);
EI_VERIFY(0x00441180, Change16<0x00441180>::InsertOverflow, 5, Change16<0x00441180>::original);
EI_VERIFY(0x0069CC20, Change16<0x0069CC20>::InsertOverflow, 5, Change16<0x0069CC20>::original);
EI_VERIFY(0x00573350, Change24<0x00573350>::InsertOverflow, 5, Change24<0x00573350>::original);
EI_VERIFY(0x0040B150, Change4<0x0040B150>::FillInsert, 8, Change4<0x0040B150>::original);
EI_VERIFY(0x0055B030, Change4<0x0055B030>::FillInsert, 8, Change4<0x0055B030>::original);
EI_VERIFY(0x005ADAD0, Change4<0x005ADAD0>::FillInsert, 8, Change4<0x005ADAD0>::original);
EI_VERIFY(0x006ADC10, Change4<0x006ADC10>::FillInsert, 5, Change4<0x006ADC10>::original);
EI_VERIFY(0x0042D370, Change1<0x0042D370>::FillInsert, 7, Change1<0x0042D370>::original);
EI_VERIFY(0x0069ECD0, Change16<0x0069ECD0>::RangeInsert, 5, Change16<0x0069ECD0>::original);
EI_VERIFY(0x0069E3F0, Change4<0x0069E3F0>::RangeInsert, 8, Change4<0x0069E3F0>::original);
EI_VERIFY(0x0069BD30, Change4<0x0069BD30>::PushBack, 6, Change4<0x0069BD30>::original);
EI_VERIFY(0x0069BDC0, Change4<0x0069BDC0>::PushBack, 6, Change4<0x0069BDC0>::original);
EI_VERIFY(0x0069BDF0, Change4<0x0069BDF0>::PushBack, 6, Change4<0x0069BDF0>::original);
EI_VERIFY(0x0042D340, Change1<0x0042D340>::EraseRange, 5, Change1<0x0042D340>::original);
EI_VERIFY(0x0056F5E0, Change4<0x0056F5E0>::EraseRange, 5, Change4<0x0056F5E0>::original);
EI_VERIFY(0x0056E870, Change4<0x0056E870>::Clear, 6, Change4<0x0056E870>::original);
EI_VERIFY(0x004D8EB0, Change1<0x004D8EB0>::Resize, 7, Change1<0x004D8EB0>::original);
EI_VERIFY(0x004AE3A0, Change4<0x004AE3A0>::Resize, 5, Change4<0x004AE3A0>::original);
EI_VERIFY(0x004FE0C0, Change4<0x004FE0C0>::Resize, 5, Change4<0x004FE0C0>::original);
EI_VERIFY(0x0056F360, Change12<0x0056F360>::Resize, 8, Change12<0x0056F360>::original);
EI_VERIFY(0x0040AF00, Change4<0x0040AF00>::Reserve, 5, Change4<0x0040AF00>::original);
EI_VERIFY(0x005AD880, Change4<0x005AD880>::Reserve, 5, Change4<0x005AD880>::original);
EI_VERIFY(0x0064AF30, Change4<0x0064AF30>::Assign, 5, Change4<0x0064AF30>::original);
EI_VERIFY(0x004387D0, Free8<0x004387D0>::DestroyBase, 5, Free8<0x004387D0>::original);

// vector<Replicated> (src/game/record_vectors.cpp; 8-byte plain records).
EI_VERIFY(0x00426B10, Change8<0x00426B10>::InsertOverflow, 5, Change8<0x00426B10>::original);
// The camp's trade records (src/game/record_vectors.cpp; 0x3C-byte plain records).
EI_VERIFY(0x0062A0B0, Change60<0x0062A0B0>::InsertOverflow, 5, Change60<0x0062A0B0>::original);
