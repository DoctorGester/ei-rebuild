// The fragment output of the programs that write the depth (mesh, terrain, solid, into_multisampled): the colour, and
// the depth again in the second target, which a multisampled pass's pipelines have (render_smooth.cpp: the depth copy,
// resolved with the colour; a multisampled depth cannot be read back). A pipeline with one target drops it.
#ifndef EI_DEPTH_COPY_HLSLI
#define EI_DEPTH_COPY_HLSLI
struct DepthCopyOut {
    float4 colour : SV_Target0;
    float depth : SV_Target1;
};
DepthCopyOut WithDepthCopy(float4 colour, float depth) {
    DepthCopyOut o;
    o.colour = colour;
    o.depth = depth;
    return o;
}
#endif
