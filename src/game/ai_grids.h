#pragma once

// The AI map's grids (SearchGrid, ai_map.h): one copy of each function per element type. Allocate: one block of
// width x height cells and the row table; the element types with a constructor are filled. See docs/notes/ai.md.

#include "ai_map.h"

#include <cstddef>
#include <cstdint>
#include <type_traits>

// The block (`perCell` elements per cell, left as allocated) and the row table of a grid whose
// width and height are set.
template <typename T>
void AllocateGrid(SearchGrid<T>& grid, int perCell = 1) {
    static_assert(std::is_trivially_default_constructible_v<T> && std::is_trivially_destructible_v<T>);
    grid.data = new T[static_cast<std::size_t>(grid.width * grid.height * perCell)];
    grid.rows = new T*[static_cast<std::size_t>(grid.height)];
    for (int i = 0; i < grid.height; ++i) grid.rows[i] = grid.data + i * grid.width * perCell;
}

// The block and the row table freed before the grid is allocated again (the pointers stay).
template <typename T>
void FreeGrid(SearchGrid<T>& grid) {
    delete[] grid.data;
    delete[] grid.rows;
}

template <typename T>
void AllocatePlainGrid(SearchGrid<T>* grid) {
    AllocateGrid(*grid);
}

// A grid sized width x height: freed and made again by `allocate` (the cells left as it leaves them) unless it has
// that size already. An owner's constructor sizes its empty grids with it.
template <typename T>
void ResizeGrid(SearchGrid<T>& grid, int width, int height, void (*allocate)(SearchGrid<T>*) = &AllocatePlainGrid<T>) {
    if (grid.width == width && grid.height == height) return;
    FreeGrid(grid);
    grid.width = width;
    grid.height = height;
    allocate(&grid);
}

// The region map's edge words, eight per block, all -1.
void AllocateEdgeGrid(SearchGrid<std::int16_t>* grid);
// The region map's representative cells, zeroed.
void AllocateZeroedByteGrid(SearchGrid<std::uint8_t>* grid);
// The map's cells: not passable, ground level 0, nothing on them, level 0 (unused_7 left as allocated).
void AllocateCellGrid(SearchGrid<AIMapCell>* grid);
// The map's occluder words: occluder::kNone, terrain type 0, kChained clear; the other bits are left as allocated.
void AllocateOccluderGrid(SearchGrid<std::uint32_t>* grid);

void FillAreaLists(CAIMap* map);

