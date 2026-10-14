#pragma once

// The AI map's grids (SearchGrid, ai_map.h): the game's grid template, one copy of each function
// per element type. Allocate: one block of width x height cells and the row table; the element
// types with a constructor are filled. Names provisional. See docs/notes/ai.md.

#include "ai_map.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>

// The block (`perCell` elements per cell, left as allocated) and the row table of a grid whose
// width and height are set.
template <typename T>
void AllocateGrid(SearchGrid<T>& grid, int perCell = 1) {
    grid.data = static_cast<T*>(
        gamecrt::operator_new(static_cast<std::size_t>(grid.width * grid.height * perCell) * sizeof(T)));
    grid.rows = static_cast<T**>(gamecrt::operator_new(static_cast<std::size_t>(grid.height) * sizeof(T*)));
    for (int i = 0; i < grid.height; ++i) grid.rows[i] = grid.data + i * grid.width * perCell;
}

// The grids' destructor: the block and the row table freed (the pointers stay).
template <typename T>
void FreeGrid(SearchGrid<T>& grid) {
    gamecrt::operator_delete(grid.data);
    gamecrt::operator_delete(grid.rows);
}

// Allocate per element type (thiscall on the grid).
void __fastcall AllocateNodeGrid(SearchGrid<SearchNode>* grid);            // 005B1470
void __fastcall AllocateByteGrid(SearchGrid<std::uint8_t>* grid);          // 005B14C0
void __fastcall AllocateBlockRecordGrid(SearchGrid<BlockRecord>* grid);    // 005B1500
// 005B1550: the region map's edge words, eight per block, all -1.
void __fastcall AllocateEdgeGrid(SearchGrid<std::int16_t>* grid);
// 005B15F0: the region map's representative cells, zeroed.
void __fastcall AllocateZeroedByteGrid(SearchGrid<std::uint8_t>* grid);
void __fastcall AllocateRegionGrid(SearchGrid<std::uint16_t>* grid);       // 005B1680
// 005BD030: the map's cells: not passable, ground level 0, nothing on them, level 0 (the byte at
// +7 is left as allocated).
void __fastcall AllocateCellGrid(SearchGrid<AIMapCell>* grid);
// 005BD0B0: the map's occluder words: no occluder (bit 8), material 0, bit 7 clear; the other
// bits are left as allocated.
void __fastcall AllocateOccluderGrid(SearchGrid<std::uint32_t>* grid);

// 005BD010 and its copies (00598400 .. 00598B00, 005B1450, 005BCC30, 005CD490): the destructor.
void __fastcall FreeAnyGrid(SearchGrid<std::uint8_t>* grid);
// 005CD4B0: the event table's cells (the Rebirth-patched resize 005C9310 calls it).
struct AIEventCell;   // unit_ai.h
void __fastcall AllocateEventGrid(SearchGrid<AIEventCell>* grid);
// 005AF5A0: the region grids' default constructor (1 x 1; the region map's constructor hands it
// to the vector constructor iterator).
void __fastcall ConstructRegionGrid(SearchGrid<std::uint16_t>* grid);

// CAIMap's dirty rectangle (+0x004: dirtyMinX .. dirtyMaxY).
struct AIDirtyRect {
    // 005B40F0. Empty: the minimums 10000, the maximums 0.
    void Reset();

    int minX, maxX;
    int minY, maxY;
};

// 005BDD50 (thiscall on the terrain type array CAIMap + 0xFC, RET 4): the address of type `index`.
AITerrainType* __fastcall TerrainTypeAt(AITerrainType** types, void*, int index);
