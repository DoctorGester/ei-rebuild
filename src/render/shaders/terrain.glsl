// The landscape's pages (render_terrain.cpp), lit here (scene_lighting.glsl): the vertices carry the
// normal, the material colour (colour word) and, on the ground, the light lost under the water (`loss`, 0
// or more; below 0 on the water layer, whose specular word carries the wave crests).
@vs terrain_vs
layout(binding = 0) uniform terrain_vs_params {
    mat4 matrix;
    mat4 light;          // to the shadow map
    vec4 shift;          // xy: the half-pixel shift (render_internal.h)
    vec4 normalOffset;   // x: how far along its normal a receiver's point is looked up in the map
};
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_col;
layout(location = 2) in vec4 in_spec;
layout(location = 3) in vec2 in_uv;
layout(location = 4) in vec3 in_normal;
layout(location = 5) in float in_loss;
layout(location = 0) out vec4 v_col;
layout(location = 1) out vec4 v_spec;
layout(location = 2) out vec2 v_uv;
layout(location = 3) out vec4 v_shadow;
layout(location = 4) out vec3 v_world;
layout(location = 5) out vec3 v_normal;
layout(location = 6) out float v_loss;
void main() {
    vec4 p = vec4(in_pos, 1.0);
    gl_Position = vec4(dot(matrix[0], p), dot(matrix[1], p), dot(matrix[2], p), dot(matrix[3], p));
    gl_Position.xy += shift.xy * gl_Position.w;   // Direct3D 7 pixel centres (render_internal.h)
    v_col = in_col.zyxw;                          // D3DCOLOR bytes are B, G, R, A
    v_spec = in_spec.zyxw;
    v_uv = in_uv;
    vec4 q = vec4(in_pos + in_normal * normalOffset.x, 1.0);
    v_shadow = vec4(dot(light[0], q), dot(light[1], q), dot(light[2], q), 1.0);
    v_world = in_pos;
    v_normal = in_normal;
    v_loss = in_loss;
}
@end

@fs terrain_fs
layout(binding = 1) uniform terrain_fs_params {
    vec4 fog;     // rgb: the fog colour
    vec4 flags;   // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels), y: fog on
};
@include_block shadow_receiver
@include_block point_shadow
@include_block scene_lighting
layout(location = 0) in vec4 v_col;
layout(location = 1) in vec4 v_spec;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_shadow;
layout(location = 4) in vec3 v_world;
layout(location = 5) in vec3 v_normal;
layout(location = 6) in float v_loss;
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    vec3 dsx = dFdx(v_shadow.xyz), dsy = dFdy(v_shadow.xyz);
    vec4 t = texture(sampler2D(tex, smp), v_uv);
    if (flags.x > 1.5) {
        frag_colour = vec4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
        return;
    }
    float hidden = 0.0;   // how much of the sun the map hides
    if (shadow.on.x > 0.5) {
        hidden = shadow_amount(v_shadow.xyz, dsx, dsy, gl_FragCoord.xy, 2.0);
        if (shadow.on.z > 0.5) {   // the harness's shadowview
            frag_colour = vec4(vec3(1.0 - hidden), 1.0);
            return;
        }
    }
    float len = length(v_normal);
    vec3 n = len > 1e-6 ? v_normal / len : vec3(0.0, 0.0, 1.0);
    vec3 spec, c;
    if (v_loss < 0.0) {   // the water: no shadow test in the game's lighting; the crests or the specular lights
        vec3 lit = ground_light(v_world, n, v_col.rgb, v_col.rgb, 0.0, 0.0, 0.0, false, vec2(0.0), spec);
        c = clamp(t.rgb * lit + max(spec, v_spec.rgb), 0.0, 1.0);
    } else {
        vec3 lit = ground_light(v_world, n, v_col.rgb, vec3(0.0), v_loss, hidden, shadow.on.y,
                                pointShadow.on.x > 0.5, pixel_turn(gl_FragCoord.xy), spec);
        c = clamp(t.rgb * lit + spec, 0.0, 1.0);
    }
    float a = flags.x > 0.5 ? v_col.a : t.a;
    if (flags.y > 0.5) c = mix(fog.rgb, c, v_spec.a);
    frag_colour = vec4(c, a);
}
@end
@program terrain terrain_vs terrain_fs
