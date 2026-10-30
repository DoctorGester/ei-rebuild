@vs shadow_caster_vs
@glsl_options fixup_clipspace
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_row0;
layout(location = 3) in vec4 in_row1;
layout(location = 4) in vec4 in_row2;
layout(location = 5) in vec4 in_row3;
layout(location = 6) in vec4 in_cut;
layout(location = 0) out vec2 v_uv;
layout(location = 1) flat out vec2 v_alpha;
void main() {
    vec4 p = vec4(in_pos, 1.0);
    gl_Position = vec4(dot(in_row0, p), dot(in_row1, p), dot(in_row2, p), dot(in_row3, p));
    // A caster before the near plane (the map starts at the nearest one the world's draw found)
    // is drawn at it: it still hides everything behind it (the map's projection is orthographic, w 1).
    gl_Position.z = max(gl_Position.z, 0.0);
    v_uv = in_uv + in_cut.xy;
    v_alpha = in_cut.zw;
}
@end

@fs shadow_caster_fs
layout(location = 0) in vec2 v_uv;
layout(location = 1) flat in vec2 v_alpha;
layout(binding = 0) uniform texture2D tex;
layout(binding = 0) uniform sampler smp;
void main() {
    if (v_alpha.x > 0.5 && textureLod(sampler2D(tex, smp), v_uv, 0.0).a < v_alpha.y) discard;
}
@end
@program shadow_caster shadow_caster_vs shadow_caster_fs
