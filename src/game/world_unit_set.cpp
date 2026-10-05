// The unit set's writer (00559E60), used by the server's SendZone (world_sync.cpp). See
// docs/notes/world.md ("Client maps", the unit set). The set's three STLport hash_maps are walked
// with the original's iterator helpers, so the order matches theirs.
#include "world.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_engine.h"
#include "originals_library.h"

#include <cstdint>
#include <cstring>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// A hashtable iterator: the node and its table.
struct Iterator {
    std::uint8_t* node;
    void* table;
};

// begin() of the outer map [0055AB50], of a node's unit map [0055ADF0] and of the third map
// [0055AE50]; the iterators' step to the next bucket's head when a chain ends [0055B9D0 outer,
// 0055B500 units]; the third map's hash of a key [0055C5C0] (bucket index).
__declspec(naked) Iterator* __fastcall OuterBegin(void*, void*, Iterator*) EI_ORIGINAL(0x0055AB50)
__declspec(naked) Iterator* __fastcall UnitsBegin(void*, void*, Iterator*) EI_ORIGINAL(0x0055ADF0)
__declspec(naked) Iterator* __fastcall ThirdBegin(void*, void*, Iterator*) EI_ORIGINAL(0x0055AE50)
__declspec(naked) std::uint8_t* __fastcall OuterNextBucket(Iterator*) EI_ORIGINAL(0x0055B9D0)
__declspec(naked) std::uint8_t* __fastcall UnitsNextBucket(Iterator*) EI_ORIGINAL(0x0055B500)
__declspec(naked) std::uint32_t __fastcall ThirdBucketOf(void*, void*, const void* /*key*/) EI_ORIGINAL(0x0055C5C0)
// A unit entry's head written to the stream [00558FA0, on the entry].
__declspec(naked) void __fastcall WriteEntryHead(void*, void*, void* /*stream*/) EI_ORIGINAL(0x00558FA0)

constexpr int kSlotWrite = 2;   // stream: (data, size)

void Put(void* stream, const void* data, std::uint32_t size) { ei::VCall<void>(stream, kSlotWrite, data, size); }

void PutWord(void* stream, std::uint32_t value) { Put(stream, &value, 4); }

// ++iterator: the chain's next node, else the helper's next bucket head.
std::uint8_t* Step(Iterator& it, std::uint8_t* (__fastcall* nextBucket)(Iterator*)) {
    std::uint8_t* next = *reinterpret_cast<std::uint8_t**>(it.node);
    return next ? next : nextBucket(&it);
}

} // namespace

void UnitSet::Write(void* stream) {
    // The number of unit entries over every node's unit map.
    std::uint32_t count = 0;
    Iterator outer;
    OuterBegin(this, nullptr, &outer);
    while (outer.node) {
        Iterator units;
        UnitsBegin(outer.node + 8, nullptr, &units);
        while (units.node) {
            ++count;
            units.node = Step(units, UnitsNextBucket);
        }
        outer.node = Step(outer, OuterNextBucket);
    }
    PutWord(stream, count);

    OuterBegin(this, nullptr, &outer);
    while (outer.node) {
        Iterator units;
        UnitsBegin(outer.node + 8, nullptr, &units);
        while (units.node) {
            std::uint8_t* entry = units.node + 8;
            WriteEntryHead(entry, nullptr, stream);
            PutWord(stream, At<std::uint32_t>(entry, 0x24));
            PutWord(stream, At<std::uint32_t>(entry, 0x28));
            PutWord(stream, At<std::uint32_t>(entry, 0x2C));
            auto strings = [entry](std::uint32_t i) { return At<const char**>(entry, 0x30)[i]; };
            auto stringCount = [entry] {
                return static_cast<std::uint32_t>((At<std::uint8_t*>(entry, 0x34) - At<std::uint8_t*>(entry, 0x30)) >> 2);
            };
            PutWord(stream, stringCount());
            for (std::uint32_t i = 0; i < stringCount(); ++i) {
                const char* text = strings(i);
                Put(stream, text, static_cast<std::uint32_t>(std::strlen(text) + 1));
            }
            units.node = Step(units, UnitsNextBucket);
        }
        outer.node = Step(outer, OuterNextBucket);
    }
    PutWord(stream, At<std::uint32_t>(this, 0x24));

    Iterator third;
    ThirdBegin(reinterpret_cast<std::uint8_t*>(this) + 0x14, nullptr, &third);
    while (third.node) {
        std::uint8_t* entry = third.node + 8;
        std::uint32_t key = At<std::uint16_t>(entry, 0);   // two bytes written
        Put(stream, &key, 2);
        auto counted = [stream, entry](int offset) {
            std::uint8_t* first = At<std::uint8_t*>(entry, offset);
            std::uint32_t n = static_cast<std::uint32_t>((At<std::uint8_t*>(entry, offset + 4) - first) >> 2);
            PutWord(stream, n);
            first = At<std::uint8_t*>(entry, offset);
            n = static_cast<std::uint32_t>((At<std::uint8_t*>(entry, offset + 4) - first) >> 2);
            Put(stream, first, n << 2);
        };
        counted(4);
        counted(0x10);
        std::uint32_t flag = At<std::uint8_t>(entry, 0x1C);
        Put(stream, &flag, 1);
        PutWord(stream, At<std::uint32_t>(entry, 0x20));

        std::uint8_t* next = *reinterpret_cast<std::uint8_t**>(third.node);
        if (!next) {
            std::uint32_t bucket = ThirdBucketOf(third.table, nullptr, third.node + 4);
            std::uint8_t** buckets = At<std::uint8_t**>(third.table, 4);
            std::uint32_t buckets_n = static_cast<std::uint32_t>(
                (At<std::uint8_t*>(third.table, 8) - At<std::uint8_t*>(third.table, 4)) >> 2);
            while (++bucket < buckets_n && !(next = buckets[bucket])) {
            }
        }
        third.node = next;
    }
}

// ---- The unit look lists' reader (0055A190) ------------------------------------------------------

namespace {

// vector<0x3C record>::resize [0055A400], vector<0x24 record>::resize [0055A580], vector<int>::
// resize(n, value) [004FE0C0], vector<CString>::resize [0051F5B0], the stream's CString reader
// [004E5840, on the stream].
__declspec(naked) void __fastcall ResizeLooks(void*, void*, std::uint32_t) EI_ORIGINAL(0x0055A400)
__declspec(naked) void __fastcall ResizeLooks2(void*, void*, std::uint32_t) EI_ORIGINAL(0x0055A580)
__declspec(naked) void __fastcall ResizeInts(void*, void*, std::uint32_t, const int*) EI_ORIGINAL(0x004FE0C0)
void __fastcall ResizeStrings(void* a0, void*, std::uint32_t a2) { CStringVectorResizeOriginal(a0, nullptr, a2); }
void __fastcall ReadCString(void* a0, void*, void* a2) { StreamReadCStringOriginal(a0, nullptr, a2); }

constexpr int kSlotRead = 1;   // stream: (data, size)

void Get(void* stream, void* data, std::uint32_t size) { ei::VCall<void>(stream, kSlotRead, data, size); }

std::uint32_t Count(void* vector) {
    return static_cast<std::uint32_t>((At<std::uint8_t*>(vector, 4) - At<std::uint8_t*>(vector, 0)) >> 2);
}

// A counted dword array: the count, the vector resized (new entries 0), the data.
void GetInts(void* stream, std::uint8_t* vector) {
    std::uint32_t n;
    Get(stream, &n, 4);
    int zero = 0;
    ResizeInts(vector, nullptr, n, &zero);
    Get(stream, At<std::uint8_t*>(vector, 0), Count(vector) << 2);
}

} // namespace

void __fastcall ReadUnitLooks(void* stream, void* first, void* second) {
    std::uint32_t count;
    Get(stream, &count, 4);
    ResizeLooks(first, nullptr, count);
    for (std::uint8_t* record = At<std::uint8_t*>(first, 0); record != At<std::uint8_t*>(first, 4); record += 0x3C) {
        Get(stream, record, 2);
        GetInts(stream, record + 4);
        GetInts(stream, record + 0x10);
        Get(stream, record + 0x1C, 1);
        Get(stream, record + 0x20, 4);
        Get(stream, record + 0x24, 4);
        Get(stream, record + 0x28, 4);
        Get(stream, record + 0x2C, 4);
        std::uint32_t strings;
        Get(stream, &strings, 4);
        ResizeStrings(record + 0x30, nullptr, strings);
        for (std::uint32_t i = 0; i < Count(record + 0x30); ++i)
            ReadCString(stream, nullptr, At<std::uint8_t*>(record, 0x30) + i * 4);
    }
    Get(stream, &count, 4);
    ResizeLooks2(second, nullptr, count);
    for (std::uint8_t* record = At<std::uint8_t*>(second, 0); record != At<std::uint8_t*>(second, 4); record += 0x24) {
        Get(stream, record, 2);
        GetInts(stream, record + 4);
        GetInts(stream, record + 0x10);
        Get(stream, record + 0x1C, 1);
        Get(stream, record + 0x20, 4);
    }
}

EI_REPLACE(0x0055A190, ReadUnitLooks);
EI_REPLACE_METHOD(0x00559E60, UnitSet::Write);
