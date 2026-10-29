// RegionMap::ReachesRegion and LabelRegions, and the region map's block grids. See ai_map.h and
// docs/notes/ai.md ("Regions").
#include "ai_map.h"
#include "core_streams.h"

#include "lib/crt.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace {

constexpr int kUnreached = 0x7FFFFFFF;
constexpr std::int16_t kNoWay = -1;     // an edge word no path crosses
constexpr int kEdgesPerBlock = 8;       // the edge words of a block, one per direction

// The block grid's size: the first size class's edge grid.
int BlocksWide(const RegionMap* map) { return map->edges[0].width; }
int BlocksHigh(const RegionMap* map) { return map->edges[0].height; }

// The direction steps of block directions 0..7 (the tables from their index 1).
const int* StepX() { return kDirectionDx + 1; }
const int* StepY() { return kDirectionDy + 1; }

void ResetChain(BlockRecord* record) {
    while (record) {
        BlockRecord* next = record->next;
        record->cost = kUnreached;
        record->next = nullptr;
        record->prev = nullptr;
        record->backDirection = 0;
        record->settled = false;
        record = next;
    }
}
void PrepareSearch(BlockSearch& search, int sizeClass) {
    search.edges = &search.map->edges[sizeClass];
    for (BlockRecord*& bucket : search.open) bucket = nullptr;
    search.active = true;
    search.best = nullptr;
}
void Exclude(BlockSearch& search, int x, int y) {
    BlockRecord* record = &search.records[y][x];
    if (record->cost < kUnreached) return;
    record->cost = -1;
    record->next = search.touched;
    search.touched = record;
}
void FinishSearch(BlockSearch& search) {
    ResetChain(search.touched);
    for (BlockRecord* bucket : search.open) ResetChain(bucket);
    search.touched = nullptr;
    search.active = false;
}

} // namespace

bool RegionMap::ReachesRegion(int sizeClass, int x, int y, int region, SearchGrid<std::int8_t>* marks) {
    int bx = x / 8, by = y / 8;
    if (BlocksWide(this) - 1 < bx) bx = BlocksWide(this) - 1;
    if (bx < 0) bx = 0;
    if (BlocksHigh(this) - 1 < by) by = BlocksHigh(this) - 1;
    if (by < 0) by = 0;

    std::vector<BlockPoint> blocks;
    for (int yy = by - 1; yy < by + 2; ++yy) {
        if (yy < 0 || yy >= BlocksHigh(this)) continue;
        for (int xx = bx - 1; xx < bx + 2; ++xx) {
            if (xx < 0 || xx >= BlocksWide(this)) continue;
            int n = std::ssize(blocks);
            blocks.resize(n + 1);
            blocks[n].x = xx;
            blocks[n].y = yy;
        }
    }

    SearchGrid<std::uint16_t>& regionRows = regions[sizeClass];
    auto inRegion = [&](int xx, int yy) {
        return regionRows[yy][xx] == static_cast<std::uint16_t>(region);
    };
    bool any = false;
    for (int k = 0; k < std::ssize(blocks); ++k)
        if (inRegion(blocks[k].x, blocks[k].y)) {
            any = true;
            break;
        }
    if (!any) {
        for (int row = by * 8; row < by * 8 + 8; ++row)
            for (int col = bx * 8; col < bx * 8 + 8; ++col) (*marks)[row][col] = 2;
        return false;
    }

    int x0 = bx * 8 - 8, y0 = by * 8 - 8, x1 = bx * 8 + 16, y1 = by * 8 + 16;
    local.backward = true;
    LocalSearch* search = &local;
    search->BeginInRectForClass(sizeClass, x0, y0, x1, y1, x, y);
    search->SaveLevels();
    search->ExpandAll();
    search->RestoreLevels();

    SearchGrid<std::uint8_t>& cellRows = cells[sizeClass];
    bool found = false;
    for (int k = 0; k < std::ssize(blocks); ++k) {
        int xx = blocks[k].x, yy = blocks[k].y;
        std::uint8_t cell = cellRows[yy][xx];
        if (search->CostAt(RepresentativeX(cell) + xx * 8, RepresentativeY(cell) + yy * 8) < kUnreached && inRegion(xx, yy)) {
            found = true;
            break;
        }
    }
    std::uint8_t mark = found ? 1 : 2;
    for (int row = y0; row < y1; ++row)
        for (int col = x0; col < x1; ++col)
            if (search->CostAt(col, row) < kUnreached) (*marks)[row][col] = static_cast<std::int8_t>(mark);
    search->Reset();
    return found;
}

void RegionMap::LabelRegions() {
    std::vector<BlockPoint> queue;
    std::vector<BlockPoint> second;
    for (int sizeClass = 0; sizeClass < 8; ++sizeClass) {
        SearchGrid<std::uint16_t>& grid = regions[sizeClass];
        grid.data.assign(grid.data.size(), 0);
        int label = 1;
        for (int by = 0; by < BlocksHigh(this); ++by)
            for (int bx = 0; bx < BlocksWide(this); ++bx) {
                if (grid[by][bx] != 0) continue;
                grid[by][bx] = static_cast<std::uint16_t>(label);
                int n = std::ssize(queue);
                queue.resize(n + 1);
                queue[n].x = bx;
                queue[n].y = by;
                FloodRegionFrom(sizeClass, &queue, &second, label);
                ++label;
            }
    }
}

std::vector<BlockCost> RegionMap::NearbyBlocks(int sizeClass, bool flag, int x, int y) {
    std::vector<BlockCost> out;
    int bx = x / 8, by = y / 8;
    if (BlocksWide(this) - 1 < bx) bx = BlocksWide(this) - 1;
    if (bx < 0) bx = 0;
    if (BlocksHigh(this) - 1 < by) by = BlocksHigh(this) - 1;
    if (by < 0) by = 0;

    local.backward = flag;
    LocalSearch* search = &local;
    search->BeginInRectForClass(sizeClass, bx * 8 - 8, by * 8 - 8, bx * 8 + 16, by * 8 + 16, x, y);
    search->SaveLevels();
    search->ExpandAll();
    search->RestoreLevels();
    SearchGrid<std::uint8_t>& cellRows = cells[sizeClass];
    for (int yy = by - 1; yy < by + 2; ++yy) {
        if (yy < 0 || yy >= BlocksHigh(this)) continue;
        for (int xx = bx - 1; xx < bx + 2; ++xx) {
            if (xx < 0 || xx >= BlocksWide(this)) continue;
            std::uint8_t cell = cellRows[yy][xx];
            int cost = search->CostAt(RepresentativeX(cell) + xx * 8, RepresentativeY(cell) + yy * 8);
            if (cost >= kUnreached) continue;
            out.push_back({xx, yy, cost});
        }
    }
    search->Reset();
    return out;
}

void RegionMap::ClampEdgeCosts() {
    // Per size class (edges): rows of blocks of 8 signed words, one per direction (direction d and (d - 4) & 7
    // are opposite).
    auto grid = [this](int sizeClass) -> SearchGrid<std::int16_t>& { return edges[sizeClass]; };
    const int* dx = StepX();
    const int* dy = StepY();
    // No way out of the map: the directions leaving the first and last column and row are -1.
    for (int by = 0; by < BlocksHigh(this); ++by)
        for (int c = 0; c < 8; ++c) {
            std::int16_t* row = grid(c)[by];
            row[1] = row[2] = row[3] = -1;
            std::int16_t* last = row + grid(c).width * 8 - 8;
            last[5] = last[6] = last[7] = -1;
        }
    for (int bx = 0; bx < BlocksWide(this); ++bx)
        for (int c = 0; c < 8; ++c) {
            std::int16_t* first = grid(c)[0] + bx * 8;
            first[7] = first[0] = first[1] = -1;
            std::int16_t* last = grid(c)[grid(c).height - 1] + bx * 8;
            last[3] = last[4] = last[5] = -1;
        }
    // Each edge between neighbouring blocks takes the smaller of its two directions.
    for (int by = 0; by < BlocksHigh(this); ++by)
        for (int bx = 0; bx < BlocksWide(this); ++bx)
            for (int dir = 0; dir < 8; ++dir) {
                int nx = bx + dx[dir], ny = by + dy[dir];
                if (nx < 0 || ny < 0 || nx >= BlocksWide(this) || ny >= BlocksHigh(this)) continue;
                int opposite = (dir - 4) & 7;
                for (int c = 0; c < 8; ++c) {
                    std::int16_t& a = grid(c)[by][bx * 8 + dir];
                    std::int16_t& b = grid(c)[ny][nx * 8 + opposite];
                    std::int16_t m = a < b ? a : b;
                    a = m;
                    b = m;
                }
            }
}

bool RegionMap::LoadBlocks(ByteStream* stream) {
    auto read = [stream](void* buffer, int size) { stream->Read(buffer, static_cast<std::uint32_t>(size)); };
    int width, height;
    read(&width, 4);
    read(&height, 4);
    if (width <= 0 || height <= 0 || width > 0x100 || height > 0x100) return false;
    for (int c = 0; c < 8; ++c) {
        SearchGrid<std::int16_t>& edgeGrid = edges[c];
        SearchGrid<std::uint8_t>& cellGrid = cells[c];
        SearchGrid<std::uint16_t>& regionGrid = regions[c];
        edgeGrid.Resize(width, height, kNoWay, kEdgesPerBlock);
        cellGrid.Resize(width, height);
        regionGrid.Resize(width, height);
        for (int y = 0; y < BlocksHigh(this); ++y) {
            read(edgeGrid[y], width * 16);
            read(cellGrid[y], width);
            read(regionGrid[y], width * 2);
        }
    }
    forward.Attach(this, false);
    backward.Attach(this, true);
    bool ok;
    std::memcpy(&ok, &stream->ok, sizeof(ok));
    return ok;
}

void RegionMap::SaveBlocks(ByteStream* stream) {
    int width = BlocksWide(this), height = BlocksHigh(this);
    stream->WriteValue(width);
    stream->WriteValue(height);
    auto rowBytes = static_cast<std::uint32_t>(width);
    for (int c = 0; c < 8; ++c) {
        for (int y = 0; y < BlocksHigh(this); ++y) {
            stream->Write(edges[c][y], rowBytes * 16);
            stream->Write(cells[c][y], rowBytes);
            stream->Write(regions[c][y], rowBytes * 2);
        }
    }
}

void RegionMap::PickRepresentatives(AIMap* map, int bx, int by) {
    for (int c = 0; c < 8; ++c) {
        std::uint8_t& cell = cells[c][by][bx];
        // (3, 3); the original sets only x's two low bits.
        cell = WithRepresentativeX(cell, RepresentativeX(cell) | 3);
        cell = WithRepresentativeY(cell, 3);
        int best = 0;
        bool found = false;
        // Square rings from the block's middle: [4, 4), [3, 5), [2, 6), [1, 7), [0, 8).
        for (int k = 8, grow = 0; k >= 0; k -= 2, grow += 2) {
            int lo = k / 2, hi = grow + k / 2;
            for (int yy = lo; yy < hi; ++yy)
                for (int xx = lo; xx < hi; ++xx) {
                    int x = bx * 8 + xx, y = by * 8 + yy;
                    if (x < 0 || y < 0 || x >= map->cells.width || y >= map->cells.height) continue;
                    int nibble = map->cells[y][x].Passability(c * 4);
                    if (nibble == 0) continue;
                    if (!(nibble > best) && found) continue;
                    bool suits = CellSuitsBlockAt(c, bx, by, xx, yy);
                    if (!suits && found) continue;
                    found = suits;
                    best = nibble;
                    cell = WithRepresentativeX(cell, xx);
                    cell = WithRepresentativeY(cell, yy);
                }
        }
    }
}

bool RegionMap::FindRegionChain(int sizeClass, int fromX, int fromY, int toX, int toY, PathCells* chain,
                                 std::vector<BlockPoint>* excluded) {
    PrepareSearch(forward, sizeClass);
    PrepareSearch(backward, sizeClass);
    const BlockPoint* points = excluded->data();
    for (int i = 0; i < std::ssize(*excluded); ++i) {
        Exclude(forward, points[i].x, points[i].y);
        Exclude(backward, points[i].x, points[i].y);
    }
    bool ok = SeedSearch(sizeClass, false, &forward, fromX, fromY);
    if (ok) ok = SeedSearch(sizeClass, true, &backward, toX, toY);
    if (ok) ok = JoinBlockSearches(chain);
    FinishSearch(forward);
    FinishSearch(backward);
    return ok;
}

int RegionMap::DominantRegion(int sizeClass, int x, int y) {
    // The reachable blocks around the cell.
    std::vector<BlockCost> blocks = NearbyBlocks(sizeClass, false, x, y);
    // Count the blocks per region, in first-seen order (the original keeps a list of {region, count}; at most
    // nine entries).
    std::uint16_t seen[9];
    int counts[9];
    int distinct = 0;
    SearchGrid<std::uint16_t>& rows = regions[sizeClass];
    for (auto& n : blocks) {
        std::uint16_t region = rows[n.y][n.x];
        int i = 0;
        while (i < distinct && seen[i] != region) ++i;
        if (i < distinct) {
            ++counts[i];
        } else if (distinct < 9) {
            seen[distinct] = region;
            counts[distinct++] = 1;
        }
    }
    int best = 0;
    std::uint16_t region = 0;
    for (int i = 0; i < distinct; ++i)
        if (counts[i] > best) {
            best = counts[i];
            region = seen[i];
        }
    return region;
}

bool RegionMap::JoinBlockSearches(PathCells* chain) {
    auto meets = [](BlockSearch& other, int x, int y) {
        const BlockRecord& r = other.records[y][x];
        return r.settled && r.cost < kUnreached;
    };
    bool ok = true;
    int x = 0, y = 0;
    for (;;) {
        forward.Expand();
        backward.Expand();
        BlockRecord* best = forward.best;
        if (!best || best->cost == kUnreached) {
            ok = false;
            break;
        }
        x = best->x;
        y = best->y;
        if (meets(backward, x, y)) break;
        best = backward.best;
        if (!best || best->cost == kUnreached) {
            ok = false;
            break;
        }
        x = best->x;
        y = best->y;
        if (meets(forward, x, y)) break;
    }
    PathCells scratch;
    if (ok) {
        forward.PathTo(x, y, chain);
        backward.PathTo(x, y, &scratch);
        chain->insert(chain->end(), scratch.rbegin(), scratch.rend());
        PathCellsRemoveRepeats(chain);
    }
    return ok;
}

static void BlockSearchPathToOf(BlockSearch* searchIn, int x, int y, PathCells* path) {
    auto& search = *searchIn;
    path->clear();
    int columns = search.records.width;
    int rows = search.records.height;
    if (x < 0 || x >= columns || y < 0 || y >= rows) return;
    // The original builds each cell's second dword (flags) from stack memory it never writes; ours writes 0.
    auto cellAt = [](int cx, int cy) {
        PathCell c{static_cast<std::int16_t>(cx), static_cast<std::int16_t>(cy), 0, 0};
        return c;
    };
    // Walk back along the stored directions (1..8; 0 ends), then turn the cells round: the start first.
    while (int dir = search.records[y][x].backDirection) {
        path->push_back(cellAt(x, y));
        switch (dir) {
        case 1: --y; break;
        case 2: --y; --x; break;
        case 3: --x; break;
        case 4: ++y; --x; break;
        case 5: ++y; break;
        case 6: ++y; ++x; break;
        case 7: ++x; break;
        case 8: --y; ++x; break;
        default: break;
        }
    }
    path->push_back(cellAt(x, y));
    std::ranges::reverse(*path);
}

void RegionMap::MeasureBlockEdges(int bx, int by) {
    const int* dx = StepX();
    const int* dy = StepY();
    LocalSearch* search = &local;
    for (int c = 0; c < 8; ++c) {
        SearchGrid<std::uint8_t>& cellRows = cells[c];
        SearchGrid<std::int16_t>& edgeRows = edges[c];
        std::uint8_t own = cellRows[by][bx];
        local.backward = false;
        search->BeginInRectForClass(c, bx * 8 - 8, by * 8 - 8, bx * 8 + 16, by * 8 + 16, RepresentativeX(own) + bx * 8,
                    RepresentativeY(own) + by * 8);
        search->ExpandAll();
        // Each direction: the search cost from the block's representative cell to the
        // neighbour's, in units of 128 (at most 0x7FFF); -1 off the map or unreached.
        for (int dir = 0; dir < 8; ++dir) {
            int nx = bx + dx[dir], ny = by + dy[dir];
            std::int16_t value = -1;
            if (nx >= 0 && ny >= 0 && nx < BlocksWide(this) && ny < BlocksHigh(this)) {
                std::uint8_t other = cellRows[ny][nx];
                int cost = search->CostAt(RepresentativeX(other) + nx * 8, RepresentativeY(other) + ny * 8);
                if (cost < kUnreached) {
                    cost /= 128;
                    if (cost > 0x7FFF) cost = 0x7FFF;
                    value = static_cast<std::int16_t>(cost);
                }
            }
            edgeRows[by][bx * 8 + dir] = value;
        }
        search->Reset();
    }
}

void RegionMap::Attach(AIMap* map) {
    local.Attach(map, false);
    int width = map->cells.width / 8, height = map->cells.height / 8;
    for (int c = 0; c < 8; ++c) {
        edges[c].Resize(width, height, kNoWay, kEdgesPerBlock);
        cells[c].Resize(width, height);
        regions[c].Resize(width, height);
    }
    forward.Attach(this, false);
    backward.Attach(this, true);
}

RegionMap::RegionMap() {
    for (SearchGrid<std::int16_t>& grid : edges) grid.Resize(1, 1, kNoWay, kEdgesPerBlock);
    for (SearchGrid<std::uint8_t>& grid : cells) grid.Resize(1, 1);
    for (SearchGrid<std::uint16_t>& grid : regions) grid.Resize(1, 1);
    forward.Attach(this, false);
    backward.Attach(this, true);
}

bool RegionMap::CellSuitsBlockAt(int sizeClass, int bx, int by, int xx, int yy) {
    LocalSearch* search = &local;
    int left = bx * 8, top = by * 8, right = bx * 8 + 8, bottom = by * 8 + 8;
    // A search confined to the block and its far edges (9x9 cells) from the candidate cell; when
    // it cannot be set up the cell is taken.
    if (!search->BeginInRectForClass(sizeClass, left, top, right, bottom, xx + left, yy + top)) return true;
    search->ExpandAll();
    // Suitable when it reaches both the left and the right edge, or else both the top and the bottom edge.
    bool first = false, second = false;
    for (int y = top; y < top + 9; ++y) {
        if (search->CostAt(left, y) < kUnreached) first = true;
        if (search->CostAt(right, y) < kUnreached) second = true;
    }
    if (first && second) {
        search->Reset();
        return true;
    }
    first = false;
    second = false;
    for (int x = left; x < left + 9; ++x) {
        if (search->CostAt(x, top) < kUnreached) first = true;
        if (search->CostAt(x, bottom) < kUnreached) second = true;
    }
    search->Reset();
    return first && second;
}

void RegionMap::FloodRegionFrom(int sizeClass, std::vector<BlockPoint>* frontier, std::vector<BlockPoint>* next, int label) {
    const int* dx = StepX();
    const int* dy = StepY();
    SearchGrid<std::int16_t>& edgeRows = edges[sizeClass];
    SearchGrid<std::uint16_t>& regionRows = regions[sizeClass];
    for (;;) {
        lib::FreeStorage(*next);
        // Every open edge (word >= 0) of a frontier block leads to a neighbour; an unlabelled one
        // takes the label and joins the next frontier.
        for (int i = 0; i < std::ssize(*frontier); ++i) {
            int bx = (*frontier)[i].x, by = (*frontier)[i].y;
            for (int dir = 0; dir < 8; ++dir) {
                if (edgeRows[by][bx * 8 + dir] < 0) continue;
                int nx = bx + dx[dir], ny = by + dy[dir];
                std::uint16_t& region = regionRows[ny][nx];
                if (region != 0) continue;
                region = static_cast<std::uint16_t>(label);
                int n = std::ssize(*next);
                next->resize(n + 1);
                (*next)[n].x = nx;
                (*next)[n].y = ny;
            }
        }
        if (next->empty()) return;
        std::vector<BlockPoint>* swap = frontier;
        frontier = next;
        next = swap;
    }
}

void BlockSearch::PathTo(int x, int y, PathCells* path) { BlockSearchPathToOf(this, x, y, path); }
