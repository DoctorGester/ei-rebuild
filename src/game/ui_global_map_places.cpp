// The global map's (CGlobalMap, ui_camp.h) places: the titles and positions of its allod, zones and quests
// from the texts and map.txt, the zone selection's marker colours and the weather. See docs/notes/ui.md
// ("Camp, global map, hero screens").
#include "global_map_clouds.h"
#include "model.h"
#include "app.h"
#include "ui_camp.h"

#include "console.h"
#include "item_db_text.h"
#include "text_reader.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "game_singletons.h"
#include "render/render_state.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>

namespace {

constexpr const char* kAllodPrefix = "allod ";
constexpr const char* kZonePrefix = "zone ";
constexpr const char* kQuestPrefix = "quest ";
constexpr const char* kMapText = "map.txt";
constexpr const char* kZoneEntry = "#zone %s";
constexpr const char* kPositionLine = "#position";
constexpr const char* kQuestEntry = "#quest %s";
constexpr const char* kMissingNumber = "0";

// The text `prefix` + `name` (empty when there is none), constructed into `out`.
mfc::CString* PlaceTitle(mfc::CString* out, const char* prefix, const char* name) {
    CTextManager* texts = TheTextManager();
    std::string text;
    if (texts->Open(mfc::CString(prefix) + name)) texts->ReadLine(&text, 1);
    return std::construct_at(out, text);
}


// ARGB from four table ints: no masking, alpha shifted in first.
std::uint32_t Argb(const int* entry) {
    std::uint32_t c = static_cast<std::uint32_t>(entry[3]);
    c = c << 8 | static_cast<std::uint32_t>(entry[0]);
    c = c << 8 | static_cast<std::uint32_t>(entry[1]);
    c = c << 8 | static_cast<std::uint32_t>(entry[2]);
    return c;
}

// IDirect3DDevice7::SetRenderState, recorded for the frame capture.
constexpr int kShadeMode = 9;          // D3DRENDERSTATE_SHADEMODE: 1 flat, 2 Gouraud
constexpr int kDitherEnable = 0x1A;    // D3DRENDERSTATE_DITHERENABLE
void SetRenderState(CTextureDevice* device, int state, std::uint32_t value) {
    uidevice::SetRenderState(device, static_cast<std::uint32_t>(state), value);
    render::state::Render(static_cast<std::uint32_t>(state), value);
}

} // namespace

// --- titles --------------------------------------------------------------------------------------

mfc::CString* CGlobalMap::AllodTitle(mfc::CString* out, const char* allod) { return PlaceTitle(out, kAllodPrefix, allod); }
EI_REWRITTEN(0x00631110, CGlobalMap::AllodTitle);

mfc::CString* CGlobalMap::ZoneTitle(mfc::CString* out, const char* zone) { return PlaceTitle(out, kZonePrefix, zone); }
EI_REWRITTEN(0x00631030, CGlobalMap::ZoneTitle);

mfc::CString* CGlobalMap::QuestTitle(mfc::CString* out, const char* quest) { return PlaceTitle(out, kQuestPrefix, quest); }
EI_REWRITTEN(0x00630F50, CGlobalMap::QuestTitle);

// --- positions -----------------------------------------------------------------------------------

bool CGlobalMap::ReadMapPosition(Vector3* out, const char* format, const char* name, bool positionLine) {
    CTextManager* texts = TheTextManager();
    std::string key;
    console::CStringArray words;
    texts->Open(kMapText);
    key = str::Format(format, name);
    bool found = false;
    if (texts->FindLine(key.c_str()) && (!positionLine || texts->FindLine(kPositionLine))) {
        TextNextLine(texts);
        if (texts->Split(&words, 1)) {
            while (words.GetSize() < 3) words.SetAtGrow(words.GetSize(), kMissingNumber);
            out->x = static_cast<float>(std::atof(words[0]));
            out->y = static_cast<float>(std::atof(words[1]));
            out->z = static_cast<float>(std::atof(words[2]));
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
EI_REWRITTEN(0x00630E00, CGlobalMap::ZonePosition);

Vector3* CGlobalMap::QuestPosition(Vector3* out, const char* quest) {
    ReadMapPosition(out, kQuestEntry, quest, false);
    return out;
}
EI_REWRITTEN(0x00630CC0, CGlobalMap::QuestPosition);

// --- the selected zone ---------------------------------------------------------------------------

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
EI_REWRITTEN(0x00630860, CGlobalMap::SelectZone);

// --- the pick picture ----------------------------------------------------------------------------

// Scene objects 0..2 (arrow, base, island) and everything after the zone markers (quests, links,
// layers and the rest) shown or hidden.
void CGlobalMap::ShowAllButZones(int visible) {
    for (int i = 0; i < 3; ++i) SceneMesh(i)->visible_ = visible;
    for (int i = zoneCount_ + 3; i < sceneObjects_.size; ++i) SceneMesh(i)->visible_ = visible;
}

void CGlobalMap::RefreshSelection() {
    if (!backdrop_) return;
    for (int i = 0; i < zoneCount_; ++i) {
        CUiMesh* marker = Marker(i);
        std::uint32_t level = 0x18u + 0x10u * static_cast<std::uint32_t>(i);
        marker->texture_ = nullptr;
        marker->flatColour_ = ((((level | 0xFFFFFF00u) << 8) | level) << 8) | level;
        marker->flags_ |= 0x100u;
    }
    surfaces_.data[1]->Freeze(0, 0);
    surfaces_.data[0]->Freeze(0, 0);
    ShowAllButZones(0);
    SetRenderState(interface_->renderer_, kShadeMode, 1);
    SetRenderState(interface_->renderer_, kDitherEnable, 0);
    std::uint32_t colour = SceneRenderer()->fogColour_;
    SceneRenderer()->fogColour_ = 0;
    app::FrameStep(false, false);
    SceneRenderer()->fogColour_ = colour;
    SetRenderState(interface_->renderer_, kShadeMode, 2);
    SetRenderState(interface_->renderer_, kDitherEnable, 1);
    surfaces_.data[1]->Freeze(1, 0);
    ShowAllButZones(1);
    SceneMesh(markerCount_ + 6)->visible_ = 0;
    SceneMesh(markerCount_ + 7)->visible_ = 0;
    backdrop_->CaptureScreen();
    SelectZone(-1, 0);
}
EI_REWRITTEN(0x00630670, CGlobalMap::RefreshSelection);

// --- the weather ---------------------------------------------------------------------------------

void CGlobalMap::SetWeather() {
    int index = weatherIndex_;
    if (index < 4) {
        SceneRenderer()->fogColour_ = Argb(&weatherTable_[0]);
    } else if (index < 8) {
        Marker(markerCount_)->flatColour_ = Argb(&weatherTable_[4]);
    } else if (index < 12) {
        Marker(markerCount_ + 1)->flatColour_ = Argb(&weatherTable_[8]);
    } else if (index < 16) {
        Marker(markerCount_ + 2)->flatColour_ = Argb(&weatherTable_[12]);
    } else {
        weather_->SetRamp(weatherTable_[16], weatherTable_[17], 2.0f);
    }
}
EI_REWRITTEN(0x0062EE90, CGlobalMap::SetWeather);
