// The nearest and farthest depth of the cells within `reach.x` cells around each (render_shadows.cpp: enough
// for the receivers' search and filter), with the cell's own nearest.
@fs shadow_dilate_fs
layout(binding = 0) uniform shadow_dilate_params {
    ivec4 reach;   // x: cells around, y: the last cell
};
layout(binding = 0) uniform texture2D cells;
layout(binding = 0) uniform sampler point_smp;
layout(location = 0) out vec4 frag_colour;
void main() {
    ivec2 c = ivec2(gl_FragCoord.xy);
    int last = reach.y;
    float lo = 1.0, hi = 0.0;
    for (int j = -reach.x; j <= reach.x; ++j) {
        for (int i = -reach.x; i <= reach.x; ++i) {
            vec4 r = texelFetch(sampler2D(cells, point_smp), clamp(c + ivec2(i, j), 0, last), 0);
            lo = min(lo, r.x);
            hi = max(hi, r.y);
        }
    }
    frag_colour = vec4(lo, hi, texelFetch(sampler2D(cells, point_smp), c, 0).x, 0.0);
}
@end
@program shadow_dilate fullscreen_vs shadow_dilate_fs
