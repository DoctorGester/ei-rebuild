#pragma once

// The scene renderer: the global renderer object over the device, the transform pipeline and the renderer's
// helper (renderer_scene.cpp, renderer_init.cpp and the draws renderer_*.cpp). See docs/notes/renderer.md.

#include "engine/scene/pipeline.h"   // TransformPipeline, SpritePoint
#include "engine/core/ref.h"
#include "render/render_state.h"

#include "lib/rect.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

class Light;
class ManagerContext;
class RenderHelper;
class TextureDevice;
class Texture;
struct LineBatch;

// The view the world is drawn from, which the program sets: the game's camera (game/world/camera.h) its projection's
// near distance and field of view (Camera::Create, Camera::Init: CreateRenderer's projection reads them) and its pose
// each frame the world is drawn (camera::TakeFrameView: the drawn view, which the particles face), the interface layout
// the axis scales.
struct SceneView {
    // The projection's shape (renderer_init.cpp): the field of view spans the window's 4:3 part across, the axis swap
    // widens it to the window (screen_layout.h).
    static constexpr float kAspect = 4.0f / 3.0f;
    static constexpr float kDefaultNear = 1.0f;
    static constexpr float kDefaultFieldOfView = kPi * (2.0f / 7.0f);

    Vector3 position;
    Quaternion rotation;
    float nearDistance = kDefaultNear;   // the projection has no far plane
    float fieldOfView = kDefaultFieldOfView;
    // The projection's x and y scales on the axis swap's diagonal (AxisSwapMatrix; the camera's near plane): the
    // window's 4:3 part over the whole window, width43 / width and height43 / height, so the 4:3 picture keeps its
    // proportions on any window (Rebirth's WideScreen scaled x); 1 and 1 on a 4:3 window. game/ui/screen_layout.cpp
    // sets them with the interface layout.
    float axisScale = 1.0f;
    float axisScaleY = 1.0f;
};

// The ambient colour (r, g, b in 0..1) SceneRenderer::SetColour set last (one for every renderer): the figure
// parts lit on the GPU take it from here.
const float* AmbientColour();

// The scene renderer (the global renderer object, &g_sceneRenderer; "CVisualizator" in the game's messages).
class SceneRenderer final {
public:
    SceneRenderer() = default;
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    // Sets the render state's fog colour (render/render_state.h) to `colour`.
    void SetFogColourState(std::uint32_t colour);
    // The renderer's start on `window` at the engine's config's size and screen (engine_globals.h).
    void CreateRenderer(void* window);
    // The sun: the client world's light factory's default light (white, directional), made at each start of the game
    // (app/main_loop.cpp, StartGame) after the device.
    void AddDefaultLight();
    // The device's screen made `width` x `height` (app::SetResolution, between frames): the viewport and the axis swap
    // (the window's 4:3 part, screen_layout.h) on the pipeline's stacks again, the projection kept.
    void ResizeScreen(int width, int height);
    // Drops the sun, the pipeline, the helper and the device object.
    void DestroyRenderer();
    // 0 unless running. Takes the frame time, clears the frame and begins the scene (a
    // scene still open is ended first; other failures are fatal). Returns 1.
    int BeginScene();
    // Ends the scene. Returns 1.
    int EndScene();
    std::uint32_t FrameTime() const { return frameTime; }
    // Sets the colour the next draws are tinted with (colour and the device object's colour) and the transform
    // object's ambient from it.
    void SetColour(std::uint32_t colour);
    // Sets SRCBLEND, then DESTBLEND.
    void SetBlend(render::Blend source, render::Blend dest);
    // Binds `texture` (a texture object, or null) to stage `stage`.
    void SetTexture(int stage, Texture* texture);
    void DrawLineList(LineBatch* lines);
    // Draws a strip along `points` (a lightning segment's aligned points) of `width` with the bound texture:
    // `visibility` the bounds' clip result, the texture's `length` and `offset` along it (0 and 0: untextured
    // lengthwise), `slant` 0.25 at every call known.
    int DrawStrip(const std::vector<Vector3>& points, float width, TransformPipeline::ClipResult visibility, float length, float offset,
                  float slant);
    // Draws sprites facing the view with the bound texture: `points` their centres and sizes, `sprites` an array of
    // their colours and texture rectangles. The first argument is 0 at every call known.
    int DrawSprites(int unknown, const std::vector<SpritePoint>& points, void* sprites);
    // The selection marker under a selected unit (the draw pass): a flat green arrow of half-transparent
    // 0x8000FF00, 0.3 above `point`, `size` long, pointing along -`axisY` (`axisX` across), drawn by the
    // device's flat geometry draw as six triangles of two vertex blocks.
    int DrawSelectionMarker(const Vector3& point, float size, const Vector3& axisX, const Vector3& axisY);
    // A black rectangle (left, top, right, bottom in screen pixels) of `alpha` over the current blend state,
    // with `outline` a white frame. Returns 1.
    int DrawRectangle(int left, int top, int right, int bottom, int alpha, int outline);
    // The renderer's gamma ramp (applied as the frame goes to the window) from the options' brightness and gamma;
    // false without a device.
    bool SetGamma(float brightness, float gamma);
    // After a switch back (WM_ACTIVATE): a full-screen device gets its cooperative level and display mode
    // back, then every surface and the texture manager are restored. False without a device object.
    bool RestoreSurfaces();

    // Makes sure `texture` is loaded and binds it to `stage`.
    void BindLoadedTexture(int stage, Texture* texture);
    // The fog (and clear) colour from r, g, b in 0..1 (each rounded to the nearest integer), stored and set
    // through SetFogColourState.
    void SetFogColour(float r, float g, float b);
    // The sun becomes `light` (counted; the old one released).
    void SetSun(Light* light);
    // SetSun, then the colour from r, g, b in 0..1, packed as SetFogColour packs them.
    void SetSunAndAmbient(Light* light, float r, float g, float b);

    // The window is active, as the main loop took it at the start of its pass (app::g_active): frames, the cursor,
    // the client's lighting and the main menu's lightning are updated and drawn only then.
    bool running = false;
    Ref<TransformPipeline> pipeline;
    std::uint32_t fogColour = 0;               // also the colour the frame is cleared with
    std::uint32_t colour = 0;                  // the draw colour
    // The sun (counted; set together with the ambient colour, the client's lighting moves it every frame).
    Ref<Light> sun;
    Ref<TextureDevice> device;                // the device object
    Ref<RenderHelper> helper;
    // The display settings SetGamma last applied, as given.
    float brightness = 0.0f;
    float gamma = 0.0f;
    // The view the world is drawn from.
    SceneView view;
    // The world drawn: its texture manager, light factory and clock (ManagerContext's scene getters). The game sets
    // its client world before the start (app/main_loop.cpp).
    ManagerContext* world = nullptr;
    // The frame time (milliseconds; BeginScene / EndScene).
    std::uint32_t frameTime = 0;               // the last frame's
    std::uint32_t frameStart = 0;              // timeGetTime at this frame's BeginScene
    std::uint32_t lastFrameStart = 0;          // the previous frame's (the creation: its time)

private:
    // The creation's first step: a new transform pipeline.
    void CreatePipeline();
    // Drops the sun and the pipeline.
    void ReleasePipeline();
};
