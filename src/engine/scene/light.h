#pragma once

// Lights and the light factory ("CLightManager"). light.cpp.

#include "engine/core/base_manager.h"
#include "engine/core/core_transform.h"
#include "engine/scene/drawn_motion.h"
#include "engine/core/engine_object.h"
#include "lib/bit_flags.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <vector>

// The bits of EngineObject::flags on a light. A point light effect's lightFlags (saved) are ored in: the spells'
// and missiles' lights take Specular | Strong.
enum class LightFlag : std::uint32_t {
    Changed = 0x8,          // SetPoint / SetDirectional; nothing reads it
    Active = 0x20,          // cleared by Destroy; nothing sets or reads it
    Specular = 0x40,        // lights the terrain water's specular and the GPU's specular (a quest light, the spells')
    Hidden = 0x80,          // not lighting: the objects' and the terrain's lighting skip it (a torch's or an object's
                            // light turned off; the factory's CreateObject clears it)
    Moving = 0x100,         // a moving effect's light, an object's own light: an object it reaches is not drawn
                            // static
    Held = 0x200,           // held on by an object's light mode 1: hiding and showing leave it alone
    Off = 0x400,            // casts no shadow: a point light effect of negative range, an object's own light
    Strong = 0x800,         // counts ten times in a shadow's light direction
};
template <> inline constexpr bool lib::kBitFlags<LightFlag> = true;

// The light factory's object types (Light::type): the default light and the point light.
inline constexpr int kLightTypeDefault = 0x2E;
inline constexpr int kLightTypePoint = 0x31;

// A light. kLightTypePoint is a point light. localPosition is the light's position (where a directional light is
// drawn from); worldPosition holds it in the space of the mesh being lit.
class Light : public TransformObject {
public:
    // Luminance of the colour, falling off with the squared distance for point lights.
    float GetIntensityAt(const Vector3& point);
    // The luminance GetIntensityAt takes of an r, g, b colour.
    static float Luminance(const float colour[3]);

    // The range is sqrt(FLT_MAX).
    Light();
    ~Light() override;
    void Create() override;
    void Destroy() override;                // LightFlag::Active cleared
    const char* ClassName() override;
    ClassId GetClassId() override;
    // Kind 1; LightFlag::Changed.
    void SetPoint(const Vector3& color, const Vector3& position);
    // Kind 3; the light also tilts towards `direction`; LightFlag::Changed.
    void SetDirectional(const Vector3& color, const Vector3& direction);
    // The terrain water layer's specular lighting: `base` plus the light's colour times `scale` and a factor,
    // each channel clamped at 1; w is then not written (the callers do not read it). Unlit, `out` is `base`. The
    // factor: 1 - distance^2 / range^2 (unlit below 0), times 1 + the cosine between `normal` and `toLight` when
    // that is not above 0.
    void AddPoint(Vector4* out, float scale, const Vector4* base, const Vector3& normal, const Vector3& toLight,
                  float distance) const;

    int kind = 0;                   // 1 point, 3 directional
    float color[3] = {};            // r, g, b
    Vector3 pointPosition = {};     // SetPoint's position
    Vector3 direction = {};         // a directional light's (the sun's) direction
    float range = 0.0f;
    float rangeSquared = 0.0f;
    float inverseRangeSquared = 0.0f;
    Vector3 localDirection;         // direction in the space of the mesh being lit
    float litColors[2][4] = {};
    drawn::LightStep drawnStep;     // client: where the draw shows it (drawn::LightPosition)
};

// The light factory ("CLightManager"): lights of class 0x2E (the renderer's default light) and 0x31 (point
// lights).
class LightFactory : public BaseManager {
public:
    ~LightFactory() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    // Other types are logged and give null.
    EngineObject* NewObject(int type) override;
    // LightFlag::Hidden cleared (no null check).
    EngineObject* CreateObject(int type, int id) override;
    Light* LightWithId(unsigned lightId) const { return FindObjectAs<Light>(lightId); }
    Light* CreateLight(int lightType, int lightId = -1) { return CreateObjectAs<Light>(lightType, lightId); }

    EngineObject* held = nullptr;  // released by the destructor
    std::vector<int> litIds;
};
