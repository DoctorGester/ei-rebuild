// Checks for combat_stat_slots.cpp (A-U5). Functions that only rewrite controller or statistics
// bytes run ours, keep the result, restore the bytes and run the original, which must leave the
// same bytes (the original's state stays). The rest run ours only and check the result.
#include "verify.h"

#include "../../src/game/combat.h"
#include "../../src/game/map_object.h"
#include "../../src/game/unit_ai.h"

#include <cstring>

namespace {
int UnitId(const CUnitServer* self) { return self->unit ? self->unit->id_ : -1; }

int FirstDifference(const void* a, const void* b, std::size_t size) {
    const auto* x = static_cast<const unsigned char*>(a);
    const auto* y = static_cast<const unsigned char*>(b);
    for (std::size_t i = 0; i < size; ++i)
        if (x[i] != y[i]) return static_cast<int>(i);
    return -1;
}

UnitVitals& VitalsOf(CUnit* unit) { return static_cast<CMapObjectServer*>(unit->MapObject()->companion)->vitals; }

// One region of memory a function writes: saved before ours, ours kept, restored, compared after
// the original.
struct Region {
    void* at;
    std::size_t size;
    unsigned char saved[sizeof(UnitStats)];
    unsigned char ours[sizeof(UnitStats)];

    void Save() { std::memcpy(saved, at, size); }
    void KeepOurs() {
        std::memcpy(ours, at, size);
        std::memcpy(at, saved, size);
    }
    int Difference() const { return FirstDifference(at, ours, size); }
};

// Large buffers: static (the game's main thread only calls these).
Region& RegionA() {
    static Region region;
    return region;
}
Region& RegionB() {
    static Region region;
    return region;
}
} // namespace

// 005533B0: PUSH EBP; MOV EBP,ESP; PUSH ECX; PUSH ESI; PUSH EDI (6 bytes). Slot 19. Compares the
// companion's vitals and nextAttackTime.
static BOOL(__fastcall* s_refreshVitals)(CUnitServer*, void*, char);
static BOOL __fastcall RefreshVitalsVerify(CUnitServer* self, void*, char withHitPoints) {
    Region& vitals = RegionA();
    vitals.at = &VitalsOf(self->unit);
    vitals.size = sizeof(UnitVitals);
    int savedTime = self->nextAttackTime;
    vitals.Save();
    BOOL actual = self->RefreshVitals(withHitPoints);
    int ourTime = self->nextAttackTime;
    vitals.KeepOurs();
    self->nextAttackTime = savedTime;
    BOOL expected = s_refreshVitals(self, nullptr, withHitPoints);
    int diff = vitals.Difference();
    ei::verify::Check("CUnitServer::RefreshVitals", true, diff < 0 && ourTime == self->nextAttackTime && actual == expected,
                      "unit %d: vitals differ at byte %d, next attack %d vs %d (from %d)", UnitId(self), diff, ourTime,
                      self->nextAttackTime, savedTime);
    return expected;
}
EI_VERIFY(0x005533B0, RefreshVitalsVerify, 6, s_refreshVitals);

// 00553450: PUSH ECX; PUSH EBX; PUSH ESI; MOV ESI,ECX; PUSH EDI (6 bytes). Writes protection[].
static void(__fastcall* s_updateProtection)(CUnitServer*);
static void __fastcall UpdateProtectionVerify(CUnitServer* self) {
    std::int16_t saved[8];
    std::int16_t ours[8];
    std::memcpy(saved, self->protection, sizeof(saved));
    self->UpdateProtection();
    std::memcpy(ours, self->protection, sizeof(ours));
    std::memcpy(self->protection, saved, sizeof(saved));
    s_updateProtection(self);
    int diff = FirstDifference(self->protection, ours, sizeof(ours));
    ei::verify::Check("CUnitServer::UpdateProtection", -1, diff, "unit %d: first difference at byte %d (defence %d vs %d)",
                      UnitId(self), diff, ours[0], self->protection[0]);
}
EI_VERIFY(0x00553450, UpdateProtectionVerify, 6, s_updateProtection);

// 00553500: PUSH ECX; PUSH ESI; MOV ESI,ECX; MOV EAX,[ESI+0x1C] (7 bytes). Slot 22. Compares the
// statistics, the protection and motionChanged.
static void(__fastcall* s_repackStats)(CUnitServer*);
static void __fastcall RepackStatsVerify(CUnitServer* self) {
    Region& stats = RegionA();
    stats.at = &self->stats;
    stats.size = sizeof(UnitStats);
    Region& rest = RegionB();
    rest.at = self->protection;
    rest.size = sizeof(self->protection);
    std::uint8_t savedFlag = self->motionChanged;
    stats.Save();
    rest.Save();
    self->RepackStats();
    std::uint8_t ourFlag = self->motionChanged;
    stats.KeepOurs();
    rest.KeepOurs();
    self->motionChanged = savedFlag;
    s_repackStats(self);
    int diff = stats.Difference();
    int restDiff = rest.Difference();
    ei::verify::Check("CUnitServer::RepackStats", true, diff < 0 && restDiff < 0 && ourFlag == self->motionChanged,
                      "unit %d: stats differ at byte %d, protection at %d, motionChanged %d vs %d", UnitId(self), diff,
                      restDiff, ourFlag, self->motionChanged);
}
EI_VERIFY(0x00553500, RepackStatsVerify, 7, s_repackStats);

// Runs ours and the original on the statistics (see Region); reports the first difference.
template <typename Ours, typename Original>
static void SameStats(const char* name, CUnitServer* self, Ours ours, Original original) {
    Region& stats = RegionA();
    stats.at = &self->stats;
    stats.size = sizeof(UnitStats);
    stats.Save();
    auto actual = ours();
    stats.KeepOurs();
    auto expected = original();
    int diff = stats.Difference();
    ei::verify::Check(name, true, diff < 0 && actual == expected, "unit %d: stats differ at byte %d, result %d vs %d",
                      UnitId(self), diff, static_cast<int>(actual), static_cast<int>(expected));
}

// 00553560: MOV EDX,[0x007C871C] (6 bytes). Slot 26.
static bool(__fastcall* s_addArmourLayerOf)(CUnitServer*, void*, std::uint32_t);
static bool __fastcall AddArmourLayerOfVerify(CUnitServer* self, void*, std::uint32_t key) {
    bool result = false;
    SameStats("CUnitServer::AddArmourLayerOf", self, [&] { return self->AddArmourLayerOf(key); },
              [&] { return result = s_addArmourLayerOf(self, nullptr, key); });
    return result;
}
EI_VERIFY(0x00553560, AddArmourLayerOfVerify, 6, s_addArmourLayerOf);

// 005535C0: MOV EAX,[ECX+0x1C]; ADD ECX,0x1E0 (9 bytes). Slot 27.
static bool(__fastcall* s_rebuildArmourLayers)(CUnitServer*, void*, int);
static bool __fastcall RebuildArmourLayersVerify(CUnitServer* self, void*, int unused) {
    bool result = false;
    SameStats("CUnitServer::RebuildArmourLayers", self, [&] { return self->RebuildArmourLayers(unused); },
              [&] { return result = s_rebuildArmourLayers(self, nullptr, unused); });
    return result;
}
EI_VERIFY(0x005535C0, RebuildArmourLayersVerify, 9, s_rebuildArmourLayers);

// 005535E0: ADD ECX,0x1E0 (6 bytes). Slot 28.
static int(__fastcall* s_clearOuterArmourLayers)(CUnitServer*);
static int __fastcall ClearOuterArmourLayersVerify(CUnitServer* self) {
    int result = 0;
    SameStats("CUnitServer::ClearOuterArmourLayers", self, [&] { return self->ClearOuterArmourLayers(); },
              [&] { return result = s_clearOuterArmourLayers(self); });
    return result;
}
EI_VERIFY(0x005535E0, ClearOuterArmourLayersVerify, 6, s_clearOuterArmourLayers);

// 005535F0. Ours only (the armour spell list and the AI's options are rebuilt). Returns 1.
static void* s_unusedRefreshEquipment;
static BOOL __fastcall RefreshEquipmentVerify(CUnitServer* self) {
    BOOL result = self->RefreshEquipment();
    ei::verify::Check("CUnitServer::RefreshEquipment", 1, result, "unit %d", UnitId(self));
    return result;
}
EI_VERIFY(0x005535F0, RefreshEquipmentVerify, 0, s_unusedRefreshEquipment);

// 00552CD0. Ours only (destroys calls). No claims and no call with a mask are left.
static void* s_unusedForgetClaims;
static void __fastcall ForgetClaimsVerify(CUnitServer* self) {
    self->ForgetClaims();
    const AICallArray& calls = reinterpret_cast<const AIBrain*>(self->brain)->calls;
    int masked = 0;
    for (int i = 0; i < calls.size; ++i)
        if (calls.data[i].unknown_10 != 0) ++masked;
    ei::verify::Check("CUnitServer::ForgetClaims", true, self->claimedHostile == 0 && masked == 0,
                      "unit %d: claims %08X, %d masked calls of %d", UnitId(self), self->claimedHostile, masked,
                      calls.size);
}
EI_VERIFY(0x00552CD0, ForgetClaimsVerify, 0, s_unusedForgetClaims);

// 00553360: PUSH ESI; PUSH EDI; MOV EDI,[ESP+0xC] (6 bytes). Output only.
static void(__fastcall* s_copyStats)(CUnitServer*, void*, UnitStats*);
static void __fastcall CopyStatsVerify(CUnitServer* self, void*, UnitStats* out) {
    Region& expected = RegionA();
    expected.at = out;
    expected.size = sizeof(UnitStats);
    expected.Save();
    self->CopyStats(out);
    expected.KeepOurs();
    s_copyStats(self, nullptr, out);
    int diff = expected.Difference();
    ei::verify::Check("CUnitServer::CopyStats", -1, diff, "unit %d: first difference at byte %d", UnitId(self), diff);
}
EI_VERIFY(0x00553360, CopyStatsVerify, 6, s_copyStats);

// 00553380. Ours only (experience, levels). Returns RefreshVitals's 1.
static void* s_unusedAddExperience;
static BOOL __fastcall AddExperienceVerify(CUnitServer* self, void*, float amount) {
    BOOL result = self->AddExperience(amount);
    ei::verify::Check("CUnitServer::AddExperience", 1, result, "unit %d amount %g", UnitId(self), amount);
    return result;
}
EI_VERIFY(0x00553380, AddExperienceVerify, 0, s_unusedAddExperience);
