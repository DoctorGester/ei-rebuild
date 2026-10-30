@vs shadow_terrain_vs
@glsl_options fixup_clipspace
layout(binding = 0) uniform shadow_terrain_vs_params {
    vec4 row0;
    vec4 row1;
    vec4 row2;
    vec4 row3;
};
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 0) out vec3 v_normal;
void main() {
    vec4 p = vec4(in_pos, 1.0);
    gl_Position = vec4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    gl_Position.z = max(gl_Position.z, 0.0);   // before the near plane: at it (as shadow_caster.glsl)
    v_normal = in_normal;
}
@end

@fs shadow_terrain_fs
layout(binding = 1) uniform shadow_terrain_fs_params { vec4 sun; };
layout(location = 0) in vec3 v_normal;
void main() {
    if (dot(v_normal, sun.xyz) < 0.0) discard;
}
@end
@program shadow_terrain shadow_terrain_vs shadow_terrain_fs
