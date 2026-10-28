// CRegionMap::ReachesRegion and LabelRegions, and the region map's block grids. See ai_map.h and
// docs/notes/ai.md ("Regions").
#include "ai_map.h"
#include "ai_grids.h"
#include "core_streams.h"

#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "lib/static_object.h"

#include <cstdlib>
#include <initializer_list>

namespace {

constinit lib::StaticObject<std::vector<BlockPoint>> g_blocks = /*007C2EE0*/ {};
std::uint8_t g_blocksGuard = /*007C2EF4*/ {};
void DestroyBlocks() { g_blocks.Destroy(); }

// LabelRegions' function-local statics: the flood queue (guard bit 0 of g_floodGuard) and its second list (bit 1).
constinit lib::StaticObject<std::vector<BlockPoint>> g_floodSecond = /*007C2EF8*/ {};
constinit lib::StaticObject<std::vector<BlockPoint>> g_floodQueue = /*007C2F10*/ {};
std::uint8_t g_floodGuard = /*007C2F24*/ {};
void DestroyFloodQueue() { g_floodQueue.Destroy(); }
void DestroyFloodSecond() { g_floodSecond.Destroy(); }

constexpr int kUnreached = 0x7FFFFFFF;

// The block grid's size: the first size class's edge grid.
int BlocksWide(const CRegionMap* map) { return map->edges[0].width; }
int BlocksHigh(const CRegionMap* map) { return map->edges[0].height; }

// The direction steps of block directions 0..7 (the tables from their index 1).
const int* StepX() { return kDirectionDx + 1; }
const int* StepY() { return kDirectionDy + 1; }

// A per size class grid of the region map reallocated for the map's size.
template <typename T>
void Fit(SearchGrid<T>& grid, int width, int height, void (*allocate)(SearchGrid<T>*)) {
    if (grid.width == width && grid.height == height) return;
    FreeGrid(grid);
    grid.width = width;
    grid.height = height;
    allocate(&grid);
}

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
    BlockRecord* record = &search.records.rows[y][x];
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

bool CRegionMap::ReachesRegion(int sizeClass, int x, int y, int region, SearchGrid<std::int8_t>* marks) {
    int bx = x / 8, by = y / 8;
    if (BlocksWide(this) - 1 < bx) bx = BlocksWide(this) - 1;
    if (bx < 0) bx = 0;
    if (BlocksHigh(this) - 1 < by) by = BlocksHigh(this) - 1;
    if (by < 0) by = 0;

    std::vector<BlockPoint>& blocks = *g_blocks;
    if (!(g_blocksGuard & 1)) {
        g_blocksGuard |= 1;
        g_blocks.Construct();
        std::atexit(DestroyBlocks);
    }
    lib::FreeStorage(blocks);
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

    std::uint16_t** regionRows = regions[sizeClass].rows;
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
            for (int col = bx * 8; col < bx * 8 + 8; ++col) marks->rows[row][col] = 2;
        return false;
    }

    int x0 = bx * 8 - 8, y0 = by * 8 - 8, x1 = bx * 8 + 16, y1 = by * 8 + 16;
    local.backward = true;
    LocalSearch* search = &local;
    search->BeginInRectForClass(sizeClass, x0, y0, x1, y1, x, y);
    search->SaveLevels(true);
    search->ExpandAll();
    search->RestoreLevels();

    std::uint8_t** cellRows = cells[sizeClass].rows;
    bool found = false;
    for (int k = 0; k < std::ssize(blocks); ++k) {
        int xx = blocks[k].x, yy = blocks[k].y;
        std::uint8_t cell = cellRows[yy][xx];
        if (search->CostAt((cell & 0xF) + xx * 8, (cell >> 4) + yy * 8) < kUnreached && inRegion(xx, yy)) {
            found = true;
            break;
        }
    }
    std::uint8_t mark = found ? 1 : 2;
    for (int row = y0; row < y1; ++row)
        for (int col = x0; col < x1; ++col)
            if (search->CostAt(col, row) < kUnreached) marks->rows[row][col] = static_cast<std::int8_t>(mark);
    search->Reset();
    return found;
}

void CRegionMap::LabelRegions() {
    std::vector<BlockPoint>& queue = *g_floodQueue;
    std::vector<BlockPoint>& second = *g_floodSecond;
    if (!(g_floodGuard & 1)) {
        g_floodGuard |= 1;
        g_floodQueue.Construct();
        std::atexit(DestroyFloodQueue);
    }
    if (!(g_floodGuard & 2)) {
        g_floodGuard |= 2;
        g_floodSecond.Construct();
        std::atexit(DestroyFloodSecond);
    }
    queue.clear();
    for (int sizeClass = 0; sizeClass < 8; ++sizeClass) {
        SearchGrid<std::uint16_t>& grid = regions[sizeClass];
        std::memset(grid.data, 0, static_cast<std::size_t>(grid.height * grid.width * 2));
        int label = 1;
        for (int by = 0; by < BlocksHigh(this); ++by)
            for (int bx = 0; bx < BlocksWide(this); ++bx) {
                if (grid.rows[by][bx] != 0) continue;
                grid.rows[by][bx] = static_cast<std::uint16_t>(label);
                int n = std::ssize(queue);
                queue.resize(n + 1);
                queue[n].x = bx;
                queue[n].y = by;
                FloodRegionFrom(sizeClass, &queue, &second, label);
                ++label;
            }
    }
}

void CRegionMap::NearbyBlocks(int sizeClass, bool flag, int x, int y, std::list<BlockCost>* out) {
    int bx = x / 8, by = y / 8;
    if (BlocksWide(this) - 1 < bx) bx = BlocksWide(this) - 1;
    if (bx < 0) bx = 0;
    if (BlocksHigh(this) - 1 < by) by = BlocksHigh(this) - 1;
    if (by < 0) by = 0;
    out->clear();

    local.backward = flag;
    LocalSearch* search = &local;
    search->BeginInRectForClass(sizeClass, bx * 8 - 8, by * 8 - 8, bx * 8 + 16, by * 8 + 16, x, y);
    search->SaveLevels(true);
    search->ExpandAll();
    search->RestoreLevels();
    std::uint8_t** cellRows = cells[sizeClass].rows;
    for (int yy = by - 1; yy < by + 2; ++yy) {
        if (yy < 0 || yy >= BlocksHigh(this)) continue;
        for (int xx = bx - 1; xx < bx + 2; ++xx) {
            if (xx < 0 || xx >= BlocksWide(this)) continue;
            std::uint8_t cell = cellRows[yy][xx];
            int cost = search->CostAt((cell & 0xF) + xx * 8, (cell >> 4) + yy * 8);
            if (cost >= kUnreached) continue;
            out->push_back({xx, yy, cost});
        }
    }
    search->Reset();
}

void CRegionMap::ClampEdgeCosts() {
    // Per size class (edges): rows of blocks of 8 signed words, one per direction (direction d and (d - 4) & 7
    // are opposite).
    auto grid = [this](int sizeClass) -> SearchGrid<std::int16_t>& { return edges[sizeClass]; };
    const int* dx = StepX();
    const int* dy = StepY();
    // No way out of the map: the directions leaving the first and last column and row are -1.
    for (int by = 0; by < BlocksHigh(this); ++by)
        for (int c = 0; c < 8; ++c) {
            std::int16_t* row = grid(c).rows[by];
            row[1] = row[2] = row[3] = -1;
            std::int16_t* last = row + grid(c).width * 8 - 8;
            last[5] = last[6] = last[7] = -1;
        }
    for (int bx = 0; bx < BlocksWide(this); ++bx)
        for (int c = 0; c < 8; ++c) {
            std::int16_t* first = grid(c).rows[0] + bx * 8;
            first[7] = first[0] = first[1] = -1;
            std::int16_t* last = grid(c).rows[grid(c).height - 1] + bx * 8;
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
                    std::int16_t& a = grid(c).rows[by][bx * 8 + dir];
                    std::int16_t& b = grid(c).rows[ny][nx * 8 + opposite];
                    std::int16_t m = a < b ? a : b;
                    a = m;
                    b = m;
                }
            }
}

bool CRegionMap::LoadBlocks(CStream* stream) {
    auto read = [stream](void* buffer, int size) { stream->Read(buffer, static_cast<std::uint32_t>(size)); };
    int width, height;
    read(&width, 4);
    read(&height, 4);
    if (width <= 0 || height <= 0 || width > 0x100 || height > 0x100) return false;
    for (int c = 0; c < 8; ++c) {
        SearchGrid<std::int16_t>& edgeGrid = edges[c];
        SearchGrid<std::uint8_t>& cellGrid = cells[c];
        SearchGrid<std::uint16_t>& regionGrid = regions[c];
        Fit(edgeGrid, width, height, &AllocateEdgeGrid);
        Fit(cellGrid, width, height, &AllocateZeroedByteGrid);
        Fit(regionGrid, width, height, &AllocateRegionGrid);
        for (int y = 0; y < BlocksHigh(this); ++y) {
            read(edgeGrid.rows[y], width * 16);
            read(cellGrid.rows[y], width);
            read(regionGrid.rows[y], width * 2);
        }
    }
    forward.Attach(this, false);
    backward.Attach(this, true);
    bool ok;
    std::memcpy(&ok, &stream->ok_, sizeof(ok));
    return ok;
}

void CRegionMap::SaveBlocks(CStream* stream) {
    int width = BlocksWide(this), height = BlocksHigh(this);
    stream->Write(&width, 4);
    stream->Write(&height, 4);
    auto rowBytes = static_cast<std::uint32_t>(width);
    for (int c = 0; c < 8; ++c) {
        for (int y = 0; y < BlocksHigh(this); ++y) {
            stream->Write(edges[c].rows[y], rowBytes * 16);
            stream->Write(cells[c].rows[y], rowBytes);
            stream->Write(regions[c].rows[y], rowBytes * 2);
        }
    }
}

void CRegionMap::PickRepresentatives(CAIMap* map, int bx, int by) {
    for (int c = 0; c < 8; ++c) {
        std::uint8_t& cell = cells[c].rows[by][bx];
        cell = static_cast<std::uint8_t>((cell & 0xF3) | 3);
        cell = static_cast<std::uint8_t>((cell & 0x0F) | 0x30);
        int best = 0;
        bool found = false;
        // Square rings from the block's middle: [4, 4), [3, 5), [2, 6), [1, 7), [0, 8).
        for (int k = 8, grow = 0; k >= 0; k -= 2, grow += 2) {
            int lo = k / 2, hi = grow + k / 2;
            for (int yy = lo; yy < hi; ++yy)
                for (int xx = lo; xx < hi; ++xx) {
                    int x = bx * 8 + xx, y = by * 8 + yy;
                    if (x < 0 || y < 0 || x >= map->cells.width || y >= map->cells.height) continue;
                    int nibble = static_cast<int>((map->cells[y][x].passable >> (c * 4)) & 0xF);
                    if (nibble == 0) continue;
                    if (!(nibble > best) && found) continue;
                    bool suits = CellSuitsBlockAt(c, bx, by, xx, yy);
                    if (!suits && found) continue;
                    found = suits;
                    best = nibble;
                    cell = static_cast<std::uint8_t>((cell & 0xF0) | (xx & 0xF));
                    cell = static_cast<std::uint8_t>((cell & 0x0F) | (yy << 4));
                }
        }
    }
}

bool CRegionMap::FindRegionChain(int sizeClass, int fromX, int fromY, int toX, int toY, PathCells* chain,
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

int CRegionMap::DominantRegion(int sizeClass, int x, int y) {
    // The reachable blocks around the cell (NearbyBlocks, through its address).
    std::list<BlockCost> blocks{};
    NearbyBlocks(sizeClass, false, x, y, &blocks);
    // Count the blocks per region, in first-seen order (the original keeps a list of {region, count}; at most
    // nine entries).
    std::uint16_t seen[9];
    int counts[9];
    int distinct = 0;
    std::uint16_t** rows = regions[sizeClass].rows;
    for (auto n = blocks.begin(); n != blocks.end(); ++n) {
        std::uint16_t region = rows[n->y][n->x];
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
    blocks.clear();
    return region;
}

bool CRegionMap::JoinBlockSearches(PathCells* chain) {
    auto meets = [](BlockSearch& other, int x, int y) {
        const BlockRecord& r = other.records.rows[y][x];
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
        PathCells reversed;
        chain->Append(scratch.Reversed(&reversed));
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
    // Walk back along the stored directions (1..8; 0 ends), each cell added at the head.
    while (int dir = search.records.rows[y][x].backDirection) {
        path->push_front(cellAt(x, y));
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
    path->push_front(cellAt(x, y));
}

void CRegionMap::MeasureBlockEdges(int bx, int by) {
    const int* dx = StepX();
    const int* dy = StepY();
    LocalSearch* search = &local;
    for (int c = 0; c < 8; ++c) {
        std::uint8_t** cellRows = cells[c].rows;
        std::int16_t** edgeRows = edges[c].rows;
        std::uint8_t own = cellRows[by][bx];
        local.backward = false;
        search->BeginInRectForClass(c, bx * 8 - 8, by * 8 - 8, bx * 8 + 16, by * 8 + 16, (own & 0xF) + bx * 8,
                    (own >> 4) + by * 8);
        search->ExpandAll();
        // Each direction: the search cost from the block's representative cell to the
        // neighbour's, in units of 128 (at most 0x7FFF); -1 off the map or unreached.
        for (int dir = 0; dir < 8; ++dir) {
            int nx = bx + dx[dir], ny = by + dy[dir];
            std::int16_t value = -1;
            if (nx >= 0 && ny >= 0 && nx < BlocksWide(this) && ny < BlocksHigh(this)) {
                std::uint8_t other = cellRows[ny][nx];
                int cost = search->CostAt((other & 0xF) + nx * 8, (other >> 4) + ny * 8);
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

void CRegionMap::Attach(CAIMap* map) {
    local.Attach(map, false);
    int width = map->cells.width / 8, height = map->cells.height / 8;
    for (int c = 0; c < 8; ++c) {
        Fit(edges[c], width, height, &AllocateEdgeGrid);
        Fit(cells[c], width, height, &AllocateZeroedByteGrid);
        // The region grid's allocation is inlined here: u16 per block, rows of `width`.
        SearchGrid<std::uint16_t>& grid = regions[c];
        if (grid.width == width && grid.height == height) continue;
        FreeGrid(grid);
        grid.width = width;
        grid.height = height;
        auto* data = new std::uint16_t[static_cast<std::size_t>(height * width)];
        grid.data = data;
        grid.rows = new std::uint16_t*[static_cast<std::size_t>(grid.height)];
        for (int y = 0; y < grid.height; ++y) grid.rows[y] = data + grid.width * y;
    }
    forward.Attach(this, false);
    backward.Attach(this, true);
}

CRegionMap* ConstructRegionMap(CRegionMap* self) {
    for (BlockSearch* search : {&self->forward, &self->backward}) {
        SearchGrid<BlockRecord>& records = search->records;
        records.width = 1;
        records.height = 1;
        records.data = new BlockRecord[1];
        records.rows = new BlockRecord*[static_cast<std::size_t>(records.height)];
        for (int y = 0; y < records.height; ++y) records.rows[y] = records.data + y * records.width;
        search->active = false;
        search->map = nullptr;
    }
    LocalSearch& local = self->local;
    local.nodes.width = 1;
    local.nodes.height = 1;
    AllocateNodeGrid(&local.nodes);
    local.levels.width = 1;
    local.levels.height = 1;
    AllocateByteGrid(&local.levels);
    local.map = nullptr;
    // The CRT's eh vector constructor iterator over each grid array, in order.
    for (SearchGrid<std::int16_t>& grid : self->edges) ConstructEdgeGrid(&grid);
    for (SearchGrid<std::uint8_t>& grid : self->cells) ConstructRepresentativeGrid(&grid);
    for (SearchGrid<std::uint16_t>& grid : self->regions) ConstructRegionGrid(&grid);
    self->forward.Attach(self, false);
    self->backward.Attach(self, true);
    self->progress = nullptr;
    return self;
}

bool CRegionMap::CellSuitsBlockAt(int sizeClass, int bx, int by, int xx, int yy) {
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

void CRegionMap::FloodRegionFrom(int sizeClass, std::vector<BlockPoint>* frontier, std::vector<BlockPoint>* next, int label) {
    const int* dx = StepX();
    const int* dy = StepY();
    std::int16_t** edgeRows = edges[sizeClass].rows;
    std::uint16_t** regionRows = regions[sizeClass].rows;
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
