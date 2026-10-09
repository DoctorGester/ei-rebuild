// Checks for spell_effect_save.cpp (CSpellEffect Load / Save). Reached by saving or loading a game
// while a cast spell is under way (a missile in flight, a wall or cloud lasting); no scenario
// saves then yet.
// - Save: the original and ours write into capture streams; bytes and pieces are compared and the
//   original's go on to the real stream.
// - Load counts references and stamps the AI map, so only ours runs, through a stream that records
//   what it reads; the original Save of the loaded effect must write exactly those bytes (an id
//   whose object is not found comes back as -1).
#include "verify.h"

#include "fake_stream.h"
#include "../../src/game/spell.h"

#include <cstdint>
#include <vector>

namespace {

class CaptureStream : public FakeStream {
public:
    std::uint32_t Read(void*, std::uint32_t) override { return 0; }
    void Write(const void* buffer, std::uint32_t size) override {
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        chunks.push_back(size);
    }
    void Forward(CStream* stream) const {
        std::size_t pos = 0;
        for (std::uint32_t size : chunks) {
            stream->Write(bytes.data() + pos, size);
            pos += size;
        }
    }
    std::vector<unsigned char> bytes;
    std::vector<std::uint32_t> chunks;
};

class RecordStream : public FakeStream {
public:
    explicit RecordStream(CStream* real) : real_(real) { version_ = real->version_; }
    std::uint32_t Read(void* buffer, std::uint32_t size) override {
        std::uint32_t result = real_->Read(buffer, size);
        auto* p = static_cast<const unsigned char*>(buffer);
        bytes.insert(bytes.end(), p, p + size);
        return result;
    }
    void Write(const void*, std::uint32_t) override {}
    std::vector<unsigned char> bytes;

private:
    CStream* real_;
};

// 0067E960: PUSH EBX; PUSH ESI; MOV ESI,[ESP+0Ch] (6).
void(__fastcall* s_save)(CSpellEffect*, void*, CStream*);
void* s_unused;

void __fastcall SaveVerify(CSpellEffect* self, void*, CStream* stream) {
    CaptureStream expected, actual;
    s_save(self, nullptr, &expected);
    self->Save(&actual);
    ei::verify::Check("CSpellEffect::Save", true, expected.bytes == actual.bytes && expected.chunks == actual.chunks,
                      "spell %X state %d: %u vs %u bytes", self->spellId >> 16, self->state,
                      static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()));
    expected.Forward(stream);
}

void __fastcall LoadVerify(CSpellEffect* self, void*, CStream* stream) {
    RecordStream read(stream);
    self->Load(&read);
    CaptureStream written;
    s_save(self, nullptr, &written);
    ei::verify::Check("CSpellEffect::Load", true, read.bytes == written.bytes,
                      "spell %X state %d: %u bytes read, %u written back", self->spellId >> 16, self->state,
                      static_cast<unsigned>(read.bytes.size()), static_cast<unsigned>(written.bytes.size()));
}

} // namespace

EI_VERIFY(0x0067E960, SaveVerify, 6, s_save);
EI_VERIFY(0x0067E7D0, LoadVerify, 0, s_unused);
