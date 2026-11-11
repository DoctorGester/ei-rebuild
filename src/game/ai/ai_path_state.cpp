// An AI path's network state: the motion order and the next steps written into a delta field (WriteState) and
// read back (ReadState). Units' state messages (net_state.cpp), the player's path message (0x0E) and the
// client's prediction (player_client.cpp) use them. The path in the save stream is below.

#include "game/ai/ai_map.h"
#include "game/ai/ai_motion.h"
#include "engine/core/core_streams.h"
#include "game/net/net_delta.h"

#include <cstdint>

namespace {
// The order angle's range (+-), set by a static initialiser (twice the cell scale constant).
constexpr float kOrderAngleLimit = /*007C4378*/ 6.2831855f;

constexpr std::uint16_t kMaxStepFlags = 0x3FF;
// A step's coordinate code (two bits each in the header): one less than the previous step's, the
// same, one more, or written out.
enum class StepCode : std::uint16_t { Minus = 0, Same = 1, Plus = 2, Written = 3 };

StepCode StepCodeFor(std::int16_t value, std::int16_t previous) {
    int difference = value - previous;
    if (difference == -1) return StepCode::Minus;
    if (difference == 0) return StepCode::Same;
    if (difference == 1) return StepCode::Plus;
    return StepCode::Written;
}

std::int16_t StepFromCode(StepCode code, std::int16_t previous) {
    if (code == StepCode::Minus) return static_cast<std::int16_t>(previous - 1);
    if (code == StepCode::Same) return previous;
    return static_cast<std::int16_t>(previous + 1);
}
} // namespace

void PathOrder::WriteState(DeltaWriter& field) {
    MotionOrder* order = OrderOf(this);
    std::uint8_t byte = static_cast<std::uint8_t>(order->kind);
    field.WriteDelta(&byte, 1);
    byte = static_cast<std::uint8_t>(std::ssize(steps) >> 8);
    field.WriteDelta(&byte, 1);
    byte = static_cast<std::uint8_t>(std::ssize(steps));
    field.WriteDelta(&byte, 1);
    field.WritePosition2D(order->position);
    const float limit = kOrderAngleLimit;
    field.WriteRanged(order->angle, -limit, limit);
    field.WriteDelta(&order->startTime, 4);
    field.WriteDelta(&order->arg5, 4);
    field.WriteDelta(&order->arg6, 4);
    field.WriteDelta(&order->arg7, 4);
    // Each step: a 16-bit header (bits 0-1 x code, 2-3 y code, 4-5 the low bits of terrainBits, 6-15
    // the flags up to 0x3FF), then the coordinates the codes say are written out.
    std::int16_t previousX = 0, previousY = 0;
    for (auto& node : steps) {
        PathCell& cell = node;
        std::uint16_t flags = cell.flags > kMaxStepFlags ? kMaxStepFlags : cell.flags;
        const StepCode xCode = StepCodeFor(cell.x, previousX);
        const StepCode yCode = StepCodeFor(cell.y, previousY);
        std::uint16_t header = static_cast<std::uint16_t>(flags << 6 | (cell.terrainBits & 3) << 4 |
                                                          static_cast<unsigned>(yCode) << 2 | static_cast<unsigned>(xCode));
        previousX = cell.x;
        previousY = cell.y;
        field.WriteBoth(&header, 2);
        if (xCode == StepCode::Written) field.WriteBoth(&cell.x, 2);
        if (yCode == StepCode::Written) field.WriteBoth(&cell.y, 2);
    }
}

void PathOrder::ReadState(DeltaReader& field) {
    MotionOrder* order = OrderOf(this);
    std::uint8_t byte = 0;
    field.ReadDelta(&byte, 1);
    order->kind = byte;
    std::uint8_t high = 0, low = 0;
    field.ReadDelta(&high, 1);
    field.ReadDelta(&low, 1);
    const int count = (high << 8) + low;
    order->position = field.ReadPosition2D();
    const float limit = kOrderAngleLimit;
    order->angle = field.ReadRanged(-limit, limit);
    field.ReadDelta(&order->startTime, 4);
    field.ReadDelta(&order->arg5, 4);
    field.ReadDelta(&order->arg6, 4);
    field.ReadDelta(&order->arg7, 4);
    steps.clear();
    std::int16_t previousX = 0, previousY = 0;
    for (int i = 0; i < count; ++i) {
        PathCell& cell = steps.emplace_back();
        std::uint16_t header = 0;
        field.ReadRaw(&header, 2);
        cell.terrainBits = static_cast<std::uint16_t>((header >> 4) & 3);
        cell.flags = static_cast<std::uint16_t>(header >> 6);
        const auto xCode = static_cast<StepCode>(header & 3);
        if (xCode == StepCode::Written) field.ReadRaw(&cell.x, 2);
        else cell.x = StepFromCode(xCode, previousX);
        const auto yCode = static_cast<StepCode>((header >> 2) & 3);
        if (yCode == StepCode::Written) field.ReadRaw(&cell.y, 2);
        else cell.y = StepFromCode(yCode, previousY);
        previousX = cell.x;
        previousY = cell.y;
    }
}

// --- The path in the save stream ---------------------------------------------------------------

// A unit's path in the save stream: load and save through the stream's read and write virtuals (buffer,
// size). Layout: the cell count and each remaining cell (8 bytes), the order, the target (8 bytes), the goal
// (12), the four flag bytes one by one, the excluded regions' count and each {x, y} as two dwords, and the
// smoothing value. Loading appends the cells to the list (created as {0, 0}, then read over). See ai_map.h
// (AIPath).
void AIPath::LoadPath(ByteStream* stream) {
    for (int count = stream->ReadCount(); count > 0; --count) {
        // The cell's second dword is uninitialised in the original; it is read over below.
        PathCell cell{0, 0, 0, 0};
        cells.push_back(cell);
    }
    for (PathCell& cell : cells) stream->ReadValue(cell);
    LoadOrder(stream);
    stream->ReadValue(target);
    stream->ReadValue(goal);
    stream->ReadValue(reached);
    stream->ReadValue(failed);
    stream->ReadValue(mayExcludeRegions);
    stream->ReadValue(firstSearch);
    std::vector<Point>* excluded = &excludedBlocks;
    excluded->resize(stream->ReadCount());
    for (int i = 0; i < std::ssize(*excluded); ++i) {
        stream->ReadValue((*excluded)[i].x);
        stream->ReadValue((*excluded)[i].y);
    }
    stream->ReadValue(smoothing);
}

void AIPath::SavePath(ByteStream* stream) {
    stream->WriteCount(cells.size());
    for (const PathCell& cell : cells) stream->WriteValue(cell);
    SaveOrder(stream);
    stream->WriteValue(target);
    stream->WriteValue(goal);
    stream->WriteValue(reached);
    stream->WriteValue(failed);
    stream->WriteValue(mayExcludeRegions);
    stream->WriteValue(firstSearch);
    const std::vector<Point>* excluded = &excludedBlocks;
    stream->WriteCount(excluded->size());
    for (int i = 0; i < std::ssize(*excluded); ++i) {
        int x = (*excluded)[i].x;
        stream->WriteValue(x);
        int y = (*excluded)[i].y;
        stream->WriteValue(y);
    }
    int smoothingCopy = smoothing;
    stream->WriteValue(smoothingCopy);
}

// ---- The order ----------------------------------------------------------

void PathOrder::LoadOrder(ByteStream* stream) {
    MotionOrder* order = OrderOf(this);
    stream->ReadValue(order->kind);
    stream->ReadValue(order->position);
    stream->ReadValue(order->angle);
    stream->ReadValue(order->startTime);
    stream->Read(&order->arg5, 0xC);
    // The new step is {0, 0} and an uninitialised dword; every step is read over below.
    for (int count = stream->ReadCount(); count > 0; --count) steps.push_back(PathCell{0, 0, 0, 0});
    for (PathCell& step : steps) stream->ReadValue(step);
}

void PathOrder::SaveOrder(ByteStream* stream) {
    MotionOrder* order = OrderOf(this);
    stream->WriteValue(order->kind);
    stream->WriteValue(order->position);
    stream->WriteValue(order->angle);
    stream->WriteValue(order->startTime);
    stream->Write(&order->arg5, 0xC);
    stream->WriteCount(steps.size());
    for (const PathCell& step : steps) stream->WriteValue(step);
}
