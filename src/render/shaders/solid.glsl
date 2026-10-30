// A solid rectangle (render_blit.cpp): the depth clear in the middle of the frame (its pipeline writes the depth
// only, at target.z), the movie player's fill (its pipeline writes the colour only) and a point shadow face's clear
// (render_point_shadows.cpp).
@vs solid_vs
layout(binding = 0) uniform solid_params {
    vec4 rect;     // left, top, right, bottom (screen pixels)
    vec4 target;   // x, y: frame width, height; z: the depth
    vec4 colour;   // the fill's colour
};
layout(location = 0) flat out vec4 v_colour;
void main() {
    int id = gl_VertexIndex;
    vec2 corner = vec2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    vec2 p = mix(rect.xy, rect.zw, corner);
    gl_Position = vec4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, target.z, 1.0);
    v_colour = colour;
}
@end

@fs solid_fs
layout(location = 0) flat in vec4 v_colour;
layout(location = 0) out vec4 frag_colour;
void main() { frag_colour = v_colour; }
@end
@program solid solid_vs solid_fs
