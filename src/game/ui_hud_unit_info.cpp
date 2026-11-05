// The battle HUD's unit info texts: the lines drawn
// for the unit under the cursor or selected. Each line is a label from the
// panel's label table followed by values, drawn with the panel surface's text routine and moved
// down 15 pixels. See docs/notes/ui.md ("Battle HUD panels").
#include "combat.h"

#include "console.h"
#include "game_singletons.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"   // figureScale
#include "object_tables.h"
#include "text_manager.h"
#include "texture.h"
#include "ui.h"
#include "ui_item_info.h"
#include "screen_layout.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"
#include "lib/rebirth.h"
#include "lib/containers.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "lib/vecmath.h"
#include "render/render.h"
#include "render/render_state.h"
#include "render/text_format.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

const char* const kValuePair = "%s %d/%d";
const char* const kMoveKey = "move_%d";
const char* const kSpeedLine = "%s %.1f %s";
const char* const kValueUnit = "%s %d%s";
const char* const kValue = "%s %d";
const char* const kDamageKey = "dmg_%d";
const char* const kDamageLine = "%s %d-%d %s";
const char* const kSpace = " ";
const char* const kStronger = "consider_more%d";
const char* const kWeaker = "consider_less%d";
const char* const kTwoTexts = "%s %s";
const char* const kCampExp = "camp_current_exp";

constexpr int kLineHeight = 15;
constexpr int kLevelDifferenceLimit = 25;

// The speed shown: the speed of the mode (mode 3) for a unit outside the group, else of the current one,
// times 7.5.
float ShownSpeed(Unit* unit, bool outsideGroup) {
    const float speed = outsideGroup ? unit->MoveSpeedFor(MoveMode::Run) : unit->MoveSpeed2();
    return speed * 7.5f;
}

struct Panel {
    UiSurface* surface = nullptr;
    const std::string* labels = nullptr;
    Rect rect;
    std::string text;

    // Draws `text` and moves down a line.
    void DrawLine() { DrawLine(text); }
    void DrawLine(std::string_view line) {
        surface->DrawTextIn(line, rect, TextStyle{0, kUiTextColour});
        rect.Offset(0, kLineHeight);
    }
    // "<label> <line>" when the line is not empty.
    void DrawLabelled(int label, const std::string& line) {
        text = labels[label] + kSpace + line;
        DrawLine();
    }
};

// Whether `unit` is not in the group (the battle screen's party).
bool OutsideGroup(HudUnitPanel* self, Unit* unit) {
    BattleScreen* group = self->owner;
    int count = lib::Count(group->party);
    Unit** units = group->party.data();
    for (int i = 0; i < count; ++i) {
        if (units[i] == unit) return false;
    }
    return true;
}

} // namespace

void HudUnitPanel::PaintUnitInfo(UiSurface& surface, Unit* unit) {
    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    UnitParams* params = &unit->params;
    std::string suffix;
    Panel panel;
    panel.surface = &surface;
    panel.labels = texts.data();
    panel.rect = {5, 0x23, 0x9B, 0x32};

    std::int16_t health = unit->packedVitals.hitPoints;
    panel.text = str::Format(kValuePair, panel.labels[0], health > 0 ? static_cast<int>(health) : 0,
                      static_cast<int>(unit->packedVitals.maxHitPoints));
    panel.DrawLine();
    bool outside = OutsideGroup(this, unit);
    panel.text = str::Format(kValuePair, panel.labels[1], static_cast<int>(unit->packedVitals.mana),
                      static_cast<int>(unit->packedVitals.maxMana));
    panel.DrawLine();

    std::uint8_t moveBonus = params->movementKind;
    if (moveBonus > 1) {
        panel.text = str::Format(kMoveKey, moveBonus - 1);
        suffix = clientWorldTexts->TextManager::GetString(panel.text);
    } else {
        suffix.clear();
    }
    float speed = ShownSpeed(unit, outside);
    panel.text = str::Format(kSpeedLine, panel.labels[2], speed, suffix);
    panel.DrawLine();

    float dayTime = g_clientWorld.NightLevel();
    int vision = std::lround(params->VisionRange(dayTime));
    panel.text = str::Format(kValueUnit, panel.labels[6], vision, panel.labels[13]);
    panel.DrawLine();

    // The controller's attack parameters: attack, defence, the damage type (below 7 unsigned: "dmg_<n>" names
    // it) and range.
    AttackParams combat;
    unit->controller->GetAttackParams(&combat);
    panel.text = str::Format(kValue, panel.labels[3], combat.attack);
    panel.DrawLine();
    panel.text = str::Format(kValue, panel.labels[4], combat.defence);
    panel.DrawLine();
    if (static_cast<std::uint32_t>(combat.damageType) < 7) {
        panel.text = str::Format(kDamageKey, combat.damageType);
        suffix = clientWorldTexts->TextManager::GetString(panel.text);   // else the move text stays
    }
    int damageMax = static_cast<int>(combat.damageMax);
    int damageMin = static_cast<int>(combat.damageMin);
    panel.text = str::Format(kDamageLine, panel.labels[9], damageMin, damageMax,
                      suffix);
    panel.DrawLine();
    panel.text = str::Format(kValue, panel.labels[15], static_cast<int>(params->rate));
    panel.DrawLine();

    auto description = params->ProtectionBlock(&unit->packedVitals, static_cast<unsigned char>(IsTextKey(unit->id)));
    const DamageLists lines = ItemDamageLists(description.data(), true);
    panel.DrawLabelled(10, lines.threshold);
    if (outside) {
        if (!lines.above.empty()) panel.DrawLabelled(11, lines.above);
        auto* prototype = unit->GetPrototype();
        if (prototype->spellNames.end() != prototype->spellNames.begin()) panel.DrawLine(panel.labels[5]);
        BattleScreen* group = owner;
        if (!group->party.empty()) {
            Unit* leader = group->party[0];
            int difference = unit->Level() - leader->Level();
            if (difference > kLevelDifferenceLimit) {
                suffix = str::Format(kStronger, kLevelDifferenceLimit);
            } else if (difference < -kLevelDifferenceLimit) {
                suffix = str::Format(kWeaker, kLevelDifferenceLimit);
            } else if (difference >= 0) {
                suffix = str::Format(kStronger, difference);
            } else {
                suffix = str::Format(kWeaker, -difference);
            }
            suffix = clientWorldTexts->TextManager::GetString(suffix);
            panel.text = str::Format(kTwoTexts, panel.labels[16], suffix);
            panel.DrawLine();
        }
    } else if (!lines.below.empty()) {
        panel.DrawLabelled(12, lines.below);
    }

    if (IsMultiplayerClient() && !outside) {
        suffix = clientWorldTexts->TextManager::GetString(kCampExp);
        panel.text = str::Format(kValue, suffix, params->unspentExperience);
        panel.DrawLine();
    }
}

// --- Ui hud unit draw --------------------------------------------------------------------------------

// The battle HUD's unit panel: its draw (HudUnitPanel). Rebirth patches it for its WideScreen layout, reproduced for
// every window (screen_layout.h). It drives the Direct3D device itself; those calls are made as they are and mirrored to the
// new renderer as the hero preview's are (ui_hero_preview.cpp). See docs/notes/ui.md, "Battle HUD unit panel draw".

namespace {

// The constants read at run time.
constexpr float kLayoutUnit = 0.0025f;       // 1 / 400, half the layout's width
constexpr float kDepthUnit = 0.48157462f;        // the layout's half height at depth 1
constexpr float kModelDepth = 9.0f;       // how far in front of the camera the model stands
constexpr float kModelDrop = 0.25f;        // of the view's half height below the centre
constexpr float kTurnAboutZ = 0.05f;
constexpr float kTurnAboutX = 1.15f;
constexpr float kSecondTurnAboutZ = 0.4f;
constexpr float kBarHeight = 15.0f;
constexpr float kPanelHeight = 220.0f;
constexpr int kBackground = 0;        // scene objects
constexpr int kHeightBar = 8;
constexpr int kMoreMark = 9;
constexpr int kFirstDisplay = 10;     // 10..31: the bars, the body parts and the effects' pictures
constexpr int kDisplaysEnd = 32;
constexpr int kInfoPage = 2;          // bodyPart: the page of texts, without the model

} // namespace

Unit* HudUnitPanel::PanelUnit() const {
    if (slide.t >= 1.0f) return nullptr;
    Unit* unit = hoverUnit ? hoverUnit : shownUnit;
    return unit && unit->AsObject3D()->attached ? unit : nullptr;
}

// The panel surface's paint: the shown unit's name and its page's texts.
void HudUnitPanel::PaintTexts(UiSurface& surface) {
    Unit* unit = PanelUnit();
    if (!unit) return;
    Rect nameRect = {5, 5, 155, 20};
    surface.DrawWrappedText(strings[hoverUnit ? 1 : 0], nameRect, 2, TextStyle{0, kUiTextColour, render::text::kCenter | render::text::kWordBreak});
    switch (bodyPart) {
    case 0:
        PaintHealth(surface, unit);
        break;
    case 1:
        PaintParts(surface, unit);
        PaintHealth(surface, unit);
        break;
    case 2:
        PaintUnitInfo(surface, unit);
        break;
    case 3:
        PaintEffects(surface, unit);
        break;
    }
}

void HudUnitPanel::Draw(SceneRenderer* pass) {
    screen_layout::EnterLeftPanel();    // addon's 100232F4
    Quaternion savedOrientation = {1.0f, 0.0f, 0.0f, 0.0f};
    Vector3 savedPosition = {};
    sceneObjects[kHeightBar]->visible = false;
    sceneObjects[kMoreMark]->visible = false;
    // The background alone, before anything else.
    sceneObjects[kBackground]->visible = true;
    sceneObjects[kBackground]->Draw(pass);
    sceneObjects[kBackground]->visible = false;

    // The unit shown while the panel is not slid away: the hover unit, else the selected one.
    Unit* unit = nullptr;
    float size = 0.0f;     // the original leaves both unset without a unit (never read then)
    float height = 0.0f;
    if (slide.t < 1.0f) {
        const int which = hoverUnit ? 1 : 0;
        unit = hoverUnit ? hoverUnit : shownUnit;
        size = viewSize[which];
        height = viewHeight[which];
        if (hoverUnit == shownUnit) size = viewSize[0];
        if (unit) size = unit->AnimationRatio() * size;
    }

    for (int i = kFirstDisplay; i < kDisplaysEnd; ++i) sceneObjects[i]->visible = false;

    if (unit && unit->AsObject3D()->attached) {
        Object3D* model = unit->AsObject3D();
        const int offset = slide.Offset();
        const bool drawn = bodyPart != kInfoPage;
        if (drawn) {
            // The screen around the panel's model area (right of it, its sling strip, below it):
            // the depth buffer goes to 0 there, so the model is drawn inside the panel only.
            Rect right = {180 - offset, 0, 400 - offset, 220};
            Rect strip = {0, 0, 20 - offset, 220};
            Rect below = {0, 220, 400 - offset, 300};
            // WideScreen: without the margin.
            ui->ScaleRectToScreenNoMargin(&right);
            ui->ScaleRectToScreenNoMargin(&strip);
            ui->ScaleRectToScreenNoMargin(&below);
            Rect rects[3] = {right, below, strip};
            ClearUiDepth(0.0f, rects, 3);

            savedPosition = model->localPosition;
            savedOrientation = model->localRotation;
            unit->AdvanceTime(g_clientWorld.tick + g_clientWorld.tickFraction);

            // In front of the interface camera, at the layout's x = 100 less the slide, lowered so
            // that its height (in units of its size) stands a quarter of the view below the centre.
            // WideScreen: 100 - shift.
            const float x = ((100 - screen_layout::EdgeShift() - offset) * kLayoutUnit - 1.0f) *
                                         kDepthUnit * kModelDepth;
            const float y = height / size - (kDepthUnit * kModelDepth) * kModelDrop;
            model->localPosition = Vector3{x, y, kModelDepth};
            // Turned with the sling buttons' angle: about z, about x, about z again.
            const Vector3 axisZ = {0.0f, 0.0f, 1.0f};
            const Vector3 axisX = {1.0f, 0.0f, 0.0f};
            const Quaternion first = Quaternion::AxisAngle(axisZ, kHudQuarterTurn * kTurnAboutZ);
            const Quaternion second = Quaternion::AxisAngle(axisX, kHudQuarterTurn * kTurnAboutX);
            const Quaternion tilted = first * second;
            const Quaternion third = Quaternion::AxisAngle(axisZ, kHudQuarterTurn * kSecondTurnAboutZ);
            model->localRotation = tilted * third;

            const float scale = 1.0f / size;
            unit->AsObject3D()->ClientSpecific()->figureScale = Vector3{scale, scale, scale};
            // The first page draws the model in full colour, the others at half.
            model->SetPartsFlatColour(bodyPart == 0 ? 0xFFFFFFFFu : 0xFF808080u);
            model->SetPartsFlatSpecular(0xFF000000u);
            model->SetPartFlags(lib::Bits(FigureMeshFlag::Flat));
            unit->flags |= MapObjectFlag::NoEdgeFade;

            // Texture stage 0's mip filter off around the model's draw.
            render::TextureStage& stage0 = render::g_renderState.stages[0];
            const render::MipFilter mipFilter = stage0.mipFilter;
            stage0.mipFilter = render::MipFilter::None;
            // The unit is out in the weather the world draws it in: the same weather on its figure here.
            const MapObject* worldObject = unit;
            render::BeginInterfaceUnit(worldObject);
            unit->DrawFigure(pass);
            render::EndInterfaceUnit();
            stage0.mipFilter = mipFilter;

            unit->flags &= ~MapObjectFlag::NoEdgeFade;
            model->ClearPartFlags(lib::Bits(FigureMeshFlag::Flat));
            ClearUiDepth(1.0f, rects, 3);
        }

        switch (bodyPart) {
        case 0: {
            ShowHealth(unit);
            // The height bar (scene object 8): the model's fifteen layout units, from the panel's
            // bottom.
            const float barScale = kBarHeight / size;
            std::uint32_t bits;
            bits = std::bit_cast<std::uint32_t>(barScale);
            sceneObjects[kHeightBar]->SetScaleYBits(bits);
            const float barY = kPanelHeight - barScale * kPanelHeight;
            sceneObjects[kHeightBar]->Place(static_cast<float>(20 - offset), barY, 4.0f);
            sceneObjects[kHeightBar]->visible = true;
            break;
        }
        case 1:
            ShowParts(unit);
            ShowHealth(unit);
            break;
        case 3:
            ShowEffects(unit);
            break;
        }

        if (bodyPart != kInfoPage) {
            unit->AsObject3D()->ClientSpecific()->figureScale = Vector3{1.0f, 1.0f, 1.0f};
            model->localPosition = savedPosition;
            model->localRotation = savedOrientation;
        }
    }

    screen_layout::LeavePanel();        // addon's 10023312, before the base draw
    DrawBase(pass);
}
