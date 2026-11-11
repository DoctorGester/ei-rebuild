#pragma once

// The interface's sprite mesh: UiMesh, the figure factory's class 0x43 (the game's figure manager makes it), and the
// sizes the dialog pieces share.

#include "engine/models/figure_manager.h"
#include "engine/models/figure_mesh.h"
#include "game/ui/ui.h"

// A sprite's depth in front of the interface camera for the dialog pieces (frames, shades, buttons), the layout
// pixels one mesh unit spans (SetRectangle's `scale`) and the side of a whole interface texture in texels.
inline constexpr float kDialogDepth = 3.0f;
inline constexpr float kSpriteScale = 100.0f;
inline constexpr int kUiTextureSize = 0x100;

// The figure factory's class 0x43 ("CI3DFigure"): a plain figure mesh with the sprite's fields.
class UiMesh : public FigureMesh {
public:
    UiMesh();
    ~UiMesh() override;
    // The sprite's start: unit scale and tint, the identity orientation at depth 10, opaque black, visible.
    void Create() override;
    // The material and the texture forgotten (not released), then the figure's own Destroy.
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // The quads get z 0 and w 1, the vertices a zero normal index.
    void SetQuadCount(int count);
    void SetVertexCount(int count);
    void SetIndexCount(int count);
    void SetUvCount(int count);
    // The sprite fills the figure mesh's arrays itself: vertices (a quad is one VertexPacket, its four
    // corners x, y, z, w), corners (MeshCorner per vertex), texCoords, indices.
    // Where Place put the sprite (the layout point and the depth; each stored as float).
    struct Placement {
        float x = 0.0f, y = 0.0f, depth = 0.0f;
    };
    Placement GetPlacement() const;
    // The shapes. A rectangle: texels (u0, v0)-(u1, v1), v upwards, both u one less when u0 > u1,
    // half a texel inwards; position the layout centre at `depth`.
    void SetRectangle(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0,
                             int u1, int v1);
    void SetRectangle(float depth, float scale, const Rect& rect, int u0, int v0, int u1, int v1) {
        SetRectangle(depth, scale, static_cast<float>(rect.left), static_cast<float>(rect.top),
                     static_cast<float>(rect.right), static_cast<float>(rect.bottom), u0, v0, u1, v1);
    }
    // A grid of columns x rows cells, each showing the whole picture; position the top left corner.
    void SetGrid(float depth, float scale, float left, float top, float right, float bottom, int u0, int v0, int u1,
                        int v1, int columns, int rows);
    // A frame `border` wide inside the rectangle, each side showing the whole picture.
    void SetFrame(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                         int v0, int u1, int v1);
    void SetFrame(float depth, float scale, const Rect& rect, float border, int u0, int v0, int u1, int v1) {
        SetFrame(depth, scale, static_cast<float>(rect.left), static_cast<float>(rect.top),
                 static_cast<float>(rect.right), static_cast<float>(rect.bottom), border, u0, v0, u1, v1);
    }
    // The first layout point is position, the other two relative to it in units of `scale`.
    void SetTriangle(float depth, float scale, float x0, float y0, float x1, float y1, float x2, float y2, float u0,
                            float v0, float u1, float v1, float u2, float v2);
    // The shape builders (ui_mesh.cpp; docs/notes/ui.md, "Sprite shapes"): each fills the sprite from
    // scratch (quad positions in the sprite's own units, layout pixels / scale, the vertex records, the
    // triangle indices, texels / 256 nudged by half a texel, the placement) and hands the arrays to the renderer.
    // A framed rectangle: an outer quad (left, top)-(right, bottom) and an inner one `border` pixels in,
    // 8 vertices and 24 indices forming the frame between them; texels (u0, v0)-(u1, v1) for the outer
    // edge, moved in proportionally for the inner one.
    void Framed(float depth, float scale, float left, float top, float right, float bottom, float border, int u0,
                int v0, int u1, int v1);
    // A disc of `count` points around the layout point (x, y), radius `radius`; texels on the circle of
    // radius `uvRadius` around (u, v); a fan of count - 2 triangles.
    void Disc(float depth, float scale, float x, float y, float radius, int count, int u, int v, int uvRadius);
    // A ring of `count` segments around (x, y) between `radius - width` and `radius`: 4 vertices and 6
    // indices per segment (the last segment closes on vertices 0 and 1), count / 2 quads; texels
    // (u0, v0)-(u1, v1) for every segment.
    void Ring(float depth, float scale, float x, float y, float radius, int count, float width, int u0, int v0,
              int u1, int v1);
    // Only the per-vertex arrays: one block (four 0s, four 0s, four 1s, four 1s) and two with four entries
    // (the builders' common start).
    void ResetColours();
    // Puts the sprite at layout point (x, y) at `depth` in front of the interface camera (`depth` copied as
    // bits).
    void Place(float x, float y, float depth);
    void SetScale(float scale);
    void SetScaleYBits(std::uint32_t bits);
    // When visible.
    void Draw(SceneRenderer* renderer);
    // Where Draw puts the sprite: its orientation's rotation with its position as column 3.
    Matrix4 PlacementMatrix() const;
    // The depth (z / w) of the sprite's nearest drawn face at screen point `at` as Draw would draw it after `camera`
    // (the pipeline's screen stack's top before the draw), Direct3D 7's pixel centres at whole coordinates; none
    // where no face is (hidden or not visible: none).
    std::optional<float> DepthAt(const Matrix4& camera, Point at) const;

    // FigureMesh's fields as a sprite uses them: FigureMeshFlag::Flat draws in flatColour (ARGB),
    // flatSpecular the HUD buttons' highlight (0xFF404040 / 0xFF000000), textureOffsetU / V_ a frame of the
    // sprite sheet, localRotation the HUD face models' turn.
    bool visible = false;
    float tint[3] = {};              // colour factors (the HUD: 1 selected, 0.5 not)
    // Applied after the placement; the diagonal is SetScale's scale.
    Matrix4 scaleMatrix;
    float& ScaleX() { return scaleMatrix.m[0]; }
    float& ScaleY() { return scaleMatrix.m[5]; }
    float& ScaleZ() { return scaleMatrix.m[10]; }
    Material* material = nullptr;   // the window's black material
    Texture* texture = nullptr;     // one of the window's referenced textures
};

// The world's figure manager in the game (World::figureManager): the engine's, which also makes the interface's
// sprites (FigureObjectType::UiMesh).
class InterfaceFigureManager final : public FigureManager {
public:
    EngineObject* NewObject(int type) override;
};

// (The interface's model manager.) A sprite of `manager` loaded from "<name>.fig" (released and null when the file is
// missing).
UiMesh* NewFigureSprite(FigureManager* manager, std::string_view name);
// An empty sprite of `manager` (no file read).
UiMesh* NewBlankSprite(FigureManager* manager);
