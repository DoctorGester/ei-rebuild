#pragma once

// The render state the game sets for the renderer: what were Direct3D 7's render states and texture stage states, one
// plain struct the renderer owns. The game's draw code writes g_renderState's members; the renderer's creation
// (SceneRenderer::CreateRenderer) puts it back to a new Direct3D 7 device's state; every mesh, terrain and screen
// pass reads it (ReadPassState, render_mesh.cpp). The values keep Direct3D 7's numbers, which the pipelines and the
// mesh shader take as they are. See docs/notes/renderer.md, section 5.

#include <cstdint>

#include "lib/bit_flags.h"

namespace render {

// D3DBLEND.
enum class Blend : std::uint8_t {
    Zero = 1,
    One,
    SourceColour,
    InvSourceColour,
    SourceAlpha,
    InvSourceAlpha,
    DestAlpha,
    InvDestAlpha,
    DestColour,
    InvDestColour,
    SourceAlphaSaturate,
    BothSourceAlpha,
    BothInvSourceAlpha,
};

// D3DCMPFUNC.
enum class Compare : std::uint8_t { Never = 1, Less, Equal, LessEqual, Greater, NotEqual, GreaterEqual, Always };

// D3DCULL: which triangles are dropped (clockwise: those whose corners turn clockwise on screen).
enum class Cull : std::uint8_t { None = 1, Clockwise, CounterClockwise };

// D3DTEXTUREOP.
enum class TextureOp : std::uint8_t {
    Disable = 1,   // this stage and the ones after it are off
    SelectArg1,
    SelectArg2,
    Modulate,
    Modulate2x,
    Modulate4x,
    Add,
    AddSigned,
    AddSigned2x,
    Subtract,
    AddSmooth,
    BlendDiffuseAlpha,
    BlendTextureAlpha,
    BlendFactorAlpha,
    BlendTextureAlphaPremultiplied,
    BlendCurrentAlpha,
    Premodulate,
    ModulateAlphaAddColour,
    ModulateColourAddAlpha,
    ModulateInvAlphaAddColour,
    ModulateInvColourAddAlpha,
    BumpEnvMap,
    BumpEnvMapLuminance,
    DotProduct3,
};

// A texture operation's argument (D3DTA_): a source, optionally with a modifier or'd in.
enum class TextureArg : std::uint8_t {
    Diffuse = 0,
    Current = 1,   // the stage before's result (stage 0: the diffuse colour)
    Texture = 2,
    Factor = 3,
    Specular = 4,
    Complement = 0x10,
    AlphaReplicate = 0x20,
};
constexpr bool EnableBitFlags(TextureArg) { return true; }

// D3DTFG / D3DTFN (the minification and magnification filters share these two values).
enum class Filter : std::uint8_t { Point = 1, Linear };
// D3DTFP.
enum class MipFilter : std::uint8_t { None = 1, Point, Linear };
// D3DTEXTUREADDRESS, without D3DTADDRESS_BORDER (the game never sets a stage's address, and SDL_GPU has no border).
enum class Address : std::uint8_t { Wrap = 1, Mirror, Clamp };

// A texture stage's colour and alpha operations, each of its first and second argument.
struct StageCombiner {
    TextureOp colourOp = {};
    TextureArg colourArg1 = TextureArg::Diffuse, colourArg2 = TextureArg::Diffuse;
    TextureOp alphaOp = {};
    TextureArg alphaArg1 = TextureArg::Diffuse, alphaArg2 = TextureArg::Diffuse;
};

struct TextureStage {
    StageCombiner combiner;
    Filter magFilter = Filter::Point, minFilter = Filter::Point;
    MipFilter mipFilter = MipFilter::None;
    Address addressU = Address::Wrap, addressV = Address::Wrap;
};

// The members start with what a new Direct3D 7 device had.
struct RenderState {
    bool blend = false;   // alpha blending: the result is source x sourceBlend + frame x destBlend
    Blend sourceBlend = Blend::One, destBlend = Blend::Zero;
    bool alphaTest = false;   // a pixel is kept when its alpha passes alphaFunc against alphaReference
    Compare alphaFunc = Compare::Always;
    std::uint8_t alphaReference = 0;
    bool depthTest = true, depthWrite = true;
    Compare depthFunc = Compare::LessEqual;
    Cull cull = Cull::CounterClockwise;
    bool specular = false;           // the vertices' specular colour added
    bool fog = false;                // the vertices' fog factor (the specular colour's alpha) mixes in fogColour
    std::uint32_t fogColour = 0;     // D3DCOLOR
    std::uint32_t textureFactor = 0xFFFFFFFF;   // TextureArg::Factor's colour
    // The renderer draws with two stages; a stage whose colour operation is Disable turns off the ones after it.
    TextureStage stages[2] = {
        {{TextureOp::Modulate, TextureArg::Texture, TextureArg::Current, TextureOp::SelectArg1, TextureArg::Texture, TextureArg::Current}},
        {{TextureOp::Disable, TextureArg::Texture, TextureArg::Current, TextureOp::Disable, TextureArg::Texture, TextureArg::Current}},
    };
};

inline RenderState g_renderState;

} // namespace render
