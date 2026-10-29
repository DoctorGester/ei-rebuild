// Record mode's sampling and output (vectors.h). Verify build only.
#include "vectors.h"

#include "../../src/hook/log.h"

#include <windows.h>

#include <cstdio>
#include <unordered_set>
#include <vector>

namespace ei::vectors {

namespace {

Sampler* g_samplers = nullptr;
SRWLOCK g_lock = SRWLOCK_INIT;

// Distinct calls remembered for dropping repeats; past this, repeats of calls not remembered are
// sampled again (rare: the families recorded so far have far fewer distinct calls).
constexpr std::size_t kMaxRemembered = 1u << 22;

std::uint64_t Hash(const void* data, std::size_t size, std::uint64_t h) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

} // namespace

struct Sampler::State {
    struct Bucket {
        std::vector<std::vector<unsigned char>> kept;
        std::uint64_t offered = 0;   // distinct calls offered to this bucket
    };
    std::vector<Bucket> buckets;
    std::unordered_set<std::uint64_t> seen;
    std::uint64_t calls = 0;
    std::uint64_t random = 0x9E3779B97F4A7C15ull;   // fixed seed: the same calls give the same sample

    std::uint64_t Next() {
        random ^= random << 13;
        random ^= random >> 7;
        random ^= random << 17;
        return random;
    }
};

bool Recording() {
    static const bool on = [] {
        char value[4];
        return GetEnvironmentVariableA("EI_VECTORS", value, sizeof(value)) && value[0] == '1';
    }();
    return on;
}

Sampler::Sampler(const char* newFamily, const char* newFunction, std::uint32_t newPayloadSize, std::uint32_t newCap,
                 unsigned newBuckets)
    : family(newFamily), function(newFunction), payloadSize(newPayloadSize), cap(newCap), buckets(newBuckets), next(g_samplers) {
    g_samplers = this;
}

void Sampler::Offer(const void* payload, std::uint16_t controlWord, unsigned bucket, bool dedupe) {
    Keep(payload, payloadSize, controlWord, bucket, dedupe);
}

void Sampler::OfferBytes(const void* payload, std::uint32_t size, std::uint16_t controlWord, unsigned bucket,
                         bool dedupe) {
    Keep(payload, size, controlWord, bucket, dedupe);
}

// A record: the RecordHeader, the byte count for a variable-size sampler, then the payload.
void Sampler::Keep(const void* payload, std::uint32_t size, std::uint16_t controlWord, unsigned bucket, bool dedupe) {
    if (bucket >= buckets) bucket = buckets - 1;
    RecordHeader header{controlWord, static_cast<std::uint8_t>(bucket), 0};
    const bool variable = payloadSize == kVariableSize;
    AcquireSRWLockExclusive(&g_lock);
    if (!state) {
        state = new State;
        state->buckets.resize(buckets);
    }
    State& s = *state;
    ++s.calls;
    bool fresh = true;
    if (dedupe) {
        std::uint64_t h = Hash(&header, sizeof(header), 0xCBF29CE484222325ull);
        if (variable) h = Hash(&size, sizeof(size), h);
        h = Hash(payload, size, h);
        if (s.seen.count(h))
            fresh = false;
        else if (s.seen.size() < kMaxRemembered)
            s.seen.insert(h);
    }
    if (fresh) {
        State::Bucket& b = s.buckets[bucket];
        ++b.offered;
        std::size_t slot = b.kept.size();
        if (slot >= cap) slot = static_cast<std::size_t>(s.Next() % b.offered);
        if (slot < cap) {
            const std::size_t prefix = sizeof(header) + (variable ? sizeof(size) : 0);
            std::vector<unsigned char> record(prefix + size);
            std::memcpy(record.data(), &header, sizeof(header));
            if (variable) std::memcpy(record.data() + sizeof(header), &size, sizeof(size));
            std::memcpy(record.data() + prefix, payload, size);
            if (slot == b.kept.size())
                b.kept.push_back(std::move(record));
            else
                b.kept[slot] = std::move(record);
        }
    }
    ReleaseSRWLockExclusive(&g_lock);
}

void Flush() {
    if (!Recording()) return;
    char folder[MAX_PATH];
    OutputPath("vectors", folder);
    CreateDirectoryA(folder, nullptr);
    AcquireSRWLockExclusive(&g_lock);
    for (Sampler* sampler = g_samplers; sampler; sampler = sampler->next) {
        Sampler::State* s = sampler->state;
        if (!s) continue;
        FileHeader header{};
        header.magic = kMagic;
        header.version = kVersion;
        header.payloadSize = sampler->payloadSize;
        header.cap = sampler->cap;
        strncpy_s(header.function, sampler->function, _TRUNCATE);
        std::uint64_t distinct = 0;
        for (const Sampler::State::Bucket& b : s->buckets) {
            header.count += static_cast<std::uint32_t>(b.kept.size());
            distinct += b.offered;
        }
        // File names: the function name with "::" as "__".
        char name[96] = "";
        std::size_t n = 0;
        for (const char* c = sampler->function; *c && n + 6 < sizeof(name); ++c) name[n++] = *c == ':' ? '_' : *c;
        std::snprintf(name + n, sizeof(name) - n, ".vec");
        char path[MAX_PATH];
        std::snprintf(path, sizeof(path), "%s\\%s", folder, sampler->family);
        CreateDirectoryA(path, nullptr);
        std::snprintf(path, sizeof(path), "%s\\%s\\%s", folder, sampler->family, name);
        FILE* f = nullptr;
        if (fopen_s(&f, path, "wb") != 0 || !f) {
            Log("vectors: cannot write %s", path);
            continue;
        }
        std::fwrite(&header, sizeof(header), 1, f);
        for (const Sampler::State::Bucket& b : s->buckets)
            for (const std::vector<unsigned char>& record : b.kept) std::fwrite(record.data(), record.size(), 1, f);
        std::fclose(f);
        Log("vectors: %s/%s: %u records of %llu calls (%llu distinct)", sampler->family, sampler->function, header.count,
            static_cast<unsigned long long>(s->calls), static_cast<unsigned long long>(distinct));
    }
    ReleaseSRWLockExclusive(&g_lock);
}

} // namespace ei::vectors
