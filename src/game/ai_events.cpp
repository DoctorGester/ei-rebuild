// The world's AI events: the time wheel and the event grid (Advance, ForgetUnit, Load, Save). See unit_ai.h
// (AIEventTable) and docs/notes/ai.md.
#include "unit_ai.h"
#include "game_strings.h"

#include "combat.h"
#include "core_streams.h"
#include "resource.h"
#include "world_data.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/mfc_slots.h"
#include "lib/static_object.h"
#include "game_data.h"

#include <cstring>
#include <string>

namespace {

constexpr float kCellScale = 0.125f;   // 8 x 8 cells
constexpr int kWheelTicks = 0x400;
constexpr int kSides = 16;
constexpr int kSideMask = kSides - 1;
constexpr int kWheelMask = kWheelTicks - 1;

// The records' pool (a CList<AIEvent> whose nodes hold them) and the free list (chained through
// AIEvent::next).
static_assert(sizeof(mfc::CList<AIEvent>) == 0x1C);
constinit lib::StaticObject<mfc::CList<AIEvent>> g_eventPool = /*007C43A8*/ {};
mfc::CList<AIEvent>& AIEventPool() { return *g_eventPool; }
AIEvent*& FreeEvents() { return g_freeEvents; }

void PushFree(AIEvent* event) {
    event->next = FreeEvents();
    FreeEvents() = event;
}

} // namespace

void __fastcall AIEventPoolDestruct(mfc::CList<AIEvent>* pool) {
    pool->vtable = mfc::impl::StaticListClass::Vtable();
    pool->RemoveAll();
}

namespace {
// The pool's static initialiser (0x400 nodes a block) and the exit function it registers.
void __cdecl DestroyEventPool() { AIEventPoolDestruct(&*g_eventPool); }
} // namespace

void __cdecl ConstructEventPool() {
    mfc::impl::ConstructListHeader<mfc::impl::StaticListClass>(&*g_eventPool, 0x400);
    libcrt::AtExit(DestroyEventPool);
}

AIEventCell* AIEventTable::CellOf(const AIEvent* event) const {
    // Each coordinate is scaled, stored to a float and rounded to nearest (FISTP).
    const int x = x87::fistp(event->x * kCellScale);
    const int y = x87::fistp(event->y * kCellScale);
    if (x < 0 || y < 0 || x >= width || y >= height) return nullptr;
    return rows[y] + x;
}

void AIEventTable::Free(AIEvent* event, int side) {
    if (AIEvent* prev = event->prev) {
        prev->next = event->next;
    } else {
        // Like the original, the cell is not tested: an event outside the grid faults here.
        AIEventCell* cell = CellOf(event);
        if (cell->bySide[side]) cell->bySide[side] = event->next;
    }
    if (event->next) event->next->prev = event->prev;
    PushFree(event);
}

void AIEventTable::Advance(int now) {
    tick = now;
    for (int side = 0; side < kSides; ++side) {
        for (AIEvent* event = slots[tick & kWheelMask][side]; event;) {
            Free(event, side);
            event = event->nextInSlot;
        }
        slots[tick & kWheelMask][side] = nullptr;
    }
}

void AIEventTable::ForgetUnit(CUnitServer* controller) {
    CUnit* unit = controller->unit;
    const int side = unit->side & kSideMask;
    const int id = unit->id_;
    for (int slot = 0; slot < kWheelTicks; ++slot) {
        AIEvent** link = &slots[slot][side];
        while (AIEvent* event = *link) {
            if (event->sourceId != id) {
                link = &event->nextInSlot;
                continue;
            }
            *link = event->nextInSlot;
            Free(event, side);
        }
    }
}

void AIEventTable::Load(CStream* stream) {
    for (;;) {
        char more = 0;
        stream->Read(&more, 1);
        if (!more) return;
        AIEvent* event = FreeEvents();
        if (event) {
            FreeEvents() = event->next;
        } else {
            event = &AIEventPool().AddHead(AIEvent{})->data;
        }
        event->next = nullptr;
        event->prev = nullptr;
        event->nextInSlot = nullptr;
        stream->Read(&event->x, 8);   // x, y
        stream->Read(event->origin, 8);
        stream->Read(&event->expiry, 4);
        stream->Read(event->payload, 8);
        stream->Read(&event->sourceId, 4);
        stream->Read(&event->side, 4);
        AIEventCell* cell = CellOf(event);
        if (!cell) {
            PushFree(event);
            continue;
        }
        const int side = event->side & kSideMask;
        event->next = cell->bySide[side];
        if (cell->bySide[side]) cell->bySide[side]->prev = event;
        event->prev = nullptr;
        cell->bySide[side] = event;
        AIEvent*& slot = slots[(event->expiry - 1) & kWheelMask][side];
        event->nextInSlot = slot;
        slot = event;
    }
}

void AIEventTable::Save(CStream* stream) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int side = 0; side < kSides; ++side) {
                for (AIEvent* event = rows[y][x].bySide[side]; event; event = event->next) {
                    const char more = 1;
                    stream->Write(&more, 1);
                    stream->Write(&event->x, 8);
                    stream->Write(event->origin, 8);
                    int value = event->expiry;
                    stream->Write(&value, 4);
                    stream->Write(event->payload, 8);
                    value = event->sourceId;
                    stream->Write(&value, 4);
                    value = event->side;
                    stream->Write(&value, 4);
                }
            }
        }
    }
    const char end = 0;
    stream->Write(&end, 1);
}

// ---- Setup for a map ----------------------------------------------------------------------------------

namespace {

constexpr const char* kAiInfoArchive = "aiinfo.res";
constexpr const char* kCantReadAiInfo = "Unable to read file aiinfo.res";
constexpr const char* kNormalFile = "normal.dat";
constexpr const char* kCantOpenNormal = "CAIMap::Load.\n Can't open file \"normal.dat\"";
constexpr std::size_t kNormalTableSize = 0x1000;
std::uint8_t g_normalTable[kNormalTableSize] = /*007C43C4*/ {};   // the 4096 bytes of normal.dat



} // namespace

int AIEventTable::Setup(CAIMap* aiMap, float sizeX, float sizeY) {
    Clear();
    map = aiMap;
    const float scale = (-0.125f /*0073FEB0*/);
    const int newWidth = 1 - static_cast<int>(gamecrt::Ftol(sizeX * scale));
    const int newHeight = 1 - static_cast<int>(gamecrt::Ftol(sizeY * scale));
    if (width != newWidth || height != newHeight) {
        gamecrt::operator_delete(cells);
        gamecrt::operator_delete(rows);
        height = newHeight;
        width = newWidth;
        cells = static_cast<AIEventCell*>(
            gamecrt::operator_new(static_cast<std::size_t>(newHeight * newWidth) * sizeof(AIEventCell)));
        rows = static_cast<AIEventCell**>(gamecrt::operator_new(static_cast<std::size_t>(height) * sizeof(AIEventCell*)));
        for (int i = 0; i < height; ++i) rows[i] = cells + width * i;
    }
    std::memset(cells, 0, static_cast<std::size_t>(height * width) * sizeof(AIEventCell));

    lib::InPlace<CMappedResource> storage;
    CMappedResource* archive = storage.Get();
    new (archive) CMappedResource();
    bool failed;
    {
        std::string folder = TheTextFolder().GetString();
        std::string path = folder + kAiInfoArchive;
        failed = archive->CMappedResource::OpenResource(path.c_str(), 0) == 0;
    }
    void* data = nullptr;
    const char* error = kCantReadAiInfo;
    if (!failed) {
        data = archive->CMappedResource::GetFile(kNormalFile, nullptr);
        error = kCantOpenNormal;
    }
    if (!data) {
        const int result = game::FatalError(error);
        archive->CMappedResource::Destroy();
        return result;
    }
    std::memcpy(g_normalTable, data, kNormalTableSize);
    archive->CMappedResource::CloseFile(data);
    archive->CMappedResource::Destroy();
    return 1;
}
