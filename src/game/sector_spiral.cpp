// The sector spiral's tables (sector_map.h): the offsets of a 21 x 21 square of sectors ordered by distance,
// the number of offsets within each distinct distance and the tree that finds a distance's place among them,
// built on first use. Here, not in sector_map.cpp, so the path vectors' replay (tests/vectors/replay_path.cpp)
// links them.
#include "sector_map.h"

#include "lib/crt.h"

#include <bit>
#include <vector>

namespace {

constexpr int kSpiralReach = 10;                                    // sectors either way
constexpr int kSpiralCells = (kSpiralReach * 2 + 1) * (kSpiralReach * 2 + 1);
constexpr float kBeyondSpiral = 1.0e30f;                            // 7149F2CA: the last radius

// A sector of the square and how far it is: the gap between the sectors, in sectors.
struct SpiralCell {
    SpiralOffset offset;
    float distance;
};

// The sort's comparison: by distance, ascending.
int ByDistance(const void* a, const void* b) {
    const float da = static_cast<const SpiralCell*>(a)->distance;
    const float db = static_cast<const SpiralCell*>(b)->distance;
    if (da > db) return 1;
    return da == db ? 0 : -1;
}

// |v| - 1, 0 for 0: the whole sectors between two sectors `v` apart.
int Gap(int v) { return v == 0 ? 0 : (v < 0 ? -v : v) - 1; }

// The tables as the original's static initialiser builds them.
SectorSpiral BuildSectorSpiral() {
    SectorSpiral spiral;
    std::vector<SpiralCell> cells;
    cells.resize(kSpiralCells);
    spiral.offsets.resize(kSpiralCells);
    int filled = 0;
    for (int y = -kSpiralReach; y <= kSpiralReach; ++y) {
        for (int x = -kSpiralReach; x <= kSpiralReach; ++x) {
            // The squares are summed as integers (rooted, stored as a float); the gaps
            // are 9 at most, so the float sum is the same number.
            const Vector2 gap{static_cast<float>(Gap(x)), static_cast<float>(Gap(y))};
            SpiralCell& cell = cells[filled++];
            cell.offset.x = static_cast<std::int16_t>(x);
            cell.offset.y = static_cast<std::int16_t>(y);
            cell.distance = Length2D(gap);
        }
    }
    // The game's own qsort: it decides the order of the sectors at the same distance.
    libcrt::Qsort(cells.data(), cells.size(), sizeof(SpiralCell), ByDistance);
    for (int i = 0; i < std::ssize(cells); ++i) spiral.offsets[i] = cells[i].offset;

    // A length per distinct distance: the offsets up to and including that distance.
    std::vector<float> radii;
    std::vector<int>& lengths = spiral.lengths;
    radii.push_back(0.0f);
    lengths.push_back(1);
    for (int i = 0; i < std::ssize(cells); ++i) {
        const float distance = cells[i].distance;
        if (distance == radii.back()) {
            lengths[std::ssize(lengths) - 1] = i + 1;
        } else {
            radii.push_back(distance);
            lengths.push_back(i + 1);
        }
    }
    radii.push_back(kBeyondSpiral);
    lengths.push_back(std::ssize(spiral.offsets));
    spiral.radii.Build(radii.data(), static_cast<int>(radii.size()));
    return spiral;
}

} // namespace

int RadiusTree::Find(float radius) const {
    std::int32_t key;
    key = std::bit_cast<std::int32_t>(radius);
    int node = 1;
    for (int level = depth; level > 0; --level) {
        std::int32_t nodeKey;
        nodeKey = std::bit_cast<std::int32_t>(keys[node]);
        node = node * 2 + (key > nodeKey ? 1 : 0);
    }
    return node & mask;
}

void RadiusTree::Build(const float* values, int count) {
    depth = 1;
    if (count > 2) {
        do ++depth;
        while ((1 << depth) < count);
    }
    keys.resize(2 << depth);
    mask = (1 << depth) - 1;
    for (int level = 0; level <= depth; ++level) {
        const int width = 1 << level;
        const int offset = depth == level ? 0 : (1 << (depth - level - 1)) - 1;
        for (int k = 0; k < width; ++k) {
            int index = (1 << (depth - level)) * k + offset;
            if (index >= count) index = count - 1;
            keys[width + k] = values[index];
        }
    }
}

const SectorSpiral& SectorSpiralTables() {
    static const SectorSpiral spiral = BuildSectorSpiral();
    return spiral;
}
