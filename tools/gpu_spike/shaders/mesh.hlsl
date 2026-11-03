// The meshes (src/render/shaders/mesh.glsl) for SDL_GPU (the spike): the old device's texture stages per pixel,
// then specular, fog and the alpha test; a figure part lit on the GPU takes its diffuse colour from figure_light.
// Vertex block b0 space1; fragment: tex0..tex2 t0..t2, the shadow map t3, its range t4, the point shadows t5;
// blocks b0 mesh_fs_params, b1 shadow_params, b2 mesh_light_params, b3 point_shadow_params (space3).
#define SHADOW_BLOCK_SLOT 1
#define SHADOW_MAP_SLOT 3
#define SHADOW_RANGE_SLOT 4
#define POINT_BLOCK_SLOT 3
#define POINT_MAP_SLOT 5
#include "receivers.hlsli"

struct MeshVsParams {
    float4 matrix[4];
    float4 distanceFog;    // x: on, y: start, z: 1 / (end - start); w: keep z inside the depth range
    float4 shift;          // xy: the half-pixel shift; z: depth scale
    float4 light[4];       // model space to the shadow map
    float4 uvOffset;       // xy: added to the texture coordinates
    float4 normalOffset;   // x: how far along its normal a receiver's point is looked up in the map
    float4 landscape[3];   // model space to the landscape's: rows (point shadows)
};
ConstantBuffer<MeshVsParams> vs : VS_BLOCK(0);

struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
    [[vk::location(2)]] float4 spec : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
    [[vk::location(4)]] float3 normal : TEXCOORD4;
};
struct VsOut {
    [[vk::location(0)]] float4 col : TEXCOORD0;
    [[vk::location(1)]] float4 spec : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 shadow : TEXCOORD3;
    [[vk::location(4)]] float3 model : TEXCOORD4;
    [[vk::location(5)]] float3 normal : TEXCOORD5;
    [[vk::location(6)]] float3 land : TEXCOORD6;
    float4 pos : SV_Position;
};

VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.matrix[0], p), dot(vs.matrix[1], p), dot(vs.matrix[2], p), dot(vs.matrix[3], p));
    if (vs.distanceFog.w > 0.5) o.pos.z = clamp(o.pos.z, 0.0, o.pos.w);
    o.pos.z *= vs.shift.z;
    o.pos.xy += vs.shift.xy * o.pos.w;
    o.col = i.col.zyxw;
    o.spec = i.spec.zyxw;
    if (vs.distanceFog.x > 0.5 && o.pos.w >= vs.distanceFog.y)
        o.spec.a = min(o.spec.a, 1.0 - min((o.pos.w - vs.distanceFog.y) * vs.distanceFog.z, 1.0));
    o.uv = i.uv + vs.uvOffset.xy;
    float len = length(i.normal);
    float4 q = len > 1e-6 ? float4(i.pos + i.normal * (vs.normalOffset.x / len), 1.0) : p;
    o.shadow = float4(dot(vs.light[0], q), dot(vs.light[1], q), dot(vs.light[2], q), 1.0);
    o.model = i.pos;
    o.normal = i.normal;
    o.land = float3(dot(vs.landscape[0], p), dot(vs.landscape[1], p), dot(vs.landscape[2], p));
    return o;
}

struct MeshFsParams {
    float4 fogColour;
    float4 factor;        // TEXTUREFACTOR
    int4 colourOps;       // stage 0: op, arg1, arg2; w: stage 1 op
    int4 colourOps1;      // stage 1: arg1, arg2
    int4 alphaOps;
    int4 alphaOps1;
    int4 switches;        // x: specular, y: fog, z: alpha test func (0 off), w: alpha reference
    float4 patchTo;
    float4 patchMap;
    float4 patchInside;
    float4 shadowFigure;  // x: a unit's mesh, y: its object's nearest depth in the map
    float4 pointOn;       // x: point shadows taken, y: a unit's mesh
    float4 pointFront;    // per map: a unit's nearest distance to the light
};
ConstantBuffer<MeshFsParams> fs : FS_BLOCK(0);
struct MeshLightParams {
    float4 on;            // x: lit here, y: the crown's pull, z: the lights
    float4 sun;
    float4 sunColour;
    float4 ambient;
    float4 emissive;
    float4 pointAt[8];
    float4 pointColour[8];
};
ConstantBuffer<MeshLightParams> lighting : FS_BLOCK(2);
Texture2D<float4> tex0 : FS_TEXTURE(0);
Texture2D<float4> tex1 : FS_TEXTURE(1);
Texture2D<float4> tex2 : FS_TEXTURE(2);
SamplerState smp0 : FS_SAMPLER(0);
SamplerState smp1 : FS_SAMPLER(1);
SamplerState smp2 : FS_SAMPLER(2);

float3 figure_light(float3 p, float3 n, float3 land, float3 face, float2 turn) {
    float3 ambient = lighting.ambient.rgb;
    float facing = dot(lighting.sun.xyz, n);
    float3 lit = max(ambient, lighting.sunColour.rgb * facing);
    int count = (int)lighting.on.z;
    for (int k = 0; k < count; ++k) {
        float4 at = lighting.pointAt[k], colour = lighting.pointColour[k];
        float3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        float c = dot(d * rsqrt(max(distance2, 1e-12)), n);
        float seen = 1.0;
        if (colour.w > 0.5 && fs.pointOn.x > 0.5 && c > 0.0) {
            int slot = (int)colour.w - 1;
            if (fs.pointOn.y > 0.5) {
                float4 fronts = fs.pointFront;
                seen = point_seen(slot, land, 0.0, fronts[slot], turn);
            } else {
                float lightFacing = saturate(dot(face, normalize(pointShadow.lights[slot].xyz - land)) * 6.0);
                if (lightFacing > 0.0) seen = lerp(1.0, point_seen(slot, land, face, kNoPointFront, turn), lightFacing);
            }
        }
        lit = max(lit, colour.rgb * c * falloff * seen);
    }
    float3 c = min(1.0, lit + lighting.emissive.rgb);
    if (lighting.on.y > 0.0) {
        float3 s = lighting.sunColour.rgb * length(lighting.sun.xyz);
        float3 t = clamp(ambient / max(s, 1e-6), -1.0, 1.0);
        float3 mean = 0.5 * (ambient * (t + 1.0) + s * (1.0 - t * t) * 0.5);
        c = lerp(c, min(1.0, mean + lighting.emissive.rgb), lighting.on.y);
    }
    return c;
}
float4 arg(int a, float4 current, float4 diffuse, float4 texel, float4 spec) {
    int sel = a & 15;
    float4 v = sel == 0 ? diffuse : sel == 2 ? texel : sel == 3 ? fs.factor : sel == 4 ? spec : current;
    if ((a & 16) != 0) v = 1.0 - v;
    if ((a & 32) != 0) v = v.aaaa;
    return v;
}
float4 combine(int op, float4 a1, float4 a2, float4 current, float4 diffuse, float4 texel) {
    if (op == 2) return a1;
    if (op == 3) return a2;
    if (op == 5) return a1 * a2 * 2.0;
    if (op == 6) return a1 * a2 * 4.0;
    if (op == 7) return a1 + a2;
    if (op == 8) return a1 + a2 - 0.5;
    if (op == 9) return (a1 + a2 - 0.5) * 2.0;
    if (op == 10) return a1 - a2;
    if (op == 11) return a1 + a2 - a1 * a2;
    if (op == 12) return lerp(a2, a1, diffuse.a);
    if (op == 13) return lerp(a2, a1, texel.a);
    if (op == 14) return lerp(a2, a1, fs.factor.a);
    if (op == 15) return a1 + a2 * (1.0 - texel.a);
    if (op == 16) return lerp(a2, a1, current.a);
    if (op == 18) return float4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op == 19) return float4(a1.rgb * a2.rgb, a1.a + a2.a);
    if (op == 20) return float4(a1.rgb + (1.0 - a1.a) * a2.rgb, a1.a);
    if (op == 24) return dot(a1.rgb * 2.0 - 1.0, a2.rgb * 2.0 - 1.0).xxxx;
    return a1 * a2;
}
bool passes(int func, float a, int reference) {
    int v = (int)round(saturate(a) * 255.0);
    if (func == 1) return false;
    if (func == 2) return v < reference;
    if (func == 3) return v == reference;
    if (func == 4) return v <= reference;
    if (func == 5) return v > reference;
    if (func == 6) return v != reference;
    if (func == 7) return v >= reference;
    return true;
}
float4 stage(int3 colourOp, int3 alphaOp, float4 current, float4 diffuse, float4 texel, float4 spec) {
    float3 c = combine(colourOp.x, arg(colourOp.y, current, diffuse, texel, spec),
                       arg(colourOp.z, current, diffuse, texel, spec), current, diffuse, texel).rgb;
    float a = current.a;
    if (alphaOp.x > 1)
        a = combine(alphaOp.x, arg(alphaOp.y, current, diffuse, texel, spec),
                    arg(alphaOp.z, current, diffuse, texel, spec), current, diffuse, texel).a;
    return saturate(float4(c, a));
}

float4 ps_main(VsOut i) : SV_Target0 {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    float3 face = cross(ddx(i.land), ddy(i.land));
    float faceLength = length(face);
    face = faceLength > 1e-20 ? face / faceLength : float3(0.0, 0.0, 1.0);
    if (dot(face, pointShadow.eye.xyz - i.land) < 0.0) face = -face;
    float hidden = 0.0;
    if (shadow.on.x > 0.5) {
        bool unit = fs.shadowFigure.x > 0.5;
        hidden = shadow_amount(i.shadow.xyz, dsx, dsy, i.pos.xy, unit ? fs.shadowFigure.y : 2.0);
        if (!unit) hidden *= shadow_facing(i.shadow.xyz, dsx, dsy);
    }
    float4 diffuse = i.col;
    if (lighting.on.x > 0.5) diffuse.rgb = figure_light(i.model, i.normal, i.land, face, pixel_turn(i.pos.xy));
    float4 current = diffuse;
    float4 t0 = tex0.Sample(smp0, i.uv);
    float2 w = i.uv - floor(i.uv);
    float4 tp = tex2.Sample(smp2, clamp(w * fs.patchMap.xy + fs.patchMap.zw, fs.patchInside.xy, fs.patchInside.zw));
    if (all(w >= fs.patchTo.xy) && all(w < fs.patchTo.zw)) t0 = tp;
    if (fs.colourOps.x > 1) {
        current = stage(fs.colourOps.xyz, fs.alphaOps.xyz, current, diffuse, t0, i.spec);
        if (fs.colourOps.w > 1) {
            float4 t1 = tex1.Sample(smp1, i.uv);
            current = stage(int3(fs.colourOps.w, fs.colourOps1.xy), int3(fs.alphaOps.w, fs.alphaOps1.xy), current,
                            diffuse, t1, i.spec);
        }
    }
    if (fs.switches.z != 0 && !passes(fs.switches.z, current.a, fs.switches.w)) discard;
    float3 rgb = current.rgb;
    if (fs.switches.x != 0) rgb = saturate(rgb + i.spec.rgb);
    if (shadow.on.x > 0.5) {
        if (shadow.on.z > 0.5) return float4((1.0 - hidden).xxx, current.a);
        rgb *= 1.0 - hidden * shadow.on.y;
    }
    if (fs.switches.y != 0) rgb = lerp(fs.fogColour.rgb, rgb, i.spec.a);
    return float4(rgb, current.a);
}
