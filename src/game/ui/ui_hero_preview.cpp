// The hero preview of the camp and the multiplayer hero screen.
// See ui.h (UiWindow::DrawHeroPreview) and docs/notes/ui.md ("Camp, global map, hero screens").
#include "game/ui/ui.h"
#include "game/ui/ui_scrollbar.h"

#include "game/app/singletons.h"
#include "game/objects/map_object_shadow.h"
#include "engine/scene/scene_renderer.h"
#include "game/players/party.h"
#include "game/ui/ui_camp.h"
#include "game/ui/ui_screens.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "lib/crt.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "platform/platform.h"
#include "render/render.h"
#include "render/render_state.h"
#include "game/ui/screen_layout.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr float kTilt = /*007C7D24*/ 1.5707964f;   // a static initialiser: pi * 0.5
// The wide preview's six rectangles cleared to the far plane (800 x 600 layout), one table per edge.
constexpr std::int32_t kWideClearLeft[6] = /*007402F8*/ {290, 272, 272, 510, 510, 510};
constexpr std::int32_t kWideClearTop[6] = /*00740310*/ {100, 190, 340, 190, 290, 390};
constexpr std::int32_t kWideClearRight[6] = /*00740328*/ {510, 290, 290, 528, 528, 528};
constexpr std::int32_t kWideClearBottom[6] = /*00740340*/ {500, 260, 410, 210, 310, 410};

} // namespace

void UiWindow::DrawHeroPreview(SceneRenderer* pass, Unit* hero, float turn, float depth, bool wide) {
    Object3D* model = hero;
    if (!model) return;

    // The whole depth buffer to the near plane, then the preview's rectangles to the far plane.
    ClearUiDepth(0.0f);
    Rect rects[6];
    if (wide) {
        for (int i = 0; i < 6; ++i) {
            Rect r = screen_layout::ToScreen(
                Rect{kWideClearLeft[i], kWideClearTop[i], kWideClearRight[i], kWideClearBottom[i]});
            rects[i] = r;
        }
        ClearUiDepth(1.0f, rects, 6);
    } else {
        Rect r;
        if (depth > 400.0f)
            r = {410, 100, 700, 500};
        else
            r = {200, 100, 600, 500};
        r = screen_layout::ToScreen(r);
        rects[0] = r;
        ClearUiDepth(1.0f, rects, 1);
    }

    // Kept to put back afterwards: the position, the orientation and the pass's colour.
    Vector3 savedPosition;
    Quaternion savedOrientation;
    savedPosition = model->localPosition;
    savedOrientation = model->localRotation;
    SceneRenderer* renderer = pass;
    std::uint32_t savedColour = renderer->colour;
    // White: 255.0 converted three times, packed as 0xFF rr gg bb.
    renderer->SetColour(0xFFFFFFFFu);
    model->flags |= MapObjectFlag::NoEdgeFade;
    {
        // In animation frames: milliseconds / 1024 * 15 (a double: the milliseconds since the system started).
        model->AdvanceTime(static_cast<float>(platform::FrameClock() * (15.0 / 1024.0)));
    }

    // The turn about y. (The original also turned the origin by it and added that zero vector to the place.)
    Quaternion q = Quaternion::AxisAngle(Vector3{0.0f, 1.0f, 0.0f}, turn);
    // Placed by the depth: x from depth * c - 1, y and z fixed offsets.
    model->localPosition = {((depth * 0.0025f - 1.0f) * 0.48157462f) * 6.0f, 0.48157462f * 2.1f, 6.0f};
    // The orientation: the turn, then a fixed tilt about x, and in the wide layout a turn about -z.
    Vector3 xAxis = {1.0f, 0.0f, 0.0f};
    q = q * Quaternion::AxisAngle(xAxis, kTilt);
    if (depth > 400.0f) {
        Vector3 zAxis = {0.0f, 0.0f, -1.0f};
        q = q * Quaternion::AxisAngle(zAxis, kTilt * 0.15f);
    }
    model->localRotation = q;

    // Drawn brighter (1.2) with texture stage 0's mip filter off, then everything put back.
    Object3DClientSpecific* drawn = model->ClientSpecific();
    drawn->figureScale.x = 1.2f;
    drawn->figureScale.y = 1.2f;
    drawn->figureScale.z = 1.2f;
    render::TextureStage& stage0 = render::g_renderState.stages[0];
    const render::MipFilter savedMipFilter = stage0.mipFilter;
    stage0.mipFilter = render::MipFilter::None;
    model->DrawFigure(pass);
    stage0.mipFilter = savedMipFilter;
    drawn = model->ClientSpecific();
    drawn->figureScale.x = 1.0f;
    drawn->figureScale.y = 1.0f;
    drawn->figureScale.z = 1.0f;
    model->localPosition = savedPosition;
    model->localRotation = savedOrientation;
    model->flags &= ~MapObjectFlag::NoEdgeFade;
    renderer->SetColour(savedColour);
}

// --- Ui hero figure ----------------------------------------------------------------------------------

// The multiplayer hero screen's figure choice. See docs/notes/ui.md, section 5.

namespace {

const char* const kHeroName = "Hero";

constexpr float kDefaultAttribute = 25.0f;
constexpr float kDefaultHeight = 0.7f;
constexpr int kVoiceRows = 11;

// The index of `name` in `names` (case-insensitive), or -1.
int IndexOf(const std::vector<std::string>& names, std::string_view name) {
    for (int i = 0; i < std::ssize(names); ++i)
        if (text::CompareIgnoringCase(names[i], name) == 0) return i;
    return -1;
}

} // namespace

void MpHeroScreen::ChooseFigure(std::string_view prototype) {
    auto* player = LocalPlayerData();
    // The attributes and height to keep (copied bit for bit).
    float strength = kDefaultAttribute;
    float dexterity = kDefaultAttribute;
    float intelligence = kDefaultAttribute;
    float height = kDefaultHeight;
    if (!player->members.empty()) {
        PartyUnitRecord* first = player->members.data();
        const auto* stats = &first->stats;
        strength = stats->strength;
        dexterity = stats->dexterity;
        height = first->height;
        intelligence = stats->intelligence;
        std::vector<PartyUnitRecord*> records{first};
        g_clientWorld.RemoveUnitsOf(records);
    }
    player->Clear(true, false);
    player->AddMember(kHeroName, prototype, prototype);
    player->EquipFromPrototype(player->members.data());
    PartyUnitRecord* hero = player->members.data();
    auto* stats = &hero->stats;
    if (mode == kModeList) {
        stats->strengthBase = stats->strength = kDefaultAttribute;
        stats->dexterityBase = stats->dexterity = kDefaultAttribute;
        stats->intelligenceBase = stats->intelligence = kDefaultAttribute;
        hero->height = kDefaultHeight;
    } else {
        stats->strengthBase = stats->strength = strength;
        stats->dexterityBase = stats->dexterity = dexterity;
        stats->intelligenceBase = stats->intelligence = intelligence;
        hero->height = height;
    }
    // Each strip selects the figure when its list has it; the second list decides the voices.
    figureStrips[1]->selected = -1;
    figureStrips[0]->selected = -1;
    int index = IndexOf(defaults, prototype);
    if (index >= 0) figureStrips[0]->selected = index;
    index = IndexOf(names, prototype);
    bool firstVoices = index < 0;
    if (index >= 0) figureStrips[1]->selected = index;

    ShowFirstEquipment();
    MakePreview(false);
    statsPanel->SetHero(0, std::nullopt);
    tradePanel->ResetRecords();
    tradePanel->SelectHero(0);
    text = prototype;
    voiceIndex = 0;
    voiceTop = 0;
    voices = firstVoices ? &voiceLists[0] : &voiceLists[1];
    const std::vector<MpVoiceEntry>& currentVoices = *voices;
    // The first entry's file (the original read it even from an empty list; ours leaves the text then).
    if (!currentVoices.empty()) hero->secondPrototype = currentVoices.front().file;
    const int count = static_cast<int>(currentVoices.size());
    voiceScroll->SetMaximum(count > kVoiceRows ? count - kVoiceRows : 0);
    voiceScroll->SetPosition(0);
    if (mode == kModeNewHero) DrawVoices();
}
