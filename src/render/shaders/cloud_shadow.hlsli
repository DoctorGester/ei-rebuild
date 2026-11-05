// The cloud shadows (render_cloud_shadows.cpp): how much of the sun the drifting clouds hide at a receiver, from its
// point in the shadow map's clip space (`s`: the landscape's x and y where it meets a level plane along the light,
// render_shadows.cpp's projection). Included after shadow_receiver.hlsli, whose block holds the clouds' part; the
// including program names the map's slot: CLOUD_MAP_SLOT.
Texture2D<float> cloud_map : FS_TEXTURE(CLOUD_MAP_SLOT);
SamplerState cloud_smp : FS_SAMPLER(CLOUD_MAP_SLOT);

static const float kCloudSoft = 0.09;   // half the cloud's edge, in the noise's units

// The share of the sun the clouds hide (0 .. shadow.cloudLight): two layers of the repeating cloud map, the second
// turned and finer, their weighted sum against the cover's threshold with a soft edge.
float cloud_hidden(float2 s) {
    if (shadow.cloudLight <= 0.0) return 0.0;
    float2 a = s * shadow.cloudScaleA + shadow.cloudShiftA;
    float2 b = float2(0.8 * s.x - 0.6 * s.y, 0.6 * s.x + 0.8 * s.y) * shadow.cloudScaleB + shadow.cloudShiftB;
    float n = 0.65 * cloud_map.SampleLevel(cloud_smp, a, 0.0) + 0.35 * cloud_map.SampleLevel(cloud_smp, b, 0.0);
    return smoothstep(shadow.cloudThreshold - kCloudSoft, shadow.cloudThreshold + kCloudSoft, n) * shadow.cloudLight;
}
// The sun hidden by the map's shadow (`hidden`) or the clouds.
float with_clouds(float hidden, float2 s) { return 1.0 - (1.0 - hidden) * (1.0 - cloud_hidden(s)); }
