// The ground's casters into the shadow map (render_shadows.cpp, render_terrain.cpp): the zone's sectors' ground
// vertices (a sector's 33 x 33 after the one before, by its place in the zone) through the light matrix, those of the
// sectors that do not cast this frame moved off the map. Only the faces turned away from the sun write their depth (as
// the vertex normals say): a lit face never hides itself, the far side of a ridge hides what lies behind it. Depth
// only. Also the ambient occlusion's ground depth (render_occlusion.cpp; `cast` 0: every vertex drawn).
#include "bindings.hlsli"
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
};
struct VsOut {
    [[vk::location(0)]] float3 normal : TEXCOORD0;
    float4 pos : SV_Position;
};
#ifdef EI_VERTEX
static const uint kSectorVertices = 33 * 33;
static const uint kMaskWords = 128;   // render_terrain.cpp's kMaskedSectors / 32
struct ShadowTerrainVsParams {
    row_major float4x4 light;   // to the shadow map
    int cast;                   // 1: only the sectors whose bit is set in `sectors` are drawn
    uint4 sectors[kMaskWords / 4];   // bit k: sector k casts (sectors past kMaskWords * 32 always do)
};
ConstantBuffer<ShadowTerrainVsParams> vs : VS_BLOCK(0);
VsOut vs_main(VsIn i, uint vertex : SV_VertexID) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.light[0], p), dot(vs.light[1], p), dot(vs.light[2], p), dot(vs.light[3], p));
    o.pos.z = max(o.pos.z, 0.0);   // before the near plane: at it (as shadow_caster.hlsl)
    if (vs.cast != 0) {
        const uint sector = vertex / kSectorVertices, word = sector / 32;
        if (word < kMaskWords && ((vs.sectors[word / 4][word % 4] >> (sector % 32)) & 1) == 0)
            o.pos = float4(2.0, 2.0, 0.5, 1.0);   // off the map: its triangles are dropped
    }
    o.normal = i.normal;
    return o;
}
#endif
#ifdef EI_FRAGMENT
struct ShadowTerrainFsParams {
    float4 sun;   // xyz: the direction the sun's light travels
};
ConstantBuffer<ShadowTerrainFsParams> fs : FS_BLOCK(0);
void ps_main(VsOut i) {
    if (dot(i.normal, fs.sun.xyz) < 0.0) discard;
}
#endif
