#pragma once

// The game's grid template (the AI map's cells, occluders, water depths and marks, the local
// searches' nodes, the region map's edges, the fog grids' marks; ai_grids.h has its functions).

// {data, rows, width, height}: a grid allocated in one block with a row pointer table (the game's
// template; its allocators are in ai_grids.h). It starts empty (0 x 0, nothing allocated); its owner sizes it
// (ResizeGrid) and the destructor frees the block and the table.
template <typename T>
struct SearchGrid {
    SearchGrid() = default;
    SearchGrid(const SearchGrid&) = delete;
    SearchGrid& operator=(const SearchGrid&) = delete;
    ~SearchGrid() {
        delete[] data;
        delete[] rows;
    }

    // Row y (grid[y][x] is the cell at (x, y)).
    T* operator[](int y) const { return rows[y]; }

    T* data = nullptr;
    T** rows = nullptr;
    int width = 0, height = 0;
};
