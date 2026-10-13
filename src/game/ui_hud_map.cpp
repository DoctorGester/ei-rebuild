// The battle HUD's minimap markers (CHudMap::BuildMarkers, 00643DC0): every frame the map panel
// rebuilds one mesh (its scene object 2) with a marker per party member and visible unit, per
// marked object, per zone exit and, in multiplayer, per other player. See docs/notes/ui.md.
//
// Floating point follows the original's x87 code: `double` for values the original keeps on the
// FPU stack, Store where it stores to a float. Compiled with precise semantics so that the
// operation order is kept and NaN compares stay as written.
#include "ui.h"

#include "base_manager.h"
#include "maths.h"
#include "unit.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <new>

#pragma float_control(precise, on)

namespace {

inline double D(float f) { return f; }

// Rounds an x87 intermediate to float, as the original's FSTP to a float does.
float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

constexpr float kUnitRadius2 = 0.56f;    // 00742AC4 units further out (squared) are not shown
constexpr double kTinyLength2 = 1e-6;     // 00742AB0
constexpr float kArrowAhead = 0.07f;      // 0073CDB0 tip of a unit's arrow from its position
constexpr float kArrowWing = 0.04f;       // 0073CE90 back corners of the arrow; size of a diamond
constexpr float kDiagonal = 0.70710677f;  // 00742AC0 (00742ABC is its negation)
constexpr float kInnerRadius2 = 0.6f;     // 0073CDE0 markers inside (squared) use the zoomed place
constexpr float kRimRadius2 = 0.68f;      // 00742AB8 \ otherwise the unzoomed place, when it is
constexpr float kRimExtent = 0.97f;       // 0073CEC8 / on the rim and inside the square
constexpr float kSquareHalf = 0.03f;      // 0073CD74 marked objects and exits

constexpr int kSpecialUnitId = 0xA2C2A;   // drawn as an enemy only in zone "gz20g"

// Texture frames of the marker sprite.
constexpr std::int16_t kFrameParty = 0;     // party members; other players in multiplayer
constexpr std::int16_t kFrameNeutral = 1;   // other units; zone exits
constexpr std::int16_t kFrameHostile = 2;
constexpr std::int16_t kFrameMarked = 3;    // marked objects

// The local view's lists of object ids (CWorldClient::LocalView).
struct MapLocalView {
    int* UnitIds() const { return Field<int*>(0x50D8); }   // units shown on the map
    int UnitCount() const { return Field<int>(0x50DC); }
    int* PlayerUnitIds() const { return Field<int*>(0x50E8); }   // multiplayer: the others
    int PlayerUnitCount() const { return Field<int>(0x50EC); }
    // The local player's side (+0x1C is the WorldPlayer, its +0x24 the side).
    int Side() const { return *reinterpret_cast<const int*>(Field<std::uint8_t*>(0x1C) + 0x24); }

    template <typename T>
    const T& Field(std::size_t offset) const {
        return *reinterpret_cast<const T*>(reinterpret_cast<const std::uint8_t*>(this) + offset);
    }
};
MapLocalView* LocalView() { return reinterpret_cast<MapLocalView*>(WorldClient()->LocalView()); }

// STLport list node of the marked-object ids.
struct IdNode {
    IdNode* next;
    IdNode* prev;
    unsigned id;
};
// The manager at 007AF748: a CBaseManager whose +0xD4 list holds the ids of the objects the
// minimap marks.
struct MarkerManager : CBaseManager {
    IdNode* Marked() const {
        return *reinterpret_cast<IdNode* const*>(reinterpret_cast<const std::uint8_t*>(this) + 0xD4);
    }
};
CBaseManager* Objects() { return *reinterpret_cast<CBaseManager* const*>(0x007AF5F8); }
MarkerManager* Markers() { return *reinterpret_cast<MarkerManager* const*>(0x007AF748); }

// Builds the mesh entries in order. Each entry is one quad block (positions), four vertex
// records and its indices: three for a unit (one triangle), six for the other markers.
class MarkerWriter {
public:
    MarkerWriter(CUiMesh* mesh, const CHudMap* map) : mesh_(mesh), map_(map) {}

    // A unit's arrow: triangle (1, 2, 3) of the entry's vertices.
    void BeginUnit() {
        std::uint16_t* index = mesh_->indices_ + indexPos_;
        index[0] = static_cast<std::uint16_t>(vertex_ + 1);
        index[1] = static_cast<std::uint16_t>(vertex_ + 2);
        index[2] = static_cast<std::uint16_t>(vertex_ + 3);
        indexPos_ += 3;
    }
    // A square or diamond: triangles (0, 2, 1) and (1, 2, 3).
    void BeginQuad() {
        std::uint16_t* index = mesh_->indices_ + indexPos_;
        index[0] = static_cast<std::uint16_t>(vertex_);
        index[1] = static_cast<std::uint16_t>(vertex_ + 2);
        index[2] = static_cast<std::uint16_t>(vertex_ + 1);
        index[3] = static_cast<std::uint16_t>(vertex_ + 1);
        index[4] = static_cast<std::uint16_t>(vertex_ + 2);
        index[5] = static_cast<std::uint16_t>(vertex_ + 3);
        indexPos_ += 6;
    }

    UiQuadBlock& Block() { return mesh_->quads_[entry_]; }

    // The entry's four vertices use their own positions and `frame`.
    void Show(std::int16_t frame) {
        for (int k = 0; k < 4; ++k) {
            UiMeshVertex& v = mesh_->vertices_[vertex_ + k];
            v.position = static_cast<std::int16_t>(vertex_ + k);
            v.frame = frame;
        }
        Next();
    }
    // Not shown: the vertex records are cleared, the positions are left as they were.
    void Hide() {
        for (int k = 0; k < 4; ++k) {
            UiMeshVertex& v = mesh_->vertices_[vertex_ + k];
            v.position = 0;
            v.frame = 0;
        }
        Next();
    }

    // Map coordinates of a world point, as the original computes them: x stays on the FPU
    // stack, y is stored (after the negation; map y grows downwards).
    double MapX(float worldX) const { return (D(worldX) - D(map_->centreX_)) * D(map_->scale_); }
    float MapY(float worldY) const { return Store(-((D(worldY) - D(map_->centreY_)) * D(map_->scale_))); }
    double Zoom() const { return D(map_->zoom_); }

    // The place of a marked object, an exit or another player: the zoomed position when it lies
    // inside the inner circle, otherwise the unzoomed one when that lies on the rim (outside the
    // rim circle, inside the square); false when neither.
    bool Place(float worldX, float worldY, float* x, float* y) const {
        double mapX = MapX(worldX);
        float mapY = MapY(worldY);
        float zoomedX = Store(mapX * Zoom());
        float zoomedY = Store(D(mapY) * Zoom());
        if (!(D(zoomedY) * D(zoomedY) + D(zoomedX) * D(zoomedX) > kInnerRadius2)) {
            *x = zoomedX;
            *y = zoomedY;
            return true;
        }
        float rimX = Store(mapX);
        if (!(D(mapY) * D(mapY) + D(rimX) * D(rimX) >= kRimRadius2)) return false;
        if (AbsBits(rimX) > kRimExtent) return false;
        if (AbsBits(mapY) > kRimExtent) return false;
        *x = rimX;
        *y = mapY;
        return true;
    }

    // A square of half size 0.03 around (x, y): corners (-,-), (+,-), (-,+), (+,+).
    void PutSquare(float x, float y) {
        UiQuadBlock& b = Block();
        float left = Store(D(x) - kSquareHalf);
        float top = Store(D(y) - kSquareHalf);
        float right = Store(D(x) + kSquareHalf);
        float bottom = Store(D(y) + kSquareHalf);
        b.x[0] = left;
        b.y[0] = top;
        b.x[1] = right;
        b.y[1] = top;
        b.x[2] = left;
        b.y[2] = bottom;
        b.x[3] = right;
        b.y[3] = bottom;
    }

    // A diamond of radius 0.04 around (x, y): corners up, right, left, down.
    void PutDiamond(float x, float y) {
        UiQuadBlock& b = Block();
        b.x[0] = x;
        b.y[0] = Store(D(y) - kArrowWing);
        b.x[1] = Store(D(x) + kArrowWing);
        b.y[1] = y;
        b.x[2] = Store(D(x) - kArrowWing);
        b.y[2] = y;
        b.x[3] = x;
        b.y[3] = Store(D(y) + kArrowWing);
    }

    int indexPos() const { return indexPos_; }

private:
    void Next() {
        vertex_ += 4;
        ++entry_;
    }

    CUiMesh* mesh_;
    const CHudMap* map_;
    int entry_ = 0;      // quad block
    int vertex_ = 0;     // first vertex of the entry (4 per entry)
    int indexPos_ = 0;   // next index
};

// Frame of a unit that is not in the party: hostile when both relation masks have the local
// side's bit. The special unit 0xA2C2A counts as hostile in zone gz20g whatever its relations.
std::int16_t UnitFrame(const CUnit* unit) {
    int side = LocalView()->Side();
    if (unit->id_ == kSpecialUnitId) {
        alignas(mfc::CString) unsigned char storage[sizeof(mfc::CString)];
        mfc::CString* zone = LocalPlayerName(reinterpret_cast<mfc::CString*>(storage));
        bool special = gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(*zone)),
                                       reinterpret_cast<const unsigned char*>("gz20g")) == 0;
        zone->~CString();
        if (special) return kFrameHostile;
    }
    std::uint32_t bit = 1u << (side & 31);
    return (bit & unit->relationsB & unit->relationsA) != 0 ? kFrameHostile : kFrameNeutral;
}

// The arrow of a unit at map position (x, y) (already zoomed), pointing along the unit's
// facing: the tip twice (vertices 0 and 1), then the two back corners.
void PutArrow(UiQuadBlock& b, const CUnit* unit, float x, float y) {
    const Quaternion* rotation =
        reinterpret_cast<const Quaternion*>(reinterpret_cast<const std::uint8_t*>(unit) + 0x34);
    // The original computes the rotated Y axis twice and takes x from the first, y from the
    // second (the function is pure).
    Vector3 first;
    rotation->AxisY(&first);
    float dirX = -first.x;
    Vector3 second;
    rotation->AxisY(&second);
    float dirY = second.y;

    double length2 = D(dirY) * D(dirY) + D(dirX) * D(dirX);
    double nx;
    double ny;
    if (length2 >= kTinyLength2) {
        float length = Store(x87::fsqrt(length2));
        nx = D(dirX) / D(length);
        ny = D(dirY) / D(length);
    } else {
        nx = 0.0f;
        ny = 1.0f;
    }

    float tipX = Store(nx * kArrowAhead + D(x));
    float tipY = Store(ny * kArrowAhead + D(y));
    b.x[0] = tipX;
    b.y[0] = tipY;
    b.x[1] = tipX;
    b.y[1] = tipY;

    // The back corners: the direction turned by +-135 degrees (sums of the scaled components).
    float sy = Store(ny * kDiagonal);
    float sx = Store(nx * kDiagonal);
    float leftX = Store(D(sy) - D(sx));
    double leftY = nx * -kDiagonal - D(sy);   // kept on the FPU stack
    b.x[2] = Store(D(leftX) * kArrowWing + D(x));
    b.y[2] = Store(leftY * kArrowWing + D(y));
    float rightY = Store(D(sx) - D(sy));
    b.x[3] = Store(leftY * kArrowWing + D(x));
    b.y[3] = Store(D(rightY) * kArrowWing + D(y));
}

} // namespace

// --- Forwarders --------------------------------------------------------------------------------

__declspec(naked) int CUiMesh::Commit() EI_ORIGINAL(0x005EA150)

// --- 00643DC0 ----------------------------------------------------------------------------------

void CHudMap::BuildMarkers() {
    CUiMesh* mesh = reinterpret_cast<CUiMesh*>(sceneObjects_.data[2]);
    CBaseManager* objects = Objects();
    MapLocalView* view = LocalView();
    int unitCount = view ? view->UnitCount() : 0;
    MarkerManager* markers = Markers();
    int markedCount = 0;
    for (IdNode* node = markers->Marked()->next; node != markers->Marked(); node = node->next) ++markedCount;
    int exitCount = WorldClient()->exitCount_;
    int playerCount = IsMultiplayerClient() ? view->PlayerUnitCount() : 0;

    int quads = markedCount + playerCount + exitCount;   // six indices each
    mesh->SetQuadCount(quads + unitCount);
    mesh->SetVertexCount((quads + unitCount) * 4);
    mesh->SetIndexCount((unitCount + quads * 2) * 3);

    MarkerWriter out(mesh, this);

    // The units the local view lists: an arrow, unless dead or outside the circle.
    for (int i = 0; i < unitCount; ++i) {
        out.BeginUnit();
        auto* unit = reinterpret_cast<CUnit*>(objects->FindObject(view->UnitIds()[i]));
        if (!unit || ei::VCall<int>(unit, 55) != 0) {   // slot 55: dead (+0x274)
            out.Hide();
            continue;
        }
        double mapX = out.MapX(unit->position.x);
        float mapY = out.MapY(unit->position.y);
        float x = Store(mapX * out.Zoom());
        double zoomedY = D(mapY) * out.Zoom();   // the original stores it but squares the register
        float y = Store(zoomedY);
        if (zoomedY * D(y) + D(x) * D(x) > kUnitRadius2) {
            out.Hide();
            continue;
        }
        PutArrow(out.Block(), unit, x, y);
        std::int16_t frame = UnitFrame(unit);
        CBattleScreen* owner = owner_;
        for (int k = 0; k < owner->partyCount_; ++k) {
            if (owner->party_[k] == unit) {
                frame = kFrameParty;
                break;
            }
        }
        out.Show(frame);
    }

    // The marked objects: squares.
    IdNode* node = markers->Marked()->next;
    for (int i = 0; i < markedCount; ++i, node = node->next) {
        if (node == markers->Marked()) break;
        out.BeginQuad();
        auto* object = reinterpret_cast<CUnit*>(markers->FindObject(node->id));
        float x, y;
        if (object && out.Place(object->position.x, object->position.y, &x, &y)) {
            out.PutSquare(x, y);
            out.Show(kFrameMarked);
        } else {
            out.Hide();
        }
    }

    // The zone exits that lead somewhere: squares. (The original copies the exit record and
    // reads the target from the copy, which shares the string.)
    CWorldClient* world = WorldClient();
    for (int i = 0; i < exitCount; ++i) {
        out.BeginQuad();
        float x, y;
        if (gamecrt::stricmp(world->exits_[i].target, "none") != 0) {
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

    // Multiplayer: the other players' units, diamonds.
    if (IsMultiplayerClient()) {
        for (int i = 0; i < playerCount; ++i) {
            out.BeginQuad();
            auto* unit = reinterpret_cast<CUnit*>(objects->FindObject(view->PlayerUnitIds()[i]));
            float x, y;
            if (unit && out.Place(unit->position.x, unit->position.y, &x, &y)) {
                out.PutDiamond(x, y);
                out.Show(kFrameParty);
            } else {
                out.Hide();
            }
        }
    }

    mesh->Commit();
}
EI_REPLACE_METHOD(0x00643DC0, CHudMap::BuildMarkers);

// --- 00642E80 ----------------------------------------------------------------------------------

namespace {

// FPATAN: atan2(y, x) left unrounded in ST0.
__declspec(naked) double __cdecl Fpatan(double /*y*/, double /*x*/) {
    __asm fld qword ptr [esp + 4]
    __asm fld qword ptr [esp + 12]
    __asm fpatan
    __asm ret
}

// The camera (global 007AF7C8; its rotation at +0x34) and the ground point it looks at.
const CTransformObject* Camera() { return *reinterpret_cast<CTransformObject* const*>(0x007AF7C8); }
std::uint32_t CameraTargetXBits() { return *reinterpret_cast<const std::uint32_t*>(0x0079B388); }
std::uint32_t CameraTargetYBits() { return *reinterpret_cast<const std::uint32_t*>(0x0079B38C); }

void SetBits(float* f, std::uint32_t bits) { std::memcpy(f, &bits, sizeof(bits)); }

// Whether the edge from `prev` to `cur` crosses the line v = edge (coordinate values of the two
// points), as the original's four compares decide it (NaN counts as below the line).
bool Crosses(float prev, float cur, float edge) {
    return (!(prev >= edge) && cur > edge) || (prev > edge && !(cur >= edge));
}

// One Sutherland-Hodgman step as the original does it in place: walking the polygon, the point
// where an edge crosses the line is inserted before the edge's end (which is then skipped);
// afterwards the points outside are removed.
template <typename Cut, typename Outside>
void ClipPass(UiPointArray& points, Cut cut, Outside outside) {
    int count = points.size;
    for (int i = 0; i < count; ++i) {
        UiPointF cur = points.data[i];
        UiPointF prev = points.data[i == 0 ? count - 1 : i - 1];
        UiPointF cross;
        if (!cut(prev, cur, &cross)) continue;
        int size = points.size;
        if (i < size) {
            UiPointsSetSize(&points, nullptr, size + 1, -1);
            std::memmove(&points.data[i + 1], &points.data[i], (size - i) * sizeof(UiPointF));
        } else {
            UiPointsSetSize(&points, nullptr, i + 1, -1);
        }
        points.data[i] = cross;
        ++i;
        ++count;
    }
    for (int i = 0; i < count; ++i) {
        if (!outside(points.data[i])) continue;
        int tail = points.size - i - 1;
        if (tail != 0) std::memmove(&points.data[i], &points.data[i + 1], tail * sizeof(UiPointF));
        --points.size;
        --i;
        --count;
    }
}

} // namespace

__declspec(naked) void __fastcall UiPointsSetSize(UiPointArray*, void*, int, int) EI_ORIGINAL(0x00453100)

void CHudMap::UpdateView() {
    // Follow the camera: the map is centred on its target and turns with its facing.
    if (const CTransformObject* camera = Camera()) {
        SetBits(&centreX_, CameraTargetXBits());
        SetBits(&centreY_, CameraTargetYBits());
        Vector3 axis;
        camera->localRotation_.AxisZ(&axis);
        facingX_ = axis.x;
        camera->localRotation_.AxisZ(&axis);
        facingY_ = axis.y;
    }
    double length2 = D(facingX_) * D(facingX_) + D(facingY_) * D(facingY_);
    if (length2 >= kTinyLength2) {
        double length = x87::fsqrt(length2);
        facingX_ = Store(D(facingX_) / length);
        facingY_ = Store(D(facingY_) / length);
    } else {
        facingX_ = 0.0f;
        facingY_ = 1.0f;
    }
    // The compass needle (scene object 10) turns about z.
    float angle = Store(Fpatan(-D(facingX_), -D(facingY_)));
    Vector3 up = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    turn.SetAxisAngle(up, angle);
    std::memcpy(reinterpret_cast<std::uint8_t*>(sceneObjects_.data[10]) + 0x34, &turn, sizeof(turn));

    // The view polygon in world units.
    auto* area = reinterpret_cast<CUiMesh*>(sceneObjects_.data[1]);
    UiPointArray& points = visibleArea_;
    int count = viewPolygon_.size;
    UiPointsSetSize(&points, nullptr, count, -1);   // the original inlines the same code
    for (int i = 0; i < count; ++i) {
        const UiPointF& v = viewPolygon_.data[i];
        double x = D(v.x) / D(zoom_);
        float y = Store(D(v.y) / D(zoom_));
        points.data[i].x = Store(x / D(scale_) + D(centreX_));
        points.data[i].y = Store(D(centreY_) - D(y) / D(scale_));
    }

    // Clipped to the world rectangle: x >= 0, y >= 0, x <= width, y <= height.
    ClipPass(points,
             [](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.x, c.x, 0.0f)) return false;
                 out->x = 0.0f;
                 out->y = Store(D(p.y) - (D(c.y) - D(p.y)) * D(p.x) / (D(c.x) - D(p.x)));
                 return true;
             },
             [](const UiPointF& v) { return !(v.x >= 0.0f); });
    ClipPass(points,
             [](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.y, c.y, 0.0f)) return false;
                 out->x = Store(D(p.x) - (D(c.x) - D(p.x)) * D(p.y) / (D(c.y) - D(p.y)));
                 out->y = 0.0f;
                 return true;
             },
             [](const UiPointF& v) { return !(v.y >= 0.0f); });
    float width = worldWidth_;
    float height = worldHeight_;
    ClipPass(points,
             [width](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.x, c.x, width)) return false;
                 out->x = width;
                 out->y = Store((D(width) - D(p.x)) * (D(c.y) - D(p.y)) / (D(c.x) - D(p.x)) + D(p.y));
                 return true;
             },
             [width](const UiPointF& v) { return v.x > width; });
    ClipPass(points,
             [height](const UiPointF& p, const UiPointF& c, UiPointF* out) {
                 if (!Crosses(p.y, c.y, height)) return false;
                 out->x = Store((D(height) - D(p.y)) * (D(c.x) - D(p.x)) / (D(c.y) - D(p.y)) + D(p.x));
                 out->y = height;
                 return true;
             },
             [height](const UiPointF& v) { return v.y > height; });

    // The visible-area mesh: a fan over the clipped polygon, textured by world position.
    count = points.size;
    area->SetVertexCount(count);
    for (int i = 0; i < count; ++i) {
        area->vertices_[i].position = static_cast<std::int16_t>(i);
        area->vertices_[i].frame = static_cast<std::int16_t>(i);
    }
    area->SetUvCount(count);
    for (int i = 0; i < count; ++i) {
        area->uvs_[i * 2] = Store(D(points.data[i].x) / D(textureSize_));
        area->uvs_[i * 2 + 1] = Store(D(points.data[i].y) / D(textureSize_));
    }
    if (count > 2) {
        area->SetIndexCount((count - 2) * 3);
        for (int k = 0; k < count - 2; ++k) {
            area->indices_[k * 3] = 0;
            area->indices_[k * 3 + 1] = static_cast<std::uint16_t>(k + 2);
            area->indices_[k * 3 + 2] = static_cast<std::uint16_t>(k + 1);
        }
    } else {
        area->SetIndexCount(0);
    }
    // Positions go in blocks of four: the polygon is padded with copies of its first point.
    int quads = count / 4;
    if (count % 4 != 0) {
        ++quads;
        UiPointF first = points.data[0];
        for (int k = 4 - count % 4; k > 0; --k) {
            int size = points.size;
            UiPointsSetSize(&points, nullptr, size + 1, -1);
            points.data[size] = first;
        }
    }
    area->SetQuadCount(quads);
    for (int q = 0; q < quads; ++q) {
        for (int k = 0; k < 4; ++k) {
            const UiPointF& v = points.data[q * 4 + k];
            double x = (D(v.x) - D(centreX_)) * D(scale_);
            float y = Store(-((D(v.y) - D(centreY_)) * D(scale_)));
            float mapY = Store(D(y) * D(zoom_));
            area->quads_[q].x[k] = Store(x * D(zoom_));
            area->quads_[q].y[k] = mapY;
        }
    }
    area->Commit();

    // The world's bounds (scene object 0), clamped to the map square [-1, 1].
    auto* bounds = reinterpret_cast<CUiMesh*>(sceneObjects_.data[0]);
    float left = Store(-D(centreX_) * D(scale_));
    float top = Store(-(-D(centreY_) * D(scale_)));
    double rightRaw = (D(worldWidth_) - D(centreX_)) * D(scale_);
    float right = Store(rightRaw);
    float bottom = Store(-((D(worldHeight_) - D(centreY_)) * D(scale_)));
    if (!(left >= -1.0f)) left = -1.0f;
    if (top > 1.0f) top = 1.0f;
    if (rightRaw > 1.0f) right = 1.0f;   // the original compares the value still on the FPU stack
    if (!(bottom >= -1.0f)) bottom = -1.0f;
    if (left >= right || bottom >= top) {
        bounds->visible_ = 0;
        return;
    }
    bounds->visible_ = 1;
    UiQuadBlock& b = bounds->quads_[0];
    b.x[0] = left;
    b.y[0] = bottom;
    b.x[1] = left;
    b.y[1] = top;
    b.x[2] = right;
    b.y[2] = bottom;
    b.x[3] = right;
    b.y[3] = top;
    float u0 = Store((D(left) / D(scale_) + D(centreX_)) / D(textureSize_));
    float vTop = Store(D(Store(D(centreY_) - D(top) / D(scale_))) / D(textureSize_));
    float u1 = Store((D(right) / D(scale_) + D(centreX_)) / D(textureSize_));
    float vBottom = Store(D(Store(D(centreY_) - D(bottom) / D(scale_))) / D(textureSize_));
    float* uv = bounds->uvs_;
    uv[0] = u0;
    uv[1] = vBottom;
    uv[2] = u0;
    uv[3] = vTop;
    uv[4] = u1;
    uv[5] = vBottom;
    uv[6] = u1;
    uv[7] = vTop;
}
EI_REPLACE_METHOD(0x00642E80, CHudMap::UpdateView);

// --- 00645640 ----------------------------------------------------------------------------------

namespace {
// 2.5^(delta / 1200): _CIpow (006E81A0) of the double at 00742AD0 and delta * 0.00083333335f,
// left unrounded.
double WheelZoomFactor(int delta) {
    double result;
    __asm {
        fld qword ptr ds:[0x00742AD0]
        fild delta
        fmul dword ptr ds:[0x00742AC8]
        mov eax, 0x006E81A0
        call eax
        fstp result
    }
    return result;
}
} // namespace

BOOL CHudMap::OnWheel(int, int delta, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0) return 1;
    if (region == 0) {
        double zoom = WheelZoomFactor(static_cast<short>(delta)) * D(zoom_);
        zoom_ = Store(zoom);
        if (zoom > D(zoomMax_)) std::memcpy(&zoom_, &zoomMax_, sizeof(zoom_));
        if (!(zoom_ >= 1.0f)) zoom_ = 1.0f;
    }
    inputHandled_ = true;
    return 1;
}

namespace {
BOOL __fastcall HudMapWheel(void* input, void*, int buttons, int delta, int x, int y) {
    return reinterpret_cast<CHudMap*>(static_cast<std::uint8_t*>(input) - 0x94)->OnWheel(buttons, delta, x, y);
}
} // namespace
EI_REPLACE(0x00645640, HudMapWheel);
