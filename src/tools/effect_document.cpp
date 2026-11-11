// The effect viewer's document (effect_document.h): an effect's layers read as the registry reads them
// (src/engine/particles/particle_data.cpp, Registry::Load), the edited text read over them, written back as TOML.
#include "tools/effect_document.h"

#include "engine/data/resource.h"
#include "engine/particles/particle_data.h"

#include "lib/toml.h"
#include "platform/platform.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tools::effect_viewer {

namespace {

using effectdata::RawNode;

constexpr std::string_view kFolder = "Res\\effects";
constexpr std::string_view kExtension = ".effect.toml";

bool IsTableArray(const RawNode& node) {
    if (node.kind != RawNode::Kind::Array || node.items.empty()) return false;
    for (const RawNode& item : node.items)
        if (item.kind != RawNode::Kind::Table) return false;
    return true;
}

// A section of its own at the top level: a table or an array of tables.
bool IsSection(const RawNode& node) { return node.kind == RawNode::Kind::Table || IsTableArray(node); }

std::string ValueText(const RawNode& node) {
    switch (node.kind) {
    case RawNode::Kind::String: return toml::Text(node.text);
    case RawNode::Kind::Integer: return toml::Int(static_cast<std::int64_t>(std::llround(node.number)));
    case RawNode::Kind::Float: return toml::Float(static_cast<float>(node.number));
    case RawNode::Kind::Bool: return toml::Bool(node.flag);
    case RawNode::Kind::Array: {
        std::vector<std::string> items;
        for (const RawNode& item : node.items) items.push_back(ValueText(item));
        return toml::Array(items);
    }
    case RawNode::Kind::Table: {
        std::vector<std::pair<std::string, std::string>> pairs;
        for (const auto& [key, value] : node.keys) pairs.emplace_back(key, ValueText(value));
        return toml::InlineTable(pairs);
    }
    case RawNode::Kind::Missing: break;
    }
    return "\"\"";
}

void Pairs(toml::Writer& writer, const RawNode& table) {
    for (const auto& [key, value] : table.keys)
        if (value.Exists()) writer.Pair(key, ValueText(value));
}

// `text` parsed as `label` into `node`; false with the parser's message in `problems`.
bool Parse(std::string_view text, const std::string& label, RawNode* node, std::vector<std::string>* problems) {
    toml::Document document;
    if (!document.Parse(text, label)) {
        problems->push_back(document.Error());
        return false;
    }
    *node = effectdata::Copy(document.Root(), label);
    return true;
}

std::string WithoutTrailingSeparators(std::string folder) {
    while (folder.size() > 1 && (folder.back() == '\\' || folder.back() == '/')) folder.pop_back();
    return folder;
}

} // namespace

std::string WriteToml(const RawNode& root) {
    toml::Writer writer;
    for (const auto& [key, value] : root.keys)
        if (value.Exists() && !IsSection(value)) writer.Pair(key, ValueText(value));
    for (const auto& [key, value] : root.keys) {
        if (value.kind == RawNode::Kind::Table) {
            writer.Table(toml::Key(key));
            Pairs(writer, value);
        } else if (IsTableArray(value)) {
            for (const RawNode& item : value.items) {
                writer.ArrayTable(toml::Key(key));
                Pairs(writer, item);
            }
        }
    }
    return writer.text;
}

bool EffectDocument::Load(std::string_view effect, std::int32_t effectId) {
    *this = EffectDocument{};
    name = std::string(effect);
    id = effectId;
    std::vector<EffectLayer> layers;
    const std::string_view builtin = effectdata::BuiltinText(effect);
    const RecordFile file = FindRecordFile(std::string(kFolder) + "\\" + name + ".effect", false);
    if (!builtin.empty() && !file.replaced)
        layers.push_back({"assets\\effects\\" + name + std::string(kExtension), std::string(builtin), true});
    for (auto toml = file.tomls.rbegin(); toml != file.tomls.rend(); ++toml) layers.push_back({toml->path, toml->text, false});
    if (layers.empty()) return false;
    EffectLayer top = std::move(layers.back());
    layers.pop_back();
    below = std::move(layers);
    editedPath = top.path;
    editedBuiltin = top.builtin;
    text = std::move(top.text);
    Apply();
    return true;
}

void EffectDocument::Start(std::string_view effect, std::string_view startText) {
    *this = EffectDocument{};
    name = std::string(effect);
    id = effectdata::IdOf(effect);
    text = std::string(startText);
    Apply();
}

bool EffectDocument::Apply() {
    std::vector<std::string> said;
    RawNode edited;
    const std::string label = editedPath.empty() ? name + std::string(kExtension) : editedPath;
    if (!Parse(text, label, &edited, &said)) {
        problems = std::move(said);
        return false;
    }
    tree = edited;
    RawNode merged;
    for (const EffectLayer& layer : below) {
        RawNode node;
        if (!Parse(layer.text, layer.path, &node, &said)) continue;
        if (!merged.Exists()) merged = std::move(node);
        else effectdata::MergeOver(merged, node);
    }
    if (!merged.Exists()) merged = std::move(edited);
    else effectdata::MergeOver(merged, edited);
    if (!effectdata::ResolveLike(merged, name, &said)) {
        problems = std::move(said);
        problems.push_back(name + " does not load: it shows nothing");
        return false;
    }
    std::shared_ptr<const effectdata::Record> read = effectdata::Read(merged, name, &said, id);
    problems = std::move(said);
    if (!read) {
        problems.push_back(name + " does not load: it shows nothing");
        return false;
    }
    record = std::move(read);
    ++applied;
    return true;
}

bool EffectDocument::ApplyTree() {
    text = WriteToml(tree);
    return Apply();
}

std::string EffectDocument::Save(const std::string& folder) const {
    const std::string root = WithoutTrailingSeparators(folder);
    if (root.empty()) return "error: no folder to save into";
    const std::string res = root + "\\Res";
    const std::string effects = res + "\\effects";
    for (const std::string& level : {root, res, effects})
        if (!platform::PathExists(level)) platform::CreateFolder(level);
    const std::string path = effects + "\\" + name + std::string(kExtension);
    std::string bytes = text;
    if (editedBuiltin && !tree["replace"].Exists()) bytes = "replace = true\n" + bytes;
    std::uint32_t error = 0;
    if (!platform::WriteWholeFile(path, bytes, &error))
        return "error: " + path + " could not be written (error " + std::to_string(error) + ")";
    return path;
}

} // namespace tools::effect_viewer
