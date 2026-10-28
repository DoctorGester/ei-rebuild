// A path on the stack as the unit AI builds one for a test search (unit_ai_moves.cpp, unit_ai_targets.cpp):
// both cell lists (blocks of 0x80), the excluded blocks, then Clear; freed in reverse.
#pragma once

#include "ai_map.h"

#include "lib/crt.h"
#include "lib/mfc_slots.h"
#include "game_data.h"

class ScopedPath {
public:
    ScopedPath() {
        InitList(path.steps);
        InitList(path.cells);
        path.Clear();
    }
    ~ScopedPath() {
        FreeList(path.cells);
        FreeList(path.steps);
    }
    ScopedPath(const ScopedPath&) = delete;
    ScopedPath& operator=(const ScopedPath&) = delete;

    AIPath path;

private:
    static void InitList(PathCellList& list) {
        list = {};
        list.vtable = PathCellListVtable();
        list.blockSize = 0x80;
    }
    static void FreeList(PathCellList& list) {
        list.vtable = PathCellListVtable();
        list.RemoveAll();
    }
};
