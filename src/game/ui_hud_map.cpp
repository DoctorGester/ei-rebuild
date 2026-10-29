// The battle HUD's minimap markers (HudMap::BuildMarkers): every frame the map panel
// rebuilds one mesh (its scene object 2) with a marker per party member and visible unit, per
// marked object, per zone exit and, in multiplayer, per other player. See docs/notes/ui.md.
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "ui.h"
#include "ui_float.h"
#include "ui_screens.h"

#include "base_manager.h"
#include "maths.h"
#include "player_client.h"
#include "spell.h"
#include "unit.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/str.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "camera.h"
#include "lib/text_case.h"
#include "lib/float_select.h"

#include <list>
#include <new>

namespace {

constexpr float kUnitRadius2 = 0.56f;    // units further out (squared) are not shown
constexpr double kTinyLength2 = 1e-6;
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

    // Map coordinates of a world point: x stays on the FPU stack, y is stored (after the negation; map y
    // grows downwards).
    double MapX(float worldX) const { return (D(worldX) - D(map->centreX)) * D(map->scale); }
    float MapY(float worldY) const { return static_cast<float>(-((D(worldY) - D(map->centreY)) * D(map->scale))); }
    double Zoom() const { return D(map->zoom); }

    // The place of a marked object, an exit or another player: the zoomed position when it lies
    // inside the inner circle, otherwise the unzoomed one when that lies on the rim (outside the
    // rim circle, inside the square); false when neither.
    bool Place(float worldX, float worldY, float* x, float* y) const {
        double mapX = MapX(worldX);
        float mapY = MapY(worldY);
        float zoomedX = static_cast<float>(mapX * Zoom());
        float zoomedY = static_cast<float>(D(mapY) * Zoom());
        EI_NAN_AUDIT(zoomedY, zoomedX);
        if (LengthSquared2DUnrounded<double>(zoomedY, zoomedX) <= kInnerRadius2) {
            *x = zoomedX;
            *y = zoomedY;
            return true;
        }
        float rimX = static_cast<float>(mapX);
        if (D(mapY) * D(mapY) + D(rimX) * D(rimX) < kRimRadius2) return false;
        if (AbsBits(rimX) > kRimExtent) return false;
        if (AbsBits(mapY) > kRimExtent) return false;
        *x = rimX;
        *y = mapY;
        return true;
    }

    void PutSquare(float x, float y) {
        VertexPacket& b = Block();
        float left = static_cast<float>(D(x) - kSquareHalf);
        float top = static_cast<float>(D(y) - kSquareHalf);
        float right = static_cast<float>(D(x) + kSquareHalf);
        float bottom = static_cast<float>(D(y) + kSquareHalf);
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
        b.y()[0] = static_cast<float>(D(y) - kArrowWing);
        b.x()[1] = static_cast<float>(D(x) + kArrowWing);
        b.y()[1] = y;
        b.x()[2] = static_cast<float>(D(x) - kArrowWing);
        b.y()[2] = y;
        b.x()[3] = x;
        b.y()[3] = static_cast<float>(D(y) + kArrowWing);
    }


private:
    void Next() {
        vertex += 4;
        ++entry;
    }

    UiMesh* mesh;
    const HudMap* map;
    int entry = 0;       // quad block
    int vertex = 0;      // first vertex of the entry (4 per entry)
    int indexPos = 0;    // next index
};

// Frame of a unit that is not in the party: hostile when both relation masks have the local
// side's bit. The special unit 0xA2C2A counts as hostile in zone gz20g whatever its relations.
std::int16_t UnitFrame(const CUnit* unit) {
    int side = g_clientWorld.LocalView()->player->playerId;
    if (unit->id == kSpecialUnitId) {
        if (str::Compare(LocalPlayerZone(), "gz20g") == 0) return kFrameHostile;
    }
    std::uint32_t bit = 1u << (side & 31);
    return (bit & unit->relationsB & unit->relationsA) != 0 ? kFrameHostile : kFrameNeutral;
}

// The arrow of a unit at map position (x, y) (already zoomed), pointing along the unit's
// facing: the tip twice (vertices 0 and 1), then the two back corners.
void PutArrow(VertexPacket& b, const CUnit* unit, float x, float y) {
    const Quaternion* rotation = &unit->localRotation;
    // The rotated Y axis is computed twice, x taken from the first, y from the second (the function is
    // pure).
    Vector3 first;
    rotation->AxisY(&first);
    float dirX = -first.x;
    Vector3 second;
    rotation->AxisY(&second);
    float dirY = second.y;

    double length2 = LengthSquared2DUnrounded<double>(dirY, dirX);
    double nx;
    double ny;
    if (length2 >= kTinyLength2) {
        float length = static_cast<float>(std::sqrt(length2));
        nx = D(dirX) / D(length);
        ny = D(dirY) / D(length);
    } else {
        nx = 0.0f;
        ny = 1.0f;
    }

    float tipX = static_cast<float>(nx * kArrowAhead + D(x));
    float tipY = static_cast<float>(ny * kArrowAhead + D(y));
    b.x()[0] = tipX;
    b.y()[0] = tipY;
    b.x()[1] = tipX;
    b.y()[1] = tipY;

    // The back corners: the direction turned by +-135 degrees (sums of the scaled components).
    float sy = static_cast<float>(ny * kDiagonal);
    float sx = static_cast<float>(nx * kDiagonal);
    float leftX = static_cast<float>(D(sy) - D(sx));
    double leftY = nx * -kDiagonal - D(sy);   // kept on the FPU stack
    b.x()[2] = static_cast<float>(D(leftX) * kArrowWing + D(x));
    b.y()[2] = static_cast<float>(leftY * kArrowWing + D(y));
    float rightY = static_cast<float>(D(sx) - D(sy));
    b.x()[3] = static_cast<float>(leftY * kArrowWing + D(x));
    b.y()[3] = static_cast<float>(D(rightY) * kArrowWing + D(y));
}


// Whether the edge from `prev` to `cur` crosses the line v = edge (coordinate values of the two points).
bool Crosses(float prev, float cur, float edge) {
    EI_NAN_AUDIT(prev, cur, edge);
    return (prev < edge && cur > edge) || (prev > edge && cur < edge);
}

// One Sutherland-Hodgman step in place: walking the polygon, the point where an edge crosses the line is
// inserted before the edge's end (which is then skipped); afterwards the points outside are removed.
template <typename Cut, typename Outside>
void ClipPass(std::vector<UiPointF>& points, Cut cut, Outside outside) {
    int count = std::ssize(points);
    for (int i = 0; i < count; ++i) {
        UiPointF cur = points[i];
        UiPointF prev = points[i == 0 ? count - 1 : i - 1];
        UiPointF cross;
        if (!cut(prev, cur, &cross)) continue;
        int size = std::ssize(points);
        if (i < size) {
            points.resize(size + 1);
            std::memmove(&points[i + 1], &points[i], (size - i) * sizeof(UiPointF));
        } else {
            points.resize(i + 1);
        }
        points[i] = cross;
        ++i;
        ++count;
    }
    for (int i = 0; i < count; ++i) {
        if (!outside(points[i])) continue;
        int tail = std::ssize(points) - i - 1;
        if (tail != 0) std::memmove(&points[i], &points[i + 1], tail * sizeof(UiPointF));
        points.pop_back();
        --i;
        --count;
    }
}

// 2.5^(delta / 1200): _CIpow of a double constant and delta * 0.00083333335f, left unrounded.
double WheelZoomFactor(int delta) {
    return libcrt::CiPow(2.5, static_cast<double>(delta) * 0.00083333335f);
}

} // namespace

void HudMap::BuildMarkers() {
    UiMesh* mesh = SceneMesh(2);
    BaseManager* objectManager = g_clientWorld.objectManager;
    // The local view: the ids of the units shown on the map (partyUnitIds) and, in multiplayer, of the other
    // players' units (otherUnitIds).
    PlayerClient* localView = g_clientWorld.LocalView();
    int unitCount = localView ? std::ssize(localView->partyUnitIds) : 0;
    LightFactory* markers = g_clientWorld.lightFactory;
    const std::list<int>& marked = markers->litIds;
    const int markedCount = static_cast<int>(marked.size());
    int exitCount = static_cast<int>(std::ssize(g_clientWorld.exits));
    int playerCount = IsMultiplayerClient() ? std::ssize(localView->otherUnitIds) : 0;

    int quads = markedCount + playerCount + exitCount;   // six indices each
    mesh->SetQuadCount(quads + unitCount);
    mesh->SetVertexCount((quads + unitCount) * 4);
    mesh->SetIndexCount((unitCount + quads * 2) * 3);

    MarkerWriter out(mesh, this);

    for (int i = 0; i < unitCount; ++i) {
        out.BeginUnit();
        CUnit* unit = UnitWithId(objectManager, localView->partyUnitIds[i]);
        if (!unit || unit->IsDead()) {
            out.Hide();
            continue;
        }
        double mapX = out.MapX(unit->localPosition.x);
        float mapY = out.MapY(unit->localPosition.y);
        float x = static_cast<float>(mapX * out.Zoom());
        double zoomedY = D(mapY) * out.Zoom();   // the original stores it but squares the register
        float y = static_cast<float>(zoomedY);
        if (zoomedY * D(y) + D(x) * D(x) > kUnitRadius2) {
            out.Hide();
            continue;
        }
        PutArrow(out.Block(), unit, x, y);
        std::int16_t frame = UnitFrame(unit);
        BattleScreen* currentOwner = owner;
        for (int k = 0; k < std::ssize(currentOwner->party); ++k) {
            if (currentOwner->party[k] == unit) {
                frame = kFrameParty;
                break;
            }
        }
        out.Show(frame);
    }

    auto node = marked.begin();
    for (int i = 0; i < markedCount; ++i, ++node) {
        if (node == marked.end()) break;
        out.BeginQuad();
        CLight* light = markers->LightWithId(static_cast<unsigned>(*node));
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
            float centre[2];
            world->ExitCentre(centre, i);
            if (out.Place(centre[0], centre[1], &x, &y)) {
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
            CUnit* unit = UnitWithId(objectManager, localView->otherUnitIds[i]);
            float x, y;
            if (unit && out.Place(unit->localPosition.x, unit->localPosition.y, &x, &y)) {
                out.PutDiamond(x, y);
                out.Show(kFrameParty);
            } else {
                out.Hide();
            }
        }
    }

    mesh->RefreshBuffers();
}

// --- The map's polygon clipping ----------------------------------------------------------------------------------

void HudMap::UpdateView() {
    if (const TransformObject* camera = g_clientWorld.camera) {
        centreX = g_camera.focus.x;
        centreY = g_camera.focus.y;
        Vector3 axis;
        camera->localRotation.AxisZ(&axis);
        facingX = axis.x;
        camera->localRotation.AxisZ(&axis);
        facingY = axis.y;
    }
    double length2 = LengthSquared2DUnrounded<double>(facingX, facingY);
    if (length2 >= kTinyLength2) {
        double length = std::sqrt(length2);
        facingX = static_cast<float>(D(facingX) / length);
        facingY = static_cast<float>(D(facingY) / length);
    } else {
        facingX = 0.0f;
        facingY = 1.0f;
    }
    float angle = static_cast<float>(x87::fpatan(-D(facingX), -D(facingY)));
    Vector3 up = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    turn.SetAxisAngle(up, angle);
    SceneMesh(10)->localRotation = turn;

    auto* area = SceneMesh(1);
    std::vector<UiPointF>& points = visibleArea;
    int count = std::ssize(viewPolygon);
    points.resize(count);   // the original inlines the same code
    for (int i = 0; i < count; ++i) {
        const UiPointF& v = viewPolygon[i];
        double x = D(v.x) / D(zoom);
        float y = static_cast<float>(D(v.y) / D(zoom));
        points[i].x = static_cast<float>(x / D(scale) + D(centreX));
        points[i].y = static_cast<float>(D(centreY) - D(y) / D(scale));
    }

    // Clipped to the world rectangle: x >= 0, y >= 0, x <= width, y <= height.
    ClipPass(points,
             [](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.x, c.x, 0.0f)) return false;
                 out->x = 0.0f;
                 out->y = static_cast<float>(D(p.y) - (D(c.y) - D(p.y)) * D(p.x) / (D(c.x) - D(p.x)));
                 return true;
             },
             [](const UiPointF& v) {
                 EI_NAN_AUDIT(v.x);
                 return v.x < 0.0f;
             });
    ClipPass(points,
             [](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.y, c.y, 0.0f)) return false;
                 out->x = static_cast<float>(D(p.x) - (D(c.x) - D(p.x)) * D(p.y) / (D(c.y) - D(p.y)));
                 out->y = 0.0f;
                 return true;
             },
             [](const UiPointF& v) {
                 EI_NAN_AUDIT(v.y);
                 return v.y < 0.0f;
             });
    float width = worldWidth;
    float height = worldHeight;
    ClipPass(points,
             [width](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.x, c.x, width)) return false;
                 out->x = width;
                 out->y = static_cast<float>((D(width) - D(p.x)) * (D(c.y) - D(p.y)) / (D(c.x) - D(p.x)) + D(p.y));
                 return true;
             },
             [width](const UiPointF& v) { return v.x > width; });
    ClipPass(points,
             [height](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.y, c.y, height)) return false;
                 out->x = static_cast<float>((D(height) - D(p.y)) * (D(c.x) - D(p.x)) / (D(c.y) - D(p.y)) + D(p.x));
                 out->y = height;
                 return true;
             },
             [height](const UiPointF& v) { return v.y > height; });

    // The visible-area mesh: a fan over the clipped polygon, textured by world position.
    count = std::ssize(points);
    area->SetVertexCount(count);
    for (int i = 0; i < count; ++i) {
        area->corners[i].position = static_cast<std::uint16_t>(i);
        area->corners[i].texCoord = static_cast<std::uint16_t>(i);
    }
    area->SetUvCount(count);
    for (int i = 0; i < count; ++i) {
        area->texCoords[i].u = static_cast<float>(D(points[i].x) / D(textureSize));
        area->texCoords[i].v = static_cast<float>(D(points[i].y) / D(textureSize));
    }
    if (count > 2) {
        area->SetIndexCount((count - 2) * 3);
        for (int k = 0; k < count - 2; ++k) {
            area->indices[k * 3] = 0;
            area->indices[k * 3 + 1] = static_cast<std::uint16_t>(k + 2);
            area->indices[k * 3 + 2] = static_cast<std::uint16_t>(k + 1);
        }
    } else {
        area->SetIndexCount(0);
    }
    // Positions go in blocks of four: the polygon is padded with copies of its first point.
    int quads = count / 4;
    if (count % 4 != 0) {
        ++quads;
        UiPointF first = points[0];
        for (int k = 4 - count % 4; k > 0; --k) {
            int size = std::ssize(points);
            points.resize(size + 1);
            points[size] = first;
        }
    }
    area->SetQuadCount(quads);
    for (int q = 0; q < quads; ++q) {
        for (int k = 0; k < 4; ++k) {
            const UiPointF& v = points[q * 4 + k];
            double x = (D(v.x) - D(centreX)) * D(scale);
            float y = static_cast<float>(-((D(v.y) - D(centreY)) * D(scale)));
            float mapY = static_cast<float>(D(y) * D(zoom));
            area->vertices[q].x()[k] = static_cast<float>(x * D(zoom));
            area->vertices[q].y()[k] = mapY;
        }
    }
    area->RefreshBuffers();

    auto* bounds = SceneMesh(0);
    float left = static_cast<float>(-D(centreX) * D(scale));
    float top = static_cast<float>(-(-D(centreY) * D(scale)));
    double rightRaw = (D(worldWidth) - D(centreX)) * D(scale);
    float right = static_cast<float>(rightRaw);
    float bottom = static_cast<float>(-((D(worldHeight) - D(centreY)) * D(scale)));
    left = left >= -1.0f ? left : -1.0f;
    if (top > 1.0f) top = 1.0f;
    if (rightRaw > 1.0f) right = 1.0f;   // the original compares the value still on the FPU stack
    bottom = bottom >= -1.0f ? bottom : -1.0f;
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
    float u0 = static_cast<float>((D(left) / D(scale) + D(centreX)) / D(textureSize));
    float vTop = static_cast<float>(D(static_cast<float>(D(centreY) - D(top) / D(scale))) / D(textureSize));
    float u1 = static_cast<float>((D(right) / D(scale) + D(centreX)) / D(textureSize));
    float vBottom = static_cast<float>(D(static_cast<float>(D(centreY) - D(bottom) / D(scale))) / D(textureSize));
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

// --- The zoom factor ----------------------------------------------------------------------------------

void HudMap::Wheel(int, int delta, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0) return;
    if (region == 0) {
        double newZoom = WheelZoomFactor(static_cast<short>(delta)) * D(zoom);
        zoom = static_cast<float>(newZoom);
        if (newZoom > D(zoomMax)) zoom = zoomMax;
        zoom = zoom >= 1.0f ? zoom : 1.0f;
    }
    handled = true;
}
