// The GPU lighting (render_lighting.cpp): the scene's sun and ambient colour as render::SceneLighting kept them, the
// point lights of the pixel's cell (point_lights.hlsli), and the game's lighting rules evaluated per pixel. Included
// after point_lights.hlsli by the terrain program, which names its slot: SCENE_BLOCK_SLOT.
struct SceneLightParams {
    float3 sun;               // the direction the sun's light travels (unit)
    float3 sunColour;
    float3 ambient;
    float3 mapSun;            // the direction the shadow map's light travels (MapLightDirection), unit
    float3 camera;            // the landscape's space
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
    uint2 span = cell_lights(p);
    for (uint k = 0; k < span.y; ++k) {
        PointLight l = pointLights[grid_word(span.x + k)];
        float3 d = l.at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * l.at.w;
        if (falloff < 0.0) continue;
        float3 to = d * rsqrt(max(distance2, 1e-12));
        float c = dot(n, to);
        float turnedTo = c <= 0.0 ? c + 1.0 : 1.0;
        float under = max(0.0, 1.0 - loss * to.z * to.z);
        float seen = 1.0;
        if (shadows && l.shadow.x >= 0) seen = point_seen(l.shadow.x, p, n, kNoPointFront, turn);
        float3 v = min(1.0, mp + turnedTo * falloff * under * seen * l.colour.rgb);
        if (l.colour.w != 0.0) spec = max(spec, v * shade);
        else lit = max(lit, v * shade);
    }
    return min(1.0, max(lit, spec));
}
