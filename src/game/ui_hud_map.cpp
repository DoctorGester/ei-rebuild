// The battle HUD's minimap markers (CHudMap::BuildMarkers): every frame the map panel
// rebuilds one mesh (its scene object 2) with a marker per party member and visible unit, per
// marked object, per zone exit and, in multiplayer, per other player. See docs/notes/ui.md.
// Floating point follows the original's x87 code: `double` for values the original keeps on the FPU
// stack, static_cast<float> where it stores to a float.
#include "ui.h"
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
#include <cstdint>
#include <cstring>
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "camera.h"
#include "lib/text_case.h"

#include <list>
#include <new>

namespace {

inline double D(float f) { return f; }

float AbsBits(float f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    bits &= 0x7FFFFFFF;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

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

// The local view (CWorldClient::LocalView): the ids of the units shown on the map (partyUnitIds_)
// and, in multiplayer, of the other players' units (otherUnitIds_).
CPlayerClient* LocalView() { return TheClientWorld()->LocalView(); }
int LocalSide() { return LocalView()->player_->playerId_; }

// Builds the mesh entries in order. Each entry is one quad block (positions), four vertex
// records and its indices: three for a unit (one triangle), six for the other markers.
class MarkerWriter {
public:
    MarkerWriter(CUiMesh* mesh, const CHudMap* map) : mesh_(mesh), map_(map) {}

    void BeginUnit() {
        std::uint16_t* index = mesh_->indices_.data() + indexPos_;
        index[0] = static_cast<std::uint16_t>(vertex_ + 1);
        index[1] = static_cast<std::uint16_t>(vertex_ + 2);
        index[2] = static_cast<std::uint16_t>(vertex_ + 3);
        indexPos_ += 3;
    }
    void BeginQuad() {
        std::uint16_t* index = mesh_->indices_.data() + indexPos_;
        index[0] = static_cast<std::uint16_t>(vertex_);
        index[1] = static_cast<std::uint16_t>(vertex_ + 2);
        index[2] = static_cast<std::uint16_t>(vertex_ + 1);
        index[3] = static_cast<std::uint16_t>(vertex_ + 1);
        index[4] = static_cast<std::uint16_t>(vertex_ + 2);
        index[5] = static_cast<std::uint16_t>(vertex_ + 3);
        indexPos_ += 6;
    }

    VertexPacket& Block() { return mesh_->vertices_[entry_]; }

    void Show(std::int16_t frame) {
        for (int k = 0; k < 4; ++k) {
            MeshCorner& v = mesh_->corners_[vertex_ + k];
            v.position = static_cast<std::uint16_t>(vertex_ + k);
            v.texCoord = static_cast<std::uint16_t>(frame);
        }
        Next();
    }
    void Hide() {
        for (int k = 0; k < 4; ++k) {
            MeshCorner& v = mesh_->corners_[vertex_ + k];
            v.position = 0;
            v.texCoord = 0;
        }
        Next();
    }

    // Map coordinates of a world point: x stays on the FPU stack, y is stored (after the negation; map y
    // grows downwards).
    double MapX(float worldX) const { return (D(worldX) - D(map_->centreX_)) * D(map_->scale_); }
    float MapY(float worldY) const { return static_cast<float>(-((D(worldY) - D(map_->centreY_)) * D(map_->scale_))); }
    double Zoom() const { return D(map_->zoom_); }

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
    int side = LocalSide();
    if (unit->id_ == kSpecialUnitId) {
        if (str::Compare(LocalPlayerZone(), "gz20g") == 0) return kFrameHostile;
    }
    std::uint32_t bit = 1u << (side & 31);
    return (bit & unit->relationsB & unit->relationsA) != 0 ? kFrameHostile : kFrameNeutral;
}

// The arrow of a unit at map position (x, y) (already zoomed), pointing along the unit's
// facing: the tip twice (vertices 0 and 1), then the two back corners.
void PutArrow(VertexPacket& b, const CUnit* unit, float x, float y) {
    const Quaternion* rotation = &unit->localRotation_;
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

} // namespace

void CHudMap::BuildMarkers() {
    CUiMesh* mesh = SceneMesh(2);
    CBaseManager* objects = TheObjectManager();
    CPlayerClient* view = LocalView();
    int unitCount = view ? std::ssize(view->partyUnitIds_) : 0;
    CLightFactory* markers = TheLightFactory();
    const std::list<int>& marked = markers->litIds_;
    const int markedCount = static_cast<int>(marked.size());
    int exitCount = static_cast<int>(std::ssize(TheClientWorld()->exits_));
    int playerCount = IsMultiplayerClient() ? std::ssize(view->otherUnitIds_) : 0;

    int quads = markedCount + playerCount + exitCount;   // six indices each
    mesh->SetQuadCount(quads + unitCount);
    mesh->SetVertexCount((quads + unitCount) * 4);
    mesh->SetIndexCount((unitCount + quads * 2) * 3);

    MarkerWriter out(mesh, this);

    for (int i = 0; i < unitCount; ++i) {
        out.BeginUnit();
        CUnit* unit = UnitWithId(objects, view->partyUnitIds_[i]);
        if (!unit || unit->IsDead() != 0) {
            out.Hide();
            continue;
        }
        double mapX = out.MapX(unit->localPosition_.x);
        float mapY = out.MapY(unit->localPosition_.y);
        float x = static_cast<float>(mapX * out.Zoom());
        double zoomedY = D(mapY) * out.Zoom();   // the original stores it but squares the register
        float y = static_cast<float>(zoomedY);
        if (zoomedY * D(y) + D(x) * D(x) > kUnitRadius2) {
            out.Hide();
            continue;
        }
        PutArrow(out.Block(), unit, x, y);
        std::int16_t frame = UnitFrame(unit);
        CBattleScreen* owner = owner_;
        for (int k = 0; k < std::ssize(owner->party_); ++k) {
            if (owner->party_[k] == unit) {
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
        if (light && out.Place(light->localPosition_.x, light->localPosition_.y, &x, &y)) {
            out.PutSquare(x, y);
            out.Show(kFrameMarked);
        } else {
            out.Hide();
        }
    }

    // The zone exits that lead somewhere: squares. (The exit record is copied and the target read from
    // the copy, which shares the string.)
    CWorldClient* world = TheClientWorld();
    for (int i = 0; i < exitCount; ++i) {
        out.BeginQuad();
        float x, y;
        if (text::CompareIgnoringCase(world->exits_[i].target, "none") != 0) {
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
            CUnit* unit = UnitWithId(objects, view->otherUnitIds_[i]);
            float x, y;
            if (unit && out.Place(unit->localPosition_.x, unit->localPosition_.y, &x, &y)) {
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

namespace {

const CTransformObject* Camera() { return TheClientWorld()->camera_; }
std::uint32_t Bits(const float& f) {
    std::uint32_t bits;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}
std::uint32_t CameraTargetXBits() { return Bits(TheCamera()->focus_.x); }
std::uint32_t CameraTargetYBits() { return Bits(TheCamera()->focus_.y); }

void SetBits(float* f, std::uint32_t bits) { std::memcpy(f, &bits, sizeof(bits)); }

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

} // namespace

void CHudMap::UpdateView() {
    if (const CTransformObject* camera = Camera()) {
        SetBits(&centreX_, CameraTargetXBits());
        SetBits(&centreY_, CameraTargetYBits());
        Vector3 axis;
        camera->localRotation_.AxisZ(&axis);
        facingX_ = axis.x;
        camera->localRotation_.AxisZ(&axis);
        facingY_ = axis.y;
    }
    double length2 = LengthSquared2DUnrounded<double>(facingX_, facingY_);
    if (length2 >= kTinyLength2) {
        double length = std::sqrt(length2);
        facingX_ = static_cast<float>(D(facingX_) / length);
        facingY_ = static_cast<float>(D(facingY_) / length);
    } else {
        facingX_ = 0.0f;
        facingY_ = 1.0f;
    }
    float angle = static_cast<float>(x87::fpatan(-D(facingX_), -D(facingY_)));
    Vector3 up = {0.0f, 0.0f, 1.0f};
    Quaternion turn;
    turn.SetAxisAngle(up, angle);
    std::memcpy(&SceneMesh(10)->localRotation_, &turn, sizeof(turn));

    auto* area = SceneMesh(1);
    std::vector<UiPointF>& points = visibleArea_;
    int count = std::ssize(viewPolygon_);
    points.resize(count);   // the original inlines the same code
    for (int i = 0; i < count; ++i) {
        const UiPointF& v = viewPolygon_[i];
        double x = D(v.x) / D(zoom_);
        float y = static_cast<float>(D(v.y) / D(zoom_));
        points[i].x = static_cast<float>(x / D(scale_) + D(centreX_));
        points[i].y = static_cast<float>(D(centreY_) - D(y) / D(scale_));
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
    float width = worldWidth_;
    float height = worldHeight_;
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
        area->corners_[i].position = static_cast<std::uint16_t>(i);
        area->corners_[i].texCoord = static_cast<std::uint16_t>(i);
    }
    area->SetUvCount(count);
    for (int i = 0; i < count; ++i) {
        area->texCoords_[i].u = static_cast<float>(D(points[i].x) / D(textureSize_));
        area->texCoords_[i].v = static_cast<float>(D(points[i].y) / D(textureSize_));
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
            double x = (D(v.x) - D(centreX_)) * D(scale_);
            float y = static_cast<float>(-((D(v.y) - D(centreY_)) * D(scale_)));
            float mapY = static_cast<float>(D(y) * D(zoom_));
            area->vertices_[q].x()[k] = static_cast<float>(x * D(zoom_));
            area->vertices_[q].y()[k] = mapY;
        }
    }
    area->RefreshBuffers();

    auto* bounds = SceneMesh(0);
    float left = static_cast<float>(-D(centreX_) * D(scale_));
    float top = static_cast<float>(-(-D(centreY_) * D(scale_)));
    double rightRaw = (D(worldWidth_) - D(centreX_)) * D(scale_);
    float right = static_cast<float>(rightRaw);
    float bottom = static_cast<float>(-((D(worldHeight_) - D(centreY_)) * D(scale_)));
    left = left >= -1.0f ? left : -1.0f;
    if (top > 1.0f) top = 1.0f;
    if (rightRaw > 1.0f) right = 1.0f;   // the original compares the value still on the FPU stack
    bottom = bottom >= -1.0f ? bottom : -1.0f;
    if (left >= right || bottom >= top) {
        bounds->visible_ = 0;
        return;
    }
    bounds->visible_ = 1;
    VertexPacket& b = bounds->vertices_[0];
    b.x()[0] = left;
    b.y()[0] = bottom;
    b.x()[1] = left;
    b.y()[1] = top;
    b.x()[2] = right;
    b.y()[2] = bottom;
    b.x()[3] = right;
    b.y()[3] = top;
    float u0 = static_cast<float>((D(left) / D(scale_) + D(centreX_)) / D(textureSize_));
    float vTop = static_cast<float>(D(static_cast<float>(D(centreY_) - D(top) / D(scale_))) / D(textureSize_));
    float u1 = static_cast<float>((D(right) / D(scale_) + D(centreX_)) / D(textureSize_));
    float vBottom = static_cast<float>(D(static_cast<float>(D(centreY_) - D(bottom) / D(scale_))) / D(textureSize_));
    TexCoord* uv = bounds->texCoords_.data();
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

namespace {
// 2.5^(delta / 1200): _CIpow of a double constant and delta * 0.00083333335f, left unrounded.
double WheelZoomFactor(int delta) {
    return libcrt::CiPow(2.5 /*00742AD0*/, static_cast<double>(delta) * 0.00083333335f /*00742AC8*/);
}
} // namespace

BOOL CHudMap::OnWheel(int, int delta, int x, int y) {
    int region = RegionAt(x, y);
    if (region < 0) return 1;
    if (region == 0) {
        double zoom = WheelZoomFactor(static_cast<short>(delta)) * D(zoom_);
        zoom_ = static_cast<float>(zoom);
        if (zoom > D(zoomMax_)) std::memcpy(&zoom_, &zoomMax_, sizeof(zoom_));
        zoom_ = zoom_ >= 1.0f ? zoom_ : 1.0f;
    }
    handled_ = true;
    return 1;
}
