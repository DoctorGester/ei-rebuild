// The renderer's shaders, one input for sokol-shdc (cmake/shaders.cmake; docs/notes/renderer.md, "Shader
// authoring"): each file below holds its programs (@program) and the blocks it shares (@block). A name used by
// several programs (a uniform block, a texture, a sampler) has the same binding in each.
// Matrix4 is row-major in memory and uploaded as is: a mat4's column k is the matrix's row k, which the vertex
// stages dot with the position.
@ctype mat4 Matrix4
// Read with texelFetch or textureGather only.
@sampler_type copy_smp nonfiltering
@sampler_type cloud_smp nonfiltering
@sampler_type point_smp nonfiltering
@image_sample_type depth_ms unfilterable_float
@image_sample_type map unfilterable_float
@image_sample_type cells unfilterable_float
@include rect.glsl
@include fullscreen.glsl
@include blit.glsl
@include solid.glsl
@include perf_overlay.glsl
@include present.glsl
@include texture_compose.glsl
@include texture_clouds.glsl
@include into_multisampled.glsl
@include depth_from_multisampled.glsl
@include shadow_caster.glsl
@include shadow_terrain.glsl
@include shadow_reduce.glsl
@include shadow_dilate.glsl
@include shadow_receiver.glsl
@include point_shadow.glsl
@include scene_lighting.glsl
@include terrain.glsl
@include mesh.glsl
@include ui_canvas.glsl
