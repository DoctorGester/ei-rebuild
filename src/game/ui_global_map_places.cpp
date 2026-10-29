// The global map's (CGlobalMap, ui_camp.h) places: the titles and positions of its allod, zones and quests
// from the texts and map.txt, the zone selection's marker colours and the pick picture. See docs/notes/ui.md
// ("Camp, global map, hero screens").
#include "model.h"
#include "app.h"
#include "ui_camp.h"

#include "console.h"
#include "item_db_text.h"

#include "game_singletons.h"
#include "render/render_state.h"
#include "lib/str.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

constexpr const char* kMapText = "map.txt";
constexpr const char* kPositionLine = "#position";
constexpr const char* kMissingNumber = "0";

} // namespace

// --- titles and positions ------------------------------------------------------------------------

std::string CGlobalMap::PlaceTitle(std::string_view prefix, std::string_view name) {
    CTextManager* texts = g_clientWorld->texts_;
    std::string title;
    std::string key(prefix);
    key += name;
    if (texts->Open(key)) texts->ReadLine(&title, true);
    return title;
}

std::optional<Vector3> CGlobalMap::MapPosition(std::string_view prefix, std::string_view name, bool positionLine) {
    CTextManager* texts = g_clientWorld->texts_;
    std::string key;
    std::vector<std::string> words;
    texts->Open(kMapText);
    key = prefix;
    key += name;
    std::optional<Vector3> position;
    if (texts->FindLine(key) && (!positionLine || texts->FindLine(kPositionLine))) {
        texts->NextLine();
        if (texts->Split(&words, true)) {
            while (words.size() < 3) words.push_back(kMissingNumber);
            position = Vector3{static_cast<float>(str::ToDouble(words[0])), static_cast<float>(str::ToDouble(words[1])),
                               static_cast<float>(str::ToDouble(words[2]))};
        }
    }
    texts->Close();
    return position;
}

// --- the selected zone ---------------------------------------------------------------------------

void CGlobalMap::SelectZone(int index, bool isLink) {
    int zone = -1;
    int link = -1;
    if (!isLink) {
        zone = index;
        if (gipat_ && index >= 0 && zones_[index].link >= 0) link = zones_[index].link;
    } else {
        link = index;
        if (gipat_ && index >= 0 && links_[index].zone >= 0) zone = links_[index].zone;
    }
    for (int i = 0; i < std::ssize(zones_); ++i) {
        CUiMesh* marker = Marker(i);
        int state = zones_[i].state;
        if (state == 0) {
            marker->texture_ = objects_[2];
        } else if (state == 1 || state == 2) {
            marker->texture_ = objects_[0];
            float scale = state == 1 ? 1.0f : 0.5f;
            for (int k = 0; k < 3; ++k) marker->tint_[k] = i == zone ? fog_[k] * scale : scale;
        } else {
            continue;
        }
        marker->flags_ &= ~FigureMeshFlag::Flat;
    }
    for (int i = 0; i < std::ssize(links_); ++i) {
        CUiMesh* marker = Marker(std::ssize(zones_) + std::ssize(quests_) + i);
        for (int k = 0; k < 3; ++k) marker->tint_[k] = i == link ? fog_[k] : 1.0f;
    }
}

// --- the pick picture ----------------------------------------------------------------------------

// Scene objects 0..2 (arrow, base, island) and everything after the zone markers (quests, links,
// layers and the rest) shown or hidden.
void CGlobalMap::ShowAllButZones(bool visible) {
    for (int i = 0; i < 3; ++i) SceneMesh(i)->visible_ = visible;
    for (int i = std::ssize(zones_) + 3; i < std::ssize(sceneObjects_); ++i) SceneMesh(i)->visible_ = visible;
}

void CGlobalMap::RefreshSelection() {
    if (!backdrop_) return;
    for (int i = 0; i < std::ssize(zones_); ++i) {
        CUiMesh* marker = Marker(i);
        std::uint32_t level = 0x18u + 0x10u * static_cast<std::uint32_t>(i);
        marker->texture_ = nullptr;
        // Opaque grey (from the 16th zone on the level's ninth bit spills into the next channel, as in the original).
        marker->flatColour_ = 0xFF000000u | level << 16 | level << 8 | level;
        marker->flags_ |= FigureMeshFlag::Flat;
    }
    surfaces_[1]->Hide(false);
    surfaces_[0]->Hide(false);
    ShowAllButZones(false);
    std::uint32_t colour = g_sceneRenderer->fogColour_;
    g_sceneRenderer->fogColour_ = 0;
    app::FrameStep(false, false);
    g_sceneRenderer->fogColour_ = colour;
    surfaces_[1]->Show();
    ShowAllButZones(true);
    SceneMesh(MarkerCount() + 6)->visible_ = false;
    SceneMesh(MarkerCount() + 7)->visible_ = false;
    backdrop_->CaptureScreen();
    SelectZone(-1, false);
}
