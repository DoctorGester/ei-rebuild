// The effect viewer's panel (effect_viewer_internal.h, DrawPanel): the effects to pick, the playing controls, the
// place, the source's values and the data effect's inputs, the timeline, and the data effect's file to edit and save.
#include "tools/effect_viewer_internal.h"

#include "game/effects/effects.h"
#include "engine/particles/particle_data.h"
#include "engine/models/texture.h"
#include "game/world/camera.h"
#include "game/app/singletons.h"
#include "game/units/unit.h"

#include "render/render.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace tools::effect_viewer {

namespace {

using effectdata::RawNode;

const char* OriginLabel(Origin origin) {
    switch (origin) {
    case Origin::Builtin: return "data";
    case Origin::Mod: return "mod";
    }
    return "";
}

int ResizeString(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

bool InputString(const char* label, std::string* text, ImGuiInputTextFlags flags = 0) {
    return ImGui::InputText(label, text->data(), text->capacity() + 1, flags | ImGuiInputTextFlags_CallbackResize,
                            &ResizeString, text);
}

bool InputStringLines(const char* label, std::string* text, const ImVec2& size) {
    return ImGui::InputTextMultiline(label, text->data(), text->capacity() + 1, size,
                                     ImGuiInputTextFlags_CallbackResize | ImGuiInputTextFlags_AllowTabInput, &ResizeString,
                                     text);
}

// ---- The effects ------------------------------------------------------------------------------------------------------

void DrawEffects(Viewer& v) {
    static ImGuiTextFilter filter;
    filter.Draw("Filter");
    if (ImGui::BeginListBox("##effects", ImVec2(-1.0f, ImGui::GetTextLineHeightWithSpacing() * 9.0f))) {
        for (std::size_t i = 0; i < v.entries.size(); ++i) {
            const EffectEntry& entry = v.entries[i];
            if (!filter.PassFilter(entry.name.c_str())) continue;
            char label[160];
            std::snprintf(label, sizeof(label), "%-22s %s", entry.name.c_str(),
                          entry.madeText.empty() ? OriginLabel(entry.origin) : "made");
            const bool selected = v.picked == static_cast<int>(i);
            if (ImGui::Selectable(label, selected)) v.Pick(static_cast<int>(i));
            if (selected && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        }
        ImGui::EndListBox();
    }
    if (ImGui::Button("Scan again")) {
        const std::string name = v.picked >= 0 ? v.entries[static_cast<std::size_t>(v.picked)].name : std::string();
        v.Scan();
        v.picked = name.empty() ? -1 : v.Find(name);
    }
    InputString("##newname", &v.newName);
    ImGui::SameLine();
    if (ImGui::Button("New effect")) v.NewEffect(v.newName);
}

// ---- Playing ----------------------------------------------------------------------------------------------------------

void DrawPlaying(Viewer& v) {
    if (ImGui::Button(v.playing ? "Pause" : "Play")) {
        v.playing = !v.playing;
        if (!v.playing) v.Freeze();
    }
    ImGui::SameLine();
    if (ImGui::Button("Step")) {
        v.playing = false;
        v.Step();
        v.Freeze();
    }
    ImGui::SameLine();
    if (ImGui::Button("Restart")) v.Restart(0);
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &v.loop);

    int tick = v.tick;
    const int last = std::max({v.furthest, v.tick, 1});
    if (ImGui::SliderInt("Tick", &tick, 0, last)) {
        v.playing = false;
        v.Restart(tick);
    }
    float size = v.size;
    if (ImGui::DragFloat("Size", &size, 0.01f, 0.05f, 20.0f, "%.2f")) {
        v.size = std::max(size, 0.05f);
        v.Restart(v.tick);
    }
}

// ---- The place --------------------------------------------------------------------------------------------------------

void DrawPlace(Viewer& v) {
    bool onUnit = v.onUnit;
    if (ImGui::RadioButton("On a unit", onUnit) && !onUnit) {
        if (Unit* unit = v.Carrier() ? v.Carrier() : v.PartyUnit()) v.PlaceOnUnit(unit->id);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Free", !onUnit) && onUnit) v.PlaceFree(LengthSquared(v.freeAt) == 0.0f ? g_camera.focus : v.freeAt);

    if (v.onUnit) {
        const Unit* carrier = v.Carrier();
        char current[96];
        std::snprintf(current, sizeof(current), "%s (%d)", carrier ? carrier->name.c_str() : "gone", v.unitId);
        if (ImGui::BeginCombo("Unit", current)) {
            for (Unit* unit : v.NearbyUnits()) {
                char label[96];
                std::snprintf(label, sizeof(label), "%s (%d)%s", unit->name.c_str(), unit->id,
                              (unit->flags & MapObjectFlag::InParty) ? "  party" : "");
                if (ImGui::Selectable(label, unit->id == v.unitId)) v.PlaceOnUnit(unit->id);
            }
            ImGui::EndCombo();
        }
        if (carrier && !(carrier->flags & MapObjectFlag::InParty))
            ImGui::TextDisabled("A source on a unit out of the party is drawn only when its unit is shown.");
    } else {
        Vector3 at = v.freeAt;
        if (ImGui::DragFloat3("At", &at.x, 0.05f)) v.PlaceFree(at);
        if (ImGui::Button("At the camera's focus")) v.PlaceFree(g_camera.focus);
    }
}

// ---- The source's values and inputs -----------------------------------------------------------------------------------

void DrawDriving(Viewer& v) {
    ParticleSource* s = v.source;
    if (!s) {
        ImGui::TextDisabled("No source.");
        return;
    }
    if (ImGui::Button("Start emitting")) v.Make({0, ChangeKind::StartEmitting, {}, {}}, true);
    ImGui::SameLine();
    if (ImGui::Button("Stop emitting")) v.Make({0, ChangeKind::StopEmitting, {}, {}}, true);
    ImGui::SameLine();
    if (ImGui::Button("Stop")) v.Make({0, ChangeKind::Stop, {}, {}}, true);

    float alpha = s->alpha;
    if (ImGui::DragFloat("alpha", &alpha, 0.01f)) v.Make({0, ChangeKind::Alpha, {}, {alpha, 0.0f, 0.0f}}, true);
    float param = s->typeParam;
    if (ImGui::DragFloat("param", &param, 0.01f)) v.Make({0, ChangeKind::Param, {}, {param, 0.0f, 0.0f}}, true);
    Vector3 extent = s->extent;
    if (ImGui::DragFloat3("extent", &extent.x, 0.01f)) v.Make({0, ChangeKind::Extent, {}, extent}, true);

    if (!s->data) return;
    const effectdata::State& state = *s->data;
    if (!state.record->inputs.empty()) ImGui::SeparatorText("Inputs");
    for (const effectdata::Input& input : state.record->inputs) {
        const auto slot = static_cast<std::size_t>(input.slot);
        ImGui::PushID(input.name.c_str());
        switch (input.type) {
        case effectdata::InputType::Float: {
            float value = state.inputs[slot];
            if (ImGui::DragFloat(input.name.c_str(), &value, 0.01f))
                v.Make({0, ChangeKind::Value, input.name, {value, 0.0f, 0.0f}}, true);
            break;
        }
        case effectdata::InputType::Bool: {
            bool value = state.inputs[slot] != 0.0f;
            if (ImGui::Checkbox(input.name.c_str(), &value))
                v.Make({0, ChangeKind::Value, input.name, {value ? 1.0f : 0.0f, 0.0f, 0.0f}}, true);
            break;
        }
        case effectdata::InputType::Enum: {
            const int index = static_cast<int>(state.inputs[slot]);
            const char* shown = index >= 0 && index < static_cast<int>(input.values.size())
                                    ? input.values[static_cast<std::size_t>(index)].c_str()
                                    : "?";
            if (ImGui::BeginCombo(input.name.c_str(), shown)) {
                for (std::size_t i = 0; i < input.values.size(); ++i)
                    if (ImGui::Selectable(input.values[i].c_str(), static_cast<int>(i) == index))
                        v.Make({0, ChangeKind::Value, input.name, {static_cast<float>(i), 0.0f, 0.0f}}, true);
                ImGui::EndCombo();
            }
            break;
        }
        case effectdata::InputType::Vector: {
            Vector3 value{state.inputs[slot], state.inputs[slot + 1], state.inputs[slot + 2]};
            if (ImGui::DragFloat3(input.name.c_str(), &value.x, 0.01f))
                v.Make({0, ChangeKind::Vector, input.name, value}, true);
            break;
        }
        case effectdata::InputType::Event:
            if (ImGui::Button(input.name.c_str())) v.Make({0, ChangeKind::Fire, input.name, {}}, true);
            ImGui::SameLine();
            ImGui::TextDisabled("event");
            break;
        }
        ImGui::PopID();
    }
}

const char* ChangeName(ChangeKind kind) {
    switch (kind) {
    case ChangeKind::StartEmitting: return "start emitting";
    case ChangeKind::StopEmitting: return "stop emitting";
    case ChangeKind::Stop: return "stop";
    case ChangeKind::Value: return "set";
    case ChangeKind::Vector: return "set";
    case ChangeKind::Fire: return "fire";
    case ChangeKind::Alpha: return "alpha";
    case ChangeKind::Param: return "param";
    case ChangeKind::Extent: return "extent";
    case ChangeKind::Turn: return "turn";
    }
    return "";
}

void DrawTimeline(Viewer& v) {
    if (v.timeline.empty()) {
        ImGui::TextDisabled("Nothing changed yet: what you change is made again at its tick on every replay.");
        return;
    }
    for (const Change& change : v.timeline) {
        if (change.kind == ChangeKind::Vector || change.kind == ChangeKind::Extent)
            ImGui::Text("%5d  %s %s %.3g %.3g %.3g", change.tick, ChangeName(change.kind), change.name.c_str(), change.value.x,
                        change.value.y, change.value.z);
        else if (change.kind == ChangeKind::Value || change.kind == ChangeKind::Alpha || change.kind == ChangeKind::Param ||
                 change.kind == ChangeKind::Turn)
            ImGui::Text("%5d  %s %s %.3g", change.tick, ChangeName(change.kind), change.name.c_str(), change.value.x);
        else
            ImGui::Text("%5d  %s %s", change.tick, ChangeName(change.kind), change.name.c_str());
    }
    if (ImGui::Button("Clear the timeline")) {
        v.timeline.clear();
        v.Restart(v.tick);
    }
}

void DrawState(const Viewer& v) {
    const ParticleSource* s = v.source;
    if (!s) return;
    ImGui::Text("tick %d   particles %d%s", v.tick, static_cast<int>(s->particles.size()), v.ended ? "   ended" : "");
    if (!v.children.empty()) {
        int particles = 0;
        for (const ParticleSource* child : v.children) particles += static_cast<int>(child->particles.size());
        ImGui::Text("sub-effects %d (particles %d)", static_cast<int>(v.children.size()), particles);
    }
    ImGui::Text("texture %s", s->texture ? s->texture->name.c_str() : "(none)");
    if (s->data) {
        const effectdata::State& state = *s->data;
        const char* stage = state.stage >= 0 && state.stage < static_cast<int>(state.record->stages.size())
                                ? state.record->stages[static_cast<std::size_t>(state.stage)].name.c_str()
                                : "-";
        ImGui::Text("stage %s (%d ticks in)   age %d   emitted %d", stage, state.stageAge, state.age, state.total);
    }
}

// ---- The document ----------------------------------------------------------------------------------------------------

// One field of the tree: true when it changed.
bool DrawNode(const char* key, RawNode& node) {
    bool changed = false;
    ImGui::PushID(key);
    switch (node.kind) {
    case RawNode::Kind::Table:
        if (ImGui::TreeNode(key)) {
            for (auto& [name, value] : node.keys) changed |= DrawNode(name.c_str(), value);
            ImGui::TreePop();
        }
        break;
    case RawNode::Kind::Array:
        if (ImGui::TreeNode(key, "%s [%d]", key, static_cast<int>(node.items.size()))) {
            for (std::size_t i = 0; i < node.items.size(); ++i) {
                RawNode& item = node.items[i];
                const RawNode& itemName = item.kind == RawNode::Kind::Table ? item["name"] : item;
                const RawNode& itemEvent = item.kind == RawNode::Kind::Table ? item["event"] : item;
                char label[96];
                if (item.kind == RawNode::Kind::Table && itemName.kind == RawNode::Kind::String)
                    std::snprintf(label, sizeof(label), "%d %s", static_cast<int>(i), itemName.text.c_str());
                else if (item.kind == RawNode::Kind::Table && itemEvent.kind == RawNode::Kind::String)
                    std::snprintf(label, sizeof(label), "%d on %s", static_cast<int>(i), itemEvent.text.c_str());
                else
                    std::snprintf(label, sizeof(label), "%d", static_cast<int>(i));
                changed |= DrawNode(label, item);
            }
            ImGui::TreePop();
        }
        break;
    case RawNode::Kind::String:
        changed = InputString(key, &node.text, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit();
        break;
    case RawNode::Kind::Integer: {
        int value = static_cast<int>(node.number);
        if (ImGui::DragInt(key, &value)) {
            node.number = value;
            changed = true;
        }
        break;
    }
    case RawNode::Kind::Float: {
        float value = static_cast<float>(node.number);
        if (ImGui::DragFloat(key, &value, 0.01f, 0.0f, 0.0f, "%.4g")) {
            node.number = value;
            changed = true;
        }
        break;
    }
    case RawNode::Kind::Bool:
        changed = ImGui::Checkbox(key, &node.flag);
        break;
    case RawNode::Kind::Missing: break;
    }
    ImGui::PopID();
    return changed;
}

void DrawDocument(Viewer& v) {
    EffectDocument& d = v.document;
    ImGui::TextWrapped("%s%s", d.editedPath.c_str(), d.editedBuiltin ? "  (built in: saved whole into the folder below)" : "");
    if (!d.below.empty()) ImGui::TextDisabled("over %d file(s) below it", static_cast<int>(d.below.size()));
    if (ImGui::BeginTabBar("document")) {
        if (ImGui::BeginTabItem("Fields", nullptr, v.documentTab == 0 ? ImGuiTabItemFlags_SetSelected : 0)) {
            bool changed = false;
            for (auto& [key, value] : d.tree.keys) changed |= DrawNode(key.c_str(), value);
            if (changed) v.Edited(true);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Text", nullptr, v.documentTab == 1 ? ImGuiTabItemFlags_SetSelected : 0)) {
            if (InputStringLines("##text", &d.text, ImVec2(-1.0f, ImGui::GetTextLineHeight() * 24.0f))) v.Edited(false);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    // A tab picked by a command ("tab") is picked once; then the tabs follow the clicks.
    v.documentTab = -1;
    for (const std::string& problem : d.problems) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "%s", problem.c_str());
    ImGui::SeparatorText("Save");
    InputString("Folder", &v.saveFolder);
    ImGui::TextDisabled("into <folder>\\Res\\effects\\%s.effect.toml", d.name.c_str());
    if (ImGui::Button("Save")) v.Save();
}

} // namespace

void DrawPanel(Viewer& v) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float width = std::min(viewport->Size.x * 0.4f, ImGui::GetFontSize() * 34.0f);
    ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(width, viewport->Size.y), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Effects")) {
        ImGui::End();
        return;
    }
    if (ImGui::CollapsingHeader("Effect", ImGuiTreeNodeFlags_DefaultOpen)) DrawEffects(v);
    if (v.picked >= 0) {
        const EffectEntry& entry = v.entries[static_cast<std::size_t>(v.picked)];
        ImGui::SeparatorText(entry.name.c_str());
        DrawPlaying(v);
        DrawState(v);
        if (ImGui::CollapsingHeader("Place", ImGuiTreeNodeFlags_DefaultOpen)) DrawPlace(v);
        if (ImGui::CollapsingHeader("Drive", ImGuiTreeNodeFlags_DefaultOpen)) DrawDriving(v);
        if (ImGui::CollapsingHeader("Timeline")) DrawTimeline(v);
        if (v.hasDocument && ImGui::CollapsingHeader("File", ImGuiTreeNodeFlags_DefaultOpen)) DrawDocument(v);
    }
    if (!v.status.empty()) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", v.status.c_str());
    ImGui::End();
}

} // namespace tools::effect_viewer
