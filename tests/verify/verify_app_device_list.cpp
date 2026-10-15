// Checks for app_device_list.cpp: ours only. A copied or assigned record equals its source
// (texts and plain fields); the archived list keeps its size when saving. The records' copy
// (00465B20): every copied record equals its source as it was before (the copy may overlap, as in
// erase); a start-up check runs the original and ours on twin destinations and compares them.
#include "verify.h"

#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

struct LeverRecord;   // lever.h
namespace app {
LeverRecord* __fastcall RecordCopyConstruct(LeverRecord* self, void*, const LeverRecord* from);
LeverRecord* __fastcall RecordAssign(LeverRecord* self, void*, const LeverRecord* from);
LeverRecord* __fastcall RecordsCopyForward(LeverRecord* first, LeverRecord* last, LeverRecord* result, const void*, int);
} // namespace app

namespace {
void* s_unused;

bool SameRecord(const void* a, const void* b) {
    auto* x = static_cast<const std::uint8_t*>(a);
    auto* y = static_cast<const std::uint8_t*>(b);
    for (std::size_t o : {0x00u, 0x20u, 0x24u, 0x28u})
        if (std::strcmp(*reinterpret_cast<const char* const*>(x + o), *reinterpret_cast<const char* const*>(y + o)) != 0) return false;
    return std::memcmp(x + 4, y + 4, 0x1C) == 0;
}

LeverRecord* __fastcall CopyVerify(LeverRecord* self, void*, const LeverRecord* from) {
    app::RecordCopyConstruct(self, nullptr, from);
    ei::verify::Check("LeverRecord copy", true, SameRecord(self, from), "");
    return self;
}
LeverRecord* __fastcall AssignVerify(LeverRecord* self, void*, const LeverRecord* from) {
    app::RecordAssign(self, nullptr, from);
    ei::verify::Check("LeverRecord assign", true, SameRecord(self, from), "");
    return self;
}
} // namespace

EI_VERIFY(0x00465A70, CopyVerify, 0, s_unused);
EI_VERIFY(0x00465BC0, AssignVerify, 0, s_unused);

namespace {
// A record as plain values: its four texts and its plain bytes.
struct Snapshot {
    std::string texts[4];
    std::uint8_t plain[0x1C];
};
Snapshot Take(const void* record) {
    auto* x = static_cast<const std::uint8_t*>(record);
    Snapshot s;
    const std::size_t offsets[] = {0x00, 0x20, 0x24, 0x28};
    for (int i = 0; i < 4; ++i) s.texts[i] = *reinterpret_cast<const char* const*>(x + offsets[i]);
    std::memcpy(s.plain, x + 4, sizeof(s.plain));
    return s;
}
bool Matches(const void* record, const Snapshot& s) {
    Snapshot now = Take(record);
    for (int i = 0; i < 4; ++i)
        if (now.texts[i] != s.texts[i]) return false;
    return std::memcmp(now.plain, s.plain, sizeof(s.plain)) == 0;
}

constexpr std::size_t kRecord = 0x2C;

// 00465B20: PUSH EDI; MOV EDI,ECX; SUB EDX,EDI (5).
LeverRecord*(__fastcall* s_copy)(LeverRecord*, LeverRecord*, LeverRecord*, const void*, int);
LeverRecord* __fastcall RecordsCopyVerify(LeverRecord* first, LeverRecord* last, LeverRecord* result, const void* a,
                                          int b) {
    const auto* f = reinterpret_cast<const std::uint8_t*>(first);
    const std::size_t n = last > first ? (reinterpret_cast<const std::uint8_t*>(last) - f) / kRecord : 0;
    std::vector<Snapshot> before;
    for (std::size_t i = 0; i < n; ++i) before.push_back(Take(f + i * kRecord));
    LeverRecord* end = app::RecordsCopyForward(first, last, result, a, b);
    bool ok = reinterpret_cast<std::uint8_t*>(end) == reinterpret_cast<std::uint8_t*>(result) + n * kRecord;
    for (std::size_t i = 0; i < n; ++i)
        if (!Matches(reinterpret_cast<std::uint8_t*>(result) + i * kRecord, before[i])) ok = false;
    ei::verify::Check("LeverRecord copy range", true, ok, "%d records", static_cast<int>(n));
    return end;
}

// Three source records, two twin destinations of three empty records; the original on one, ours
// on the other: the same end and the same records.
void RecordsCopyStartupCheck() {
    struct Raw {
        mfc::CString name;
        float plain[7];
        mfc::CString text20, sound, text28;
    };
    static_assert(sizeof(Raw) == kRecord);
    Raw source[3], theirs[3], ours[3];
    const char* names[] = {"lever", "gate", ""};
    for (int i = 0; i < 3; ++i) {
        source[i].name = names[i];
        for (int k = 0; k < 7; ++k) source[i].plain[k] = static_cast<float>(i * 10 + k);
        source[i].text20 = "a";
        source[i].sound = names[2 - i];
        source[i].text28 = "zz";
    }
    for (int i = 0; i < 3; ++i) {
        std::memset(theirs[i].plain, 0xCD, sizeof(theirs[i].plain));
        std::memset(ours[i].plain, 0xCD, sizeof(ours[i].plain));
    }
    auto* first = reinterpret_cast<LeverRecord*>(source);
    auto* last = reinterpret_cast<LeverRecord*>(source + 3);
    LeverRecord* theirEnd = s_copy(first, last, reinterpret_cast<LeverRecord*>(theirs), nullptr, 0);
    LeverRecord* ourEnd = app::RecordsCopyForward(first, last, reinterpret_cast<LeverRecord*>(ours), nullptr, 0);
    bool ok = theirEnd == reinterpret_cast<LeverRecord*>(theirs + 3) && ourEnd == reinterpret_cast<LeverRecord*>(ours + 3);
    for (int i = 0; i < 3; ++i)
        if (!SameRecord(&theirs[i], &ours[i])) ok = false;
    ei::verify::Check("LeverRecord copy range (startup)", true, ok, "");
}
} // namespace

EI_VERIFY(0x00465B20, RecordsCopyVerify, 5, s_copy);
EI_STARTUP_CHECK(RecordsCopyStartupCheck);
