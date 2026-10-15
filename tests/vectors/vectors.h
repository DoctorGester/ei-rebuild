#pragma once

// Function vectors (docs/notes/standalone-verification.md, section 3): calls of the original game.exe,
// recorded in the verify build's record mode, replayed against our code by ei_vectors.exe without
// the game.
//
// Record mode (EI_VECTORS=1, verify build): every function with a recorder (EI_RECORD in
// tests/vectors/record_<family>.cpp) runs the original through it. The recorder fills a payload (the
// arguments, the state the function reads, the result and the state afterwards) and offers it to its
// Sampler, which keeps a deterministic sample: repeats are dropped, and of the distinct calls a
// reservoir of `cap` per bucket is kept (a bucket is a path the function must not lose among
// common calls, such as the generator's refill). At the end of a harness run they are written to
// <run folder>/vectors/<family>/<function>.vec; tools/vectors.py merges runs into tests/vectors/data.
//
// Replay (ei_vectors.exe, tests/vectors/replay_<family>.cpp): each file's records go to the check
// registered under the file's function name, under the x87 control word recorded with the call.
//
// File: FileHeader, then `count` records, each a RecordHeader followed by `payloadSize` bytes. A
// payload is a plain struct (tests/vectors/<family>_vectors.h) with explicit padding, zeroed before
// it is filled, so equal calls give equal bytes.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace ei::vectors {

constexpr std::uint32_t kMagic = 0x31564945;   // "EIV1"
constexpr std::uint32_t kVersion = 1;

struct FileHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t payloadSize;   // bytes of one payload
    std::uint32_t count;         // records in the file
    std::uint32_t cap;           // records kept per bucket when recorded
    std::uint32_t reserved[3];
    char function[64];           // the name the replay finds its check by (NUL-terminated)
};
static_assert(sizeof(FileHeader) == 96);

struct RecordHeader {
    std::uint16_t controlWord;   // the x87 control word at the call
    std::uint8_t bucket;
    std::uint8_t reserved;
};
static_assert(sizeof(RecordHeader) == 4);

// The x87 control word now.
inline std::uint16_t ControlWord() {
    std::uint16_t cw;
    __asm fnstcw cw
    return cw;
}

// A zeroed payload (padding included), to be filled by a recorder.
template <typename T>
T Zeroed() {
    static_assert(std::is_trivially_copyable_v<T>);
    T v;
    std::memset(&v, 0, sizeof(v));
    return v;
}

// ---- Record side (verify build) ----

class Sampler {
public:
    // Written to <family>/<function>.vec; `function` must match the replay's check name; `cap` records
    // are kept per bucket.
    Sampler(const char* family, const char* function, std::uint32_t payloadSize, std::uint32_t cap,
            unsigned buckets = 1);

    // Offers one call's payload (taken under `controlWord`, normally ControlWord() before the call).
    // `dedupe` false keeps calls whose payloads are too large to be worth hashing (they rarely repeat).
    void Offer(const void* payload, std::uint16_t controlWord, unsigned bucket = 0, bool dedupe = true);

    template <typename T>
    void Offer(const T& payload, std::uint16_t controlWord, unsigned bucket = 0, bool dedupe = true) {
        static_assert(std::is_trivially_copyable_v<T>);
        Offer(static_cast<const void*>(&payload), controlWord, bucket, dedupe);
    }

    struct State;   // vectors_record.cpp

private:
    friend void Flush();
    const char* family_;
    const char* function_;
    std::uint32_t payloadSize_;
    std::uint32_t cap_;
    unsigned buckets_;
    State* state_ = nullptr;
    Sampler* next_;
};

// Writes every sampler's records to <output folder>/vectors (the harness calls it with its report).
// Does nothing outside record mode.
void Flush();

// Whether record mode is on (EI_VECTORS=1).
bool Recording();

// ---- Replay side (ei_vectors.exe) ----

// A check replays one payload against our code; false (with `detail` filled) when ours differs.
using CheckFn = bool (*)(const void* payload, char* detail, std::size_t detailSize);

struct Check {
    const char* function;
    std::uint32_t payloadSize;
    CheckFn run;
    Check* next;
};

struct CheckRegistrar {
    Check entry;
    CheckRegistrar(const char* function, std::uint32_t payloadSize, CheckFn run);
};

// The registered checks (replay_main.cpp).
Check* Checks();

// Clears the stand-in for the game's data (src/game/game_data.h; replay_game_data.cpp) before a record.
void ResetGameData();

template <typename T, bool (*Fn)(const T&, char*, std::size_t)>
bool RunTyped(const void* payload, char* detail, std::size_t detailSize) {
    T v;
    std::memcpy(&v, payload, sizeof(T));
    return Fn(v, detail, detailSize);
}

// Bitwise equality (the verify wrappers' Check): floats must round the same.
template <typename T>
bool Same(const T& a, const T& b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

} // namespace ei::vectors

// Registers `fn` (bool fn(const Type&, char* detail, std::size_t size)) for the records of `function`.
#define EI_VECTOR_CHECK(function, Type, fn)                                                            \
    static ::ei::vectors::CheckRegistrar EI_VECTORS_CAT(ei_vector_check_, __LINE__)(                  \
        function, sizeof(Type), &::ei::vectors::RunTyped<Type, &fn>)

#define EI_VECTORS_CAT_(a, b) a##b
#define EI_VECTORS_CAT(a, b) EI_VECTORS_CAT_(a, b)
