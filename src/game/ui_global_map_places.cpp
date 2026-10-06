// The global map's (CGlobalMap, ui_camp.h) places: the titles and positions of its allod, zones
// and quests from the texts and map.txt, the zone selection's marker colours and the weather. The
// map's Create (0062D350, ui_global_map.cpp) calls them through their addresses. See
// docs/notes/ui.md ("Camp, global map, hero screens").
#include "ui_camp.h"

#include "console.h"
#include "item_db_text.h"
#include "text_reader.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "originals_effects.h"
#include "originals_library.h"
#include "originals_render.h"

#include <cstdint>
#include <new>

namespace {

constexpr const char* kAllodPrefix = "allod ";     // 00794200
constexpr const char* kZonePrefix = "zone ";       // 007941F8
constexpr const char* kQuestPrefix = "quest ";     // 00790790
constexpr const char* kMapText = "map.txt";        // 007856A0
constexpr const char* kZoneEntry = "#zone %s";     // 007941EC
constexpr const char* kPositionLine = "#position"; // 007941E0
constexpr const char* kQuestEntry = "#quest %s";   // 007941D4
constexpr const char* kMissingNumber = "0";        // 007856B8

// The text `prefix` + `name` (empty when there is none), constructed into `out`.
mfc::CString* PlaceTitle(mfc::CString* out, const char* prefix, const char* name) {
    CTextManager* texts = GameTexts();
    mfc::CString text;
    if (TextOpen(texts, nullptr, mfc::CString(prefix) + name)) TextReadLine(texts, nullptr, &text, 1);
    return new (out) mfc::CString(text);
}

// The display object (00500960): +0x14 the colour weather entries 0-3 set (the pick render of
// 00630670 clears it while it draws).
struct DisplayColours {
    std::uint8_t unknown_00[0x14];
    std::uint32_t colour;            // +0x14 ARGB
};
DisplayColours* Display() { return static_cast<DisplayColours*>(DisplayObject()); }

// ARGB from four table ints as the original combines them: no masking, alpha shifted in first.
std::uint32_t Argb(const int* entry) {
    std::uint32_t c = static_cast<std::uint32_t>(entry[3]);
    c = c << 8 | static_cast<std::uint32_t>(entry[0]);
    c = c << 8 | static_cast<std::uint32_t>(entry[1]);
    c = c << 8 | static_cast<std::uint32_t>(entry[2]);
    return c;
}

} // namespace

// --- 00631110 / 00631030 / 00630F50: titles ----------------------------------------------------

mfc::CString* CGlobalMap::AllodTitle(mfc::CString* out, const char* allod) { return PlaceTitle(out, kAllodPrefix, allod); }
EI_REPLACE_METHOD(0x00631110, CGlobalMap::AllodTitle);

mfc::CString* CGlobalMap::ZoneTitle(mfc::CString* out, const char* zone) { return PlaceTitle(out, kZonePrefix, zone); }
EI_REPLACE_METHOD(0x00631030, CGlobalMap::ZoneTitle);

mfc::CString* CGlobalMap::QuestTitle(mfc::CString* out, const char* quest) { return PlaceTitle(out, kQuestPrefix, quest); }
EI_REPLACE_METHOD(0x00630F50, CGlobalMap::QuestTitle);

// --- 00630E00 / 00630CC0: positions -------------------------------------------------------------

bool CGlobalMap::ReadMapPosition(Vector3* out, const char* format, const char* name, bool positionLine) {
    CTextManager* texts = GameTexts();
    mfc::CString key;
    console::CStringArray words;
    TextOpen(texts, nullptr, kMapText);
    key.Format(format, name);
    bool found = false;
    if (TextFindLine(texts, nullptr, key) && (!positionLine || TextFindLine(texts, nullptr, kPositionLine))) {
        TextNextLine(texts);
        if (TextSplit(texts, nullptr, &words, 1)) {
            while (words.GetSize() < 3) words.SetAtGrow(words.GetSize(), kMissingNumber);
            out->x = static_cast<float>(GameAtof(words[0]));
            out->y = static_cast<float>(GameAtof(words[1]));
            out->z = static_cast<float>(GameAtof(words[2]));
            found = true;
        }
    }
    TextClose(texts);
    return found;
}

Vector3* CGlobalMap::ZonePosition(Vector3* out, const char* zone) {
    ReadMapPosition(out, kZoneEntry, zone, true);
    return out;
}
EI_REPLACE_METHOD(0x00630E00, CGlobalMap::ZonePosition);

Vector3* CGlobalMap::QuestPosition(Vector3* out, const char* quest) {
    ReadMapPosition(out, kQuestEntry, quest, false);
    return out;
}
EI_REPLACE_METHOD(0x00630CC0, CGlobalMap::QuestPosition);

// --- 00630860: the selected zone -----------------------------------------------------------------

void CGlobalMap::SelectZone(int index, bool isLink) {
    int zone = -1;
    int link = -1;
    if (!isLink) {
        zone = index;
        if (gipat_ && index >= 0 && zones_.data[index].link >= 0) link = zones_.data[index].link;
    } else {
        link = index;
        if (gipat_ && index >= 0 && links_.data[index].zone >= 0) zone = links_.data[index].zone;
    }
    for (int i = 0; i < zoneCount_; ++i) {
        CUiMesh* marker = Marker(i);
        int state = zones_.data[i].state;
        if (state == 0) {
            marker->texture_ = objects_.data[2];
        } else if (state == 1 || state == 2) {
            marker->texture_ = objects_.data[0];
            float scale = state == 1 ? 1.0f : 0.5f;
            for (int k = 0; k < 3; ++k) marker->tint_[k] = i == zone ? fog_[k] * scale : scale;
        } else {
            continue;
        }
        marker->flags_ &= ~0x100u;
    }
    for (int i = 0; i < linkCount_; ++i) {
        CUiMesh* marker = Marker(zoneCount_ + questCount_ + i);
        for (int k = 0; k < 3; ++k) marker->tint_[k] = i == link ? fog_[k] : 1.0f;
    }
}
EI_REPLACE_METHOD(0x00630860, CGlobalMap::SelectZone);

// --- 0062EE90: the weather -----------------------------------------------------------------------

void CGlobalMap::SetWeather() {
    int index = weatherIndex_;
    if (index < 4) {
        Display()->colour = Argb(&weatherTable_[0]);
    } else if (index < 8) {
        Marker(markerCount_)->plainColour_ = Argb(&weatherTable_[4]);
    } else if (index < 12) {
        Marker(markerCount_ + 1)->plainColour_ = Argb(&weatherTable_[8]);
    } else if (index < 16) {
        Marker(markerCount_ + 2)->plainColour_ = Argb(&weatherTable_[12]);
    } else {
        WeatherSetRampOriginal(weather_, nullptr, weatherTable_[16], weatherTable_[17], 2.0f);
    }
}
EI_REPLACE_METHOD(0x0062EE90, CGlobalMap::SetWeather);
