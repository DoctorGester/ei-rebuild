#pragma once

// The AI map's grids (SearchGrid, ai_map.h): one copy of each function per element type. Allocate: one block of
// width x height cells and the row table; the element types with a constructor are filled. See docs/notes/ai.md.

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

void AllocateNodeGrid(SearchGrid<SearchNode>* grid);
void AllocateByteGrid(SearchGrid<std::uint8_t>* grid);
// The region map's edge words, eight per block, all -1.
void AllocateEdgeGrid(SearchGrid<std::int16_t>* grid);
// The region map's representative cells, zeroed.
void AllocateZeroedByteGrid(SearchGrid<std::uint8_t>* grid);
void AllocateRegionGrid(SearchGrid<std::uint16_t>* grid);
// The map's cells: not passable, ground level 0, nothing on them, level 0 (unused_7 left as allocated).
void AllocateCellGrid(SearchGrid<AIMapCell>* grid);
// The map's occluder words: no occluder (bit 8), material 0, bit 7 clear; the other
// bits are left as allocated.
void AllocateOccluderGrid(SearchGrid<std::uint32_t>* grid);

// The event table's cells (Rebirth patches the resize that calls it).
// The edge and representative-cell grids' default constructors (1 x 1, then the allocator; the region map's
// constructor hands them to the vector constructor iterator).
void ConstructEdgeGrid(SearchGrid<std::int16_t>* grid);
void ConstructRepresentativeGrid(SearchGrid<std::uint8_t>* grid);
// The region grids' default constructor (1 x 1; the region map's constructor hands it
// to the vector constructor iterator).
void ConstructRegionGrid(SearchGrid<std::uint16_t>* grid);

void FillAreaLists(CAIMap* map);

