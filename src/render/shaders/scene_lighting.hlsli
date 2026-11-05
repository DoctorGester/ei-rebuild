// The GPU lighting (render_lighting.cpp): the scene's sun, ambient colour and point lights as render::SceneLighting
// kept them, and the game's lighting rules evaluated per pixel. Included after point_shadow.hlsli by the terrain
// program, which names its slot: SCENE_BLOCK_SLOT.
struct SceneLightParams {
    float3 sun;               // the direction the sun's light travels (unit)
    int pointCount;           // the point lights
    float3 sunColour;
    float3 ambient;
    float3 mapSun;            // the direction the shadow map's light travels (MapLightDirection), unit
    float3 camera;            // the landscape's space
    float4 pointAt[16];       // per point light: position xyz, w: 1 / range squared
    float4 pointColour[16];   // per point light: colour rgb
    int4 pointSpecular[4];    // per point light (4 a vector): 1 specular
    int4 pointShadows[4];     // per point light (4 a vector): its point shadow's map, -1 none
};
ConstantBuffer<SceneLightParams> scene : FS_BLOCK(SCENE_BLOCK_SLOT);

// The landscape lit as the game's vertex lighting lit each vertex. `m`: the material colour the sun and ambient light
// start from, `mp`: the point lights', `loss`: the light lost under the water, `hidden`: how much sun the shadow map
// hides, `strength`: the map's darkening, `occlusion`: how much of the ambient light reaches the point (1 all). See
// docs/notes/renderer.md, "The renderer's source files".
float3 ground_light(float3 p, float3 n, float3 m, float3 mp, float loss, float hidden, float strength, bool shadows,
                    float2 turn, float occlusion, out float3 spec) {
    float3 ambient = scene.ambient * saturate(1.0 - loss) * occlusion;
    float facing = -dot(scene.sun, n);
    float3 direct = facing > 0.0 ? min(1.0, m + facing * scene.sunColour) : m;
    float3 sunlit = max(direct, ambient);
    float shade = 1.0 - hidden * strength * saturate(-dot(scene.mapSun, n) * 6.0);
    float3 lit = min(lerp(sunlit, min(1.0, ambient + m), hidden), sunlit * shade);
    spec = 0.0;
    int count = scene.pointCount;
    for (int k = 0; k < count; ++k) {
        float4 at = scene.pointAt[k], colour = scene.pointColour[k];
        float3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        float3 to = d * rsqrt(max(distance2, 1e-12));
        float c = dot(n, to);
        float turnedTo = c <= 0.0 ? c + 1.0 : 1.0;
        float under = max(0.0, 1.0 - loss * to.z * to.z);
        int4 slots = scene.pointShadows[k >> 2];
        int slot = slots[k & 3];
        int4 specular = scene.pointSpecular[k >> 2];
        float seen = 1.0;
        if (shadows && slot >= 0) seen = point_seen(slot, p, n, kNoPointFront, turn);
        float3 v = min(1.0, mp + turnedTo * falloff * under * seen * colour.rgb);
        if (specular[k & 3] != 0) spec = max(spec, v * shade);
        else lit = max(lit, v * shade);
    }
    return min(1.0, max(lit, spec));
}
