// The distance fog (render_state.h's DistanceFog; docs/notes/renderer.md, "The view's depth"): the share of its own
// colour a vertex at clip w keeps, its depth along the view w * depthPerW: all of it up to `start`, then
// exp(-((depth - start) * density)^2). `density` 0: all of it.
float fog_kept(float w, float start, float density, float depthPerW) {
    float x = max(w * depthPerW - start, 0.0) * density;
    return exp(-x * x);
}
