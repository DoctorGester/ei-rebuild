// A path on the stack as the unit AI builds one for a test search (unit_ai_moves.cpp,
// unit_ai_targets.cpp): both cell lists (blocks of 0x80; 00545750 / 0054EAC0 + 00556D10), the
// point array (00557230), then Clear (005B23A0); freed in reverse (00557250, 00539890 or the
// vtable and 00556D40).
#pragma once

#include "ai_map.h"

#include "lib/crt.h"

class ScopedPath {
public:
    ScopedPath() {
        InitList(path.steps);
        InitList(path.cells);
        path.pointsVtable = reinterpret_cast<void*>(AIPath::kPointsVtable);
        path.points = nullptr;
        path.pointCount = 0;
        path.pointMax = 0;
        path.pointGrowBy = 0;
        path.Clear();
    }
    ~ScopedPath() {
        path.pointsVtable = reinterpret_cast<void*>(AIPath::kPointsVtable);
        if (path.points) gamecrt::operator_delete(path.points);
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
