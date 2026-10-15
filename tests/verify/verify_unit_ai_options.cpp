// Checks for the attack option list (src/game/unit_ai_options.cpp).
#include "verify.h"
#include "fake_stream.h"

#include "../../src/game/unit_ai.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {
void* s_unused;

// A deep copy of an options array (the original frees and reallocates the copy's storage).
AIAttackOptions CopyOptions(const AIAttackOptions& from) {
    AIAttackOptions copy = from;
    if (from.data) {
        copy.data = static_cast<AIAttackOption*>(gamecrt::operator_new(from.maxSize * sizeof(AIAttackOption)));
        std::memcpy(copy.data, from.data, from.size * sizeof(AIAttackOption));
    }
    return copy;
}
bool SameOptions(const AIAttackOptions& a, const AIAttackOptions& b) {
    return a.size == b.size && (a.size == 0 || std::memcmp(a.data, b.data, a.size * sizeof(AIAttackOption)) == 0);
}

using BuildFn = void(__fastcall*)(AIAttention*, void*, CUnit*);
BuildFn s_originalBuild;
} // namespace

// 005CEED0: the original on a copy of the attention (its arrays copied), ours on the real one;
// both lists and the heal weight compared.
static void __fastcall BuildVerify(AIAttention* self, void*, CUnit* unit) {
    AIAttention scratch;
    std::memcpy(&scratch, self, sizeof(scratch));
    scratch.weapons = CopyOptions(self->weapons);
    scratch.spells = CopyOptions(self->spells);
    s_originalBuild(&scratch, nullptr, unit);
    self->Build(unit);
    bool ok = SameOptions(scratch.weapons, self->weapons) && SameOptions(scratch.spells, self->spells) &&
              std::memcmp(&scratch.healWeight, &self->healWeight, 4) == 0;
    ei::verify::Check("AIAttention::Build", true, ok, "%d / %d options (original %d / %d)", self->weapons.size,
                      self->spells.size, scratch.weapons.size, scratch.spells.size);
    if (scratch.weapons.data) gamecrt::operator_delete(scratch.weapons.data);
    if (scratch.spells.data) gamecrt::operator_delete(scratch.spells.data);
}
EI_VERIFY(0x005CEED0, BuildVerify, 6, s_originalBuild);

namespace {
using InitFn = void(__fastcall*)(AIAttention*, void*, AIAttackOption*, int, int, float, int, float, int);
InitFn s_originalInit;
}

// 005CEA30: the original into a copy of the option, ours into the real one; compared.
static void __fastcall InitOptionVerify(AIAttention* self, void*, AIAttackOption* option, int kind, int part, float range,
                                        int maxCount, float radius, int effect) {
    AIAttackOption copy;
    std::memcpy(&copy, option, sizeof(copy));
    s_originalInit(self, nullptr, &copy, kind, part, range, maxCount, radius, effect);
    self->InitOption(option, kind, part, range, maxCount, radius, effect);
    ei::verify::Check("AIAttention::InitOption", true, std::memcmp(&copy, option, sizeof(copy)) == 0, "kind %d part %d",
                      kind, part);
}
EI_VERIFY(0x005CEA30, InitOptionVerify, 8, s_originalInit);

namespace {
bool LastIs(const AIAttackOptions& options, int before, const std::uint16_t* result, int kind, int part, int maxCount,
            int effect) {
    if (options.size != before + 1) return false;
    const AIAttackOption& last = options.data[options.size - 1];
    return result == last.damage && last.kind == kind && last.part == part && last.maxCount == maxCount &&
           last.effect == effect;
}
} // namespace

// 005CEA60 / 005CEBD0: grow the array, so ours only: one more option, holding the arguments.
static std::uint16_t* __fastcall AddWeaponOptionVerify(AIAttention* self, void*, int kind, int part, float range,
                                                       int maxCount, float radius, int effect) {
    int before = self->weapons.size;
    std::uint16_t* result = self->AddWeaponOption(kind, part, range, maxCount, radius, effect);
    ei::verify::Check("AIAttention::AddWeaponOption", true,
                      LastIs(self->weapons, before, result, kind, part, maxCount, effect), "kind %d part %d", kind, part);
    return result;
}
EI_VERIFY(0x005CEA60, AddWeaponOptionVerify, 0, s_unused);

static std::uint16_t* __fastcall AddSpellOptionVerify(AIAttention* self, void*, int kind, int part, float range,
                                                      int maxCount, float radius, int effect) {
    int before = self->spells.size;
    std::uint16_t* result = self->AddSpellOption(kind, part, range, maxCount, radius, effect);
    ei::verify::Check("AIAttention::AddSpellOption", true,
                      LastIs(self->spells, before, result, kind, part, maxCount, effect), "kind %d part %d", kind, part);
    return result;
}
EI_VERIFY(0x005CEBD0, AddSpellOptionVerify, 0, s_unused);

namespace {
using KindFn = int(__fastcall*)(AIAttention*, void*, const CSpell*);
KindFn s_originalKind;
using WeightFn = double(__fastcall*)(const AIAttackOption*);
WeightFn s_originalWeight;
} // namespace

// 005CEDC0: pure.
static int __fastcall SpellKindVerify(AIAttention* self, void*, const CSpell* spell) {
    int expected = s_originalKind(self, nullptr, spell);
    int actual = self->SpellKind(spell);
    ei::verify::Check("AIAttention::SpellKind", expected, actual, "%p", static_cast<const void*>(spell));
    return actual;
}
EI_VERIFY(0x005CEDC0, SpellKindVerify, 6, s_originalKind);

// 005CE9B0: pure.
static double __fastcall WeightVerify(const AIAttackOption* option) {
    double expected = s_originalWeight(option);
    double actual = option->Weight();
    ei::verify::CheckNear("AIAttackOption::Weight", expected, actual, "kind %d", option->kind);
    return actual;
}
EI_VERIFY(0x005CE9B0, WeightVerify, 6, s_originalWeight);

// 005D9250: reallocates, so ours only: the new size, the storage large enough.
static void __fastcall SetSizeVerify(AIAttackOptions* self, void*, int size, int growBy) {
    self->SetSize(size, growBy);
    bool ok = self->size == size && self->maxSize >= size && (size == 0) == (self->data == nullptr);
    ei::verify::Check("AIAttackOptions::SetSize", true, ok, "%d", size);
}
EI_VERIFY(0x005D9250, SetSizeVerify, 0, s_unused);

namespace {
using StreamFn = void(__fastcall*)(AIAttackOption*, void*, CStream*);
StreamFn s_originalSave;
} // namespace

// 005CE920: the original into a recording stream, ours into another; the bytes compared, then
// ours into the real stream.
static void __fastcall SaveVerify(AIAttackOption* self, void*, CStream* stream) {
    RecordingStream expected(stream->version_), actual(stream->version_);
    s_originalSave(self, nullptr, &expected);
    self->Save(&actual);
    ei::verify::Check("AIAttackOption::Save", true, expected.written == actual.written, "%d bytes (original %d)",
                      static_cast<int>(actual.written.size()), static_cast<int>(expected.written.size()));
    self->Save(stream);
}
EI_VERIFY(0x005CE920, SaveVerify, 5, s_originalSave);

// 005CE860: reads the stream, so ours only: an old record gets the derived kind and range.
static void __fastcall OptionLoadVerify(AIAttackOption* self, void*, CStream* stream) {
    self->Load(stream);
    bool ok = stream->version_ >= 0x5A || (self->kind == (self->part >= 0 ? 1 : 0) && self->range == 1.0f);
    ei::verify::Check("AIAttackOption::Load", true, ok, "version %d kind %d part %d", stream->version_, self->kind,
                      self->part);
}
EI_VERIFY(0x005CE860, OptionLoadVerify, 0, s_unused);

void __fastcall LoadAttackOptions(AIAttackOptions* options, CStream* stream);

// 005D9B80: reads the stream, so ours only: the storage holds the read count.
static void __fastcall LoadAttackOptionsVerify(AIAttackOptions* options, CStream* stream) {
    LoadAttackOptions(options, stream);
    bool ok = options->size >= 0 && options->maxSize >= options->size && (options->size == 0 || options->data);
    ei::verify::Check("LoadAttackOptions", true, ok, "%d options", options->size);
}
EI_VERIFY(0x005D9B80, LoadAttackOptionsVerify, 0, s_unused);
