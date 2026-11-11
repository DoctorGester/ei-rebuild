// The effect viewer's windows (effect_viewer_internal.h, DrawPanel), docked over the main window (imgui's dockspace,
// the scene showing through its centre): the effects to pick; the timeline with the playing controls; the inputs with
// the source's values, its place and the figure; the data effect's fields and its text, each with the file's save.
#include "tools/effect_viewer_internal.h"
#include "tools/effect_viewer_scene.h"

#include "engine/models/texture.h"
#include "engine/particles/particle_data.h"
#include "engine/particles/particle_source.h"

#include <imgui/imgui.h>
#include <imgui/imgui_internal.h>   // the first layout (DockBuilder)

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
    // The list fills the window above the two rows of buttons below it.
    const float listHeight = std::max(ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.2f,
                                      ImGui::GetTextLineHeightWithSpacing() * 9.0f);
    if (ImGui::BeginListBox("##effects", ImVec2(-1.0f, listHeight))) {
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
    if (ImGui::Button("Scan again")) v.Scan();
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
    ViewerScene& scene = g_scene;
    if (ImGui::RadioButton("On the figure", v.onFigure) && !v.onFigure) v.PlaceOnFigure();
    ImGui::SameLine();
    if (ImGui::RadioButton("Free", !v.onFigure) && v.onFigure)
        v.PlaceFree(LengthSquared(v.freeAt) == 0.0f ? scene.camera.focus : v.freeAt);
    if (v.onFigure && scene.figure) {
        // The figure's points: the file's way and the origin, the game's points and the parts' bones in one list, the
        // other bones (a few hundred) in a second one with a filter.
        using Group = FigurePoint::Group;
        const std::vector<FigurePoint> points = scene.Points();
        const bool onBone = v.point.group == Group::Bones;
        std::string shown = onBone ? "(another bone)" : v.point.label.empty() ? points.front().label : v.point.label;
        if (!onBone && v.point.kind == FigurePoint::Kind::File && v.source && v.source->data) {
            const std::string_view part = effectdata::PartName(v.source->data->record->part);
            shown += part.empty() ? std::string(": none, the origin") : ": " + std::string(part);
        }
        const auto listed = [&v](const FigurePoint& at, const FigurePoint* before) {
            if (before && before->group != at.group) ImGui::Separator();
            // The file's way and the origin by their kind alone (the viewer's start point has no name).
            const bool named = at.kind == FigurePoint::Kind::Game || at.kind == FigurePoint::Kind::Named;
            const bool selected = at.kind == v.point.kind && (!named || at.name == v.point.name);
            if (ImGui::Selectable(at.label.c_str(), selected)) v.PlaceAt(at);
            if (selected && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        };
        if (ImGui::BeginCombo("Point", shown.c_str(), ImGuiComboFlags_HeightLarge)) {
            const FigurePoint* before = nullptr;
            for (const FigurePoint& at : points)
                if (at.group != Group::Bones) {
                    listed(at, before);
                    before = &at;
                }
            ImGui::EndCombo();
        }
        static ImGuiTextFilter filter;
        if (ImGui::BeginCombo("Other bone", onBone ? v.point.label.c_str() : "-", ImGuiComboFlags_HeightLarge)) {
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            filter.Draw("Filter");
            for (const FigurePoint& at : points)
                if (at.group == Group::Bones && filter.PassFilter(at.label.c_str())) listed(at, nullptr);
            ImGui::EndCombo();
        }
    }
    if (!v.onFigure) {
        Vector3 at = v.freeAt;
        if (ImGui::DragFloat3("At", &at.x, 0.05f)) v.PlaceFree(at);
        if (ImGui::Button("At the camera's focus")) v.PlaceFree(scene.camera.focus);
    }
}

// The figure: its animation, its walk.
void DrawFigure(Viewer& v) {
    ViewerScene& scene = g_scene;
    if (!scene.figure) {
        ImGui::TextDisabled("No figure (the install has no %s).", scene.model.c_str());
        return;
    }
    const std::vector<std::string> names = scene.AnimationNames();
    const char* shown = scene.animation >= 0 && scene.animation < static_cast<int>(names.size())
                            ? names[static_cast<std::size_t>(scene.animation)].c_str()
                            : "-";
    if (ImGui::BeginCombo("Animation", shown)) {
        for (std::size_t i = 0; i < names.size(); ++i)
            if (ImGui::Selectable(names[i].c_str(), static_cast<int>(i) == scene.animation))
                scene.PlayAnimation(static_cast<int>(i));
        ImGui::EndCombo();
    }
    bool walk = scene.walking, run = scene.host.running;
    const bool walkChanged = ImGui::Checkbox("Walk a circle", &walk);
    ImGui::SameLine();
    if (walkChanged | ImGui::Checkbox("Running", &run)) {
        scene.SetGait(walk, run);
        if (walkChanged && v.onFigure) v.Restart(v.tick);
    }
    if (scene.walking) {
        ImGui::DragFloat("Speed", &scene.walkSpeed, 0.005f, 0.0f, 2.0f, "%.3f");
        ImGui::DragFloat("Radius", &scene.circleRadius, 0.05f, 0.5f, 20.0f, "%.2f");
    }
    ImGui::Checkbox("World paused", &scene.paused);
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

void DrawFileHeader(const EffectDocument& d) {
    ImGui::TextWrapped("%s%s", d.editedPath.empty() ? "(a new effect)" : d.editedPath.c_str(),
                       d.editedBuiltin ? "  (built in: saved whole into the folder below)" : "");
    if (!d.below.empty()) ImGui::TextDisabled("over %d file(s) below it", static_cast<int>(d.below.size()));
}

void DrawProblemsAndSave(Viewer& v, const char* id) {
    const EffectDocument& d = v.document;
    for (const std::string& problem : d.problems) ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "%s", problem.c_str());
    ImGui::PushID(id);
    ImGui::SeparatorText("Save");
    InputString("Folder", &v.saveFolder);
    ImGui::TextDisabled("into <folder>\\Res\\effects\\%s.effect.toml", d.name.c_str());
    if (ImGui::Button("Save")) v.Save();
    ImGui::PopID();
}

void DrawFields(Viewer& v) {
    if (!v.hasDocument) {
        ImGui::TextDisabled("The effect has no file.");
        return;
    }
    EffectDocument& d = v.document;
    DrawFileHeader(d);
    bool changed = false;
    for (auto& [key, value] : d.tree.keys) changed |= DrawNode(key.c_str(), value);
    if (changed) v.Edited(true);
    DrawProblemsAndSave(v, "fields");
}

void DrawText(Viewer& v) {
    if (!v.hasDocument) {
        ImGui::TextDisabled("The effect has no file.");
        return;
    }
    EffectDocument& d = v.document;
    DrawFileHeader(d);
    const float rest = ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4.0f;
    if (InputStringLines("##text", &d.text, ImVec2(-1.0f, std::max(rest, ImGui::GetTextLineHeight() * 8.0f))))
        v.Edited(false);
    DrawProblemsAndSave(v, "text");
}

// The windows' names: imgui's ids, and the layout file's.
constexpr const char* kEffectsWindow = "Effects";
constexpr const char* kTimelineWindow = "Timeline";
constexpr const char* kInputsWindow = "Inputs";
constexpr const char* kFieldsWindow = "Fields";
constexpr const char* kTextWindow = "Text";

// The first layout (no layout file yet, or one without the dockspace): the effects and the timeline down the left,
// the inputs and the file (fields and text as tabs) down the right, the scene in the middle.
void DefaultLayout(ImGuiID dockspace, const ImGuiViewport* viewport) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);
    ImGuiID centre = dockspace;
    const ImGuiID left = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Left, 0.22f, nullptr, &centre);
    const ImGuiID right = ImGui::DockBuilderSplitNode(centre, ImGuiDir_Right, 0.30f, nullptr, &centre);
    ImGuiID leftBottom = 0, rightBottom = 0;
    const ImGuiID leftTop = ImGui::DockBuilderSplitNode(left, ImGuiDir_Up, 0.55f, nullptr, &leftBottom);
    const ImGuiID rightTop = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.42f, nullptr, &rightBottom);
    ImGui::DockBuilderDockWindow(kEffectsWindow, leftTop);
    ImGui::DockBuilderDockWindow(kTimelineWindow, leftBottom);
    ImGui::DockBuilderDockWindow(kInputsWindow, rightTop);
    ImGui::DockBuilderDockWindow(kFieldsWindow, rightBottom);
    ImGui::DockBuilderDockWindow(kTextWindow, rightBottom);
    ImGui::DockBuilderFinish(dockspace);
}

} // namespace

void DrawPanel(Viewer& v) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspace = ImGui::GetID("ViewerDock");
    if (!ImGui::DockBuilderGetNode(dockspace)) DefaultLayout(dockspace, viewport);
    ImGui::DockSpaceOverViewport(dockspace, viewport, ImGuiDockNodeFlags_PassthruCentralNode);

    const EffectEntry* entry = v.picked >= 0 ? &v.entries[static_cast<std::size_t>(v.picked)] : nullptr;
    if (ImGui::Begin(kEffectsWindow)) DrawEffects(v);
    ImGui::End();

    if (ImGui::Begin(kTimelineWindow)) {
        if (entry) {
            ImGui::SeparatorText(entry->name.c_str());
            DrawPlaying(v);
            DrawState(v);
            ImGui::SeparatorText("Changes");
            DrawTimeline(v);
        } else {
            ImGui::TextDisabled("Pick an effect.");
        }
        if (!v.status.empty()) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.4f, 1.0f), "%s", v.status.c_str());
    }
    ImGui::End();

    if (ImGui::Begin(kInputsWindow)) {
        if (entry) DrawDriving(v);
        if (ImGui::CollapsingHeader("Place", ImGuiTreeNodeFlags_DefaultOpen)) DrawPlace(v);
        if (ImGui::CollapsingHeader("Figure", ImGuiTreeNodeFlags_DefaultOpen)) DrawFigure(v);
    }
    ImGui::End();

    if (ImGui::Begin(kFieldsWindow)) DrawFields(v);
    ImGui::End();
    if (ImGui::Begin(kTextWindow)) DrawText(v);
    ImGui::End();
    // A window a command picked ("tab") comes to the front of its tabs once; then the tabs follow the clicks.
    if (v.documentTab >= 0) ImGui::SetWindowFocus(v.documentTab == 1 ? kTextWindow : kFieldsWindow);
    v.documentTab = -1;
}

} // namespace tools::effect_viewer
