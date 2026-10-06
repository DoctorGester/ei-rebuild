// Checks for map_object_save.cpp (A-U2).
// - SaveMapObject (0050F900) / SaveServer (00515AC0): the original and ours write into capture
//   streams; bytes and write sizes compared, the original's writes go on to the real stream.
// - LoadMapObject (0050F7A0) / LoadServer (005159D0): ours only, reading through a recorder; round
//   trip: the original Save writes the object into a capture stream, which must equal the bytes
//   read (map objects: from version 0x4F, where the base scale is saved; the server companion:
//   when its string array was empty before, as Load appends to it).
// Reached by: saving and loading a game (world_load: every map object and unit, CUnit's
// SaveUnit / LoadUnit and the traps call the map object part).
#include "verify.h"
#include "unit_capture_stream.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/map_object.h"
#include "../../src/game/unit.h"

#include <cstdint>
#include <vector>

namespace {
using unitverify::CaptureStream;

void* s_unusedLoad;
void* s_unusedLoadServer;

// Forwards every call to the real stream and records the bytes read.
class ReadRecorder : public UnitStream {
public:
    explicit ReadRecorder(UnitStream* real) : real_(real) {
        unknown_04 = real->unknown_04;
        version = real->version;
    }
    void Slot0() override { real_->Slot0(); }
    int Read(void* buffer, unsigned size) override {
        int result = real_->Read(buffer, size);
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        return result;
    }
    void Write(const void* buffer, unsigned size) override { real_->Write(buffer, size); }
    std::vector<unsigned char> bytes;

private:
    UnitStream* real_;
};

CStream* AsStream(UnitStream* stream) { return reinterpret_cast<CStream*>(stream); }
} // namespace

// 0050F900: PUSH EBX; MOV EBX,ECX; PUSH EBP; PUSH ESI (5 bytes).
static void(__fastcall* s_saveMapObject)(CMapObject*, void*, UnitStream*);
static void __fastcall SaveMapObjectVerify(CMapObject* self, void*, UnitStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_saveMapObject(self, nullptr, &expected);
    self->SaveMapObject(AsStream(&actual));
    ei::verify::Check("CMapObject::SaveMapObject", true, expected.Same(actual), "object %d: %u vs %u bytes, first difference at %d",
                      self->id_, static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()),
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x0050F900, SaveMapObjectVerify, 5, s_saveMapObject);

// 00515AC0: PUSH EBX; PUSH EBP; MOV EBX,ECX; PUSH ESI (5 bytes).
static void(__fastcall* s_saveServer)(CMapObjectServer*, void*, UnitStream*);
static void __fastcall SaveServerVerify(CMapObjectServer* self, void*, UnitStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_saveServer(self, nullptr, &expected);
    self->SaveServer(AsStream(&actual));
    ei::verify::Check("CMapObjectServer::SaveServer", true, expected.Same(actual), "%u vs %u bytes, first difference at %d",
                      static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()),
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x00515AC0, SaveServerVerify, 5, s_saveServer);

static void __fastcall LoadMapObjectVerify(CMapObject* self, void*, UnitStream* stream) {
    bool appendingCompanion = false;
    if (self->companion && self->companion->GetClassId() == 0x39)
        appendingCompanion = static_cast<CMapObjectServer*>(self->companion)->stringCount != 0;
    ReadRecorder record(stream);
    self->LoadMapObject(AsStream(&record));
    bool ok = true;
    if (s_saveMapObject && stream->version > 0x4E && !appendingCompanion) {
        CaptureStream saved(stream);
        s_saveMapObject(self, nullptr, &saved);
        ok = saved.bytes == record.bytes;
    }
    ei::verify::Check("CMapObject::LoadMapObject", true, ok, "object %d version %d: %u bytes read", self->id_,
                      stream->version, static_cast<unsigned>(record.bytes.size()));
}
EI_VERIFY(0x0050F7A0, LoadMapObjectVerify, 0, s_unusedLoad);

static void __fastcall LoadServerVerify(CMapObjectServer* self, void*, UnitStream* stream) {
    bool appending = self->stringCount != 0;
    ReadRecorder record(stream);
    self->LoadServer(AsStream(&record));
    bool ok = true;
    if (s_saveServer && !appending) {
        CaptureStream saved(stream);
        s_saveServer(self, nullptr, &saved);
        ok = saved.bytes == record.bytes;
    }
    ei::verify::Check("CMapObjectServer::LoadServer", true, ok, "%u bytes read, %d strings",
                      static_cast<unsigned>(record.bytes.size()), self->stringCount);
}
EI_VERIFY(0x005159D0, LoadServerVerify, 0, s_unusedLoadServer);
