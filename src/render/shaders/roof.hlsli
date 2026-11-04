// The roofs over the weather (render_weather.cpp's roof map; docs/notes/renderer.md section 13, item 11b, "Roofs"): how
// much of a receiver lies in the open, 1 under the sky, 0 under a roof. The map holds, seen straight down, the depth of
// the highest roof over each point of the visible ground. Included after shadow_receiver.hlsli, whose block holds the
// map's part; the including program names the map's slot: ROOF_MAP_SLOT.
Texture2D<float> roof_map : FS_TEXTURE(ROOF_MAP_SLOT);
SamplerComparisonState roof_smp : FS_SAMPLER(ROOF_MAP_SLOT);

// `s`: the receiver in the shadow map's clip space, `n` its normal (the landscape's space, turned to the camera). The
// point looked up a little out along the normal, compared with the roofs over a 3 x 3 tent of taps for a soft edge; a
// receiver outside the map, or nearly as high as the highest roof (its own top), is in the open.
float roof_open(float3 s, float3 n) {
    if (shadow.roof.x < 0.5) return 1.0;
    float4 p = float4(s, 1.0);
    float3 r = float3(dot(shadow.roofRows[0], p), dot(shadow.roofRows[1], p), dot(shadow.roofRows[2], p)) +
               n * shadow.roof.z * shadow.roofScale.xyz;
    if (any(r.xy < 0.0) || any(r.xy > 1.0)) return 1.0;
    float z = min(r.z - shadow.roof.y, 1.0);
    float open = 0.0;
    [unroll] for (int i = -1; i <= 1; ++i) {
        [unroll] for (int j = -1; j <= 1; ++j) {
            float w = (2.0 - abs(i)) * (2.0 - abs(j));
            open += w * roof_map.SampleCmpLevelZero(roof_smp, r.xy + float2(i, j) * shadow.roofScale.w, z);
        }
    }
    return open / 16.0;
}
