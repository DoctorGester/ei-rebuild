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
// A search's record holds the map it ran on as the id of its "@AIMap" and the differences from it
// (units move, searches leave marks), the unit's fields the searches read, the arguments and the
// path before; then the result, the path after and a hash of the marks after.

#include "vectors.h"

#include "../../src/game/ai_map.h"

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
    {0x00791108, 36},   // DirectionDx
    {0x0079112C, 36},   // DirectionDy
    {0x00791978, 36},   // DirectionSteps (indexed from its middle)
    {0x0079169C, 32},   // the size classes' standing heights
    {0x0073F0A8, 8},    // PlanLongPath's distance for aiming at the best cell
    {0x0073B86C, 4},    // 0.5
    {0x007C2FEC, 8},    // the spiral's radius tree: depth, mask
};
// CArrays read through their data pointer: {address of the CArray, element size}.
constexpr Range kTableArrays[] = {
    {0x007C2FC0, 4},   // the spiral's lengths per radius
    {0x007C2FD8, 4},   // the radius tree's keys
    {0x007C2FF8, 4},   // the spiral's offsets {dx, dy}
};
constexpr std::uintptr_t kSmoothTables = 0x007C3058;   // 81 SmoothTables (3 CArrays each)
constexpr int kSmoothTableCount = 81;
constexpr std::uint32_t kSmoothCopySize = sizeof(SmoothCopy);
constexpr std::uint32_t kSmoothStepSize = sizeof(SmoothStep);

// ---- Run-length coding of byte planes ------------------------------------------------------------
// A grid of `count` elements of `stride` bytes is coded plane by plane (byte k of every element),
// each control byte c either c + 1 literal bytes (c < 0x80) or the next byte c - 0x80 + 3 times.

inline void PutPacked(Writer& w, const std::uint8_t* data, std::uint32_t count, std::uint32_t stride) {
    std::vector<std::uint8_t> out;
    std::vector<std::uint8_t> plane(count);
    for (std::uint32_t k = 0; k < stride; ++k) {
        for (std::uint32_t i = 0; i < count; ++i) plane[i] = data[static_cast<std::size_t>(i) * stride + k];
        std::uint32_t i = 0;
        while (i < count) {
            std::uint32_t run = 1;
            while (i + run < count && run < 130 && plane[i + run] == plane[i]) ++run;
            if (run >= 3) {
                out.push_back(static_cast<std::uint8_t>(0x80 + run - 3));
                out.push_back(plane[i]);
                i += run;
                continue;
            }
            // Literals up to the next run of three.
            std::uint32_t start = i, n = 0;
            while (i < count && n < 128) {
                if (i + 2 < count && plane[i] == plane[i + 1] && plane[i] == plane[i + 2]) break;
                ++i;
                ++n;
            }
            out.push_back(static_cast<std::uint8_t>(n - 1));
            out.insert(out.end(), plane.begin() + start, plane.begin() + start + n);
        }
    }
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
    for (std::uint32_t k = 0; k < stride; ++k) {
        std::uint32_t i = 0;
        while (i < count) {
            if (p >= end) return false;
            std::uint8_t c = *p++;
            if (c < 0x80) {
                std::uint32_t n = c + 1u;
                if (i + n > count || p + n > end) return false;
                for (std::uint32_t j = 0; j < n; ++j) out[static_cast<std::size_t>(i + j) * stride + k] = p[j];
                p += n;
                i += n;
            } else {
                std::uint32_t n = c - 0x80u + 3u;
                if (i + n > count || p >= end) return false;
                for (std::uint32_t j = 0; j < n; ++j) out[static_cast<std::size_t>(i + j) * stride + k] = *p;
                ++p;
                i += n;
            }
        }
    }
    return p == end;
}

// ---- Differences from the saved map --------------------------------------------------------------
// The runs where `now` differs from `base` (`size` bytes): u32 count, then each u32 offset, u32
// length and the bytes of `now`. Runs closer than kDiffGap bytes are joined.
constexpr std::uint32_t kDiffGap = 8;

inline void PutDiff(Writer& w, const std::uint8_t* base, const std::uint8_t* now, std::uint32_t size) {
    struct Run {
        std::uint32_t offset, length;
    };
    std::vector<Run> runs;
    std::uint32_t i = 0;
    while (i < size) {
        if (base[i] == now[i]) {
            ++i;
            continue;
        }
        std::uint32_t start = i, last = i;
        for (++i; i < size && i - last <= kDiffGap; ++i)
            if (base[i] != now[i]) last = i;
        runs.push_back({start, last + 1 - start});
        i = last + 1;
    }
    w.Put(static_cast<std::uint32_t>(runs.size()));
    for (const Run& run : runs) {
        w.Put(run.offset);
        w.Put(run.length);
        w.PutBytes(now + run.offset, run.length);
    }
}

// Applies what PutDiff wrote to `data` (`size` bytes); false when a run does not fit.
inline bool ApplyDiff(Reader& r, std::uint8_t* data, std::uint32_t size) {
    const std::uint32_t count = r.Get<std::uint32_t>();
    for (std::uint32_t k = 0; k < count; ++k) {
        const std::uint32_t offset = r.Get<std::uint32_t>();
        const std::uint32_t length = r.Get<std::uint32_t>();
        const std::uint8_t* bytes = r.Bytes(length);
        if (!bytes || offset > size || length > size - offset) return false;
        std::memcpy(data + offset, bytes, length);
    }
    return r.ok();
}

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

// The marks after a search (the next searches read them).
inline std::uint64_t MarksHash(const CAIMap& map) {
    return HashBytes(map.marks.data, static_cast<std::size_t>(map.marks.width) * map.marks.height);
}

// Buckets.
enum FindPathBucket : unsigned { kFindFailed, kFindNear, kFindPlanned, kFindBuckets };
enum PlanBucket : unsigned { kPlanFailed, kPlanReached, kPlanAimed, kPlanBuckets };

} // namespace ei::vectors::path
