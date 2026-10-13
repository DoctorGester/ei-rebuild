// 005AEA80 (thiscall on an AI alarm state {level, pending, active, timer}, RET 4): the step of a
// unit's alarm. A pending level above the current one becomes active for 20 steps: the AI log
// (the ostream at 007C2EB8) gets "Unit <id> acknowledges with <what>" and some levels make the
// unit speak (the world's 0058D340 with a remark code). Then, while a level is active and its
// timer runs, every group member (owner + 0xA78) on a side the owner's side is allied with gets
// its controller's alarm (+0x240 -> +0xB10) raised to the level. See docs/notes/ai.md.
#include "ai_alarm.h"

#include "unit_ai.h"

#include "ei/hook.h"

#include <cstdint>
#include "originals_engine.h"
#include "originals_library.h"

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// The game's ostream (MSVC 6 iostream).
struct AiLogStream {
    AiLogStream* Write(const char* text);   // 006FC31D
    AiLogStream* Write(long value);         // 006FC9C6
    AiLogStream* Put(int c);                // 006FC0B0
    AiLogStream* Apply(void* manipulator);  // 00434FE0
};
AiLogStream* AiLogStream::Write(const char* text) { return static_cast<AiLogStream*>(OstreamPutString(this, nullptr, text)); }
AiLogStream* AiLogStream::Write(long v) { return static_cast<AiLogStream*>(OstreamPutLongOriginal(this, nullptr, v)); }
AiLogStream* AiLogStream::Put(int c) { return static_cast<AiLogStream*>(OstreamPutChar(this, nullptr, static_cast<char>(c))); }
AiLogStream* AiLogStream::Apply(void* manipulator) { return static_cast<AiLogStream*>(OstreamManipulate(this, nullptr, manipulator)); }
AiLogStream* AlarmLog() { return *reinterpret_cast<AiLogStream**>(0x007C2EB8); }
const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// 00455940: the owner's manager context (the world). 0058D340 (RET 8): the object says a remark.
static inline std::uint8_t* ManagerContextAt(void* object) { return static_cast<std::uint8_t*>(ManagerContextOf(object)); }
__declspec(naked) void __fastcall SayRemark(void*, void*, void*, int) EI_ORIGINAL(0x0058D340)

struct GroupNode {
    GroupNode* next;
    GroupNode* prev;
    std::uint8_t* member;
};

} // namespace

void __fastcall StepAlarm(int* stateIn, void*, std::uint8_t* owner) {
    auto* state = reinterpret_cast<AIAlarm*>(stateIn);
    std::uint8_t* world = ManagerContextAt(owner);
    if (state->pending != 0) {
        if (state->pending > state->level) {
            state->active = state->pending;
            state->timer = 20;
            std::uint8_t* unit = At<std::uint8_t*>(owner, 0x1C);
            AlarmLog()->Write(Text(0x0078FDBC))->Write(static_cast<long>(At<int>(unit, 0xC)))->Write(Text(0x007910E0));
            std::uintptr_t what;
            switch (state->active) {
            case 1: SayRemark(world, nullptr, At<void*>(owner, 0x1C), 0x31); what = 0x007910CC; break;
            case 2: SayRemark(world, nullptr, At<void*>(owner, 0x1C), 0x32); what = 0x007910CC; break;
            case 3: what = 0x007910B4; break;   // "back from Suspection"
            case 4: what = 0x0079109C; break;   // "back from Aggression"
            case 5: SayRemark(world, nullptr, At<void*>(owner, 0x1C), 0x2F); what = 0x0079108C; break;
            case 6: SayRemark(world, nullptr, At<void*>(owner, 0x1C), 0x2E); what = 0x0079107C; break;
            case 7: what = 0x00791070; break;   // "to Alarm!"
            case 8: what = 0x0079105C; break;   // "ALARM ALARM ALARM"
            case 9: SayRemark(world, nullptr, At<void*>(owner, 0x1C), 0x30); what = 0x0079104C; break;
            default: what = 0x00791040; break;  // "error... :)"
            }
            AlarmLog()->Write(Text(what))->Put(10)->Apply(reinterpret_cast<void*>(0x00435000));
        }
        state->pending = 0;
    }
    int active = state->active;
    state->level = 0;
    if (!active) return;
    if (--state->timer <= 0) {
        state->active = 0;
        return;
    }
    int side = At<int>(At<std::uint8_t*>(owner, 0x1C), 0x250);
    std::uint32_t allies = At<std::uint32_t>(world, 0x12C64 + side * 4);
    for (GroupNode* node = At<GroupNode*>(owner, 0xA78); node; node = node->next) {
        std::uint8_t* member = node->member;
        if (!(allies & (1u << (At<int>(member, 0x250) & 31)))) continue;
        int& alarm = At<int>(At<std::uint8_t*>(member, 0x240), 0xB10);
        if (alarm <= active) alarm = active;
    }
}

EI_REPLACE(0x005AEA80, StepAlarm);

// ---- The state's small members (A-W14) ----------------------------------------------------------

void AIAlarm::Reset() {
    level = 0;
    pending = 0;
    active = 0;
    timer = 0;
}
EI_REPLACE_METHOD(0x005AEA40, AIAlarm::Reset);

AIAlarm* AIAlarm::Construct() {
    Reset();
    return this;
}
EI_REPLACE_METHOD(0x005AEA50, AIAlarm::Construct);

void AIAlarm::Raise(int newLevel) { pending = newLevel > pending ? newLevel : pending; }
EI_REPLACE_METHOD(0x005AEA60, AIAlarm::Raise);

void AIAlarm::Load(AIStream* stream) {
    stream->Read(&level, 4);
    stream->Read(&pending, 4);
    stream->Read(&active, 4);
    stream->Read(&timer, 4);
}
EI_REPLACE_METHOD(0x005AEC40, AIAlarm::Load);

void AIAlarm::Save(AIStream* stream) {
    stream->Write(&level, 4);
    stream->Write(&pending, 4);
    stream->Write(&active, 4);
    int steps = timer;   // the original writes a copy of the last one
    stream->Write(&steps, 4);
}
EI_REPLACE_METHOD(0x005AEC80, AIAlarm::Save);
