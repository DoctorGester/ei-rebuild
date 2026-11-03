// The landscape's pages (src/render/shaders/terrain.glsl) for SDL_GPU (the spike), lit as scene_lighting.glsl
// lights them. Vertex block b0 space1; fragment: tex t0, the shadow map t1, its range t2, the point shadows t3;
// blocks b0 terrain_fs_params, b1 shadow_params, b2 scene_light_params, b3 point_shadow_params (space3).
#define SHADOW_BLOCK_SLOT 1
#define SHADOW_MAP_SLOT 1
#define SHADOW_RANGE_SLOT 2
#define POINT_BLOCK_SLOT 3
#define POINT_MAP_SLOT 3
#include "receivers.hlsli"

struct TerrainVsParams {
    float4 matrix[4];
    float4 light[4];       // to the shadow map
    float4 shift;          // xy: the half-pixel shift
    float4 normalOffset;   // x: how far along its normal a receiver's point is looked up in the map
};
ConstantBuffer<TerrainVsParams> vs : VS_BLOCK(0);

struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
    [[vk::location(2)]] float4 spec : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
    [[vk::location(4)]] float3 normal : TEXCOORD4;
    [[vk::location(5)]] float loss : TEXCOORD5;
};
struct VsOut {
    [[vk::location(0)]] float4 col : TEXCOORD0;
    [[vk::location(1)]] float4 spec : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 shadow : TEXCOORD3;
    [[vk::location(4)]] float3 world : TEXCOORD4;
    [[vk::location(5)]] float3 normal : TEXCOORD5;
    [[vk::location(6)]] float loss : TEXCOORD6;
    float4 pos : SV_Position;
};

VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.matrix[0], p), dot(vs.matrix[1], p), dot(vs.matrix[2], p), dot(vs.matrix[3], p));
    o.pos.xy += vs.shift.xy * o.pos.w;
    o.col = i.col.zyxw;   // D3DCOLOR bytes are B, G, R, A
    o.spec = i.spec.zyxw;
    o.uv = i.uv;
    float4 q = float4(i.pos + i.normal * vs.normalOffset.x, 1.0);
    o.shadow = float4(dot(vs.light[0], q), dot(vs.light[1], q), dot(vs.light[2], q), 1.0);
    o.world = i.pos;
    o.normal = i.normal;
    o.loss = i.loss;
    return o;
}

struct TerrainFsParams {
    float4 fog;     // rgb: the fog colour
    float4 flags;   // x: water (2: the level map), y: fog on
};
ConstantBuffer<TerrainFsParams> fs : FS_BLOCK(0);
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);

struct SceneLightParams {
    float4 sun;
    float4 sunColour;
    float4 ambient;
    float4 counts;
    float4 mapSun;
    float4 pointAt[16];
    float4 pointColour[16];
    float4 shadowSlots[4];
};
ConstantBuffer<SceneLightParams> scene : FS_BLOCK(2);

float3 ground_light(float3 p, float3 n, float3 m, float3 mp, float loss, float hidden, float strength, bool shadows,
                    float2 turn, out float3 spec) {
    float3 ambient = scene.ambient.rgb * saturate(1.0 - loss);
    float facing = -dot(scene.sun.xyz, n);
    float3 direct = facing > 0.0 ? min(1.0, m + facing * scene.sunColour.rgb) : m;
    float3 sunlit = max(direct, ambient);
    float shade = 1.0 - hidden * strength * saturate(-dot(scene.mapSun.xyz, n) * 6.0);
    float3 lit = min(lerp(sunlit, min(1.0, ambient + m), hidden), sunlit * shade);
    spec = 0.0;
    int count = (int)scene.counts.x;
    for (int k = 0; k < count; ++k) {
        float4 at = scene.pointAt[k], colour = scene.pointColour[k];
        float3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        float3 to = d * rsqrt(max(distance2, 1e-12));
        float c = dot(n, to);
        float turnedTo = c <= 0.0 ? c + 1.0 : 1.0;
        float under = max(0.0, 1.0 - loss * to.z * to.z);
        float4 slots = scene.shadowSlots[k >> 2];
        float slot = slots[k & 3];
        float seen = 1.0;
        if (shadows && slot > 0.5) seen = point_seen((int)slot - 1, p, n, kNoPointFront, turn);
        float3 v = min(1.0, mp + turnedTo * falloff * under * seen * colour.rgb);
        if (colour.w > 0.5) spec = max(spec, v * shade);
        else lit = max(lit, v * shade);
    }
    return min(1.0, max(lit, spec));
}

float4 ps_main(VsOut i) : SV_Target0 {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    float4 t = tex.Sample(smp, i.uv);
    if (fs.flags.x > 1.5) return float4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
    float hidden = 0.0;
    if (shadow.on.x > 0.5) {
        hidden = shadow_amount(i.shadow.xyz, dsx, dsy, i.pos.xy, 2.0);
        if (shadow.on.z > 0.5) return float4((1.0 - hidden).xxx, 1.0);
    }
    float len = length(i.normal);
    float3 n = len > 1e-6 ? i.normal / len : float3(0.0, 0.0, 1.0);
    float3 spec, c;
    if (i.loss < 0.0) {
        float3 lit = ground_light(i.world, n, i.col.rgb, i.col.rgb, 0.0, 0.0, 0.0, false, 0.0, spec);
        c = saturate(t.rgb * lit + max(spec, i.spec.rgb));
    } else {
        float3 lit = ground_light(i.world, n, i.col.rgb, 0.0, i.loss, hidden, shadow.on.y, pointShadow.on.x > 0.5,
                                  pixel_turn(i.pos.xy), spec);
        c = saturate(t.rgb * lit + spec);
    }
    float a = fs.flags.x > 0.5 ? i.col.a : t.a;
    if (fs.flags.y > 0.5) c = lerp(fs.fog.rgb, c, i.spec.a);
    return float4(c, a);
}
