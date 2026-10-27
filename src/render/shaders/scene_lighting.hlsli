// The GPU lighting (render_lighting.cpp, LightParams): the scene's sun, ambient colour and point
// lights as render::SceneLighting kept them, and the game's lighting rules evaluated per pixel.
cbuffer light_params : register(b1) {
    float4 sun_dir;        // xyz: the direction the sun's light travels
    float4 sun_colour;
    float4 ambient_colour;
    float4 light_counts;   // x: the point lights
    float4 map_sun;        // xyz: the direction the shadow map's light travels (unit)
    float4 point_lights[32];   // per light: position, 1 / range squared; colour, w: 1 specular
    float4 point_slots[4];     // per light (4 a vector): its point shadow's map + 1, 0 none
};

// The landscape lit as its vertex lighting lit each vertex (terrain_lighting.cpp). `m`: the material colour the
// sun and ambient light start from, `mp`: the point lights', `loss`: the light lost under the water, `hidden`:
// how much sun the shadow map hides, `strength`: the map's darkening. See docs/notes/renderer.md, "The
// renderer's source files".
float3 ground_light(float3 p, float3 n, float3 m, float3 mp, float loss, float hidden, float strength,
                    bool shadows, float2 turn, out float3 spec) {
    float3 ambient = ambient_colour.rgb * saturate(1.0 - loss);
    float facing = -dot(sun_dir.xyz, n);
    float3 direct = facing > 0.0 ? min(1.0, m + facing * sun_colour.rgb) : m;
    float3 sunlit = max(direct, ambient);
    float shade = 1.0 - hidden * strength * saturate(-dot(map_sun.xyz, n) * 6.0);
    float3 lit = min(lerp(sunlit, min(1.0, ambient + m), hidden), sunlit * shade);
    spec = 0.0;
    int count = (int)light_counts.x;
    for (int k = 0; k < count; ++k) {
        float4 at = point_lights[2 * k], colour = point_lights[2 * k + 1];
        float3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        float3 to = d * rsqrt(max(distance2, 1e-12));
        float c = dot(n, to);
        float turned = c <= 0.0 ? c + 1.0 : 1.0;
        float under = max(0.0, 1.0 - loss * to.z * to.z);
        float slot = point_slots[k >> 2][k & 3];
        float seen = 1.0;
        if (shadows && slot > 0.5) seen = point_seen((int)slot - 1, p, n, kNoPointFront, turn);
        float3 v = min(1.0, mp + turned * falloff * under * seen * colour.rgb);
        if (colour.w > 0.5) spec = max(spec, v * shade);
        else lit = max(lit, v * shade);
    }
    return min(1.0, max(lit, spec));
}
