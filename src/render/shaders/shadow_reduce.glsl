// The shadow map's depth range per cell (render_shadows.cpp; shadow_receiver.glsl's `range`), two passes after the
// casters: this one, then shadow_dilate.glsl. Here the nearest and farthest texel of each kRangeCell x kRangeCell
// cell of the map (textureGather: four texels a fetch). The constants are render_shadows.cpp's (its static_assert).
@fs shadow_reduce_fs
layout(binding = 0) uniform texture2D map;
layout(binding = 0) uniform sampler point_smp;
const int kCell = 16;           // kRangeCell
const float kTexels = 4096.0;   // kMapSize
layout(location = 0) out vec4 frag_colour;
void main() {
    vec2 base = floor(gl_FragCoord.xy) * kCell;
    float lo = 1.0, hi = 0.0;
    for (int j = 0; j < kCell / 2; ++j) {
        for (int i = 0; i < kCell / 2; ++i) {
            vec4 g = textureGather(sampler2D(map, point_smp), (base + vec2(i * 2 + 1, j * 2 + 1)) / kTexels);
            lo = min(lo, min(min(g.x, g.y), min(g.z, g.w)));
            hi = max(hi, max(max(g.x, g.y), max(g.z, g.w)));
        }
    }
    frag_colour = vec4(lo, hi, 0.0, 0.0);
}
@end
@program shadow_reduce fullscreen_vs shadow_reduce_fs
