// The GPU lighting (render_lighting.cpp): the scene's sun, ambient colour and point lights as render::SceneLighting
// kept them, and the game's lighting rules evaluated per pixel. Included after point_shadow.hlsli by the terrain
// program, which names its slot: SCENE_BLOCK_SLOT.
struct SceneLightParams {
    float4 sun;               // xyz: the direction the sun's light travels (unit)
    float4 sunColour;
    float4 ambient;
    float4 counts;            // x: the point lights
    float4 mapSun;            // xyz: the direction the shadow map's light travels (MapLightDirection), unit
    float4 pointAt[16];       // per point light: position xyz, w: 1 / range squared
    float4 pointColour[16];   // per point light: colour rgb, w: 1 specular
    float4 shadowSlots[4];    // per point light (4 a vector): its point shadow's map + 1, 0 none
    float4 camera;            // xyz: the camera (the landscape's space)
};
ConstantBuffer<SceneLightParams> scene : FS_BLOCK(SCENE_BLOCK_SLOT);

// The landscape lit as the game's vertex lighting lit each vertex. `m`: the material colour the sun and ambient light
// start from, `mp`: the point lights', `loss`: the light lost under the water, `hidden`: how much sun the shadow map
// hides, `strength`: the map's darkening. See docs/notes/renderer.md, "The renderer's source files".
float3 ground_light(float3 p, float3 n, float3 m, float3 mp, float loss, float hidden, float strength, bool shadows,
                    float2 turn, out float3 spec) {
    float3 ambient = scene.ambient.rgb * saturate(1.0 - loss);
    float facing = -dot(scene.sun.xyz, n);
    float3 direct = facing > 0.0 ? min(1.0, m + facing * scene.sunColour.rgb) : m;
    float3 sunlit = max(direct, ambient);
    float shade = 1.0 - hidden * strength * saturate(-dot(scene.mapSun.xyz, n) * 6.0);
    float3 lit = min(lerp(sunlit, min(1.0, ambient + m), hidden), sunlit * shade);
    spec = 0.0;
    int count = (int)scene.counts.x;
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
        float4 slots = scene.shadowSlots[k >> 2];
        float slot = slots[k & 3];
        float seen = 1.0;
        if (shadows && slot > 0.5) seen = point_seen((int)slot - 1, p, n, kNoPointFront, turn);
        float3 v = min(1.0, mp + turnedTo * falloff * under * seen * colour.rgb);
        if (colour.w > 0.5) spec = max(spec, v * shade);
        else lit = max(lit, v * shade);
    }
    return min(1.0, max(lit, spec));
}
