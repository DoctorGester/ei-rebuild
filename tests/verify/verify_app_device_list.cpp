// Checks for app_device_list.cpp: ours only. A copied or assigned record equals its source
// (texts and plain fields); the archived list keeps its size when saving.
#include "verify.h"

#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>

namespace app {
struct DeviceRecord;
DeviceRecord* __fastcall RecordCopyConstruct(DeviceRecord* self, void*, const DeviceRecord* from);
DeviceRecord* __fastcall RecordAssign(DeviceRecord* self, void*, const DeviceRecord* from);
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

app::DeviceRecord* __fastcall CopyVerify(app::DeviceRecord* self, void*, const app::DeviceRecord* from) {
    app::RecordCopyConstruct(self, nullptr, from);
    ei::verify::Check("DeviceRecord copy", true, SameRecord(self, from), "");
    return self;
}
app::DeviceRecord* __fastcall AssignVerify(app::DeviceRecord* self, void*, const app::DeviceRecord* from) {
    app::RecordAssign(self, nullptr, from);
    ei::verify::Check("DeviceRecord assign", true, SameRecord(self, from), "");
    return self;
}
} // namespace

EI_VERIFY(0x00465A70, CopyVerify, 0, s_unused);
EI_VERIFY(0x00465BC0, AssignVerify, 0, s_unused);
