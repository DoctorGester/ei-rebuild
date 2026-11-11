// Dear ImGui's platform and renderer backends on the game's own layers (imgui_layer.h): the events of the platform
// layer (src/platform) in, the renderer's overlay (src/render/render_overlay.cpp) out.
#include "tools/imgui_layer.h"

#include "imgui/imgui.h"
#include "platform/platform.h"
#include "render/render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tools::imgui_layer {

namespace {

constexpr float kFontPixels = 16.0f;          // the font's size in a 1080-pixel-high frame
constexpr float kReferenceHeight = 1080.0f;
constexpr float kSmallestScale = 0.85f;
constexpr float kLargestScale = 4.0f;

bool g_started = false;
std::string g_layoutFile;                      // imgui keeps the pointer: it lives as long as the context
std::uint32_t g_lastClock = 0;
float g_scale = 0.0f;                          // the style's scale, applied when the frame's height changes it
ImGuiStyle g_baseStyle;                        // the style at scale 1
std::vector<render::OverlayVertex> g_vertices;
std::vector<std::uint32_t> g_indices;
std::vector<render::OverlayDraw> g_draws;

ImGuiKey KeyOf(platform::Key key) {
    using platform::Key;
    const int code = static_cast<int>(key);
    if (code >= static_cast<int>(Key::A) && code <= static_cast<int>(Key::Z))
        return static_cast<ImGuiKey>(ImGuiKey_A + (code - static_cast<int>(Key::A)));
    if (code >= static_cast<int>(Key::Digit0) && code <= static_cast<int>(Key::Digit9))
        return static_cast<ImGuiKey>(ImGuiKey_0 + (code - static_cast<int>(Key::Digit0)));
    if (code >= static_cast<int>(Key::F1) && code <= static_cast<int>(Key::F12))
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (code - static_cast<int>(Key::F1)));
    if (code >= static_cast<int>(Key::Keypad0) && code < static_cast<int>(Key::Keypad0) + 10)
        return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (code - static_cast<int>(Key::Keypad0)));
    switch (key) {
    case Key::Backspace: return ImGuiKey_Backspace;
    case Key::Tab: return ImGuiKey_Tab;
    case Key::Return: return ImGuiKey_Enter;
    case Key::Shift: return ImGuiKey_LeftShift;
    case Key::Control: return ImGuiKey_LeftCtrl;
    case Key::Alt: return ImGuiKey_LeftAlt;
    case Key::Escape: return ImGuiKey_Escape;
    case Key::Space: return ImGuiKey_Space;
    case Key::PageUp: return ImGuiKey_PageUp;
    case Key::PageDown: return ImGuiKey_PageDown;
    case Key::End: return ImGuiKey_End;
    case Key::Home: return ImGuiKey_Home;
    case Key::Left: return ImGuiKey_LeftArrow;
    case Key::Up: return ImGuiKey_UpArrow;
    case Key::Right: return ImGuiKey_RightArrow;
    case Key::Down: return ImGuiKey_DownArrow;
    case Key::Insert: return ImGuiKey_Insert;
    case Key::Delete: return ImGuiKey_Delete;
    case Key::KeypadMultiply: return ImGuiKey_KeypadMultiply;
    case Key::KeypadPlus: return ImGuiKey_KeypadAdd;
    case Key::KeypadMinus: return ImGuiKey_KeypadSubtract;
    case Key::KeypadPeriod: return ImGuiKey_KeypadDecimal;
    case Key::KeypadDivide: return ImGuiKey_KeypadDivide;
    case Key::Semicolon: return ImGuiKey_Semicolon;
    case Key::Equals: return ImGuiKey_Equal;
    case Key::Comma: return ImGuiKey_Comma;
    case Key::Minus: return ImGuiKey_Minus;
    case Key::Period: return ImGuiKey_Period;
    case Key::Slash: return ImGuiKey_Slash;
    case Key::Grave: return ImGuiKey_GraveAccent;
    case Key::LeftBracket: return ImGuiKey_LeftBracket;
    case Key::Backslash: return ImGuiKey_Backslash;
    case Key::RightBracket: return ImGuiKey_RightBracket;
    case Key::Apostrophe: return ImGuiKey_Apostrophe;
    default: return ImGuiKey_None;
    }
}

int ButtonOf(platform::MouseButton button) {
    switch (button) {
    case platform::MouseButton::Left: return ImGuiMouseButton_Left;
    case platform::MouseButton::Right: return ImGuiMouseButton_Right;
    case platform::MouseButton::Middle: return ImGuiMouseButton_Middle;
    default: return -1;
    }
}

void MousePosition(ImGuiIO& io, const platform::Event& event) {
    const Point at = render::WindowToFrame(Point{event.x, event.y});
    io.AddMousePosEvent(static_cast<float>(at.x), static_cast<float>(at.y));
}

void Modifiers(ImGuiIO& io, std::uint8_t modifiers) {
    io.AddKeyEvent(ImGuiMod_Ctrl, (modifiers & platform::kModifierControl) != 0);
    io.AddKeyEvent(ImGuiMod_Shift, (modifiers & platform::kModifierShift) != 0);
    io.AddKeyEvent(ImGuiMod_Alt, (modifiers & platform::kModifierAlt) != 0);
}

// imgui's texture requests (ImGuiBackendFlags_RendererHasTextures): each made, written in part or dropped as an
// overlay texture.
void UpdateTexture(ImTextureData& texture) {
    if (texture.Status == ImTextureStatus_WantCreate) {
        const std::uint32_t id = render::MakeOverlayTexture(texture.Width, texture.Height);
        if (id != 0)
            render::WriteOverlayTexture(id, Rect{0, 0, texture.Width, texture.Height}, texture.GetPixels(),
                                        static_cast<std::size_t>(texture.GetPitch()));
        texture.SetTexID(static_cast<ImTextureID>(id));
        texture.SetStatus(ImTextureStatus_OK);
    } else if (texture.Status == ImTextureStatus_WantUpdates) {
        const auto id = static_cast<std::uint32_t>(texture.GetTexID());
        for (const ImTextureRect& r : texture.Updates)
            render::WriteOverlayTexture(id, Rect{r.x, r.y, r.x + r.w, r.y + r.h}, texture.GetPixelsAt(r.x, r.y),
                                        static_cast<std::size_t>(texture.GetPitch()));
        texture.SetStatus(ImTextureStatus_OK);
    } else if (texture.Status == ImTextureStatus_WantDestroy && texture.UnusedFrames > 0) {
        render::DropOverlayTexture(static_cast<std::uint32_t>(texture.GetTexID()));
        texture.SetTexID(ImTextureID_Invalid);
        texture.SetStatus(ImTextureStatus_Destroyed);
    }
}

// The style for frames `height` pixels high: imgui's sizes and its font scaled from a 1080-pixel frame.
void ScaleFor(int height) {
    const float scale = std::clamp(static_cast<float>(height) / kReferenceHeight, kSmallestScale, kLargestScale);
    if (scale == g_scale) return;
    g_scale = scale;
    ImGuiStyle& style = ImGui::GetStyle();
    style = g_baseStyle;
    style.ScaleAllSizes(scale);
    style.FontSizeBase = kFontPixels;
    style.FontScaleMain = scale;
}

} // namespace

void Start(const std::string& layoutFile) {
    if (g_started) return;
    g_started = true;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    g_layoutFile = layoutFile;
    io.IniFilename = g_layoutFile.empty() ? nullptr : g_layoutFile.c_str();
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.BackendPlatformName = "ei-rebuild platform layer";
    io.BackendRendererName = "ei-rebuild overlay";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
    io.Fonts->AddFontDefaultVector();
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg].w = 0.9f;
    g_baseStyle = style;
    g_lastClock = platform::FrameClock();
}

void SaveLayout() {
    if (g_started && !g_layoutFile.empty()) ImGui::SaveIniSettingsToDisk(g_layoutFile.c_str());
}

bool Feed(const platform::Event& event) {
    if (!g_started) return false;
    ImGuiIO& io = ImGui::GetIO();
    switch (event.type) {
    case platform::EventType::MouseMove:
        MousePosition(io, event);
        return io.WantCaptureMouse;
    case platform::EventType::MouseDown:
    case platform::EventType::MouseUp: {
        MousePosition(io, event);
        const int button = ButtonOf(event.button);
        if (button >= 0) io.AddMouseButtonEvent(button, event.type == platform::EventType::MouseDown);
        return event.type == platform::EventType::MouseDown && io.WantCaptureMouse;
    }
    case platform::EventType::Wheel:
        MousePosition(io, event);
        io.AddMouseWheelEvent(0.0f, event.wheel);
        return io.WantCaptureMouse;
    case platform::EventType::KeyDown:
    case platform::EventType::KeyUp: {
        Modifiers(io, event.modifiers);
        const ImGuiKey key = KeyOf(event.key);
        if (key != ImGuiKey_None) io.AddKeyEvent(key, event.type == platform::EventType::KeyDown);
        return event.type == platform::EventType::KeyDown && io.WantCaptureKeyboard;
    }
    case platform::EventType::Text: {
        // The control characters a key types (Backspace, Enter, Ctrl+letter) come as keys.
        std::string typed;
        for (char c : event.text)
            if (static_cast<unsigned char>(c) >= 0x20 && c != 0x7F) typed += c;
        if (!typed.empty()) io.AddInputCharactersUTF8(typed.c_str());
        return io.WantTextInput || io.WantCaptureKeyboard;
    }
    case platform::EventType::Focus:
        io.AddFocusEvent(event.active);
        return false;
    default:
        return false;
    }
}

bool BeginFrame() {
    if (!g_started) return false;
    const std::optional<render::FramePlacement> placed = render::FrameInWindow();
    if (!placed || placed->frame.w <= 0 || placed->frame.h <= 0) return false;
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(placed->frame.w), static_cast<float>(placed->frame.h));
    const std::uint32_t now = platform::FrameClock();
    const std::uint32_t elapsed = now - g_lastClock;
    g_lastClock = now;
    io.DeltaTime = elapsed > 0 && elapsed < 1000 ? static_cast<float>(elapsed) / 1000.0f : 1.0f / 60.0f;
    ScaleFor(placed->frame.h);
    ImGui::NewFrame();
    return true;
}

void EndFrame() {
    ImGui::Render();
    ImDrawData* data = ImGui::GetDrawData();
    if (!data) return;
    if (data->Textures)
        for (ImTextureData* texture : *data->Textures)
            if (texture->Status != ImTextureStatus_OK) UpdateTexture(*texture);
    g_vertices.clear();
    g_indices.clear();
    g_draws.clear();
    for (const ImDrawList* list : data->CmdLists) {
        const auto base = static_cast<std::uint32_t>(g_vertices.size());
        for (const ImDrawVert& v : list->VtxBuffer) g_vertices.push_back({v.pos.x, v.pos.y, v.uv.x, v.uv.y, v.col});
        for (const ImDrawCmd& cmd : list->CmdBuffer) {
            if (cmd.UserCallback || cmd.ElemCount == 0) continue;
            const float left = cmd.ClipRect.x - data->DisplayPos.x, top = cmd.ClipRect.y - data->DisplayPos.y;
            const float right = cmd.ClipRect.z - data->DisplayPos.x, bottom = cmd.ClipRect.w - data->DisplayPos.y;
            if (right <= left || bottom <= top) continue;
            const auto at = static_cast<std::uint32_t>(g_indices.size());
            for (unsigned i = 0; i < cmd.ElemCount; ++i)
                g_indices.push_back(base + cmd.VtxOffset + list->IdxBuffer[static_cast<int>(cmd.IdxOffset + i)]);
            const Rect clip{static_cast<int>(std::floor(left)), static_cast<int>(std::floor(top)),
                            static_cast<int>(std::ceil(right)), static_cast<int>(std::ceil(bottom))};
            g_draws.push_back({at, cmd.ElemCount, static_cast<std::uint32_t>(cmd.GetTexID()), clip});
        }
    }
    if (!g_draws.empty()) render::OverlayTriangles(g_vertices, g_indices, g_draws);
}

} // namespace tools::imgui_layer
