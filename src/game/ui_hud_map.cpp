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

__declspec(naked) void CUiMesh::SetQuadCount(int) EI_ORIGINAL(0x005EA210)
__declspec(naked) void CUiMesh::SetVertexCount(int) EI_ORIGINAL(0x005EA600)
__declspec(naked) void CUiMesh::SetIndexCount(int) EI_ORIGINAL(0x005EA4C0)
__declspec(naked) int CUiMesh::Commit() EI_ORIGINAL(0x005EA150)

// --- 00643DC0 ----------------------------------------------------------------------------------

void CHudMap::BuildMarkers() {
    CUiMesh* mesh = reinterpret_cast<CUiMesh*>(sceneObjects_.data_[2]);
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
        double mapX = out.MapX(unit->positionX);
        float mapY = out.MapY(unit->positionY);
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
        if (object && out.Place(object->positionX, object->positionY, &x, &y)) {
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
            if (unit && out.Place(unit->positionX, unit->positionY, &x, &y)) {
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
