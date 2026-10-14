// Checks for unit_stats_io.cpp (A-U4).
// - SaveTo (00528800): the original and ours write into capture streams; the writes (sizes and
//   bytes) compared, then ours are passed on to the real stream.
// - LoadFrom (005287A0): ours only (it consumes the stream); pendingExperience is 0 for formats
//   below 0x63 (not read).
// Reached by: saving and loading a game (world_load: the server controllers' statistics,
// combat_save.cpp, and the party records, party_save.cpp).
#include "verify.h"

#include "fake_stream.h"

#include "../../src/game/unit.h"

#include <cstdint>
#include <vector>

namespace {
void* s_unusedLoad;

class CaptureStream : public FakeStream {
public:
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        sizes.push_back(size);
        const auto* bytes = static_cast<const std::uint8_t*>(buffer);
        data.insert(data.end(), bytes, bytes + size);
    }
    std::vector<std::uint32_t> sizes;
    std::vector<std::uint8_t> data;
};
} // namespace

// 00528800: MOV EAX,ECX; PUSH 0x704 (7 bytes).
static void(__fastcall* s_saveTo)(UnitStats*, void*, CStream*);
static void __fastcall SaveToVerify(UnitStats* self, void*, CStream* stream) {
    CaptureStream original, ours;
    s_saveTo(self, nullptr, &original);
    self->SaveTo(&ours);
    bool same = original.sizes == ours.sizes && original.data == ours.data;
    ei::verify::Check("UnitStats::SaveTo", true, same, "%u / %u writes", static_cast<unsigned>(original.sizes.size()),
                      static_cast<unsigned>(ours.sizes.size()));
    self->SaveTo(stream);
}
EI_VERIFY(0x00528800, SaveToVerify, 7, s_saveTo);

static int __fastcall LoadFromVerify(UnitStats* self, void*, CStream* stream) {
    int version = stream->version_;
    int result = self->LoadFrom(stream);
    bool ok = version >= 0x63 || self->pendingExperience == 0.0f;
    ei::verify::Check("UnitStats::LoadFrom", true, ok, "version 0x%X", version);
    return result;
}
EI_VERIFY(0x005287A0, LoadFromVerify, 0, s_unusedLoad);
