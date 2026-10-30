// The world's AI events: the time wheel and the event grid (Advance, ForgetUnit, Load, Save). See unit_ai.h
// (AIEventTable) and docs/notes/ai.md.
#include "unit_ai.h"

#include "ai_alarm.h"
#include "combat.h"
#include "console.h"
#include "core_streams.h"
#include "game_strings.h"
#include "object_diplomacy.h"
#include "resource.h"
#include "world.h"
#include "world_data.h"
#include "lib/crt.h"
#include "lib/in_place.h"
#include "lib/rounding.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <list>
#include <new>
#include <string>

namespace {

constexpr float kCellScale = 0.125f;   // 8 x 8 cells
constexpr int kWheelTicks = 0x400;
constexpr int kSides = 16;
constexpr int kSideMask = kSides - 1;
constexpr int kWheelMask = kWheelTicks - 1;

// The records' pool: a list, as the wheel and the free list (chained through AIEvent::next) point into it while it
// grows.
std::list<AIEvent> g_eventPool;

void PushFree(AIEvent* event) {
    event->next = g_freeEvents;
    g_freeEvents = event;
}

constexpr const char* kAiInfoArchive = "aiinfo.res";
constexpr const char* kCantReadAiInfo = "Unable to read file aiinfo.res";
constexpr const char* kNormalFile = "normal.dat";
constexpr const char* kCantOpenNormal = "CAIMap::Load.\n Can't open file \"normal.dat\"";
constexpr std::size_t kNormalTableSize = 0x1000;
std::uint8_t g_normalTable[kNormalTableSize] = /*007C43C4*/ {};   // the 4096 bytes of normal.dat

} // namespace

AIEventCell* AIEventTable::CellOf(const AIEvent* event) {
    // Each coordinate is scaled, stored to a float and rounded to nearest.
    const int x = lib::RoundEven(event->x * kCellScale);
    const int y = lib::RoundEven(event->y * kCellScale);
    if (x < 0 || y < 0 || x >= width || y >= height) return nullptr;
    return &cells[y * width + x];
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

void AIEventTable::ForgetUnit(UnitServer* controller) {
    Unit* unit = controller->unit;
    const int side = unit->side & kSideMask;
    const int id = unit->id;
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

void AIEventTable::Load(ByteStream* stream) {
    for (;;) {
        char more = 0;
        stream->ReadValue(more);
        if (!more) return;
        AIEvent* event = g_freeEvents;
        if (event) {
            g_freeEvents = event->next;
        } else {
            g_eventPool.push_front(AIEvent{});
            event = &g_eventPool.front();
        }
        event->next = nullptr;
        event->prev = nullptr;
        event->nextInSlot = nullptr;
        stream->Read(&event->x, 8);   // x, y
        stream->Read(event->origin, 8);
        stream->ReadValue(event->expiry);
        stream->Read(event->payload, 8);
        stream->ReadValue(event->sourceId);
        stream->ReadValue(event->side);
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

void AIEventTable::Save(ByteStream* stream) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            for (int side = 0; side < kSides; ++side) {
                for (AIEvent* event = cells[y * width + x].bySide[side]; event; event = event->next) {
                    const char more = 1;
                    stream->WriteValue(more);
                    stream->Write(&event->x, 8);
                    stream->Write(event->origin, 8);
                    int value = event->expiry;
                    stream->WriteValue(value);
                    stream->Write(event->payload, 8);
                    value = event->sourceId;
                    stream->WriteValue(value);
                    value = event->side;
                    stream->WriteValue(value);
                }
            }
        }
    }
    const char end = 0;
    stream->WriteValue(end);
}

// ---- Setup for a map ----------------------------------------------------------------------------------

int AIEventTable::Setup(AIMap* aiMap, float sizeX, float sizeY) {
    Clear();
    map = aiMap;
    const float scale = (-0.125f);
    const int newWidth = 1 - static_cast<int>(static_cast<std::int64_t>(sizeX * scale));
    const int newHeight = 1 - static_cast<int>(static_cast<std::int64_t>(sizeY * scale));
    width = newWidth;
    height = newHeight;
    cells.assign(static_cast<std::size_t>(width * height), AIEventCell{});

    lib::InPlace<MappedResource> storage;
    MappedResource* archive = storage.Get();
    new (archive) MappedResource();
    bool failed;
    {
        std::string folder = g_textFolder;
        std::string path = folder + kAiInfoArchive;
        failed = archive->MappedResource::OpenResource(path, 0) == 0;
    }
    void* data = nullptr;
    const char* error = kCantReadAiInfo;
    if (!failed) {
        data = archive->MappedResource::GetFile(kNormalFile, nullptr);
        error = kCantOpenNormal;
    }
    if (!data) {
        const int result = game::FatalError(error);
        archive->MappedResource::Destroy();
        return result;
    }
    std::memcpy(g_normalTable, data, kNormalTableSize);
    archive->MappedResource::CloseFile(data);
    archive->MappedResource::Destroy();
    return 1;
}

// --- Ai alarm ----------------------------------------------------------------------------------------

// The step of a unit's AI alarm. A pending level above the current one becomes active for 20 steps: the AI
// log (the unitacks stream) gets "Unit <id> acknowledges with <what>" and some levels make the unit speak.
// Then, while a level is active and its timer runs, every group member on a side the owner's side is allied
// with gets its controller's alarm raised to the level. See docs/notes/ai.md.

namespace {

// The alarm's log: the unitacks stream.
constexpr console::Stream& kAlarmLog = console::g_unitAcksLogOut;

} // namespace

static void StepAlarmOf(AIAlarm* state, UnitServer* owner) {
    UnitServer* server = owner;
    WorldServer* world = owner->ServerWorld();
    if (state->pending != AIRemark::None) {
        if (state->pending > state->level) {
            state->active = state->pending;
            state->timer = 20;
            Unit* unit = server->unit;
            kAlarmLog << "Unit " << static_cast<long>(unit->id) << " acknowledges with ";
            const char* what;
            switch (state->active) {
            case AIRemark::Idle: world->TellParties(server->unit, AckKind::Rest); what = "idleSound = AAn4XyU"; break;
            case AIRemark::FightingIdle: world->TellParties(server->unit, AckKind::FightingRest); what = "idleSound = AAn4XyU"; break;
            case AIRemark::CalmFromSuspicion: what = "back from Suspection"; break;
            case AIRemark::CalmFromAggression: what = "back from Aggression"; break;
            case AIRemark::Suspicion: world->TellParties(server->unit, AckKind::Suspicion); what = "to Suspection"; break;
            case AIRemark::Aggression: world->TellParties(server->unit, AckKind::Aggression); what = "to Aggression"; break;
            case AIRemark::ToAlarm: what = "to Alarm!"; break;
            case AIRemark::Alarm: what = "ALARM ALARM ALARM"; break;
            case AIRemark::Kill: world->TellParties(server->unit, AckKind::Kill); what = "poluchi gadina"; break;
            default: what = "error... :)"; break;
            }
            console::EndLine(kAlarmLog << what);
        }
        state->pending = AIRemark::None;
    }
    AIRemark active = state->active;
    state->level = AIRemark::None;
    if (active == AIRemark::None) return;
    if (--state->timer <= 0) {
        state->active = AIRemark::None;
        return;
    }
    int side = server->unit->side;
    for (auto& node : server->brain.memory.nearby) {
        Unit* member = node;
        if (!world->diplomacy.IsAllied(side, member->side)) continue;
        AIRemark& alarm = member->ServerController()->voice.level;
        if (alarm <= active) alarm = active;
    }
}

void AIAlarm::Step(UnitServer* owner) { StepAlarmOf(this, owner); }

// ---- The state's small members ----------------------------------------------------------

void AIAlarm::Reset() {
    level = AIRemark::None;
    pending = AIRemark::None;
    active = AIRemark::None;
    timer = 0;
}

void AIAlarm::Raise(AIRemark newLevel) { pending = newLevel > pending ? newLevel : pending; }

void AIAlarm::Load(ByteStream* stream) {
    stream->ReadValue(level);
    stream->ReadValue(pending);
    stream->ReadValue(active);
    stream->ReadValue(timer);
}

void AIAlarm::Save(ByteStream* stream) {
    stream->WriteValue(level);
    stream->WriteValue(pending);
    stream->WriteValue(active);
    int steps = timer;   // keeps the original's copy of the last one
    stream->WriteValue(steps);
}
