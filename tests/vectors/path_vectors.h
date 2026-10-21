#pragma once

// The "path" family (docs/notes/standalone-verification.md, SV4c): the path searches on a saved AI
// map. Shared by record_path.cpp (verify build) and replay_path.cpp (ei_vectors).
//
// "@PathTables" (fixture, once): the game's tables the searches read: plain ranges written to the
// game data stand-in, and CArrays (the spiral of cell offsets, the smoother's 81 transition tables)
// whose elements the replay copies to its heap.
//
// "@AIMap" (fixture, one record per map and record run): the AI map when a run first searches on
// it: cost and climb tables, the cells, water depths and marks, the region map's grids. Its id is
// a hash of the content. The grids are stored byte plane by byte plane, run-length coded.
//
// A search's record holds the map it ran on as the id of its "@AIMap", the planes it differs in
// (CallPlanes) and the differences (units move, searches leave marks), the unit's fields the
// searches read, the arguments and the path before; then the result, the path after and what the
// search changed in the marks.

#include "vectors.h"

#include "../../src/game/ai_map.h"
#include "../../src/game/game_data.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace ei::vectors::path {

constexpr const char* kFamily = "path";

// Game data the searches read at fixed addresses (ai_map.h, ai_map.cpp, ai_plan.cpp).
struct Range {
    std::uint32_t address;
    std::uint32_t size;
};
constexpr Range kTableRanges[] = {
    {ei::DataId(0x00791108), 36},   // DirectionDx
    {ei::DataId(0x0079112C), 36},   // DirectionDy
    {ei::DataId(0x00791978), 36},   // DirectionSteps (indexed from its middle)
    {ei::DataId(0x0079169C), 32},   // the size classes' standing heights
    {ei::DataId(0x0073F0A8), 8},    // PlanLongPath's distance for aiming at the best cell
    {ei::DataId(0x0073B86C), 4},    // 0.5
    {ei::DataId(0x007C2FEC), 8},    // the spiral's radius tree: depth, mask
};
// CArrays read through their data pointer: {address of the CArray, element size}.
constexpr Range kTableArrays[] = {
    {ei::DataId(0x007C2FC0), 4},   // the spiral's lengths per radius
    {ei::DataId(0x007C2FD8), 4},   // the radius tree's keys
    {ei::DataId(0x007C2FF8), 4},   // the spiral's offsets {dx, dy}
};
constexpr std::uintptr_t kSmoothTables = 0x007C3058;   // 81 SmoothTables (3 CArrays each)
constexpr int kSmoothTableCount = 81;
constexpr std::uint32_t kSmoothCopySize = sizeof(SmoothCopy);
constexpr std::uint32_t kSmoothStepSize = sizeof(SmoothStep);

// ---- Run-length coding of byte planes ------------------------------------------------------------
// A grid of `count` elements of `stride` bytes is coded plane by plane (byte k of every element),
// each control byte c either c + 1 literal bytes (c < 0x80) or the next byte c - 0x80 + 3 times.

// Codes `count` bytes (every `stride`th from `data`) onto `out`.
inline void RleEncode(std::vector<std::uint8_t>& out, const std::uint8_t* data, std::uint32_t count,
                      std::uint32_t stride = 1) {
    auto at = [&](std::uint32_t i) { return data[static_cast<std::size_t>(i) * stride]; };
    std::uint32_t i = 0;
    while (i < count) {
        std::uint32_t run = 1;
        while (i + run < count && run < 130 && at(i + run) == at(i)) ++run;
        if (run >= 3) {
            out.push_back(static_cast<std::uint8_t>(0x80 + run - 3));
            out.push_back(at(i));
            i += run;
            continue;
        }
        // Literals up to the next run of three.
        const std::size_t control = out.size();
        out.push_back(0);
        std::uint32_t n = 0;
        while (i < count && n < 128) {
            if (i + 2 < count && at(i) == at(i + 1) && at(i) == at(i + 2)) break;
            out.push_back(at(i));
            ++i;
            ++n;
        }
        out[control] = static_cast<std::uint8_t>(n - 1);
    }
}

// Decodes `count` bytes (to every `stride`th of `out`) from [p, end); the position after them, or
// null when the coding does not fit.
inline const std::uint8_t* RleDecode(const std::uint8_t* p, const std::uint8_t* end, std::uint8_t* out,
                                     std::uint32_t count, std::uint32_t stride = 1) {
    std::uint32_t i = 0;
    while (i < count) {
        if (p >= end) return nullptr;
        const std::uint8_t c = *p++;
        if (c < 0x80) {
            const std::uint32_t n = c + 1u;
            if (n > count - i || n > static_cast<std::size_t>(end - p)) return nullptr;
            for (std::uint32_t j = 0; j < n; ++j) out[static_cast<std::size_t>(i + j) * stride] = p[j];
            p += n;
            i += n;
        } else {
            const std::uint32_t n = c - 0x80u + 3u;
            if (n > count - i || p >= end) return nullptr;
            for (std::uint32_t j = 0; j < n; ++j) out[static_cast<std::size_t>(i + j) * stride] = *p;
            ++p;
            i += n;
        }
    }
    return p;
}

inline void PutPacked(Writer& w, const std::uint8_t* data, std::uint32_t count, std::uint32_t stride) {
    std::vector<std::uint8_t> out;
    for (std::uint32_t k = 0; k < stride; ++k) RleEncode(out, data + k, count, stride);
    w.Put(static_cast<std::uint32_t>(out.size()));
    w.PutBytes(out.data(), out.size());
}

// Decodes what PutPacked wrote into `out` (`count` elements of `stride` bytes); false when the
// coding does not fit.
inline bool GetPacked(Reader& r, std::uint8_t* out, std::uint32_t count, std::uint32_t stride) {
    const std::uint32_t size = r.Get<std::uint32_t>();
    const std::uint8_t* p = r.Bytes(size);
    if (!p) return false;
    const std::uint8_t* end = p + size;
    for (std::uint32_t k = 0; k < stride && p; ++k) p = RleDecode(p, end, out + k, count, stride);
    return p == end;
}

// ---- Differences from the saved map --------------------------------------------------------------
// The elements where `now` differs from `base` (`count` elements of `stride` bytes), byte plane by
// byte plane: per plane a run count, then each run as the gap since the previous run's end and its
// length (both LEB128); then the bytes of `now` in every run, run-length coded as above (u32 size
// first). Runs closer than kDiffGap are joined.
constexpr std::uint32_t kDiffGap = 4;

inline void PutVarint(Writer& w, std::uint32_t v) {
    while (v >= 0x80) {
        w.Put(static_cast<std::uint8_t>(v | 0x80));
        v >>= 7;
    }
    w.Put(static_cast<std::uint8_t>(v));
}

inline std::uint32_t GetVarint(Reader& r) {
    std::uint32_t v = 0;
    for (int shift = 0; shift < 35; shift += 7) {
        const std::uint8_t b = r.Get<std::uint8_t>();
        v |= static_cast<std::uint32_t>(b & 0x7F) << shift;
        if (!(b & 0x80) || !r.ok()) return v;
    }
    r.Bytes(~std::size_t(0));   // too long: fails the reader
    return 0;
}

inline void PutDiff(Writer& w, const std::uint8_t* base, const std::uint8_t* now, std::uint32_t count,
                    std::uint32_t stride) {
    std::vector<std::uint8_t> bytes;
    for (std::uint32_t k = 0; k < stride; ++k) {
        auto differs = [&](std::uint32_t i) {
            return base[static_cast<std::size_t>(i) * stride + k] != now[static_cast<std::size_t>(i) * stride + k];
        };
        std::vector<std::uint32_t> runs;   // start, length
        for (std::uint32_t i = 0; i < count;) {
            if (!differs(i)) {
                ++i;
                continue;
            }
            std::uint32_t start = i, last = i;
            for (++i; i < count && i - last <= kDiffGap; ++i)
                if (differs(i)) last = i;
            runs.push_back(start);
            runs.push_back(last + 1 - start);
            i = last + 1;
        }
        PutVarint(w, static_cast<std::uint32_t>(runs.size() / 2));
        std::uint32_t at = 0;
        for (std::size_t j = 0; j < runs.size(); j += 2) {
            PutVarint(w, runs[j] - at);
            PutVarint(w, runs[j + 1]);
            for (std::uint32_t i = runs[j]; i < runs[j] + runs[j + 1]; ++i)
                bytes.push_back(now[static_cast<std::size_t>(i) * stride + k]);
            at = runs[j] + runs[j + 1];
        }
    }
    std::vector<std::uint8_t> coded;
    RleEncode(coded, bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    w.Put(static_cast<std::uint32_t>(coded.size()));
    w.PutBytes(coded.data(), coded.size());
}

// Applies what PutDiff wrote to `data`; false when a run does not fit.
inline bool ApplyDiff(Reader& r, std::uint8_t* data, std::uint32_t count, std::uint32_t stride) {
    std::vector<std::uint32_t> runs;   // element index, length, plane
    std::uint32_t total = 0;
    for (std::uint32_t k = 0; k < stride; ++k) {
        const std::uint32_t n = GetVarint(r);
        std::uint32_t at = 0;
        for (std::uint32_t j = 0; j < n && r.ok(); ++j) {
            const std::uint32_t gap = GetVarint(r);
            const std::uint32_t length = GetVarint(r);
            if (gap > count - at || length > count - at - gap) return false;
            at += gap;
            runs.insert(runs.end(), {at, length, k});
            at += length;
            total += length;
        }
    }
    const std::uint32_t size = r.Get<std::uint32_t>();
    const std::uint8_t* p = r.Bytes(size);
    if (!p) return false;
    std::vector<std::uint8_t> bytes(total);
    if (RleDecode(p, p + size, bytes.data(), total) != p + size) return false;
    std::size_t from = 0;
    for (std::size_t j = 0; j < runs.size(); j += 3)
        for (std::uint32_t i = 0; i < runs[j + 1]; ++i)
            data[static_cast<std::size_t>(runs[j] + i) * stride + runs[j + 2]] = bytes[from++];
    return r.ok();
}

// Which of the map's planes a record's differences cover (the rest are the saved map's): the
// smoother reads only the cells.
enum CallPlanes : std::uint8_t {
    kCallCells = 1,
    kCallWater = 2,
    kCallMarks = 4,
    kCallRegions = 8,
    kCallAll = 15,
};

// ---- The unit and the path -------------------------------------------------------------------------

// What the searches read of the unit (CUnit, its CMapObject and its movement speeds).
struct UnitState {
    std::int32_t classId;
    float position[3];
    float orientation[4];
    std::int32_t sizeClass;
    float standingHeight;
    std::uint32_t altPassCosts;
    float forwardSpeed, backwardSpeed;   // CUnit::GetMoveSpeeds
};
static_assert(sizeof(UnitState) == 52);

inline void PutCells(Writer& w, const PathCellList& list) {
    w.Put(static_cast<std::uint32_t>(list.count));
    for (const PathCellList::Node* n = list.head; n; n = n->next) w.Put(n->data);
}

// An AIPath: the order, the steps, the cells, the target and goal, the four flags, the excluded
// blocks and the smoothing value.
inline void PutPath(Writer& w, const AIPath& path) {
    w.Put(path.motionOrder);
    PutCells(w, path.steps);
    PutCells(w, path.cells);
    w.Put(path.targetX);
    w.Put(path.targetY);
    w.Put(path.goal);
    w.Put(path.reached);
    w.Put(path.failed);
    w.Put(path.flag6E);
    w.Put(path.flag6F);
    w.Put(static_cast<std::uint32_t>(path.excludedBlocks.size));
    for (int i = 0; i < path.excludedBlocks.size; ++i) w.Put(path.excludedBlocks.data[i]);
    w.Put(path.smoothing);
}

// Buckets.
enum FindPathBucket : unsigned { kFindFailed, kFindNear, kFindPlanned, kFindBuckets };
enum PlanBucket : unsigned { kPlanFailed, kPlanReached, kPlanAimed, kPlanBuckets };

} // namespace ei::vectors::path
