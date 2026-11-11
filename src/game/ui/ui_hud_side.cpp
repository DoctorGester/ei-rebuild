// The battle HUD's two sliding side panels, the map (HudMap) and the unit panel (HudUnitPanel), and PanelSlide (ui_hud.h):
// their mouse handlers, layouts at the current slide offset, per-frame updates and setups, the map's markers and
// view, the unit panel's bars, body parts, effects and info texts, and its draw. Rebirth patches the panels for its
// WideScreen layout, reproduced for every window (screen_layout.h). See docs/notes/ui.md, section 7 ("Battle HUD sliding
// panels", "Battle HUD unit panel draw").
#include "engine/scene/light.h"
#include "game/ui/ui_hud.h"
#include "engine/core/base_manager.h"
#include "engine/scene/camera.h"
#include "game/combat/combat.h"
#include "game/app/singletons.h"
#include "game/objects/map_object.h"
#include "game/objects/object3d.h"
#include "game/objects/map_object_shadow.h"   // figureScale
#include "game/map/map_database.h"
#include "game/players/player_client.h"
#include "game/ui/screen_layout.h"
#include "game/spells/spell.h"
#include "engine/data/text_manager.h"
#include "game/players/player.h"
#include "game/spells/spell_db.h"
#include "game/ui/ui.h"
#include "game/ui/ui_battle.h"
#include "game/ui/ui_cursor.h"
#include "game/ui/ui_hud_pieces.h"
#include "game/ui/ui_item_info.h"
#include "game/ui/ui_mesh.h"
#include "game/units/unit_db.h"
#include "game/units/unit.h"
#include "game/world/world.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/rect.h"
#include "lib/str.h"
#include "lib/text_case.h"
#include "lib/vecmath.h"
#include "render/render.h"
#include "render/render_state.h"
#include "render/text_format.h"

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <format>
#include <iterator>
#include <list>
#include <string>

namespace {
// The map's centre in the layout, and the camera flag a click on the map clears.
constexpr int kMapCentreX = 700;
constexpr int kMapCentreY = 80;
constexpr std::uint32_t kCameraFollows = 8;

constexpr float kUnitRadius2 = 0.56f;    // units further out (squared) are not shown
constexpr float kTinyLength2 = 1e-6f;
constexpr float kArrowAhead = 0.07f;      // tip of a unit's arrow from its position
constexpr float kArrowWing = 0.04f;       // back corners of the arrow; size of a diamond
constexpr float kDiagonal = 0.70710677f;  // (its negation is -kDiagonal)
constexpr float kInnerRadius2 = 0.6f;     // markers inside (squared) use the zoomed place
constexpr float kRimRadius2 = 0.68f;      // \ otherwise the unzoomed place, when it is
constexpr float kRimExtent = 0.97f;       // / on the rim and inside the square
constexpr float kSquareHalf = 0.03f;      // marked objects and exits

constexpr int kSpecialUnitId = 0xA2C2A;   // drawn as an enemy only in zone "gz20g"

constexpr std::int16_t kFrameParty = 0;     // party members; other players in multiplayer
constexpr std::int16_t kFrameNeutral = 1;   // other units; zone exits
constexpr std::int16_t kFrameHostile = 2;
constexpr std::int16_t kFrameMarked = 3;    // marked objects

// Builds the mesh entries in order. Each entry is one quad block (positions), four vertex
// records and its indices: three for a unit (one triangle), six for the other markers.
class MarkerWriter {
public:
    MarkerWriter(UiMesh* newMesh, const HudMap* newMap) : mesh(newMesh), map(newMap) {}

    void BeginUnit() {
        std::uint16_t* index = mesh->indices.data() + indexPos;
        index[0] = static_cast<std::uint16_t>(vertex + 1);
        index[1] = static_cast<std::uint16_t>(vertex + 2);
        index[2] = static_cast<std::uint16_t>(vertex + 3);
        indexPos += 3;
    }
    void BeginQuad() {
        std::uint16_t* index = mesh->indices.data() + indexPos;
        index[0] = static_cast<std::uint16_t>(vertex);
        index[1] = static_cast<std::uint16_t>(vertex + 2);
        index[2] = static_cast<std::uint16_t>(vertex + 1);
        index[3] = static_cast<std::uint16_t>(vertex + 1);
        index[4] = static_cast<std::uint16_t>(vertex + 2);
        index[5] = static_cast<std::uint16_t>(vertex + 3);
        indexPos += 6;
    }

    VertexPacket& Block() { return mesh->vertices[entry]; }

    void Show(std::int16_t frame) {
        for (int k = 0; k < 4; ++k) {
            MeshCorner& v = mesh->corners[vertex + k];
            v.position = static_cast<std::uint16_t>(vertex + k);
            v.texCoord = static_cast<std::uint16_t>(frame);
        }
        Next();
    }
    void Hide() {
        for (int k = 0; k < 4; ++k) {
            MeshCorner& v = mesh->corners[vertex + k];
            v.position = 0;
            v.texCoord = 0;
        }
        Next();
    }

    // Map coordinates of a world point (map y grows downwards).
    float MapX(float worldX) const { return (worldX - map->centre.x) * map->scale; }
    float MapY(float worldY) const { return -((worldY - map->centre.y) * map->scale); }

    // The place of a marked object, an exit or another player: the zoomed position when it lies
    // inside the inner circle, otherwise the unzoomed one when that lies on the rim (outside the
    // rim circle, inside the square); false when neither.
    bool Place(float worldX, float worldY, float* x, float* y) const {
        float mapX = MapX(worldX);
        float mapY = MapY(worldY);
        float zoomedX = mapX * map->zoom;
        float zoomedY = mapY * map->zoom;
        if (LengthSquared2D(zoomedY, zoomedX) <= kInnerRadius2) {
            *x = zoomedX;
            *y = zoomedY;
            return true;
        }
        if (LengthSquared2D(mapY, mapX) < kRimRadius2) return false;
        if (AbsBits(mapX) > kRimExtent) return false;
        if (AbsBits(mapY) > kRimExtent) return false;
        *x = mapX;
        *y = mapY;
        return true;
    }

    void PutSquare(float x, float y) {
        VertexPacket& b = Block();
        float left = x - kSquareHalf;
        float top = y - kSquareHalf;
        float right = x + kSquareHalf;
        float bottom = y + kSquareHalf;
        b.x()[0] = left;
        b.y()[0] = top;
        b.x()[1] = right;
        b.y()[1] = top;
        b.x()[2] = left;
        b.y()[2] = bottom;
        b.x()[3] = right;
        b.y()[3] = bottom;
    }

    void PutDiamond(float x, float y) {
        VertexPacket& b = Block();
        b.x()[0] = x;
        b.y()[0] = y - kArrowWing;
        b.x()[1] = x + kArrowWing;
        b.y()[1] = y;
        b.x()[2] = x - kArrowWing;
        b.y()[2] = y;
        b.x()[3] = x;
        b.y()[3] = y + kArrowWing;
    }


private:
    void Next() {
        vertex += 4;
        ++entry;
    }

    UiMesh* mesh = nullptr;
    const HudMap* map = nullptr;
    int entry = 0;       // quad block
    int vertex = 0;      // first vertex of the entry (4 per entry)
    int indexPos = 0;    // next index
};

// Frame of a unit that is not in the party: hostile when both relation masks have the local
// side's bit. The special unit 0xA2C2A counts as hostile in zone gz20g whatever its relations.
std::int16_t UnitFrame(const Unit* unit) {
    int side = g_clientWorld.LocalView()->player->playerId;
    if (unit->id == kSpecialUnitId) {
        if (str::Compare(LocalPlayerZone(), "gz20g") == 0) return kFrameHostile;
    }
    std::uint32_t bit = 1u << (side & 31);
    return (bit & unit->relationsB & unit->relationsA) != 0 ? kFrameHostile : kFrameNeutral;
}

// The arrow of a unit at map position (x, y) (already zoomed), pointing along the unit's
// facing: the tip twice (vertices 0 and 1), then the two back corners.
void PutArrow(VertexPacket& packet, const Unit* unit, float x, float y) {
    const Quaternion* rotation = &unit->localRotation;
    // The rotated Y axis is computed twice, x taken from the first, y from the second (the function is
    // pure).
    Vector3 first = rotation->AxisY();
    float dirX = -first.x;
    Vector3 second = rotation->AxisY();
    float dirY = second.y;

    float length2 = LengthSquared2D(dirY, dirX);
    float nx;
    float ny;
    if (length2 >= kTinyLength2) {
        float length = std::sqrt(length2);
        nx = dirX / length;
        ny = dirY / length;
    } else {
        nx = 0.0f;
        ny = 1.0f;
    }

    float tipX = nx * kArrowAhead + x;
    float tipY = ny * kArrowAhead + y;
    packet.x()[0] = tipX;
    packet.y()[0] = tipY;
    packet.x()[1] = tipX;
    packet.y()[1] = tipY;

    // The back corners: the direction turned by +-135 degrees (sums of the scaled components).
    float sy = ny * kDiagonal;
    float sx = nx * kDiagonal;
    float leftX = sy - sx;
    float leftY = nx * -kDiagonal - sy;
    packet.x()[2] = leftX * kArrowWing + x;
    packet.y()[2] = leftY * kArrowWing + y;
    float rightY = sx - sy;
    packet.x()[3] = leftY * kArrowWing + x;
    packet.y()[3] = rightY * kArrowWing + y;
}

// Whether the edge from `prev` to `cur` crosses the line v = edge (coordinate values of the two points).
bool Crosses(float prev, float cur, float edge) {
    return (prev < edge && cur > edge) || (prev > edge && cur < edge);
}

// One Sutherland-Hodgman step in place: walking the polygon, the point where an edge crosses the line is
// inserted before the edge's end (which is then skipped); afterwards the points outside are removed.
template <typename Cut, typename Outside>
void ClipPass(std::vector<Vector2>& points, Cut cut, Outside outside) {
    int count = lib::Count(points);
    for (int i = 0; i < count; ++i) {
        Vector2 cur = points[i];
        Vector2 prev = points[i == 0 ? count - 1 : i - 1];
        Vector2 cross;
        if (!cut(prev, cur, &cross)) continue;
        int size = lib::Count(points);
        if (i < size) {
            points.resize(size + 1);
            std::memmove(&points[i + 1], &points[i], (size - i) * sizeof(Vector2));
        } else {
            points.resize(i + 1);
        }
        points[i] = cross;
        ++i;
        ++count;
    }
    for (int i = 0; i < count; ++i) {
        if (!outside(points[i])) continue;
        int tail = lib::Count(points) - i - 1;
        if (tail != 0) std::memmove(&points[i], &points[i + 1], tail * sizeof(Vector2));
        points.pop_back();
        --i;
        --count;
    }
}

// 2.5 to the power of a tenth of the notches, as the original's 2.5^(units / 1200).
float WheelZoomFactor(float notches) {
    return lib::Pow(2.5f, notches * kWheelUnits * 0.00083333335f);
}

constexpr TextStyle kPanelText{0, kUiTextColour};
constexpr std::uint32_t kRed = 0x0000FF;                             // COLORREF

// Whether `unit` is one of the battle screen's party (its mana is shown).
bool InParty(const BattleScreen* owner, const Unit* unit) {
    for (const Unit* member : owner->party)
        if (member == unit) return true;
    return false;
}

constexpr int kEffectPlaces = 6;    // the effects listed

// A horizontal bar sprite filled to `ratio` (0..1): its right edge moves to ratio - 0.5 and its
// texture's right edge to (ratio * `width` + 64) / 256 + 1/512 texels, between the rows `top`
// and `bottom` of the HUD texture.
void FillBar(UiMesh* bar, float ratio, float width, float top, float bottom) {
    bar->visible = true;
    float u = (ratio * width + 64.0f) * (1.0f / 256.0f) + (1.0f / 512.0f);
    float edge = ratio - 0.5f;
    bar->vertices[0].x()[2] = edge;
    bar->vertices[0].x()[3] = edge;
    bar->texCoords[2].u = u;
    bar->texCoords[2].v = top;
    bar->texCoords[3].u = u;
    bar->texCoords[3].v = bottom;
}

// `value` / `maximum` clamped to 1, or 0 when the bar is not filled (`ratio` is left
// alone then, like the original).
bool BarRatio(int value, int maximum, float* ratio) {
    *ratio = static_cast<float>(value) / static_cast<float>(maximum);
    if (*ratio > 1.0f) {
        *ratio = 1.0f;
        return true;
    }
    return *ratio > 0.0f;
}

constexpr const char* kSoundSling = "buttons\\battle\\sling.wav";
constexpr float kMapMiddleX = 700.0f;
constexpr float kMapButtonsX = 790.0f;
constexpr float kMapFrameX = 710.0f;
constexpr float kMapNeedleX = 694.0f;
constexpr float kUnitBackX = 100.0f;
constexpr float kUnitEdgeX = 10.0f;
constexpr float kUnitMiddleX = 90.0f;
constexpr float kUnitMoreX = 172.0f;
constexpr float kUnitPartDown = 17.5f;
constexpr float kUnitEffectX = 40.0f;
constexpr float kSlideTime = 0.3f;       // 0.3 seconds for the whole way
// The unit panel's six body parts: where each one's pair of sprites goes (two tables of six ints).
constexpr int kPartX[6] = /*00742434*/ {50, 50, 95, 5, 95, 5};
constexpr int kPartY[6] = /*0074244C*/ {35, 105, 70, 70, 140, 140};
constexpr int kParts = 6;
constexpr int kEffects = 6;

constexpr float kZoomRate = 2.5f;                   // the map's zoom changes 2.5 times a second
constexpr float kSlingDepthBits = 8.106086f;   // 8.106: the sling buttons and the needle
constexpr float kSlingY = 20.0f;


// Every region but the sling button moves with the panel.
void MoveRegions(UiWindow* window, const UiRegion* sling, int offsetX) {
    for (const auto& region : window->regions) {
        if (region.get() == sling) continue;
        region->offsetX = offsetX;
        region->offsetY = 0;
    }
}

void PlaceSling(UiMesh* sling, float angle, float x) {
    const Vector3 axis = {0.0f, 0.0f, 1.0f};
    sling->localRotation = Quaternion::AxisAngle(axis, angle);
    sling->Place(x, kSlingY, kSlingDepthBits);
}

constexpr const char* kInfoTextFormat = "infounit_%d";
constexpr const char* kPartLabelKeys[4] = {"hl_skull", "hl_torso", "hl_leg", "hl_arm"};
float StripScale() { return 100.0f / 0.48157462f; }
constexpr int kUnitTravel = 180;
constexpr int kInfoTexts = 18;

constexpr const char* kDefaultPicture = "battle00";
constexpr float kZoneUnit = 32.0f;            // world units per unit of a zone's size
constexpr float kMapSide = 2.0f;             // the map's side in map units
constexpr float kZoomRange = 0.05f;           // the largest zoom per world unit
constexpr float kOutlineStep = 0.125f;         // sixteen points round the map
constexpr float kOutlineRadius = 65.0f;
constexpr float kOutlineCentreX = 700.0f;
constexpr float kOutlineCentreY = 80.0f;
constexpr float kDepthUnit = 0.48157462f;           // the layout's half height at depth 1
constexpr float kBoundsScale = 0.045f;
constexpr float kMarkersScale = 0.03f;
constexpr float kSlingQuarter = -0.5f;        // the sling starts a quarter turn back
constexpr int kMapTravel = 180;
constexpr int kOutlinePoints = 16;

const MapRecord* LocalZone() {
    return g_mapDatabase.FindMap(LocalPlayerZone());
}

void SetPlain(UiMesh* sprite, std::uint32_t colour) {
    sprite->texture = nullptr;
    sprite->flags |= FigureMeshFlag::Flat;
    sprite->flatColour = colour;
}

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
    for (const Unit* member : group->party) {
        if (member == unit) return false;
    }
    return true;
}

// The constants read at run time.
constexpr float kLayoutUnit = 0.0025f;       // 1 / 400, half the layout's width
constexpr float kModelDepth = 9.0f;       // how far in front of the camera the model stands
constexpr float kModelDrop = 0.25f;        // of the view's half height below the centre
constexpr float kTurnAboutZ = 0.05f;
constexpr float kTurnAboutX = 1.15f;
constexpr float kSecondTurnAboutZ = 0.4f;
constexpr float kBarHeight = 15.0f;
constexpr float kPanelHeight = 220.0f;
constexpr int kInfoPage = 2;          // bodyPart: the page of texts, without the model
} // namespace

// --- The slide -------------------------------------------------------------------------------------------------------

void PanelSlide::Step() {
    t = UiContainer::FrameTime() * direction / kSlideTime + t;
    if (t > 1.0f) t = 1.0f;
    t = MaxKeepB(t, 0.0f);      // NaN too
    if (t == 1.0f || t == 0.0f) moving = false;
}

void PanelSlide::Toggle() {
    g_interface.PlayInterfaceSound(kSoundSling);
    direction = -direction;
    moving = true;
}

int PanelSlide::Offset() const {
    float eased;
    if (t < 0.5f) {
        eased = 2.0f * (t * t);
    } else {
        const float rest = 1.0f - t;
        eased = 1.0f - 2.0f * (rest * rest);
    }
    return static_cast<int>(travel * eased);
}

// --- Map -------------------------------------------------------------------------------------------------------------

void HudMap::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { LeftDown(event); }, [&] { LeftUp(event); }, [&] { HitRegion(event); }, [&] { Wheel(event); });
}

void HudMap::LeftDown(UiEvent& event) {
    const UiRegion* region = PressRegion(event);
    if (region != viewButtons[0] && region != viewButtons[1]) return;
    g_interface.PlayInterfaceSound(kSoundClick);
    view = region == viewButtons[0] ? 1 : 2;
    ShowView();
}

void HudMap::ShowView() {
    for (int i = 0; i < 2; ++i) SetTint(viewIcons[i], view == i + 1 ? kTintOn : kTintOff);
}

void HudMap::LeftUp(UiEvent& event) {
    if (!ReleaseMouse()) return;
    if (view != 0) {
        // A view button was held: the zoom stops and both buttons go dim (the event is not marked used).
        view = 0;
        ShowView();
        return;
    }
    const UiRegion* region = HitRegion(event);
    if (region == slingButton) {
        slide.Toggle();
    } else if (region == roundMap || region == mapSquare) {
        // The world point under the click.
        Point at = screen_layout::FromScreen(event.at);
        float right = static_cast<float>(at.x - (kMapCentreX + originX)) / halfSide;
        float down = static_cast<float>(at.y - kMapCentreY) / halfSide;
        if (region == roundMap) {
            right /= zoom;
            down /= zoom;
        }
        const float worldX = right / scale + centre.x;
        const float worldY = centre.y - down / scale;
        if (worldX > 0.0f && worldX < worldWidth && worldY > 0.0f && worldY < worldHeight) {
            // The camera stops following its unit and looks there.
            Camera* camera = &g_camera;
            camera->flags &= ~kCameraFollows;
            camera->SetTargetId(-1);
            camera->SetFocus(WithZ(Vector2{worldX, worldY}, camera->focus.z));
        }
    } else if (region == northButton) {
        FaceNorth();
    }
}

void HudMap::FaceNorth() {
    g_interface.PlayInterfaceSound(kSoundClick);
    g_camera.SetHeading(0.0f);
    northTime = 0.5f;
}

void HudMap::BuildMarkers() {
    BaseManager* objectManager = g_clientWorld.objectManager.get();
    // The local view: the ids of the units shown on the map (partyUnitIds) and, in multiplayer, of the other
    // players' units (otherUnitIds).
    PlayerClient* localView = g_clientWorld.LocalView();
    int unitCount = localView ? lib::Count(localView->partyUnitIds) : 0;
    LightFactory* lightFactory = g_clientWorld.lightFactory.get();
    const std::vector<int>& marked = lightFactory->litIds;
    const int markedCount = static_cast<int>(marked.size());
    int exitCount = static_cast<int>(std::ssize(g_clientWorld.exits));
    int playerCount = IsMultiplayerClient() ? lib::Count(localView->otherUnitIds) : 0;

    int quads = markedCount + playerCount + exitCount;   // six indices each
    markers->SetQuadCount(quads + unitCount);
    markers->SetVertexCount((quads + unitCount) * 4);
    markers->SetIndexCount((unitCount + quads * 2) * 3);

    MarkerWriter out(markers, this);

    for (int i = 0; i < unitCount; ++i) {
        out.BeginUnit();
        Unit* unit = UnitWithId(objectManager, localView->partyUnitIds[i]);
        if (!unit || unit->IsDead()) {
            out.Hide();
            continue;
        }
        float x = out.MapX(unit->localPosition.x) * zoom;
        float y = out.MapY(unit->localPosition.y) * zoom;
        if (LengthSquared2D(y, x) > kUnitRadius2) {
            out.Hide();
            continue;
        }
        PutArrow(out.Block(), unit, x, y);
        std::int16_t picture = UnitFrame(unit);
        BattleScreen* currentOwner = owner;
        for (const Unit* member : currentOwner->party) {
            if (member == unit) {
                picture = kFrameParty;
                break;
            }
        }
        out.Show(picture);
    }

    auto node = marked.begin();
    for (int i = 0; i < markedCount; ++i, ++node) {
        if (node == marked.end()) break;
        out.BeginQuad();
        Light* light = lightFactory->LightWithId(static_cast<unsigned>(*node));
        float x, y;
        if (light && out.Place(light->localPosition.x, light->localPosition.y, &x, &y)) {
            out.PutSquare(x, y);
            out.Show(kFrameMarked);
        } else {
            out.Hide();
        }
    }

    // The zone exits that lead somewhere: squares. (The exit record is copied and the target read from
    // the copy, which shares the string.)
    WorldClient* world = &g_clientWorld;
    for (int i = 0; i < exitCount; ++i) {
        out.BeginQuad();
        float x, y;
        if (text::CompareIgnoringCase(world->exits[i].target, "none") != 0) {
            Vector2 exitCentre = world->ExitCentre(i);
            if (out.Place(exitCentre.x, exitCentre.y, &x, &y)) {
                out.PutSquare(x, y);
                out.Show(kFrameNeutral);
                continue;
            }
        }
        out.Hide();
    }

    if (IsMultiplayerClient()) {
        for (int i = 0; i < playerCount; ++i) {
            out.BeginQuad();
            Unit* unit = UnitWithId(objectManager, localView->otherUnitIds[i]);
            float x, y;
            if (unit && out.Place(unit->localPosition.x, unit->localPosition.y, &x, &y)) {
                out.PutDiamond(x, y);
                out.Show(kFrameParty);
            } else {
                out.Hide();
            }
        }
    }

    markers->RefreshBuffers();
}

void HudMap::UpdateView() {
    if (const TransformObject* camera = g_clientWorld.camera) {
        centre = XY(g_camera.focus);
        facing = XY(camera->localRotation.AxisZ());
    }
    float length2 = LengthSquared2D(facing);
    if (length2 >= kTinyLength2) {
        facing = facing / std::sqrt(length2);
    } else {
        facing = {0.0f, 1.0f};
    }
    float angle = lib::Atan2(-facing.x, -facing.y);
    Vector3 up = {0.0f, 0.0f, 1.0f};
    Quaternion turn = Quaternion::AxisAngle(up, angle);
    needle->localRotation = turn;

    std::vector<Vector2>& points = visibleArea;
    int count = lib::Count(viewPolygon);
    points.resize(count);   // the original inlines the same code
    for (int i = 0; i < count; ++i) {
        const Vector2& v = viewPolygon[i];
        float x = v.x / zoom;
        float y = v.y / zoom;
        points[i].x = x / scale + centre.x;
        points[i].y = centre.y - y / scale;
    }

    // Clipped to the world rectangle: x >= 0, y >= 0, x <= width, y <= height.
    ClipPass(points,
             [](const Vector2& p, const Vector2& c, Vector2* out) {
                 if (!Crosses(p.x, c.x, 0.0f)) return false;
                 out->x = 0.0f;
                 out->y = p.y - (c.y - p.y) * p.x / (c.x - p.x);
                 return true;
             },
             [](const Vector2& v) {
                 return v.x < 0.0f;
             });
    ClipPass(points,
             [](const Vector2& p, const Vector2& c, Vector2* out) {
                 if (!Crosses(p.y, c.y, 0.0f)) return false;
                 out->x = p.x - (c.x - p.x) * p.y / (c.y - p.y);
                 out->y = 0.0f;
                 return true;
             },
             [](const Vector2& v) {
                 return v.y < 0.0f;
             });
    float width = worldWidth;
    float height = worldHeight;
    ClipPass(points,
             [width](const Vector2& p, const Vector2& c, Vector2* out) {
                 if (!Crosses(p.x, c.x, width)) return false;
                 out->x = width;
                 out->y = (width - p.x) * (c.y - p.y) / (c.x - p.x) + p.y;
                 return true;
             },
             [width](const Vector2& v) { return v.x > width; });
    ClipPass(points,
             [height](const Vector2& p, const Vector2& c, Vector2* out) {
                 if (!Crosses(p.y, c.y, height)) return false;
                 out->x = (height - p.y) * (c.x - p.x) / (c.y - p.y) + p.x;
                 out->y = height;
                 return true;
             },
             [height](const Vector2& v) { return v.y > height; });

    // The visible-area mesh: a fan over the clipped polygon, textured by world position.
    count = lib::Count(points);
    visibleDisc->SetVertexCount(count);
    for (int i = 0; i < count; ++i) {
        visibleDisc->corners[i].position = static_cast<std::uint16_t>(i);
        visibleDisc->corners[i].texCoord = static_cast<std::uint16_t>(i);
    }
    visibleDisc->SetUvCount(count);
    for (int i = 0; i < count; ++i) {
        visibleDisc->texCoords[i].u = points[i].x / textureSize;
        visibleDisc->texCoords[i].v = points[i].y / textureSize;
    }
    if (count > 2) {
        visibleDisc->SetIndexCount((count - 2) * 3);
        for (int k = 0; k < count - 2; ++k) {
            visibleDisc->indices[k * 3] = 0;
            visibleDisc->indices[k * 3 + 1] = static_cast<std::uint16_t>(k + 2);
            visibleDisc->indices[k * 3 + 2] = static_cast<std::uint16_t>(k + 1);
        }
    } else {
        visibleDisc->SetIndexCount(0);
    }
    // Positions go in blocks of four: the polygon is padded with copies of its first point.
    int quads = count / 4;
    if (count % 4 != 0) {
        ++quads;
        Vector2 first = points[0];
        for (int k = 4 - count % 4; k > 0; --k) {
            int size = lib::Count(points);
            points.resize(size + 1);
            points[size] = first;
        }
    }
    visibleDisc->SetQuadCount(quads);
    for (int q = 0; q < quads; ++q) {
        for (int k = 0; k < 4; ++k) {
            const Vector2& v = points[q * 4 + k];
            float x = (v.x - centre.x) * scale;
            float y = -((v.y - centre.y) * scale);
            visibleDisc->vertices[q].x()[k] = x * zoom;
            visibleDisc->vertices[q].y()[k] = y * zoom;
        }
    }
    visibleDisc->RefreshBuffers();

    float left = -centre.x * scale;
    float top = -(-centre.y * scale);
    float right = (worldWidth - centre.x) * scale;
    float bottom = -((worldHeight - centre.y) * scale);
    left = MaxKeepB(left, -1.0f);
    if (top > 1.0f) top = 1.0f;
    if (right > 1.0f) right = 1.0f;
    bottom = MaxKeepB(bottom, -1.0f);
    if (left >= right || bottom >= top) {
        bounds->visible = false;
        return;
    }
    bounds->visible = true;
    VertexPacket& b = bounds->vertices[0];
    b.x()[0] = left;
    b.y()[0] = bottom;
    b.x()[1] = left;
    b.y()[1] = top;
    b.x()[2] = right;
    b.y()[2] = bottom;
    b.x()[3] = right;
    b.y()[3] = top;
    float u0 = (left / scale + centre.x) / textureSize;
    float vTop = (centre.y - top / scale) / textureSize;
    float u1 = (right / scale + centre.x) / textureSize;
    float vBottom = (centre.y - bottom / scale) / textureSize;
    TexCoord* uv = bounds->texCoords.data();
    uv[0].u = u0;
    uv[0].v = vBottom;
    uv[1].u = u0;
    uv[1].v = vTop;
    uv[2].u = u1;
    uv[2].v = vBottom;
    uv[3].u = u1;
    uv[3].v = vTop;
}

void HudMap::Wheel(UiEvent& event) {
    if (HitRegion(event) != slingButton) return;
    float newZoom = WheelZoomFactor(event.delta) * zoom;
    zoom = newZoom;
    if (newZoom > zoomMax) zoom = zoomMax;
    zoom = MaxKeepB(zoom, 1.0f);
}

void HudMap::Layout() {
    const int offset = slide.Offset();
    const float slideOffset = static_cast<float>(offset);
    const float middle = slideOffset + kMapMiddleX + originX;
    const float buttons = slideOffset + kMapButtonsX + originX;
    bounds->Place(middle, 80.0f, 18.0f);
    visibleDisc->Place(middle, 80.0f, 14.0f);
    markers->Place(middle, 80.0f, 12.0f);
    mapBack->Place(middle, 80.0f, 20.0f);
    buttonsBack->Place(buttons, 80.0f, 20.0f);
    viewIcons[0]->Place(buttons, 115.0f, 10.0f);
    viewIcons[1]->Place(buttons, 145.0f, 10.0f);
    ring->Place(middle, 80.0f, 10.0f);
    frame->Place(slideOffset + kMapFrameX + originX, 80.0f, 10.0f);
    needle->Place(slideOffset + kMapNeedleX + originX, 84.0f, kSlingDepthBits);
    northIcon->Place(buttons, 85.0f, 10.0f);
    surfaces[0]->MoveTo({offset + 700 + originX, 80});
    MoveRegions(this, slingButton, offset);
}

void HudMap::Update() {
    // The zoom runs towards the chosen view's end: up to zoomMax (view 1) or down to 1 (view 2).
    if (view == 1) {
        const float newZoom = lib::Pow(kZoomRate, UiContainer::FrameTime()) * zoom;
        zoom = newZoom;
        if (newZoom > zoomMax) zoom = zoomMax;
    }
    if (view == 2) {
        const float newZoom = zoom / lib::Pow(kZoomRate, UiContainer::FrameTime());
        zoom = newZoom;
        if (newZoom < 1.0f) zoom = 1.0f;
    }
    UpdateView();
    BuildMarkers();

    if (slide.moving) {
        slide.Step();
        Layout();
        if (slide.direction == 1.0f)
            PlaceSling(slingArrow, kHudQuarterTurn, 713.0f + originX);
        else
            PlaceSling(slingArrow, -kHudQuarterTurn, 850.0f + originX);
    }
    g_interface.hudPanelsIn[2] = slide.direction > 0.0f ? 1 : 0;

    if (northTime > 0.0f) northTime = northTime - UiContainer::FrameTime();
    SetTint(northIcon, northTime > 0.0f ? 1.0f : 0.5f);
}

void HudMap::Setup(float newZoom, bool slid) {
    originX = screen_layout::EdgeShift();
    rect = Rect{615 + originX, 0, 800 + originX, 165};
    halfSide = 80.0f;

    // The world's extent and the map's picture (referenced object 1), from the zone's record.
    if (const MapRecord* zone = LocalZone()) {
        worldWidth = zone->sizeX * kZoneUnit;
        worldHeight = zone->sizeY * kZoneUnit;
        LoadTexture(zone->picture, TextureKind::Interface);
    } else {
        worldWidth = 32.0f;
        worldHeight = 32.0f;
        LoadTexture(kDefaultPicture, TextureKind::Interface);
    }
    textureSize = worldWidth < worldHeight ? worldHeight : worldWidth;
    scale = kMapSide / textureSize;
    zoomMax = textureSize * kZoomRange;
    zoom = zoomMax < newZoom ? zoomMax : newZoom;
    centre = {0.0f, 0.0f};
    facing = {0.0f, 1.0f};
    slide.t = slid ? 1.0f : 0.0f;
    slide.moving = true;
    slide.travel = kMapTravel;
    slide.direction = slid ? 1.0f : -1.0f;

    slingButton = AddRegion();
    slingButton->SetRect(780 + originX, 0, 800 + originX, 30);
    slingButton->SetTip(0x283C, 0x34);
    // The round map's outline (a point whose product falls just short of a whole pixel may truncate one
    // pixel lower than the original's, see OnArc).
    roundMap = AddRegion();
    for (int i = 0; i < kOutlinePoints; ++i) {
        const float angle = (i * kPi) * kOutlineStep;
        const float down = lib::Cos(angle) * kOutlineRadius;
        const int y = static_cast<int>((down + kOutlineCentreY));
        const float across = lib::Sin(angle) * kOutlineRadius;
        const int x = static_cast<int>((kOutlineCentreX - across));
        roundMap->AddPoint(x + originX, y);
    }
    mapSquare = AddRegion();
    mapSquare->SetRect(620 + originX, 0, 780 + originX, 160);
    viewButtons[0] = AddRegion();
    viewButtons[0]->SetRect(780 + originX, 100, 800 + originX, 130);
    viewButtons[0]->SetTip(0x283D);
    viewButtons[1] = AddRegion();
    viewButtons[1]->SetRect(780 + originX, 130, 800 + originX, 160);
    viewButtons[1]->SetTip(0x283E);
    northButton = AddRegion();
    northButton->SetRect(780 + originX, 70, 800 + originX, 100);
    northButton->SetTip(0x283F, 0xC);
    AddRegion()->SetRect(615 + originX, 0, 800 + originX, 165);

    bounds = AddBlankSprite();
    bounds->SetRectangle(18.0f, kSpriteScale, 620.0f + originX, 0.0f, 780.0f + originX, 160.0f, 0, 0, 256, 256);
    bounds->texture = objects[1];
    bounds->tint[0] = bounds->tint[1] = bounds->tint[2] = 0.8f;
    bounds->SetScale((kDepthUnit * halfSide) * kBoundsScale);
    visibleDisc = AddBlankSprite();
    visibleDisc->Disc(14.0f, 80.0f, 700.0f + originX, 80.0f, 64.0f, kOutlinePoints, 0x80, 0x80, 0x80);
    visibleDisc->texture = objects[1];
    visibleDisc->tint[0] = visibleDisc->tint[1] = visibleDisc->tint[2] = 1.0f;
    markers = AddBlankSprite();
    markers->Place(700.0f + originX, 80.0f, 12.0f);
    markers->SetScale((kDepthUnit * halfSide) * kMarkersScale);
    markers->ResetColours();
    markers->SetUvCount(4);
    const TexCoord markerTexels[4] = {
        {0.9375f, 0.984375f}, {0.953125f, 0.984375f}, {0.96875f, 0.984375f}, {0.984375f, 0.984375f}};
    for (int i = 0; i < 4; ++i) markers->texCoords[i] = markerTexels[i];
    mapBack = AddBlankSprite();
    mapBack->SetRectangle(20.0f, kSpriteScale, 620.0f + originX, 0.0f, 780.0f + originX, 160.0f, 0, 0, 256, 256);
    SetPlain(mapBack, 0x50000000);
    buttonsBack = AddBlankSprite();
    buttonsBack->SetRectangle(20.0f, kSpriteScale, 780.0f + originX, 0.0f, 800.0f + originX, 160.0f, 0, 0, 256, 256);
    SetPlain(buttonsBack, 0xB4000000);
    slingArrow = AddSprite(kArrowSprite);
    slingArrow->textureOffsetU = 0.22265625f;
    slingArrow->textureOffsetV = 0.140625f;
    PlaceSling(slingArrow, kPi * kSlingQuarter, 850.0f + originX);
    slingArrow->SetScale(0.8f);
    viewIcons[0] = AddBlankSprite();
    viewIcons[0]->SetRectangle(10.0f, 80.0f, 780.0f + originX, 100.0f, 800.0f + originX, 130.0f, 0x19, 0x3E, 0x2D,
                               0x5C);
    viewIcons[1] = AddBlankSprite();
    viewIcons[1]->SetRectangle(10.0f, 80.0f, 780.0f + originX, 130.0f, 800.0f + originX, 160.0f, 3, 0x3E, 0x17, 0x5C);
    ring = AddBlankSprite();
    // At the layout's 700 until the layout below places it (the ring's shape takes no origin).
    ring->Ring(10.0f, 60.0f, 700.0f, 80.0f, 66.0f, kOutlinePoints, 3.0f, 0x1B, 0x6B, 0x5B, 0x70);
    frame = AddBlankSprite();
    frame->SetFrame(10.0f, 80.0f, 615.0f + originX, -5.0f, 805.0f + originX, 165.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);
    needle = AddSprite(kArrowSprite);
    needle->textureOffsetU = 0.22265625f;
    needle->textureOffsetV = 0.140625f;
    needle->Place(900.0f + originX, 80.0f, kSlingDepthBits);
    needle->SetScale(0.8f);
    northIcon = AddBlankSprite();
    northIcon->SetRectangle(10.0f, 80.0f, 780.0f + originX, 70.0f, 800.0f + originX, 100.0f, 0x22, 0x76, 0x36, 0x94);

    UiSurface* dot = AddSurface(std::make_unique<UiSurface>(700, 80, 705, 85, SurfaceBlend::Keyed, true));
    dot->PaintWith([](UiSurface& out) { out.Dot({0, 0}, 0xFFFFFF); });

    for (int block = 0; block < kOutlinePoints / 4; ++block) {
        const VertexPacket& quad = visibleDisc->vertices[block];
        for (int corner = 0; corner < 4; ++corner) viewPolygon.push_back(Vector2{quad.x()[corner], quad.y()[corner]});
    }
    UpdateView();
    BuildMarkers();

    view = 0;
    for (int i = 0; i < 2; ++i) SetTint(viewIcons[i], view == i + 1 ? 1.0f : 0.5f);
    Layout();
    northTime = 0.0f;
}

// --- Unit panel ------------------------------------------------------------------------------------------------------

void HudUnitPanel::OnEvent(UiEvent& event) {
    DispatchMouse(event, [&] { PressRegion(event); }, [&] { LeftUp(event); }, [&] { HitRegion(event); }, [&] { HitRegion(event); });
}

void HudUnitPanel::LeftUp(UiEvent& event) {
    const UiRegion* region = PressedRegion(event);
    if (ToggleSling(region, slingButton, slide)) return;
    if (const int part = RegionIndex(partButtons, region); part >= 0) {
        g_interface.PlayInterfaceSound(kSoundClick);
        bodyPart = part;
        ShowBodyPart();
    }
}

void HudUnitPanel::ShowHealth(Unit* unit) {
    healthFrame->visible = true;
    float ratio;
    if (unit->packedVitals.maxHitPoints != 0 && unit->packedVitals.hitPoints > 0 && BarRatio(unit->packedVitals.hitPoints, unit->packedVitals.maxHitPoints, &ratio))
        FillBar(healthBar, ratio, 100.0f, 0.396484375f, 0.376953125f);
    if (!InParty(owner, unit)) return;
    manaFrame->visible = true;
    if (unit->packedVitals.maxMana != 0 && unit->packedVitals.mana != 0 && BarRatio(unit->packedVitals.mana, unit->packedVitals.maxMana, &ratio))
        FillBar(manaBar, ratio, 100.0f, 0.369140625f, 0.349609375f);
}

void HudUnitPanel::PaintHealth(UiSurface& surface, Unit* unit) {
    std::string health = std::format("{}", unit->packedVitals.hitPoints > 0 ? unit->packedVitals.hitPoints : 0);
    surface.DrawTextIn(health, Rect{130, 185, 160, 200}, kPanelText);
    if (!InParty(owner, unit)) return;
    std::string mana = std::format("{}", static_cast<int>(unit->packedVitals.mana));
    surface.DrawTextIn(mana, Rect{130, 200, 160, 215}, kPanelText);
}

void HudUnitPanel::ShowParts(Unit* unit) {
    for (int i = 0; i < 6; ++i) {
        const UnitParamsSlot& part = unit->params.slots[i];
        if (part.kind == 0) continue;
        bodyFrames[i]->visible = true;
        UiMesh* bar = bodyBars[i];
        float ratio = 0.0f;
        if (part.kind == 3 && part.maximum != 0 && part.current > 0 &&
            BarRatio(part.current, part.maximum, &ratio))
            FillBar(bar, ratio, 50.0f, 0.505859375f, 0.486328125f);
        else
            bar->visible = false;
        // The bar's colour row: green above 2/3, yellow above 1/3, else red.
        bar->textureOffsetV = ratio > 2.0f / 3.0f ? 0.0f : ratio > 1.0f / 3.0f ? -0.02734375f : -0.0546875f;
    }
}

void HudUnitPanel::PaintParts(UiSurface& surface, Unit* unit) {
    // Label positions of the six parts.
    static constexpr int kLabelX[6] = {50, 50, 95, 5, 95, 5};
    static constexpr int kLabelY[6] = {35, 105, 70, 70, 140, 140};
    static const char* const kPartNames[4] = {"skull", "torso", "leg", "arm"};

    for (int i = 0; i < 6; ++i) {
        const UnitParamsSlot& part = unit->params.slots[i];
        if (part.kind == 0) continue;
        std::string name = unit->GetPrototypeModel()->slots[i].levelName;
        for (int k = 0; k < 4; ++k) {
            if (str::Compare(name,
                                kPartNames[k]) == 0) {
                name = partLabels[k];
                break;
            }
        }
        surface.DrawTextIn(name, Rect{kLabelX[i], kLabelY[i], kLabelX[i] + 60, kLabelY[i] + 15},
                           TextStyle{0, part.kind == 1 ? kRed : kUiTextColour, render::text::Format::Center});
    }
}

void HudUnitPanel::PaintEffects(UiSurface& surface, Unit* unit) {
    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    std::string text;
    Rect line{40, 35, 155, 50};
    // The unit's effects are the map object's records.
    const MapEffect* effects = unit->effects.data();
    for (int i = 0; i < std::ssize(unit->effects) && i < kEffectPlaces; ++i) {
        text.clear();
        if (const itemdb::SpellRecord* spell = itemdb::SpellAt(effects[i].type)) {
            // The texts' section "spell <code>": its first line is the spell's name.
            if (clientWorldTexts->TextManager::Open(std::string("spell ") + spell->code)) text = clientWorldTexts->ReadLine(true);
        }
        surface.DrawTextIn(text, line, kPanelText);
        line.Offset(0, kLineHeight);
        surface.DrawTextIn(std::format("{}", effects[i].ticksLeft / 15), line, kPanelText);
        line.Offset(0, kLineHeight);
    }
}

void HudUnitPanel::ShowEffects(Unit* unit) {
    constexpr int kPlaces = kEffectPlaces;
    constexpr int kIconSize = 64;
    // Where each kept picture sits in the panel's 256 x 256 icon texture (its object 1).
    static constexpr int kPlaceX[kPlaces] = {0, 0, 64, 64, 64, 64};
    static constexpr int kPlaceY[kPlaces] = {128, 192, 0, 64, 128, 192};

    int icons[kPlaces], places[kPlaces];
    bool taken[kPlaces] = {};
    for (int i = 0; i < kPlaces; ++i) icons[i] = places[i] = -1;

    if (std::ssize(unit->effects) > kPlaces) moreMark->visible = true;
    const MapEffect* effects = unit->effects.data();
    for (int i = 0; i < std::ssize(unit->effects) && i < kPlaces; ++i) {
        if (const itemdb::SpellRecord* spell = itemdb::SpellAt(effects[i].type)) icons[i] = static_cast<int>(spell->icon);
    }

    // The pictures already in the texture keep their places (the last one that matches).
    for (int i = 0; i < kPlaces; ++i) {
        for (int place = 0; place < kPlaces; ++place) {
            if (icons[i] >= 0 && icons[i] == effectIcons[place]) {
                taken[place] = true;
                places[i] = place;
            }
        }
    }
    // The others go into the free places in order; the list ends when there is none left.
    std::string picture;
    int free = 0;
    for (int i = 0; i < kPlaces; ++i) {
        if (icons[i] < 0) continue;
        bool fresh = false;
        if (places[i] < 0) {
            while (free < kPlaces && taken[free]) ++free;
            if (free == kPlaces) break;
            taken[free] = true;
            places[i] = free;
            fresh = true;
        }
        int x = kPlaceX[places[i]];
        int y = kPlaceY[places[i]];
        if (fresh) {
            picture = std::format("spell{:04}", icons[i]);
            CopyImage(objects[1], picture, kIconSize, kIconSize, kIconSize, kIconSize, x, y);
            effectIcons[free] = icons[i];
        }
        UiMesh* icon = effectSprites[i];
        icon->visible = true;
        icon->textureOffsetU = x * (1.0f / 256.0f);
        icon->textureOffsetV = (y + kIconSize) * (1.0f / 256.0f);
    }
}

void HudUnitPanel::ShowBodyPart() {
    for (int i = 0; i < 4; ++i) {
        float tint = bodyPart == i ? 1.0f : 0.5f;
        UiMesh* button = partIcons[i];
        button->tint[0] = tint;
        button->tint[1] = tint;
        button->tint[2] = tint;
    }
}

void HudUnitPanel::Layout() {
    const int offset = slide.Offset();
    const float slideOffset = static_cast<float>(offset);
    const float edgeX = kUnitEdgeX - slideOffset + originX;
    const float middle = kUnitMiddleX - slideOffset + originX;
    background->Place(kUnitBackX - slideOffset + originX, 110.0f, 60.0f);
    edge->Place(edgeX, 110.0f, 20.0f);
    partIcons[0]->Place(edgeX, 115.0f, 10.0f);
    partIcons[1]->Place(edgeX, 145.0f, 10.0f);
    partIcons[2]->Place(edgeX, 175.0f, 10.0f);
    partIcons[3]->Place(edgeX, 205.0f, 10.0f);
    frame->Place(middle, 110.0f, 10.0f);
    moreMark->Place(kUnitMoreX - slideOffset + originX, 212.0f, 4.0f);
    healthBar->Place(middle, 192.5f, 4.0f);
    healthFrame->Place(middle, 192.5f, 5.0f);
    manaBar->Place(middle, 207.5f, 4.0f);
    manaFrame->Place(middle, 207.5f, 5.0f);
    for (int i = 0; i < kParts; ++i) {
        const float y = kPartY[i] + kUnitPartDown;
        const float x = (kPartX[i] + 50) - slideOffset + originX;
        bodyBars[i]->Place(x, y, 4.0f);
        bodyFrames[i]->Place(x, y, 5.0f);
    }
    for (int i = 0; i < kEffects; ++i)
        effectSprites[i]->Place(kUnitEffectX - slideOffset + originX, static_cast<float>(50 + 30 * i), 5.0f);
    surfaces[0]->MoveTo({20 + originX - offset, 0});
    MoveRegions(this, slingButton, -offset);
}

void HudUnitPanel::Update() {
    if (slide.moving) {
        slide.Step();
        Layout();
        if (slide.direction == -1.0f)
            PlaceSling(slingArrow, kHudQuarterTurn, -50.0f + originX);
        else
            PlaceSling(slingArrow, -kHudQuarterTurn, 87.0f + originX);
    }
    if (hoverUnit && hoverUnit->IsDead() != 0) hoverUnit = nullptr;
    g_interface.hudPanelsIn[0] = slide.direction > 0.0f ? 1 : 0;
    g_interface.hudBodyPart = bodyPart;
}

void HudUnitPanel::Setup(int newBodyPart, bool slid) {
    slide.t = slid ? 1.0f : 0.0f;
    slide.direction = slid ? 1.0f : -1.0f;
    // The original's rectangle moved to the window's left edge. addon wrote (-133, 0)-(52, 225) and `moving` 0
    // whatever the margin: its rectangle is this one at 16:9 but a unit to the right, and without the first update's
    // slide a panel set up slid out kept its sling button where it was made, turned the other way.
    originX = -screen_layout::EdgeShift();
    rect = Rect{originX, 0, 185 + originX, 225};
    slide.moving = true;
    slide.travel = kUnitTravel;

    slingButton = AddRegion();
    slingButton->SetRect(originX, 0, 20 + originX, 30);
    slingButton->SetTip(0x2774);
    for (int i = 0; i < 4; ++i) {
        partButtons[i] = AddRegion();
        partButtons[i]->SetRect(originX, 100 + 30 * i, 20 + originX, 130 + 30 * i);
        partButtons[i]->SetTip(0x2775 + i, 0x2E + i);
    }
    AddRegion()->SetRect(originX, 0, 185 + originX, 225);

    background = AddBlankSprite();
    background->SetRectangle(60.0f, kSpriteScale, 20.0f + originX, 0.0f, 180.0f + originX, 220.0f, 0, 0, 256, 256);
    background->texture = nullptr;
    background->flags |= FigureMeshFlag::Flat;
    background->flatColour = 0x50000000;
    background->visible = false;
    edge = AddBlankSprite();
    edge->SetRectangle(20.0f, kSpriteScale, 0.0f + originX, 0.0f, 20.0f + originX, 220.0f, 0, 0, 256, 256);
    edge->texture = nullptr;
    edge->flags |= FigureMeshFlag::Flat;
    edge->flatColour = 0xB4000000;
    slingArrow = AddSprite(kArrowSprite);
    slingArrow->textureOffsetU = 0.22265625f;
    slingArrow->textureOffsetV = 0.140625f;
    PlaceSling(slingArrow, kHudQuarterTurn, -50.0f + originX);
    slingArrow->SetScale(0.8f);
    partIcons[0] = AddBlankSprite();
    partIcons[0]->SetRectangle(10.0f, 80.0f, 0.0f + originX, 100.0f, 20.0f + originX, 130.0f, 0x2F, 0x3E, 0x43, 0x5C);
    partIcons[1] = AddBlankSprite();
    partIcons[1]->SetRectangle(10.0f, 80.0f, 0.0f + originX, 130.0f, 20.0f + originX, 160.0f, 0x45, 0x3E, 0x59, 0x5C);
    partIcons[2] = AddBlankSprite();
    partIcons[2]->SetRectangle(10.0f, 80.0f, 0.0f + originX, 160.0f, 20.0f + originX, 190.0f, 3, 0x5E, 0x17, 0x7C);
    partIcons[3] = AddBlankSprite();
    partIcons[3]->SetRectangle(10.0f, 80.0f, 0.0f + originX, 190.0f, 20.0f + originX, 220.0f, 0xE9, 0x70, 0xFD, 0x8E);
    frame = AddBlankSprite();
    frame->SetFrame(10.0f, 80.0f, -5.0f + originX, -5.0f, 185.0f + originX, 225.0f, 5.0f, 4, 0x37, 0x5A, 0x3C);
    heightBar = AddBlankSprite();
    heightBar->SetGrid(4.0f, StripScale(), 20.0f + originX, 0.0f, 25.0f + originX, 220.0f, 0x55, 2, 0x5A, 0x2A, 1, 20);
    heightBar->visible = false;
    moreMark = AddBlankSprite();
    moreMark->SetRectangle(4.0f, kSpriteScale, 164.0f + originX, 204.0f, 180.0f + originX, 220.0f, 0xD3, 2, 0xE3, 0x12);
    moreMark->visible = false;
    healthBar = AddBlankSprite();
    healthBar->SetRectangle(4.0f, kSpriteScale, 40.0f + originX, 190.0f, 140.0f + originX, 195.0f, 0x40, 0x9A, 0xA4,
                            0x9F);
    healthFrame = AddBlankSprite();
    healthFrame->SetRectangle(5.0f, kSpriteScale, 35.0f + originX, 190.0f, 145.0f + originX, 195.0f, 0x3B, 0x93, 0xA9,
                              0x98);
    manaBar = AddBlankSprite();
    manaBar->SetRectangle(4.0f, kSpriteScale, 40.0f + originX, 205.0f, 140.0f + originX, 210.0f, 0x40, 0xA1, 0xA4,
                          0xA6);
    manaFrame = AddBlankSprite();
    manaFrame->SetRectangle(5.0f, kSpriteScale, 35.0f + originX, 205.0f, 145.0f + originX, 210.0f, 0x3B, 0x93, 0xA9,
                            0x98);
    // The body parts' bars at the tables' places.
    for (int i = 0; i < kParts; ++i) {
        const int x = kPartX[i] + originX;
        const int y = kPartY[i];
        bodyBars[i] = AddBlankSprite();
        bodyBars[i]->SetRectangle(4.0f, 50.0f, static_cast<float>(x + 25), static_cast<float>(y + 15),
                                  static_cast<float>(x + 75), static_cast<float>(y + 20), 0x40, 0x7E, 0x72, 0x83);
        bodyFrames[i] = AddBlankSprite();
        bodyFrames[i]->SetRectangle(5.0f, 50.0f, static_cast<float>(x + 20), static_cast<float>(y + 15),
                                    static_cast<float>(x + 80), static_cast<float>(y + 20), 0x3B, 0x77, 0x77, 0x7C);
    }
    for (int i = 0; i < kEffects; ++i) {
        const int top = 38 + 30 * i;
        effectSprites[i] = AddBlankSprite();
        effectSprites[i]->SetRectangle(5.0f, 30.0f, 28.0f + originX, static_cast<float>(top), 52.0f + originX,
                                       static_cast<float>(top + 24), 0, 0, 64, 64);
        effectSprites[i]->texture = objects[1];
    }

    UiSurface* panel = AddSurface(std::make_unique<UiSurface>(20, 0, 180, 220, SurfaceBlend::Keyed, true));
    panel->PaintWith([this](UiSurface& out) { PaintTexts(out); });

    bodyPart = newBodyPart;
    for (int i = 0; i < 4; ++i) SetTint(partIcons[i], bodyPart == i ? 1.0f : 0.5f);
    Layout();
    shownUnit = nullptr;
    hoverUnit = nullptr;

    TextManager* clientWorldTexts = g_clientWorld.texts.get();
    std::string text;
    std::string key;
    for (int i = 0; i < kInfoTexts; ++i) {
        key = str::Format(kInfoTextFormat, i);
        text.clear();
        text = clientWorldTexts->GetString(key);
        texts.push_back(text);
    }
    // A missing label keeps the text before it.
    for (const char* labelKey : kPartLabelKeys) {
        text = clientWorldTexts->GetString(labelKey);
        partLabels.push_back(text);
    }
    for (int& icon : effectIcons) icon = -1;
}

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
    const DamageLists lines = ItemDamageLists(description.data());
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
    surface.DrawWrappedText(strings[hoverUnit ? 1 : 0], nameRect, 2, TextStyle{0, kUiTextColour, render::text::Format::Center | render::text::Format::WordBreak});
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

void HudUnitPanel::DrawSprites(SceneRenderer* pass) {
    Quaternion savedOrientation = {1.0f, 0.0f, 0.0f, 0.0f};
    Vector3 savedPosition = {};
    heightBar->visible = false;
    moreMark->visible = false;
    // The background alone, before anything else.
    background->visible = true;
    background->Draw(pass);
    background->visible = false;

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

    // The bars and the effects' pictures show only what this frame's page sets.
    healthBar->visible = healthFrame->visible = manaBar->visible = manaFrame->visible = false;
    for (int i = 0; i < 6; ++i) {
        bodyBars[i]->visible = false;
        bodyFrames[i]->visible = false;
        effectSprites[i]->visible = false;
    }

    if (unit && unit->AsObject3D()->attached) {
        Object3D* model = unit->AsObject3D();
        const int offset = slide.Offset();
        const bool drawn = bodyPart != kInfoPage;
        if (drawn) {
            // The screen around the panel's model area (right of it, its sling strip, below it):
            // the depth buffer goes to 0 there, so the model is drawn inside the panel only.
            // WideScreen: without the margin.
            const Rect right = screen_layout::ToScreenNoMargin(Rect{180 - offset, 0, 400 - offset, 220});
            const Rect strip = screen_layout::ToScreenNoMargin(Rect{0, 0, 20 - offset, 220});
            const Rect below = screen_layout::ToScreenNoMargin(Rect{0, 220, 400 - offset, 300});
            Rect rects[3] = {right, below, strip};
            ClearUiDepth(0.0f, rects, 3);

            savedPosition = model->localPosition;
            savedOrientation = model->localRotation;
            unit->AdvanceTime(g_clientWorld.tick + g_clientWorld.tickFraction);

            // In front of the interface camera, at the layout's x = 100 less the slide, lowered so
            // that its height (in units of its size) stands a quarter of the view below the centre.
            const float x = ((100 + originX - offset) * kLayoutUnit - 1.0f) *
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
            // The height bar: the model's fifteen layout units, from the panel's bottom.
            const float barScale = kBarHeight / size;
            std::uint32_t bits;
            bits = std::bit_cast<std::uint32_t>(barScale);
            heightBar->SetScaleYBits(bits);
            const float barY = kPanelHeight - barScale * kPanelHeight;
            heightBar->Place(static_cast<float>(20 - offset) + originX, barY, 4.0f);
            heightBar->visible = true;
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

    DrawBase(pass);
}
