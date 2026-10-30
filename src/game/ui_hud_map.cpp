// The battle HUD's minimap markers (HudMap::BuildMarkers): every frame the map panel
// rebuilds one mesh (its scene object 2) with a marker per party member and visible unit, per
// marked object, per zone exit and, in multiplayer, per other player. See docs/notes/ui.md.
// Floating point: float maths in the original's order (its x87 registers held floats at the main thread's 24-bit
// precision); the arc tangent and the zoom's power through lib/pc24.h.
#include "ui.h"
#include "ui_screens.h"

#include "base_manager.h"
#include "maths.h"
#include "player_client.h"
#include "spell.h"
#include "unit.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/pc24.h"
#include "lib/str.h"
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
constexpr double kTinyLength2 = 1e-6;    // compared as a double (exact)
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
    float MapX(float worldX) const { return (worldX - map->centreX) * map->scale; }
    float MapY(float worldY) const { return -((worldY - map->centreY) * map->scale); }

    // The place of a marked object, an exit or another player: the zoomed position when it lies
    // inside the inner circle, otherwise the unzoomed one when that lies on the rim (outside the
    // rim circle, inside the square); false when neither.
    bool Place(float worldX, float worldY, float* x, float* y) const {
        float mapX = MapX(worldX);
        float mapY = MapY(worldY);
        float zoomedX = mapX * map->zoom;
        float zoomedY = mapY * map->zoom;
        EI_NAN_AUDIT(zoomedY, zoomedX);
        if (LengthSquared2DUnrounded(zoomedY, zoomedX) <= kInnerRadius2) {
            *x = zoomedX;
            *y = zoomedY;
            return true;
        }
        if (LengthSquared2DUnrounded(mapY, mapX) < kRimRadius2) return false;
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

    UiMesh* mesh;
    const HudMap* map;
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

    float length2 = LengthSquared2DUnrounded(dirY, dirX);
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
    EI_NAN_AUDIT(prev, cur, edge);
    return (prev < edge && cur > edge) || (prev > edge && cur < edge);
}

// One Sutherland-Hodgman step in place: walking the polygon, the point where an edge crosses the line is
// inserted before the edge's end (which is then skipped); afterwards the points outside are removed.
template <typename Cut, typename Outside>
void ClipPass(std::vector<UiPointF>& points, Cut cut, Outside outside) {
    int count = lib::Count(points);
    for (int i = 0; i < count; ++i) {
        UiPointF cur = points[i];
        UiPointF prev = points[i == 0 ? count - 1 : i - 1];
        UiPointF cross;
        if (!cut(prev, cur, &cross)) continue;
        int size = lib::Count(points);
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
        int tail = lib::Count(points) - i - 1;
        if (tail != 0) std::memmove(&points[i], &points[i + 1], tail * sizeof(UiPointF));
        points.pop_back();
        --i;
        --count;
    }
}

// 2.5^(delta / 1200): _CIpow of a double constant and delta * 0.00083333335f (a short times the float: one
// rounding), its double as the CRT returns it.
double WheelZoomFactor(short delta) {
    return libcrt::CiPow(2.5, static_cast<float>(delta) * 0.00083333335f);
}

} // namespace

void HudMap::BuildMarkers() {
    UiMesh* mesh = SceneMesh(2);
    BaseManager* objectManager = g_clientWorld.objectManager;
    // The local view: the ids of the units shown on the map (partyUnitIds) and, in multiplayer, of the other
    // players' units (otherUnitIds).
    PlayerClient* localView = g_clientWorld.LocalView();
    int unitCount = localView ? lib::Count(localView->partyUnitIds) : 0;
    LightFactory* markers = g_clientWorld.lightFactory;
    const std::vector<int>& marked = markers->litIds;
    const int markedCount = static_cast<int>(marked.size());
    int exitCount = static_cast<int>(std::ssize(g_clientWorld.exits));
    int playerCount = IsMultiplayerClient() ? lib::Count(localView->otherUnitIds) : 0;

    int quads = markedCount + playerCount + exitCount;   // six indices each
    mesh->SetQuadCount(quads + unitCount);
    mesh->SetVertexCount((quads + unitCount) * 4);
    mesh->SetIndexCount((unitCount + quads * 2) * 3);

    MarkerWriter out(mesh, this);

    for (int i = 0; i < unitCount; ++i) {
        out.BeginUnit();
        Unit* unit = UnitWithId(objectManager, localView->partyUnitIds[i]);
        if (!unit || unit->IsDead()) {
            out.Hide();
            continue;
        }
        float x = out.MapX(unit->localPosition.x) * zoom;
        float y = out.MapY(unit->localPosition.y) * zoom;
        if (LengthSquared2DUnrounded(y, x) > kUnitRadius2) {
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
        Light* light = markers->LightWithId(static_cast<unsigned>(*node));
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

    mesh->RefreshBuffers();
}

// --- The map's polygon clipping ----------------------------------------------------------------------------------

void HudMap::UpdateView() {
    if (const TransformObject* camera = g_clientWorld.camera) {
        centreX = g_camera.focus.x;
        centreY = g_camera.focus.y;
        Vector3 axis = camera->localRotation.AxisZ();
        facingX = axis.x;
        axis = camera->localRotation.AxisZ();
        facingY = axis.y;
    }
    float length2 = LengthSquared2DUnrounded(facingX, facingY);
    if (length2 >= kTinyLength2) {
        float length = std::sqrt(length2);
        facingX = facingX / length;
        facingY = facingY / length;
    } else {
        facingX = 0.0f;
        facingY = 1.0f;
    }
    float angle = pc24::Round(pc24::Atan2(-facingX, -facingY));
    Vector3 up = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    turn.SetAxisAngle(up, angle);
    SceneMesh(10)->localRotation = turn;

    auto* area = SceneMesh(1);
    std::vector<UiPointF>& points = visibleArea;
    int count = lib::Count(viewPolygon);
    points.resize(count);   // the original inlines the same code
    for (int i = 0; i < count; ++i) {
        const UiPointF& v = viewPolygon[i];
        float x = v.x / zoom;
        float y = v.y / zoom;
        points[i].x = x / scale + centreX;
        points[i].y = centreY - y / scale;
    }

    // Clipped to the world rectangle: x >= 0, y >= 0, x <= width, y <= height.
    ClipPass(points,
             [](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.x, c.x, 0.0f)) return false;
                 out->x = 0.0f;
                 out->y = p.y - (c.y - p.y) * p.x / (c.x - p.x);
                 return true;
             },
             [](const UiPointF& v) {
                 EI_NAN_AUDIT(v.x);
                 return v.x < 0.0f;
             });
    ClipPass(points,
             [](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.y, c.y, 0.0f)) return false;
                 out->x = p.x - (c.x - p.x) * p.y / (c.y - p.y);
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
                 out->y = (width - p.x) * (c.y - p.y) / (c.x - p.x) + p.y;
                 return true;
             },
             [width](const UiPointF& v) { return v.x > width; });
    ClipPass(points,
             [height](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.y, c.y, height)) return false;
                 out->x = (height - p.y) * (c.x - p.x) / (c.y - p.y) + p.x;
                 out->y = height;
                 return true;
             },
             [height](const UiPointF& v) { return v.y > height; });

    // The visible-area mesh: a fan over the clipped polygon, textured by world position.
    count = lib::Count(points);
    area->SetVertexCount(count);
    for (int i = 0; i < count; ++i) {
        area->corners[i].position = static_cast<std::uint16_t>(i);
        area->corners[i].texCoord = static_cast<std::uint16_t>(i);
    }
    area->SetUvCount(count);
    for (int i = 0; i < count; ++i) {
        area->texCoords[i].u = points[i].x / textureSize;
        area->texCoords[i].v = points[i].y / textureSize;
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
            int size = lib::Count(points);
            points.resize(size + 1);
            points[size] = first;
        }
    }
    area->SetQuadCount(quads);
    for (int q = 0; q < quads; ++q) {
        for (int k = 0; k < 4; ++k) {
            const UiPointF& v = points[q * 4 + k];
            float x = (v.x - centreX) * scale;
            float y = -((v.y - centreY) * scale);
            area->vertices[q].x()[k] = x * zoom;
            area->vertices[q].y()[k] = y * zoom;
        }
    }
    area->RefreshBuffers();

    auto* bounds = SceneMesh(0);
    float left = -centreX * scale;
    float top = -(-centreY * scale);
    float right = (worldWidth - centreX) * scale;
    float bottom = -((worldHeight - centreY) * scale);
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
    float u0 = (left / scale + centreX) / textureSize;
    float vTop = (centreY - top / scale) / textureSize;
    float u1 = (right / scale + centreX) / textureSize;
    float vBottom = (centreY - bottom / scale) / textureSize;
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
        float newZoom = pc24::Mul(WheelZoomFactor(static_cast<short>(delta)), zoom);
        zoom = newZoom;
        if (newZoom > zoomMax) zoom = zoomMax;
        zoom = MaxKeepB(zoom, 1.0f);
    }
    handled = true;
}
