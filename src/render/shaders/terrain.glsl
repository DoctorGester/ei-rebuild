@vs terrain_vs
@glsl_options fixup_clipspace
layout(binding = 0) uniform terrain_vs_params {
    mat4 matrix;
    vec4 fog;     // rgb: the fog colour
    vec4 flags;   // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels),
                  // y: fog on; zw: the half-pixel shift
    mat4 light;
    vec4 shadow_on;   // the shadow map (render_shadows.cpp, ShadowParams)
    vec4 shadow_scale;
    vec4 shadow_camera;
    vec4 shadow_soft;
};
const float kNormalOffset = 1.5;
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
    gl_Position.xy += flags.zw * gl_Position.w;   // Direct3D 7 pixel centres (render_internal.h)
    v_col = in_col.zyxw;                          // D3DCOLOR bytes are B, G, R, A
    v_spec = in_spec.zyxw;
    v_uv = in_uv;
    // The normal offset (the map's texel in world units: its extent over its texels).
    float texel = shadow_on.x > 0.5 ? shadow_scale.x * 2.0 / shadow_soft.w : 0.0;
    vec4 q = vec4(in_pos + in_normal * (texel * kNormalOffset), 1.0);
    v_shadow = vec4(dot(light[0], q), dot(light[1], q), dot(light[2], q), 1.0);
    v_world = in_pos;
    v_normal = in_normal;
    v_loss = in_loss;
}
@end

@fs terrain_fs
layout(binding = 1) uniform terrain_fs_params {
    mat4 matrix;
    vec4 fog;     // rgb: the fog colour
    vec4 flags;   // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels),
                  // y: fog on; zw: the half-pixel shift
    mat4 light;
    vec4 shadow_on;   // the shadow map (render_shadows.cpp, ShadowParams)
    vec4 shadow_scale;
    vec4 shadow_camera;
    vec4 shadow_soft;
};
layout(binding = 2) uniform light_params {
    vec4 sun_dir;   // xyz: the direction the sun's light travels
    vec4 sun_colour;
    vec4 ambient_colour;
    vec4 light_counts;       // x: the point lights
    vec4 map_sun;            // xyz: the direction the shadow map's light travels (unit)
    vec4 point_lights[32];   // per light: position, 1 / range squared; colour, w: 1 specular
    vec4 point_slots[4];     // per light (4 a vector): its point shadow's map + 1, 0 none
};
@include_block point_params
layout(location = 0) in vec4 v_col;
layout(location = 1) in vec4 v_spec;
layout(location = 2) in vec2 v_uv;
layout(location = 3) in vec4 v_shadow;
layout(location = 4) in vec3 v_world;
layout(location = 5) in vec3 v_normal;
layout(location = 6) in float v_loss;
layout(binding = 0) uniform texture2D tex;
layout(binding = 3) uniform texture2D shadow_map;
layout(binding = 4) uniform texture2D shadow_range;
layout(binding = 5) uniform texture2D point_shadow_map;
layout(binding = 0) uniform sampler smp;
layout(binding = 3) uniform samplerShadow shadow_smp;
layout(binding = 4) uniform sampler shadow_linear;
layout(binding = 5) uniform samplerShadow point_shadow_smp;
@include_block shadow_receiver
@include_block point_shadow
@include_block scene_lighting
layout(location = 0) out vec4 frag_colour;
void main() {
    vec3 dsx = dFdx(v_shadow.xyz), dsy = dFdy(v_shadow.xyz);
    vec4 t = texture(sampler2D(tex, smp), v_uv);
    if (flags.x > 1.5) {
        frag_colour = vec4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
        return;
    }
    float hidden = 0.0;   // how much of the sun the map hides
    if (shadow_on.x > 0.5) {
        hidden = shadow_amount(shadow_map, shadow_smp, shadow_range, shadow_linear, v_shadow.xyz, dsx, dsy, shadow_on,
                               shadow_scale, shadow_soft, gl_FragCoord.xy, 2.0);
        if (shadow_on.z > 0.5) {
            frag_colour = vec4((1.0 - hidden).xxx, 1.0);
            return;
        }   // the harness's shadowview
    }
    float len = length(v_normal);
    vec3 n = len > 1e-6 ? v_normal / len : vec3(0.0, 0.0, 1.0);
    vec3 spec, c;
    if (v_loss < 0.0) {   // the water: no shadow test in the game's lighting; the crests or the specular lights
        vec3 light = ground_light(v_world, n, v_col.rgb, v_col.rgb, 0.0, 0.0, 0.0, false, vec2(0.0, 0.0), spec);
        c = clamp(t.rgb * light + max(spec, v_spec.rgb), 0.0, 1.0);
    } else {
        vec3 light = ground_light(v_world, n, v_col.rgb, vec3(0.0, 0.0, 0.0), v_loss, hidden, shadow_on.y,
                                  point_shadow_on.x > 0.5, pixel_turn(gl_FragCoord.xy), spec);
        c = clamp(t.rgb * light + spec, 0.0, 1.0);
    }
    float a = flags.x > 0.5 ? v_col.a : t.a;
    if (flags.y > 0.5) c = mix(fog.rgb, c, v_spec.a);
    frag_colour = vec4(c, a);
}
@end
@program terrain terrain_vs terrain_fs
