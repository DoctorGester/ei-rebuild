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
// it is filled, so equal calls give equal bytes. A file whose payloadSize is kVariableSize (0) holds
// payloads of their own sizes (buffers, strings, lists): each RecordHeader is followed by the
// payload's byte count (uint32) and its bytes, written with Writer and read with Reader.
//
// A fixture sets up what other checks read and the replay cannot build (game data such as the script
// keyword table, the game CRT's character classes): its function name starts with '@', the replay
// runs fixtures before every other file, and the game data a fixture writes stays (KeepGameData).

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace ei::vectors {

constexpr std::uint32_t kMagic = 0x31564945;   // "EIV1"
constexpr std::uint32_t kVersion = 1;
constexpr std::uint32_t kVariableSize = 0;     // FileHeader::payloadSize of length-prefixed records

struct FileHeader {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t payloadSize;   // bytes of one payload (kVariableSize: each record gives its own)
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

// A variable-size payload being built: plain values, byte runs and strings appended in order.
class Writer {
public:
    template <typename T>
    void Put(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        PutBytes(&value, sizeof(T));
    }
    void PutBytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const unsigned char*>(data);
        bytes_.insert(bytes_.end(), p, p + size);
    }
    // A string: its length (uint32, 0xFFFFFFFF for null) and characters.
    void PutString(const char* text) {
        std::uint32_t length = text ? static_cast<std::uint32_t>(std::strlen(text)) : kNullString;
        Put(length);
        if (text) PutBytes(text, length);
    }
    const unsigned char* data() const { return bytes_.data(); }
    std::uint32_t size() const { return static_cast<std::uint32_t>(bytes_.size()); }

    static constexpr std::uint32_t kNullString = 0xFFFFFFFFu;

private:
    std::vector<unsigned char> bytes_;
};

// Reads a variable-size payload in the order Writer wrote it. Reading past the end gives zeros and
// clears ok().
class Reader {
public:
    Reader(const void* data, std::uint32_t size) : p_(static_cast<const unsigned char*>(data)), left_(size) {}

    template <typename T>
    T Get() {
        static_assert(std::is_trivially_copyable_v<T>);
        T value;
        std::memset(&value, 0, sizeof(T));
        if (const unsigned char* p = Bytes(sizeof(T))) std::memcpy(&value, p, sizeof(T));
        return value;
    }
    // The next `size` bytes, or null (and ok() false) when fewer are left.
    const unsigned char* Bytes(std::size_t size) {
        if (size > left_) {
            ok_ = false;
            left_ = 0;
            return nullptr;
        }
        const unsigned char* p = p_;
        p_ += size;
        left_ -= size;
        return p;
    }
    // A string written by PutString (`isNull` set for a null one).
    std::string String(bool* isNull = nullptr) {
        std::uint32_t length = Get<std::uint32_t>();
        if (isNull) *isNull = length == Writer::kNullString;
        if (length == Writer::kNullString) return std::string();
        const unsigned char* p = Bytes(length);
        return p ? std::string(reinterpret_cast<const char*>(p), length) : std::string();
    }
    bool ok() const { return ok_; }
    std::size_t left() const { return left_; }

private:
    const unsigned char* p_;
    std::size_t left_;
    bool ok_ = true;
};

// ---- Record side (verify build) ----

class Sampler {
public:
    // Written to <family>/<function>.vec; `function` must match the replay's check name; `cap` records
    // are kept per bucket. With payloadSize kVariableSize the payloads are offered with OfferBytes.
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

    // A variable-size payload (a sampler made with kVariableSize).
    void OfferBytes(const void* payload, std::uint32_t size, std::uint16_t controlWord, unsigned bucket = 0,
                    bool dedupe = true);
    void OfferBytes(const Writer& payload, std::uint16_t controlWord, unsigned bucket = 0, bool dedupe = true) {
        OfferBytes(payload.data(), payload.size(), controlWord, bucket, dedupe);
    }

    struct State;   // vectors_record.cpp

private:
    friend void Flush();
    void Keep(const void* payload, std::uint32_t size, std::uint16_t controlWord, unsigned bucket, bool dedupe);
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

// A check replays one payload (of `size` bytes) against our code; false (with `detail` filled) when
// ours differs.
using CheckFn = bool (*)(const void* payload, std::uint32_t size, char* detail, std::size_t detailSize);

struct Check {
    const char* function;
    std::uint32_t payloadSize;   // kVariableSize: records of their own sizes
    CheckFn run;
    Check* next;
};

struct CheckRegistrar {
    Check entry;
    CheckRegistrar(const char* function, std::uint32_t payloadSize, CheckFn run);
};

// The registered checks (replay_main.cpp).
Check* Checks();

// Whether `function` names a fixture (above).
inline bool IsFixture(const char* function) { return function[0] == '@'; }

// Clears the stand-in for the game's data (src/game/game_data.h; replay_game_data.cpp) before a
// record, and writes back what fixtures left there.
void ResetGameData();
// After a fixture's records: what they wrote to the stand-in stays for the rest of the run.
void KeepGameData();

// A forwarder (EI_ORIGINAL) to `address` goes to `function` instead of our rewrite there, for code
// outside the family that the replay leaves out (hook_stub.cpp). Called before the checks run.
void ReplayStandIn(std::uintptr_t address, void* function, const char* name);

template <typename T, bool (*Fn)(const T&, char*, std::size_t)>
bool RunTyped(const void* payload, std::uint32_t, char* detail, std::size_t detailSize) {
    T v;
    std::memcpy(&v, payload, sizeof(T));
    return Fn(v, detail, detailSize);
}

template <bool (*Fn)(Reader&, char*, std::size_t)>
bool RunBytes(const void* payload, std::uint32_t size, char* detail, std::size_t detailSize) {
    Reader r(payload, size);
    if (!Fn(r, detail, detailSize)) return false;
    if (r.ok() && r.left() == 0) return true;
    std::snprintf(detail, detailSize, "payload of %u bytes %s", size, r.ok() ? "not read to its end" : "too short");
    return false;
}

// Bitwise equality (the verify wrappers' Check): floats must round the same.
template <typename T>
bool Same(const T& a, const T& b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

// FNV-1a over bytes (large state compared by hash instead of stored whole).
inline std::uint64_t HashBytes(const void* data, std::size_t size) {
    std::uint64_t h = 0xCBF29CE484222325ull;
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

} // namespace ei::vectors

// Registers `fn` (bool fn(const Type&, char* detail, std::size_t size)) for the records of `function`.
#define EI_VECTOR_CHECK(function, Type, fn)                                                            \
    static ::ei::vectors::CheckRegistrar EI_VECTORS_CAT(ei_vector_check_, __LINE__)(                  \
        function, sizeof(Type), &::ei::vectors::RunTyped<Type, &fn>)

// Registers `fn` (bool fn(Reader&, char* detail, std::size_t size)) for the variable-size records of
// `function` (a fixture when it starts with '@'). A record whose payload `fn` does not read to its
// end, or reads past it, fails.
#define EI_VECTOR_CHECK_BYTES(function, fn)                                                            \
    static ::ei::vectors::CheckRegistrar EI_VECTORS_CAT(ei_vector_check_, __LINE__)(                  \
        function, ::ei::vectors::kVariableSize, &::ei::vectors::RunBytes<&fn>)

#define EI_VECTORS_CAT_(a, b) a##b
#define EI_VECTORS_CAT(a, b) EI_VECTORS_CAT_(a, b)
