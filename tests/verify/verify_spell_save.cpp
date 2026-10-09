// Checks for spell_save.cpp (CSpell Load / Save, MapEffect Load / Save). Reached by saving and
// loading a game with spells (the party's spell books and scrolls, cast spells) or map objects with
// active effects (ui_saveload, world_load).
// - Save: the original and ours write into capture streams; bytes and pieces are compared and the
//   original's go on to the real stream.
// - Load: ours reads the real stream through a recorder; the original then reads the recorded
//   bytes into a copy of the object as it was before, and the two objects are compared.
#include "verify.h"

#include "fake_stream.h"
#include "../../src/game/map_object.h"
#include "../../src/game/spell.h"

#include <cstdint>
#include <cstring>
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
    bool Same(const CaptureStream& other) const { return bytes == other.bytes && chunks == other.chunks; }
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

class ReplayStream : public FakeStream {
public:
    ReplayStream(const std::vector<unsigned char>& bytes, int version) : bytes_(bytes) { version_ = version; }
    std::uint32_t Read(void* buffer, std::uint32_t size) override {
        for (std::uint32_t i = 0; i < size; ++i)
            static_cast<unsigned char*>(buffer)[i] = pos_ < bytes_.size() ? bytes_[pos_++] : 0;
        return size;
    }
    void Write(const void*, std::uint32_t) override {}

private:
    const std::vector<unsigned char>& bytes_;
    std::size_t pos_ = 0;
};

template <typename T>
using StreamFn = void(__fastcall*)(T*, void*, CStream*);

// Stolen bytes: 006885A0 PUSH ECX; PUSH ESI; MOV ESI,[ESP+0Ch] (6); 00688550 and 0068B920 PUSH ESI;
// MOV ESI,[ESP+8] (5); 0068B860 PUSH EBX; PUSH ESI; PUSH EDI; MOV EDI,ECX (5).
StreamFn<const MapEffect> s_effectSave;
StreamFn<MapEffect> s_effectLoad;
StreamFn<CSpell> s_spellSave;
StreamFn<CSpell> s_spellLoad;

void __fastcall EffectSaveVerify(const MapEffect* self, void*, CStream* stream) {
    CaptureStream expected, actual;
    s_effectSave(self, nullptr, &expected);
    self->Save(&actual);
    ei::verify::Check("MapEffect::Save", true, expected.Same(actual), "type %u: %u vs %u bytes", self->type,
                      static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()));
    expected.Forward(stream);
}

void __fastcall EffectLoadVerify(MapEffect* self, void*, CStream* stream) {
    MapEffect before;
    std::memcpy(&before, self, sizeof(before));
    RecordStream read(stream);
    self->Load(&read);
    ReplayStream replay(read.bytes, stream->version_);
    s_effectLoad(&before, nullptr, &replay);
    ei::verify::Check("MapEffect::Load", true, std::memcmp(&before, self, sizeof(before)) == 0, "type %u / %u",
                      before.type, self->type);
}

void __fastcall SpellSaveVerify(CSpell* self, void*, CStream* stream) {
    CaptureStream expected, actual;
    s_spellSave(self, nullptr, &expected);
    self->SaveSpell(&actual);
    ei::verify::Check("CSpell::SaveSpell", true, expected.Same(actual), "spell %u: %u vs %u bytes",
                      self->prototypeIndex_, static_cast<unsigned>(expected.bytes.size()),
                      static_cast<unsigned>(actual.bytes.size()));
    expected.Forward(stream);
}

void __fastcall SpellLoadVerify(CSpell* self, void*, CStream* stream) {
    alignas(CSpell) unsigned char before[sizeof(CSpell)];
    std::memcpy(before, self, sizeof(CSpell));
    RecordStream read(stream);
    self->LoadSpell(&read);
    ReplayStream replay(read.bytes, stream->version_);
    s_spellLoad(reinterpret_cast<CSpell*>(before), nullptr, &replay);
    // flags_ takes the stream's address when its read writes nothing; the replay always writes.
    ei::verify::Check("CSpell::LoadSpell", true, std::memcmp(before, self, sizeof(CSpell)) == 0,
                      "spell %u, %u bytes read", self->prototypeIndex_, static_cast<unsigned>(read.bytes.size()));
}

} // namespace

EI_VERIFY(0x006885A0, EffectSaveVerify, 6, s_effectSave);
EI_VERIFY(0x00688550, EffectLoadVerify, 5, s_effectLoad);
EI_VERIFY(0x0068B920, SpellSaveVerify, 5, s_spellSave);
EI_VERIFY(0x0068B860, SpellLoadVerify, 5, s_spellLoad);
