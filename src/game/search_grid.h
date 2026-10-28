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

    // ai_grids.cpp. The fog grid's marks, FogGrid::cells. Reallocated to
    // newWidth x newHeight (the cells are not kept or cleared) unless the grid already has that size.
    void Resize(int newWidth, int newHeight);
    // The constructor for a size (several copies); returns this.
    SearchGrid* Construct(int newWidth, int newHeight);
};
template <> void SearchGrid<std::uint8_t>::Resize(int newWidth, int newHeight);
template <> SearchGrid<std::int8_t>* SearchGrid<std::int8_t>::Construct(int newWidth, int newHeight);
template <> SearchGrid<std::uint32_t>* SearchGrid<std::uint32_t>::Construct(int newWidth, int newHeight);
