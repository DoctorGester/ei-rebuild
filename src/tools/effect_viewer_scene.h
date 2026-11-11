#pragma once

// The effect viewer's world (ei-effects; docs/notes/effects.md, "The effect viewer"): the scene the engine draws from
// without the game's (a ManagerContext of its own: the install's textures, materials, lights, figures and particles), a
// flat ground, one figure to put effects on (a hero's, playing an animation of its database, standing or walking a
// circle), the particle host on them (particlehost::FigureHost) and the free camera. Its clock is the game's: a tick
// every 55 ms.

#include "engine/core/manager_context.h"
#include "engine/particles/particle_figure_host.h"

#include "lib/vecmath.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class FigureInstance;
class FigureManager;
class LightFactory;
class LinkFigureLoader;
class MaterialManager;
class ParticleManager;
class ParticleSource;
class ResourceManager;
class TextureManager;
struct AnimationDatabase;
struct FigureAnimation;

namespace platform {
struct Event;
}

namespace tools::effect_viewer {

// The free camera: it looks at `focus` from `distance` away, turned by `heading` about the up axis and tilted down by
// `pitch` (radians), as the game's camera is built (Camera::ApplyView).
struct FreeCamera {
    Vector3 focus;
    float heading = 0.6f;
    float pitch = 0.75f;
    float distance = 20.0f;   // about the game camera's view of a unit

    Quaternion Rotation() const;
    Vector3 Position() const;
};

class ViewerScene final : public ManagerContext {
public:
    ViewerScene();
    ~ViewerScene() override;
    void Destroy() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    FigureManager* GetFigureManager() override;
    BaseManager* SpecificManager() override;
    LinkFigureLoader* FigureLoader() override;
    TextureManager* GetTextureManager() override;
    MaterialManager* GetMaterialManager() override;
    LightFactory* GetLightFactory() override;
    float SceneTime() override;

    // The managers made (the install's archives opened through the mod's layers), the scene renderer's world and the
    // particle host set, after the renderer's start (its device); then the figure built. False without the install's
    // archives.
    bool Start();
    // Everything the scene holds let go, before the renderer goes.
    void Stop();

    // The figure `model` (a figure of figures.res; the hero's parts for the heroes) dressed in its own skin, its
    // animation database read; it plays its first standing animation. False when the install has no such figure.
    bool BuildFigure(const std::string& model);
    // The animation `index` of the figure's database plays (looped).
    void PlayAnimation(int index);
    // The animations of the figure's database by name.
    std::vector<std::string> AnimationNames() const;
    // The figure walks its circle or stands (`walk`) and runs or not (`run`: the host's `running`, faster, what the
    // effects take it for), playing the first animation of its gait ("run", "walk", else "idle" / "stand").
    void SetGait(bool walk, bool run);

    // The world's ticks due by `now` (platform::FrameClock; at most 5 in a frame), each the figure's animation and walk,
    // then `ticked` (the viewer's source); the frame's time since the last tick (TickFraction) and the camera's keys.
    void Advance(std::uint32_t now, void (*ticked)());
    // The scene drawn in the frame from the camera: the ground, the figure and every source of the particle manager
    // (DrawAll: the viewer's too, which it ticks itself).
    void Draw();
    // The camera's mouse and keys (a right drag turns it, a middle drag or W A S D / arrows move its focus, the wheel
    // zooms) when the panel did not take them.
    void TakeEvent(const platform::Event& event);

    std::unique_ptr<TextureManager> textures;
    std::unique_ptr<MaterialManager> materials;
    std::unique_ptr<LightFactory> lights;
    std::unique_ptr<FigureManager> figures;
    std::unique_ptr<BaseManager> drawers;                  // the figure parts' drawers (FigureManager::CreateObject)
    std::unique_ptr<LinkFigureLoader, void (*)(LinkFigureLoader*)> loader;
    std::unique_ptr<ParticleManager> particles;
    std::unique_ptr<ResourceManager> database;             // database.res: the figures' animation databases (.adb)
    particlehost::FigureHost host;

    FigureInstance* figure = nullptr;                      // one reference of the scene's
    std::string model;
    std::unique_ptr<AnimationDatabase> animations;
    int animation = -1;                                    // the entry playing
    bool walking = false;                                  // the figure walks a circle around `standAt` (SetGait)
    float walkSpeed = 0.12f;                               // units a tick (running: twice that)
    float circleRadius = 2.5f;
    float walkAngle = 0.0f;
    Vector3 standAt;                                       // where the figure stands when not walking

    FreeCamera camera;
    int tick = 0;                                          // ticks run
    float tickFraction = 0.0f;
    std::uint32_t lastTick = 0;
    std::uint32_t lastFrame = 0;
    bool clockStarted = false;
    bool paused = false;                                   // the world's ticks (the figure) held

private:
    // The figure placed for this tick (walking: along its circle, facing where it goes).
    void PlaceFigure();
    // The first animation whose name holds one of `words` (in their order), else -1.
    int FindAnimation(std::initializer_list<std::string_view> words) const;
    void DrawGround();
    // The way walked in a tick (running: twice the walk's).
    float Stride() const { return host.running ? walkSpeed * 2.0f : walkSpeed; }

    bool dragTurning = false, dragMoving = false;
    int dragX = 0, dragY = 0;
    std::uint32_t keysHeld = 0;                            // bits: forward, back, left, right
};

extern ViewerScene g_scene;

} // namespace tools::effect_viewer
