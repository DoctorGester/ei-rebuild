#pragma once

// The weather the picture carries that the game's save does not (docs/notes/renderer.md, "What a save keeps of the
// picture"), as a save's side file keeps it (game/client_visuals.h; render.h's WeatherToKeep and RestoreWeather).

namespace render {

// The surfaces' wetness, puddles and snow cover (0 .. 1; section 13, item 11b) and the clouds' drift (landscape units,
// within each layer's repeat) and cover (0 .. 1; item 11).
struct KeptWeather {
    float wet = 0.0f, puddles = 0.0f, snow = 0.0f;
    double cloudDriftA[2] = {}, cloudDriftB[2] = {};
    float cloudCover = 0.0f;
};

// A unit's own wetness and snow (0 .. 1; item 11b, "Each unit's own weather").
struct UnitWeatherLevels {
    float wet = 0.0f, snow = 0.0f;
};

}   // namespace render
