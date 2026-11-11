// EffectShowVisibility: a unit's sight shown in the fog of war (clairvoyance), and FogGrid
// (effects.h): the squares a seer sees, rows of bytes (Clear, Read, Write). See world_effect.h
// and docs/notes/effects.md ("Missiles and visibility"), docs/notes/world.md.
#include "game/effects/world_effect.h"

#include "game/ai/ai_map.h"
#include "game/ai/unit_ai.h"
#include "engine/core/core_streams.h"
#include "game/effects/effects.h"
#include "game/objects/map_object.h"
#include "game/units/unit.h"
#include "game/world/world.h"

#include "lib/crt.h"

#include <cstring>

namespace {

constexpr int kNoOrigin = 0x7FFFFFFF;

} // namespace

// --- EffectShowVisibility ---

EffectShowVisibility::EffectShowVisibility() {
    grid.cells.Resize(1, 1);
}

// The destructor body resets twice (inline, then the virtual); the grid frees itself.
EffectShowVisibility::~EffectShowVisibility() {
    Reset();
    Reset();
}

// The base is not reset: only the grid (taken out of the fog first), the units and the fog.
void EffectShowVisibility::Reset() {
    grid.Clear();
    if (fog != nullptr) fog->Reveal(&grid);
    observerId = -1;
    seerId = -1;
    fog.Reset();
}

void EffectShowVisibility::SetUnits(int newObserverId, int newSeerId) {
    observerId = newObserverId;
    seerId = newSeerId;
}

void EffectShowVisibility::Start(int update) {
    if (update == 0) {
        fog.Assign(VisFogManagerAdd(g_clientWorld.arrayManager.get()));
    }
    if (fog != nullptr) fog->Reveal(&grid);
}

int EffectShowVisibility::Unpack(int update, DeltaReader& field) {
    MemoryStream stream;
    if (field.ReadStream(&stream) == 0) return 0;
    grid.Read(&stream);
    Start(update);
    return 1;
}

void EffectShowVisibility::Pack(DeltaWriter& field) {
    MemoryStream stream;
    grid.Write(&stream);
    field.WriteStreamBoth(&stream);
}

bool EffectShowVisibility::RunTick() {
    if (!OwningWorld()->IsServer()) return true;
    Unit* seer = OwningWorld()->FindUnit(seerId);
    Unit* observer = OwningWorld()->FindUnit(observerId);
    if (seer == nullptr || observer == nullptr) return false;
    localPosition = seer->localPosition;
    // The squares `seer` sees, with `observer`'s skills.
    MarkSightCells(seer, observer, &grid);
    MarkChanged();
    return true;
}

void EffectShowVisibility::Load(ByteStream* s) {
    s->ReadValue(observerId);
    s->ReadValue(seerId);
}

void EffectShowVisibility::Save(ByteStream* s) {
    int copy = observerId;
    s->WriteValue(copy);
    copy = seerId;
    s->WriteValue(copy);
}

// --- FogGrid ---

void FogGrid::Clear() {
    x0 = kNoOrigin;
    y0 = kNoOrigin;
    cells.Resize(1, 1);
    cells.data.assign(cells.data.size(), 0);
}

void FogGrid::Read(MemoryStream* stream) {
    stream->ReadValue(x0);
    stream->ReadValue(y0);
    // Like the original, the locals hold the stream and the grid pointers when a read fails.
    int newWidth = 0;
    int newHeight = 0;
    stream->ReadValue(newWidth);
    stream->ReadValue(newHeight);
    cells.Resize(newWidth, newHeight);
    stream->Read(cells.data.data(), static_cast<std::uint32_t>(newHeight * newWidth));
}

void FogGrid::Write(MemoryStream* stream) {
    int value = x0;
    stream->WriteValue(value);
    value = y0;
    stream->WriteValue(value);
    const int w = cells.width;
    const int h = cells.height;
    value = w;
    stream->WriteValue(value);
    value = h;
    stream->WriteValue(value);
    stream->Write(cells.data.data(), static_cast<std::uint32_t>(h * w));
}
