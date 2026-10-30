// Shader programs share explicit sokol binding slots across the native backends.
// Matrix4 stores rows; shader dot products index each uploaded row explicitly.
@ctype mat4 Matrix4
@sampler_type copy_smp nonfiltering
@sampler_type cloud_smp nonfiltering
@sampler_type point_smp nonfiltering
@image_sample_type depth_ms unfilterable_float
@image_sample_type map unfilterable_float
@image_sample_type cells unfilterable_float
@include blit.glsl
@include solid.glsl
@include perf_overlay.glsl
@include present.glsl
@include texture_compose.glsl
@include fullscreen.glsl
@include into_multisampled.glsl
@include depth_from_multisampled.glsl
@include shadow_reduce.glsl
@include shadow_dilate.glsl
@include ui_canvas.glsl
@include texture_clouds.glsl
@include shadow_receiver.glsl
@include point_shadow.glsl
@include scene_lighting.glsl
@include shadow_caster.glsl
@include shadow_terrain.glsl
@include terrain.glsl
@include mesh.glsl
