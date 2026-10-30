// The casters into a depth map (render_shadows.cpp's CasterDraws: the sun's map, the point lights' faces):
// positions through the light matrix; a pass with alpha test or blending cut out where its stage-0 texel's alpha
// is below the reference. The texel is the texture's own (level 0): seen from the sun at a grazing angle a smaller
// level's averaged alpha would cut out edges and fronds that the camera's view keeps. The caster's own data is
// the draw's instance (CasterInstance): its light matrix by rows, its texture offsets and its cut.
@vs shadow_caster_vs
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_row0;
layout(location = 3) in vec4 in_row1;
layout(location = 4) in vec4 in_row2;
layout(location = 5) in vec4 in_row3;
layout(location = 6) in vec4 in_cut;   // xy: the texture offsets, z: cut out, w: the least alpha kept
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

// Depth only.
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
