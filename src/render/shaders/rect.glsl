// The rectangle shaders' vertex stage (blit, perf_overlay, texture_compose, texture_clouds, ui_canvas): two
// triangles (six vertices, no buffer) covering a rectangle of the target. render_internal.h's ApplyRect sets it.
@block rect_params
layout(binding = 0) uniform rect_params {
    vec4 rect;     // left, top, right, bottom (target pixels)
    vec4 target;   // x, y: the target's width, height
};
@end

@block rect_vertex
@include_block rect_params
// `p` (target pixels) in clip space.
vec4 to_clip(vec2 p) { return vec4(p.x * 2.0 / target.x - 1.0, 1.0 - p.y * 2.0 / target.y, 0.0, 1.0); }
vec4 rect_position() {
    int id = gl_VertexIndex;
    vec2 corner = vec2((id == 1 || id == 2 || id == 4) ? 1.0 : 0.0, (id == 2 || id == 4 || id == 5) ? 1.0 : 0.0);
    return to_clip(mix(rect.xy, rect.zw, corner));
}
@end

@vs rect_vs
@include_block rect_vertex
void main() { gl_Position = rect_position(); }
@end
