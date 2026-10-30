// The selection marker: a flat arrow
// under each selected unit, drawn by the world's draw pass in single-player mode. Its six
// corners lie 0.3 above the given point, in the plane of the unit's x and y axes; the device's
// flat geometry draw projects them with the scene's matrices. See docs/notes/renderer.md.
#include "figure_mesh.h"   // VertexPacket

#include "model.h"         // SceneRenderer
#include "texture.h"       // TextureDevice
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "lib/vecmath.h"
#include "render/render.h"
#include "render/render_state.h"

#include <bit>
#include <cmath>
#include <cstdint>

namespace {

// The arrow's proportions: the unit length is a third of the size.
constexpr float kThird = 0.33333334f;   // 1/3
static_assert(std::bit_cast<std::uint32_t>(kThird) == 0x3EAAAAABu);
constexpr float kWing = 3.0f;          // half the width of the arrow's back
constexpr float kShoulder = 1.4f;      // the shoulders, along y
constexpr float kNotch = 4.0f;         // the notch in the back, along -y
constexpr float kTip = 5.0f;           // the tip, along -y
constexpr float kLift = 0.3f;          // above the point

// The draw: 8 vertices in two blocks (the last two unused), six triangles.
constexpr int kBlocks = 2;
constexpr int kVertices = kBlocks * 4;
constexpr std::uint32_t kColour = 0x8000FF00;     // half-transparent green
constexpr std::uint32_t kSpecular = 0xFF000000;
constexpr std::uint16_t kIndices[18] = {0, 4, 3, 0, 1, 4, 1, 5, 4, 1, 2, 5, 2, 3, 5, 2, 0, 3};

void SetVertex(VertexPacket* blocks, int index, float x, float y, float z) {
    float(&block)[4][4] = blocks[index >> 2].block;
    block[0][index & 3] = x;
    block[1][index & 3] = y;
    block[2][index & 3] = z;
    block[3][index & 3] = 1.0f;
}

} // namespace

int SceneRenderer::DrawSelectionMarker(const Vector3& point, float size, const Vector3& axisX, const Vector3& axisY) {
    const Vector3& p = point;
    const Vector3& x = axisX;
    const Vector3& y = axisY;
    float unit = size * kThird;
    float twice = unit + unit;
    float z = p.z + kLift;

    // Each corner is summed so: the scaled axes are rounded to floats first.
    Vector3 across = (unit * kWing) * x;
    Vector3 back = twice * y;
    Vector3 shoulder = (unit * kShoulder) * y;
    Vector3 side = twice * x;
    Vector3 corners[6] = {
        (p + across) + back,                // 0, 1: the back's ends
        (p - across) + back,
        p - (unit * kTip) * y,              // 2: the tip
        shoulder + (side + p),              // 3, 4: the shoulders
        shoulder + (p - side),
        p - (unit * kNotch) * y,            // 5: the notch
    };

    VertexPacket blocks[kBlocks];
    for (int i = 0; i < 6; ++i) SetVertex(blocks, i, corners[i].x, corners[i].y, z);
    SetVertex(blocks, 6, p.x, p.y, z);
    SetVertex(blocks, 7, p.x, p.y, z);
    float uvs[kVertices][2] = {};
    std::uint16_t indices[18];
    for (int i = 0; i < 18; ++i) indices[i] = kIndices[i];
    device->DrawFlatBlocks(blocks, uvs, kBlocks, indices, 18, kColour, kSpecular);
    return 1;
}

// --- Renderer rect -----------------------------------------------------------------------------------

// The renderer's rectangle draw: a black rectangle with the given alpha, filled as two
// pre-transformed triangles over the current blend state, and with `outline` a white one-pixel frame drawn as
// a line strip around it (the fill then stays a pixel inside). Used for the rubber-band selection and the
// interface's fades. The new renderer (src/render) takes both draws. See docs/notes/renderer.md.

namespace {

void SetStage0Ops(render::TextureOp op) {
    render::g_renderState.stages[0].combiner.colourOp = op;
    render::g_renderState.stages[0].combiner.alphaOp = op;
}

} // namespace

// Returns 0 when the rectangle is inverted (right < left or bottom < top), else 1.
int SceneRenderer::DrawRectangle(int left, int top, int right, int bottom, int alphaArgument, int outlineArgument) {
    const char alpha = static_cast<char>(alphaArgument);   // the low bytes count
    const char outline = static_cast<char>(outlineArgument);
    if (right < left || bottom < top) return 0;
    TextureDevice* currentDevice = device;
    std::uint32_t packed = static_cast<std::uint32_t>(static_cast<std::uint8_t>(alpha)) << 24;
    // With the outline the fill stays a pixel inside it.
    float inset = outline ? 1.0f : 0.0f;
    float x0 = static_cast<float>(left), y0 = static_cast<float>(top);
    float x1 = static_cast<float>(right), y1 = static_cast<float>(bottom);
    render::ScreenVertex v[5] = {
        {x0 + inset, y0 + inset, 0.0f, 1.0f, packed, 0xFF000000u},
        {x0 + inset, y1 - inset, 0.0f, 1.0f, packed, 0xFF000000u},
        {x1 - inset, y1 - inset, 0.0f, 1.0f, packed, 0xFF000000u},
        {x1 - inset, y0 + inset, 0.0f, 1.0f, packed, 0xFF000000u},
        {x0 + inset, y0 + inset, 0.0f, 1.0f, packed, 0xFF000000u},
    };
    const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

    currentDevice->stageSurfaces[0] = nullptr;    // no texture on stage 0
    render::ScreenDraw(currentDevice, v, 4, indices, 6);
    if (!outline) return 1;

    // The frame: white, on the rectangle's own edges, the texture stage off.
    for (auto& p : v) p.colour = 0xFFFFFFFFu;
    v[0].x = x0, v[0].y = y0;
    v[1].x = x0, v[1].y = y1;
    v[2].x = x1, v[2].y = y1;
    v[3].x = x1, v[3].y = y0;
    v[4].x = x0, v[4].y = y0;
    SetStage0Ops(render::TextureOp::Disable);
    render::ScreenDraw(currentDevice, v, 5, nullptr, 0);
    SetStage0Ops(render::TextureOp::Modulate);
    return 1;
}

// --- Renderer gamma ----------------------------------------------------------------------------------

// The display's gamma ramp: brightness, contrast and gamma from the options become the gamma ramp (the new
// renderer's, applied as the frame goes to the window). Called by WinMain at start-up and with zeros at exit, and by the options screen when a value
// changes; no caller reads the result. See docs/notes/renderer.md.

namespace {

constexpr float kLevelStep = 0.003921569f;   // 1/255
static_assert(std::bit_cast<std::uint32_t>(kLevelStep) == 0x3B808081u);
constexpr float kRampScale = 65280.0f;
constexpr int kLevels = 256;

// A branchless minimum / maximum (the comparison's bit as a mask): `limit` when it is below / above `v`.
float Clamp(float v, float lo, float hi) { return MaxKeepA(lo, MinKeepB(hi, v)); }

// The ramp of the clamped, halved settings (one table: every channel the same).
void BuildRamp(std::uint16_t (&ramp)[kLevels], float brightness, float contrast, float gamma) {
    float scale = std::fabs(contrast) * 4.0f + 1.0f;
    EI_NAN_AUDIT(contrast, gamma);
    if (contrast < 0.0f) scale = 1.0f / scale;
    float offset = (1.0f - scale) * 0.5f;
    float exponent = 1.0f;
    if (gamma > 0.0f) exponent = 1.0f / (gamma * 5.0f + 1.0f);
    else if (gamma < 0.0f) exponent = 1.0f / (gamma * 0.5f + 1.0f);

    for (int i = 0; i < kLevels; ++i) {
        float value = std::pow(i * kLevelStep, exponent) * scale + offset;
        value = Clamp(value, 0.0f, 1.0f);
        value = Clamp(value + brightness, 0.0f, 1.0f);
        auto level = static_cast<std::uint16_t>(static_cast<int>(value * kRampScale));   // _ftol: truncated
        ramp[i] = level;
    }
}

} // namespace

bool SceneRenderer::SetGamma(float newBrightness, float newContrast, float newGamma) {
    TextureDevice* currentDevice = device;
    if (!currentDevice) return false;
    // The renderer applies the ramp as the frame goes to the window (render::SetGammaRamp), so there is
    // always gamma control.
    brightness = newBrightness;
    contrast = newContrast;
    gamma = newGamma;
    std::uint16_t ramp[kLevels];
    BuildRamp(ramp, Clamp(newBrightness, -1.0f, 1.0f) * 0.5f, Clamp(newContrast, -1.0f, 1.0f) * 0.5f,
              Clamp(newGamma, -1.0f, 1.0f) * 0.5f);
    render::SetGammaRamp(ramp, ramp, ramp);
    return true;
}
