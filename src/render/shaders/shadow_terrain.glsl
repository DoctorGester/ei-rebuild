// The ground's casters into the shadow map (render_shadows.cpp, render_terrain.cpp): the sectors'
// ground vertices through the light matrix. Only the faces turned away from the sun write their depth (as
// the vertex normals say): a lit face never hides itself, the far side of a ridge hides what lies behind it.
@vs shadow_terrain_vs
layout(binding = 0) uniform shadow_terrain_vs_params {
    mat4 light;   // to the shadow map
};
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 0) out vec3 v_normal;
void main() {
    vec4 p = vec4(in_pos, 1.0);
    gl_Position = vec4(dot(light[0], p), dot(light[1], p), dot(light[2], p), dot(light[3], p));
    gl_Position.z = max(gl_Position.z, 0.0);   // before the near plane: at it (as shadow_caster.glsl)
    v_normal = in_normal;
}
@end

// Depth only.
@fs shadow_terrain_fs
layout(binding = 1) uniform shadow_terrain_fs_params {
    vec4 sun;   // xyz: the direction the sun's light travels
};
layout(location = 0) in vec3 v_normal;
void main() {
    if (dot(v_normal, sun.xyz) < 0.0) discard;
}
@end
@program shadow_terrain shadow_terrain_vs shadow_terrain_fs
