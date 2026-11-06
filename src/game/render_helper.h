#pragma once

// The renderer's helper and its render state presets (render_helper.cpp). See docs/notes/renderer.md (sections 5
// and 12).

#include "render/render_state.h"

#include <vector>

// A render state preset the renderer's helper keeps (counted): the texture stages of a material's draw, one per
// state; the stage after them is turned off.
class RenderPreset final {
public:
    // One texture stage's operations.
    struct State {
        bool enabled = false;             // the stage's operations are set
        render::StageCombiner combiner;
    };

    explicit RenderPreset(const std::vector<State>& newStates) : states(newStates) {}

    RenderPreset(const RenderPreset&) = delete;
    RenderPreset& operator=(const RenderPreset&) = delete;

    int StageCount() const { return static_cast<int>(states.size()); }
    // The states' stage operations set in the renderer's render state (render/render_state.h).
    void Apply() const;

    void AddRef() { ++refs; }
    // Drops a reference; the last one deletes the preset.
    void Release() {
        if (--refs == 0) delete this;
    }

    int refs = 0;
    std::vector<State> states;
};

// The renderer's counted helper: the nine built-in render state presets of the materials.
class RenderHelper final {
public:
    static constexpr unsigned kPresetCount = 9;

    // Records the nine presets.
    RenderHelper();
    ~RenderHelper();
    // Preset `index` of the nine, or null beyond them.
    RenderPreset* Preset(unsigned index) const { return index < kPresetCount ? presets[index] : nullptr; }

    RenderHelper(const RenderHelper&) = delete;
    RenderHelper& operator=(const RenderHelper&) = delete;

    void AddRef() { ++refs; }
    // Drops a reference; the last one deletes the helper.
    void Release() {
        if (--refs == 0) delete this;
    }

    int refs = 0;

private:
    // Preset `index` made of `states`.
    void Record(const std::vector<RenderPreset::State>& states, unsigned index);

    RenderPreset* presets[kPresetCount] = {};    // counted
};
