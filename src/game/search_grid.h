#pragma once

// The game's grid template (the AI map's cells, occluders, water depths and marks, the local
// searches' nodes, the region map's edges, the fog grids' marks; ai_grids.cpp has its functions).

#include <cstdint>

// {data, rows, width, height}: a grid allocated in one block with a row pointer table (the game's
// template; its instantiations are in ai_grids.h).
template <typename T>
struct SearchGrid {
    T* data;
    T** rows;
    int width, height;

    // Row y (grid[y][x] is the cell at (x, y)).
    T* operator[](int y) const { return rows[y]; }

    // ai_grids.cpp. 005CD3A0 (RET 8; the fog grid's marks, FogGrid::cells). Reallocated to
    // newWidth x newHeight (the cells are not kept or cleared) unless the grid already has that size.
    void Resize(int newWidth, int newHeight);
    // 005CD410 (RET 4) / 005C4BA0 (RET 4; A-W14: the local search's nodes, whose Recenter 005C25D0
    // keeps the old window in such a copy). Makes the grid a copy of `source`: its size, a block and
    // row table of its own, the cells copied.
    void CopyFrom(const SearchGrid* source);
    // 005BCBD0 / 005BCC50 / 005BCEE0 / 005BCF00 (RET 8). The constructor for a size; returns this.
    SearchGrid* Construct(int newWidth, int newHeight);
};
template <> void SearchGrid<std::uint8_t>::Resize(int newWidth, int newHeight);
template <> void SearchGrid<std::uint8_t>::CopyFrom(const SearchGrid* source);
template <> SearchGrid<std::int8_t>* SearchGrid<std::int8_t>::Construct(int newWidth, int newHeight);
template <> SearchGrid<std::uint32_t>* SearchGrid<std::uint32_t>::Construct(int newWidth, int newHeight);
static_assert(sizeof(SearchGrid<std::uint8_t>) == 0x10);
