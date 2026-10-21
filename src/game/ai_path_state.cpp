// An AI path's network state: the motion order and the next steps written into a delta field
// (WriteState, 005C7DB0) and read back (ReadState, 005C7FC0). Units' state messages (net_state.cpp),
// the player's path message (0x0E) and the client's prediction (player_client.cpp) use them.
#include "ai_motion.h"

#include "net_delta.h"

#include "ei/hook.h"
#include "game_data.h"

#include <cstdint>

namespace {
// The order angle's range (+-), set by the static initialiser 005C5CB0 (twice 0073FE44).
constexpr float kOrderAngleLimit = /*007C4378*/ 6.2831855f;

constexpr std::uint16_t kMaxStepFlags = 0x3FF;
// A step's coordinate code (two bits each in the header): one less than the previous step's, the
// same, one more, or written out.
enum StepCode : std::uint16_t { kStepMinus = 0, kStepSame = 1, kStepPlus = 2, kStepWritten = 3 };

std::uint16_t StepCodeFor(std::int16_t value, std::int16_t previous) {
    int difference = value - previous;
    if (difference == -1) return kStepMinus;
    if (difference == 0) return kStepSame;
    if (difference == 1) return kStepPlus;
    return kStepWritten;
}

std::int16_t StepFromCode(std::uint16_t code, std::int16_t previous) {
    if (code == kStepMinus) return static_cast<std::int16_t>(previous - 1);
    if (code == kStepSame) return previous;
    return static_cast<std::int16_t>(previous + 1);
}
} // namespace

void AIPath::WriteState(DeltaField* field) {
    MotionOrder* order = OrderOf(this);
    std::uint8_t byte = static_cast<std::uint8_t>(order->kind);
    field->WriteDelta(&byte, 1);
    byte = static_cast<std::uint8_t>(steps.count >> 8);
    field->WriteDelta(&byte, 1);
    byte = static_cast<std::uint8_t>(steps.count);
    field->WriteDelta(&byte, 1);
    field->WritePosition2D(&order->x);
    const float limit = kOrderAngleLimit;
    field->WriteRanged(order->angle, -limit, limit);
    field->WriteDelta(&order->startTime, 4);
    field->WriteDelta(&order->arg5, 4);
    field->WriteDelta(&order->arg6, 4);
    field->WriteDelta(&order->arg7, 4);
    // Each step: a 16-bit header (bits 0-1 x code, 2-3 y code, 4-5 the low bits of unknown_6, 6-15
    // the flags up to 0x3FF), then the coordinates the codes say are written out.
    std::int16_t previousX = 0, previousY = 0;
    for (PathCellList::Node* node = steps.head; node; node = node->next) {
        PathCell& cell = node->data;
        std::uint16_t flags = cell.flags > kMaxStepFlags ? kMaxStepFlags : cell.flags;
        std::uint16_t xCode = StepCodeFor(cell.x, previousX);
        std::uint16_t yCode = StepCodeFor(cell.y, previousY);
        std::uint16_t header = static_cast<std::uint16_t>(flags << 6 | (cell.terrainBits & 3) << 4 | yCode << 2 | xCode);
        previousX = cell.x;
        previousY = cell.y;
        field->WriteBoth(&header, 2);
        if (xCode == kStepWritten) field->WriteBoth(&cell.x, 2);
        if (yCode == kStepWritten) field->WriteBoth(&cell.y, 2);
    }
}
EI_REPLACE_METHOD(0x005C7DB0, AIPath::WriteState);

void AIPath::ReadState(DeltaField* field) {
    MotionOrder* order = OrderOf(this);
    std::uint8_t byte = 0;
    field->ReadDelta(&byte, 1);
    order->kind = byte;
    std::uint8_t high = 0, low = 0;
    field->ReadDelta(&high, 1);
    field->ReadDelta(&low, 1);
    const int count = (high << 8) + low;
    field->ReadPosition2D(&order->x);
    const float limit = kOrderAngleLimit;
    field->ReadRanged(&order->angle, -limit, limit);
    field->ReadDelta(&order->startTime, 4);
    field->ReadDelta(&order->arg5, 4);
    field->ReadDelta(&order->arg6, 4);
    field->ReadDelta(&order->arg7, 4);
    steps.RemoveAll();
    std::int16_t previousX = 0, previousY = 0;
    for (int i = 0; i < count; ++i) {
        PathCell& cell = steps.AddTail(PathCell{})->data;
        std::uint16_t header = 0;
        field->ReadRaw(&header, 2);
        cell.terrainBits = static_cast<std::uint16_t>((header >> 4) & 3);
        cell.flags = static_cast<std::uint16_t>(header >> 6);
        const std::uint16_t xCode = header & 3;
        if (xCode == kStepWritten) field->ReadRaw(&cell.x, 2);
        else cell.x = StepFromCode(xCode, previousX);
        const std::uint16_t yCode = (header >> 2) & 3;
        if (yCode == kStepWritten) field->ReadRaw(&cell.y, 2);
        else cell.y = StepFromCode(yCode, previousY);
        previousX = cell.x;
        previousY = cell.y;
    }
}
EI_REWRITTEN(0x005C7FC0, AIPath::ReadState);
