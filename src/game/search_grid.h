#pragma once

// The game's grid template (the AI map's cells, occluders, water depths and marks, the local
// searches' nodes, the region map's edges, the fog grids' marks).

#include <cstddef>
#include <vector>

// A grid of width x height cells, row by row in one block. It starts empty (0 x 0); its owner sizes it
// (Resize).
template <typename T>
struct SearchGrid {
    // Row y (grid[y][x] is the cell at (x, y)).
    T* operator[](int y) { return data.data() + y * rowLength; }
    const T* operator[](int y) const { return data.data() + y * rowLength; }

    // Unless the grid is width x height already: made again at that size, `perCell` elements per cell, each
    // `fill`. An owner's constructor sizes its empty grids with it.
    void Resize(int newWidth, int newHeight, T fill = T{}, int perCell = 1) {
        if (width == newWidth && height == newHeight) return;
        width = newWidth;
        height = newHeight;
        rowLength = newWidth * perCell;
        data.assign(static_cast<std::size_t>(rowLength * newHeight), fill);
    }

    std::vector<T> data;    // height rows of rowLength elements
    int width = 0, height = 0;
    int rowLength = 0;      // elements per row: width x the elements per cell
};
