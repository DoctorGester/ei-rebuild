// The fragment output of the programs that write the depth (mesh, terrain, solid, into_multisampled): the colour, and
// the depth again in the second target, which a multisampled pass's pipelines have (render_smooth.cpp: the depth copy,
// resolved with the colour; a multisampled depth cannot be read back). A pipeline with one target drops it.
// The target is R32F but the output has four components, alpha 1: lavapipe blends the second target with the first's
// blend state (SRC_ALPHA / INV_SRC_ALPHA of the game's alpha-blended draws), whatever the pipeline says, and a one
// component output has alpha 0 there, so every blended draw left the depth copy at its clear value (docs/notes/linux.md,
// "The harness"). With alpha 1 that blend writes the depth; where the state is the pipeline's own it is no blend.
#ifndef EI_DEPTH_COPY_HLSLI
#define EI_DEPTH_COPY_HLSLI
struct DepthCopyOut {
    float4 colour : SV_Target0;
    float4 depth : SV_Target1;
};
DepthCopyOut WithDepthCopy(float4 colour, float depth) {
    DepthCopyOut o;
    o.colour = colour;
    o.depth = float4(depth, 0.0, 0.0, 1.0);
    return o;
}
#endif
