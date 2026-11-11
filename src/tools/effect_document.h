#pragma once

// A data effect being edited in the effect viewer (effect_viewer.h): its file's text, the layers below it and the record
// the text reads as over them (docs/notes/effects.md, "The effect viewer").

#include "engine/particles/particle_record.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tools::effect_viewer {

// One file of an effect's layers: the built-in one (assets/effects, in the executable) or a mod's.
struct EffectLayer {
    std::string path;
    std::string text;
    bool builtin = false;
};

class EffectDocument {
public:
    // The effect `name` (an original type on data or a mod's effect; its id `id`) as the registry reads it: the
    // built-in file, then the mods' files bottom first. The top one is edited; false when there is none.
    bool Load(std::string_view effect, std::int32_t effectId);
    // A new effect `effect` from `startText` (no file below it).
    void Start(std::string_view effect, std::string_view startText);
    // `text` parsed into `tree` and read over the layers below it: `record` the result, `problems` what was said.
    // False (`record` kept from the last success) when it does not parse or does not load.
    bool Apply();
    // `tree` (changed field by field) written as `text`, then Apply. The text's comments are not kept.
    bool ApplyTree();
    // `text` written to `folder`\Res\effects\<name>.effect.toml (the folders made); an edited built-in file is written
    // whole, with `replace = true`, so the mod's file is exactly what the viewer showed. The path written, or an error
    // that starts with "error: ".
    std::string Save(const std::string& folder) const;

    std::string name;
    std::int32_t id = 0;
    std::vector<EffectLayer> below;   // the layers under the edited one, bottom first
    std::string editedPath;           // where the edited text came from (empty for a new effect)
    bool editedBuiltin = false;
    std::string text;
    effectdata::RawNode tree;         // `text` as last parsed
    std::vector<std::string> problems;
    std::shared_ptr<const effectdata::Record> record;
    int applied = 0;                  // successful Apply calls (a new record for the viewer to play)
};

// `root` written as an effect file: the top level's values, its tables as [sections] and its arrays of tables as
// [[sections]], everything below them inline.
std::string WriteToml(const effectdata::RawNode& root);

} // namespace tools::effect_viewer
