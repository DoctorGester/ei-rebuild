// The GPU lighting (R16i; render_lighting.cpp, LightParams): the scene's sun, ambient colour and point
// lights as render::SceneLighting kept them, and the game's lighting rules evaluated per pixel.
cbuffer light_params : register(b1) {
    float4 sun_dir;        // xyz: the direction the sun's light travels
    float4 sun_colour;
    float4 ambient_colour;
    float4 light_counts;   // x: the point lights
    float4 map_sun;        // xyz: the direction the shadow map's light travels (unit)
    float4 point_lights[32];   // per light: position, 1 / range squared; colour, w: 1 specular
};

// The landscape lit as its vertex lighting lit each vertex (terrain_lighting.cpp: ShadeSun, then
// LightVertex), at a point `p` (the landscape's space) with the unit normal `n`: `m` the material colour
// the sun and ambient light start from (the water's; on the ground the water's above it, else black), `mp`
// the one the point lights start from (the record's own), `loss` the light lost under the water (0 above
// it), `hidden` how much of the sun the shadow map hides at the point (the vertex lighting's ray march
// gave 0 or 1) and `strength` the map's darkening (render_shadows.cpp kStrength). Ambient and direct light
// apart: in the sun max(material + facing x sun, ambient), in the shade ambient + material, as the game
// lit them; but a shadow is never lighter than the sunlit light times 1 - strength (the old decals' and
// the map's look before R16i: where the ambient light is nearly the sun's, as in the towns, the game's
// shade alone hardly shows), on faces turned to the map's light (by their facing to it: the map's sun,
// at least 20 degrees high, is what casts; a face turned away keeps its light, its own object's shadow
// there is no acne). The point lights (each material + its light, at most 1 per channel, by its falloff
// and facing, dimmed under the water by the light's vertical part) take the largest per channel, darkened
// by the same shade (the towns' lamps light the ground by day too: the units' shadows would vanish under
// them; the point lights cast no shadow of their own), the specular ones (shaded alike) apart (`spec`,
// added after the texture as the specular word was). The colour word's light is returned.
float3 ground_light(float3 p, float3 n, float3 m, float3 mp, float loss, float hidden, float strength,
                    out float3 spec) {
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
        float3 v = min(1.0, mp + turned * falloff * under * colour.rgb);
        if (colour.w > 0.5) spec = max(spec, v * shade);
        else lit = max(lit, v * shade);
    }
    return min(1.0, max(lit, spec));
}
