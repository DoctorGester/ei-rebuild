// One triangle covering the target (three vertices, no buffer): the vertex stage of the passes whose fragment
// stage reads its source at its own pixel (into_multisampled, depth_from_multisampled, shadow_reduce,
// shadow_dilate).
@vs fullscreen_vs
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * vec2(2.0, -2.0) + vec2(-1.0, 1.0), 0.0, 1.0);
}
@end
