// Checks for the global map's places (ui_global_map_places.cpp). All run side by side with the
// original. Reached by `screen 4` (ui_camp, ui_screens): the map's Create (0062D350) looks up the
// allod's title, the current zone's position, every zone's title, the links' titles and
// positions and the held quests' titles and positions, applies the 18 weather entries and
// selects nothing (SelectZone(-1, 0)); the mouse over a marker selects it (00630010).
#include "verify.h"

#include "../../src/game/originals_render.h"
#include "../../src/game/ui_camp.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using TitleFn = mfc::CString*(__fastcall*)(CGlobalMap*, void*, mfc::CString*, const char*);
using PositionFn = Vector3*(__fastcall*)(CGlobalMap*, void*, Vector3*, const char*);

// Ours into `out`, then the original's text into a scratch string (SV2h1: ours allocates first); the
// texts and results must agree.
mfc::CString* CompareTitle(const char* name, TitleFn original, CGlobalMap* self, mfc::CString* out, const char* key,
                           mfc::CString* (CGlobalMap::*ours)(mfc::CString*, const char*)) {
    mfc::CString* actual = (self->*ours)(out, key);
    alignas(4) std::uint8_t raw[sizeof(mfc::CString)];
    mfc::CString* expected = original(self, nullptr, reinterpret_cast<mfc::CString*>(raw), key);
    const char* a = *actual;
    const char* e = *expected;
    ei::verify::Check(name, true, actual == out && std::strcmp(e, a) == 0, "\"%.40s\": \"%.80s\" / \"%.80s\"", key, e,
                      a);
    expected->~CString();
    return actual;
}

// The original's position into a scratch vector, ours into `out` (left as it is when not found);
// compared only when ours found the entry (the original leaves uninitialised stack otherwise).
Vector3* ComparePosition(const char* name, PositionFn original, CGlobalMap* self, Vector3* out, const char* key,
                         const char* format, bool positionLine) {
    Vector3 expected = {};
    original(self, nullptr, &expected, key);
    bool found = CGlobalMap::ReadMapPosition(out, format, key, positionLine);
    if (found) {
        ei::verify::CheckNear(name, expected.x, out->x, "\"%.40s\" x", key);
        ei::verify::CheckNear(name, expected.y, out->y, "\"%.40s\" y", key);
        ei::verify::CheckNear(name, expected.z, out->z, "\"%.40s\" z", key);
    }
    return out;
}

} // namespace

// CGlobalMap::AllodTitle / ZoneTitle / QuestTitle (00631110 / 00631030 / 00630F50).
// Each starts PUSH -1; PUSH imm32 (7).
static TitleFn s_allodTitle;
static mfc::CString* __fastcall AllodTitleVerify(CGlobalMap* self, void*, mfc::CString* out, const char* allod) {
    return CompareTitle("CGlobalMap::AllodTitle", s_allodTitle, self, out, allod, &CGlobalMap::AllodTitle);
}
EI_VERIFY(0x00631110, AllodTitleVerify, 7, s_allodTitle);

static TitleFn s_zoneTitle;
static mfc::CString* __fastcall ZoneTitleVerify(CGlobalMap* self, void*, mfc::CString* out, const char* zone) {
    return CompareTitle("CGlobalMap::ZoneTitle", s_zoneTitle, self, out, zone, &CGlobalMap::ZoneTitle);
}
EI_VERIFY(0x00631030, ZoneTitleVerify, 7, s_zoneTitle);

static TitleFn s_questTitle;
static mfc::CString* __fastcall QuestTitleVerify(CGlobalMap* self, void*, mfc::CString* out, const char* quest) {
    return CompareTitle("CGlobalMap::QuestTitle", s_questTitle, self, out, quest, &CGlobalMap::QuestTitle);
}
EI_VERIFY(0x00630F50, QuestTitleVerify, 7, s_questTitle);

// CGlobalMap::ZonePosition / QuestPosition (00630E00 / 00630CC0). PUSH -1; PUSH imm32 (7).
static PositionFn s_zonePosition;
static Vector3* __fastcall ZonePositionVerify(CGlobalMap* self, void*, Vector3* out, const char* zone) {
    return ComparePosition("CGlobalMap::ZonePosition", s_zonePosition, self, out, zone, "#zone %s", true);
}
EI_VERIFY(0x00630E00, ZonePositionVerify, 7, s_zonePosition);

static PositionFn s_questPosition;
static Vector3* __fastcall QuestPositionVerify(CGlobalMap* self, void*, Vector3* out, const char* quest) {
    return ComparePosition("CGlobalMap::QuestPosition", s_questPosition, self, out, quest, "#quest %s", false);
}
EI_VERIFY(0x00630CC0, QuestPositionVerify, 7, s_questPosition);

namespace {

// What the selection and the weather write: per marker the texture, flags, tint and plain
// colour; the display's colour; the weather object's ramp parameters.
struct MarkerState {
    CEngineObject* texture;
    std::uint32_t flags;
    float tint[3];
    std::uint32_t plain;
};
struct MapState {
    std::vector<MarkerState> markers;
    std::uint32_t displayColour;
    std::uint8_t weather[0x10];

    bool operator==(const MapState& o) const {
        return markers.size() == o.markers.size() &&
               std::memcmp(markers.data(), o.markers.data(), markers.size() * sizeof(MarkerState)) == 0 &&
               displayColour == o.displayColour && std::memcmp(weather, o.weather, sizeof(weather)) == 0;
    }
};

// Markers 0 .. markerCount + 2 (the zones, quests, links and the three layers) when they exist.
int MarkerCount(CGlobalMap* self) {
    int n = self->markerCount_ + 3;
    int available = self->sceneObjects_.size - 3;
    return n < available ? n : (available > 0 ? available : 0);
}

std::uint32_t* DisplayColour() { return reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(DisplayObject()) + 0x14); }

// The weather object (+0x214) only with `withWeather`: SelectZone runs in Create before the
// object exists (the field is still uninitialised heap then), SetWeather after it.
MapState Snapshot(CGlobalMap* self, bool withWeather) {
    MapState s;
    for (int i = 0; i < MarkerCount(self); ++i) {
        CUiMesh* m = self->Marker(i);
        s.markers.push_back(MarkerState{m->texture_, m->flags_, {m->tint_[0], m->tint_[1], m->tint_[2]}, m->plainColour_});
    }
    s.displayColour = *DisplayColour();
    std::memset(s.weather, 0, sizeof(s.weather));
    if (withWeather) std::memcpy(s.weather, self->weather_, sizeof(s.weather));
    return s;
}

void Restore(CGlobalMap* self, const MapState& s, bool withWeather) {
    for (int i = 0; i < static_cast<int>(s.markers.size()); ++i) {
        CUiMesh* m = self->Marker(i);
        const MarkerState& k = s.markers[i];
        m->texture_ = k.texture;
        m->flags_ = k.flags;
        for (int c = 0; c < 3; ++c) m->tint_[c] = k.tint[c];
        m->plainColour_ = k.plain;
    }
    *DisplayColour() = s.displayColour;
    if (withWeather) std::memcpy(self->weather_, s.weather, sizeof(s.weather));
}

} // namespace

// CGlobalMap::SelectZone (00630860): the original first, the markers recorded and put back, then
// ours. SUB ESP,0x38; OR EAX,-1 (6).
static void(__fastcall* s_selectZone)(CGlobalMap*, void*, int, bool);
static void __fastcall SelectZoneVerify(CGlobalMap* self, void*, int index, bool isLink) {
    MapState before = Snapshot(self, false);
    s_selectZone(self, nullptr, index, isLink);
    MapState expected = Snapshot(self, false);
    Restore(self, before, false);
    self->CGlobalMap::SelectZone(index, isLink);
    MapState actual = Snapshot(self, false);
    ei::verify::Check("CGlobalMap::SelectZone", true, expected == actual, "index %d link %d zones %d links %d gipat %d",
                      index, isLink ? 1 : 0, self->zoneCount_, self->linkCount_, self->gipat_ ? 1 : 0);
}
EI_VERIFY(0x00630860, SelectZoneVerify, 6, s_selectZone);

// CGlobalMap::SetWeather (0062EE90): the same (the ramp entry recomputes the weather object's
// table from the same values, so running it twice changes nothing more). MOV EAX,[ECX+0x260] (6).
static void(__fastcall* s_setWeather)(CGlobalMap*, void*);
static void __fastcall SetWeatherVerify(CGlobalMap* self, void*) {
    // Entries 16 and up set the weather object's ramp (it exists by then).
    bool withWeather = self->weatherIndex_ >= 16 && self->weather_;
    MapState before = Snapshot(self, withWeather);
    s_setWeather(self, nullptr);
    MapState expected = Snapshot(self, withWeather);
    Restore(self, before, withWeather);
    self->CGlobalMap::SetWeather();
    MapState actual = Snapshot(self, withWeather);
    ei::verify::Check("CGlobalMap::SetWeather", true, expected == actual, "entry %d colour %08X / %08X",
                      self->weatherIndex_, expected.displayColour, actual.displayColour);
}
EI_VERIFY(0x0062EE90, SetWeatherVerify, 6, s_setWeather);
