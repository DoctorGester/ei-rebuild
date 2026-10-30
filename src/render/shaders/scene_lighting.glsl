// The GPU lighting (render_lighting.cpp): the scene's sun, ambient colour and point lights as render::SceneLighting
// kept them, and the game's lighting rules evaluated per pixel. Included after point_shadow by the terrain shader.
@block scene_lighting
layout(binding = 3) uniform scene_light_params {
    vec4 sun;               // xyz: the direction the sun's light travels (unit)
    vec4 sunColour;
    vec4 ambient;
    vec4 counts;            // x: the point lights
    vec4 mapSun;            // xyz: the direction the shadow map's light travels (MapLightDirection), unit
    vec4 pointAt[16];       // per point light: position xyz, w: 1 / range squared
    vec4 pointColour[16];   // per point light: colour rgb, w: 1 specular
    vec4 shadowSlots[4];    // per point light (4 a vector): its point shadow's map + 1, 0 none
} scene;

// The landscape lit as the game's vertex lighting lit each vertex. `m`: the material colour the
// sun and ambient light start from, `mp`: the point lights', `loss`: the light lost under the water, `hidden`:
// how much sun the shadow map hides, `strength`: the map's darkening. See docs/notes/renderer.md, "The
// renderer's source files".
vec3 ground_light(vec3 p, vec3 n, vec3 m, vec3 mp, float loss, float hidden, float strength, bool shadows, vec2 turn,
                  out vec3 spec) {
    vec3 ambient = scene.ambient.rgb * clamp(1.0 - loss, 0.0, 1.0);
    float facing = -dot(scene.sun.xyz, n);
    vec3 direct = facing > 0.0 ? min(vec3(1.0), m + facing * scene.sunColour.rgb) : m;
    vec3 sunlit = max(direct, ambient);
    float shade = 1.0 - hidden * strength * clamp(-dot(scene.mapSun.xyz, n) * 6.0, 0.0, 1.0);
    vec3 lit = min(mix(sunlit, min(vec3(1.0), ambient + m), hidden), sunlit * shade);
    spec = vec3(0.0);
    int count = int(scene.counts.x);
    for (int k = 0; k < count; ++k) {
        vec4 at = scene.pointAt[k], colour = scene.pointColour[k];
        vec3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        vec3 to = d * inversesqrt(max(distance2, 1e-12));
        float c = dot(n, to);
        float turned = c <= 0.0 ? c + 1.0 : 1.0;
        float under = max(0.0, 1.0 - loss * to.z * to.z);
        vec4 slots = scene.shadowSlots[k >> 2];   // (sokol-shdc takes no variable component of a uniform)
        float slot = slots[k & 3];
        float seen = 1.0;
        if (shadows && slot > 0.5) seen = point_seen(int(slot) - 1, p, n, kNoPointFront, turn);
        vec3 v = min(vec3(1.0), mp + turned * falloff * under * seen * colour.rgb);
        if (colour.w > 0.5) spec = max(spec, v * shade);
        else lit = max(lit, v * shade);
    }
    return min(vec3(1.0), max(lit, spec));
}
@end
