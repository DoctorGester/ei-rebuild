#pragma once

#include "engine_object.h"
#include "render/render_state.h"
#include "renderer_base.h"
#include "resource_manager.h"
#include "surface.h"

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/crt.h"

#include <list>
#include <string>
#include <vector>
#include <windows.h>

class CTexture;
class CTextureManager;
struct ClipBounds;
class CTransformPipeline;
class CDeviceMaterial;
struct NamedTexture;
struct SkinRequest;
struct ComposedTexture;
struct MmpHeader;
struct TLVertex;
struct VertexPacket;
struct NormalPacket;
struct TexCoord;
struct MeshCorner;
namespace devicebuffers { struct ProjectedVertex; }

// The device object: the screen (its window, size and colour depth), the transform pipeline, the texture stages' holders and the
// draws' state. Made once by the scene renderer's creation (renderer_init.cpp); the new renderer (src/render) draws
// and presents. Held by the scene renderer and the texture manager.
class CTextureDevice final : public CRendererBase {
public:
    int SetMaterial(CDeviceMaterial* material);
    int DrawFlatBlocks(const void* positions, const void* uvs, int blocks, const std::uint16_t* indices, int indexCount,
                 std::uint32_t colour, std::uint32_t specular, const ClipBounds* bounds, float zScale);
    int DrawMeshBlocks2(Surface* firstTexture, Surface* secondTexture, const VertexPacket* positions, int blocks,
                  const NormalPacket* normals, int colourCount, std::uint32_t* colours, std::uint32_t* speculars,
                  const TexCoord* uvs, int unused10, const MeshCorner* corners, int cornerCount,
                  const std::uint32_t* words, int wordCount, std::uint16_t* indices, int indexCount, float uOffset,
                  float vOffset, int alpha, int flags);
    int DrawMeshBlocks(const VertexPacket* positions, int blocks, const NormalPacket* normals, int colourCount,
                 std::uint32_t* colours, std::uint32_t* speculars, const TexCoord* uvs, int unused10,
                 const MeshCorner* corners, int cornerCount, const std::uint32_t* words, int wordCount,
                 std::uint16_t* indices, int indexCount, float uOffset, float vOffset, int alpha, int flags);
    int DrawTwoTextures(Surface* first, Surface* second);
    int DrawIndexed();
    // Draw flag 1's rectangle: the bounds of the projected `positions` (screen positions, or with `clipSpace` the
    // clip-space ones inside the depth range, mapped to the screen) clamped to the screen.
    void BoundsOfProjected(const std::vector<devicebuffers::ProjectedVertex>* positions, bool clipSpace);
    int DrawTlTriangles(TLVertex* vertices, int vertexCount, const std::uint16_t* indices, int indexCount);
    // The holder the renderer draws the stage with. Returns 1.
    int BindSurface(int stage, Surface* holder);
    // A null texture binds none.
    int BindTexture(int stage, CTexture* texture);
    // A texture not looked up yet is found by its name among the texture manager's registered and cached
    // textures (an empty name is not looked up) and kept, counted.
    int BindNamedTexture(int stage, NamedTexture* named);

    // Starts the new renderer (its failure ends the game). `width` x `height` pixels of `bitsPerPixel` (16 or 32) on
    // `window`.
    CTextureDevice(HWND window, bool fullScreen, int width, int height, int bitsPerPixel);
    ~CTextureDevice() override;

    bool fullScreen_ = false;   // the window covers the monitor
    RECT screen_ = {};          // (0, 0, width, height): the screen's size
    HWND window_ = nullptr;
    int bitsPerPixel_ = 32;     // the screen's colour depth (16 or 32): the surfaces' format (ScreenFormat)
    CTransformPipeline* pipeline_ = nullptr;
    Surface* stageSurfaces_[8] = {};   // the holder bound to each texture stage
    CDeviceMaterial* material_ = nullptr;
    std::uint32_t colour_ = 0xFF000000;   // the renderer's draw colour (mesh draw flag 4)
    // The draws fog their screen vertices by distance when set; cleared around the sky and hit point draws.
    int distanceFog_ = 0;
    RECT drawnRect_ = {};       // the screen rectangle of the last mesh draw
};

// Texture stage 0's three setups the world's and the interface's draws switch between: the scene's (texture x
// diffuse, alpha diffuse), the alpha passes' (texture x diffuse for colour and alpha) and the terrain's (texture x
// current, alpha texture).
inline constexpr render::StageCombiner kSceneStage = {render::TextureOp::Modulate, render::kArgTexture,
                                                      render::kArgDiffuse, render::TextureOp::SelectArg1,
                                                      render::kArgDiffuse, render::kArgCurrent};
inline constexpr render::StageCombiner kAlphaPassStage = {render::TextureOp::Modulate, render::kArgTexture,
                                                          render::kArgDiffuse, render::TextureOp::Modulate,
                                                          render::kArgTexture, render::kArgDiffuse};
inline constexpr render::StageCombiner kTerrainStage = {render::TextureOp::Modulate, render::kArgTexture,
                                                        render::kArgCurrent, render::TextureOp::SelectArg1,
                                                        render::kArgTexture, render::kArgCurrent};

// Once a frame: the new renderer's frame shown. Returns 0.
long DevicePresent(CTextureDevice* device);

constexpr std::uint32_t kMmpFormatPnt3 = 0x33544E50;   // "PNT3"

// Header of an .mmp file; the extra block follows it, then the mips. Each channel: its mask, the shift of its
// lowest bit and its bit count.
struct MmpHeader {
    std::uint32_t magic;        // "MMP"
    std::uint32_t width;
    std::uint32_t height;
    std::uint32_t mipCount;
    std::uint32_t format;       // pixel format id (0x8888, 0x4444, 0x5551, 0x5650, ...)
    std::uint32_t bitsPerPixel;
    std::uint32_t alphaMask;
    std::uint32_t alphaShift;
    std::uint32_t alphaKind;    // the alpha bit count: 0, 1, 4 or 8 (TextureFlag::Alpha0..Alpha8)
    std::uint32_t redMask;
    std::uint32_t redShift;
    std::uint32_t redBits;
    std::uint32_t greenMask;
    std::uint32_t greenShift;
    std::uint32_t greenBits;
    std::uint32_t blueMask;
    std::uint32_t blueShift;
    std::uint32_t blueBits;
    std::uint32_t extraSize;

    // After the header: the extra block plus the sizes of the larger levels (32-bit arithmetic).
    std::uint32_t MipOffset(unsigned mip) const;
    // The texture format the file's texture is held in (IsTextureFormat): its own when listed; for another DXT code
    // the RGB format its channel bit counts name. 0 when none is.
    std::uint32_t HeldFormat() const;
    // What filling a texture of format `heldFormat` from this file returned (the texture cache counts it as the
    // texture's size): per level width x height times the bits written, / 8 (the file's own bits, 16 for a DXT file
    // held in an RGB format); 32-bit arithmetic.
    std::uint32_t FilledBytes(std::uint32_t heldFormat, unsigned mip, unsigned levels) const;
    void SetNoFormat();
    void SetFormat5551();
    void SetFormat4444();
};
static_assert(offsetof(MmpHeader, alphaKind) == 0x20);
static_assert(offsetof(MmpHeader, blueBits) == 0x44);
static_assert(sizeof(MmpHeader) == 0x4C);

// The texture formats, one fixed list: the RGB ids 0x5650 (5:6:5), 0x5551 (1:5:5:5), 0x4444 and 0x8888 and the
// compressed DXT1 .. DXT5 by their four-character codes (texture.cpp).
bool IsTextureFormat(std::uint32_t id);
// The pixel format of an RGB texture format; none (all zero) for a compressed or unknown one.
render::PixelFormat TextureFormat(std::uint32_t id);

// A texture's kind: its type_, the texture manager's object type. Neither the data nor the strings name them; the
// callers pass a figure part's meshType_ (1..0x10, CObject3DClientSpecific's part sets; 1 and 2 also the units'
// composed skins "mo%dp" / "mo%ds") or one of these. CreateObject gives three more kinds their own flag
// (TextureFlag::Type3E..Type40); nothing creates them.
enum class TextureKind : int {
    Interface = 3,      // the interface's pictures (CUiWindow::LoadTexture)
    Effects = 6,        // the effects', the ground prints', the fog, the wounds and the hit numbers
    TerrainPage = 0xB,  // the terrain pages
    Blank565 = 0x1E,    // NewTexture's blank 5:6:5 texture
    Blank1555 = 0x20,   // NewTexture's blank 1:5:5:5 texture
    Blank4444 = 0x21,   // NewTexture's blank 4:4:4:4 texture
    SkyFace = 0x3D,     // the sky and the units' faces
    Type3E = 0x3E,
    Type3F = 0x3F,
    Type40 = 0x40,
};

// The bits of CEngineObject::flags_ on a texture.
enum class TextureFlag : std::uint32_t {
    Loaded = 0x8,
    // With Loaded the mask that makes a released texture cacheable: nothing in the game sets 0x10 or 0x20.
    Cacheable = 0x38,
    // The texture manager's CreateObject by the kind: TextureKind::Type3E, Type3F, Type40, any other. Nothing reads them.
    Type3E = 0x80,
    Type3F = 0x100,
    Type40 = 0x200,
    OtherType = 0x40,
    Tree = 0x2000,          // a tree's part texture (CObject3DClientSpecific's part sets); nothing reads it
    // The .mmp header's alpha bit count (MarkLoaded): 0, 1, 4, 8. Nothing reads them.
    Alpha0 = 0x4000,
    Alpha1 = 0x8000,
    Alpha4 = 0x10000,
    Alpha8 = 0x20000,
    AlphaBits = Alpha0 | Alpha1 | Alpha4 | Alpha8,
    Keep = 0x40000,         // CWorldClient::SetUpGraphics's digits texture, CUiWindow::CreateTexture's; nothing reads it
};
template <> inline constexpr bool lib::kBitFlags<TextureFlag> = true;

// Class id 6, "CTexture".
class CTexture : public CEngineObject {
public:
    CTexture();
    ~CTexture() override;

    // From mip level `mip` on: a holder for the levels the new renderer decodes; copies the file's extra
    // block. Returns 1.
    int CreateFromMmp(CTextureDevice* device, const MmpHeader* data, unsigned mip);
    // From mip level `mip` on: a holder for the levels the new renderer composes from the layer textures (no
    // check that the level exists, as the original's load of its cache file); copies the extra block. Returns 1.
    int CreateComposed(CTextureDevice* device, const ComposedTexture& composed, unsigned mip);
    // The alpha kind from the header (TextureFlag::Alpha0..Alpha8), and the texture loaded.
    void MarkLoaded(const MmpHeader* header);
    // With its alpha from "a_<file>" beside it. 1 without loading when this texture is loaded and named `path`.
    int LoadBmp(CTextureDevice* device, const char* path);
    // A square texture header->width wide (the height is not read) the game draws into on the GPU (TakeBlankHolder).
    // Returns 1.
    int CreateBlank(CTextureDevice* device, const MmpHeader* header);
    // A new holder in surface_ of a `width` x `height` texture the game draws into on the GPU (render::BlankTexture),
    // keeping what format_ keeps of the alpha; with `mipmapped` its levels made after each draw. The game's error for
    // a surface that cannot be made when format_ is none.
    void TakeBlankHolder(CTextureDevice* device, int width, int height, bool mipmapped);
    // A 24-bit .bmp and, when `alphaPath` is an 8-bit .bmp, its alpha. 1 (also when already loaded); 0 when
    // the .bmp cannot be opened.
    int LoadBmpFiles(CTextureDevice* device, const char* path, const char* alphaPath);

    // Back to the constructed state. Returns 1.
    void Create() override;
    void Destroy() override;
    const char* GetName() override;
    const char* ClassName() override;
    ClassId GetClassId() override;

    std::string name_;
    std::uint32_t width_;
    std::uint32_t height_;
    int uploadResult_;          // the surface fill's byte count (MmpHeader::FilledBytes); 0 after CreateBlank
    std::vector<std::uint8_t> extra_;       // the .mmp file's extra block
    std::uint32_t format_;      // the texture format it is held in (TextureFormat); 0 none
    Ref<Surface> surface_;

    CTextureManager* Manager() const;
};

// A null texture is a fatal error (its result returned); a loaded one gives 1.
int EnsureTextureLoaded(CTexture* texture);
// Drops the texture `slot` holds, then takes the texture manager's texture of `kind` named `name`, made when
// missing.
void LoadNamedTexture(Ref<CTexture>& slot, int kind, const char* name);

// A texture the texture manager composes from layer textures on the GPU (CTextureManager::Compose): what the
// original drew on the CPU into <data path>textures\<name>.mmp. `header` is that file's (format, size, levels, the
// extra block's size), `extra` its extra block (the request's layer bytes), `layers` the layer textures, bottom first.
struct ComposedTexture {
    MmpHeader header;
    std::vector<std::uint8_t> extra;
    std::vector<std::string> layers;
};

// Forgets every composed texture (the original deleted its cache files, <data path>textures\*.mmp).
void ForgetComposedTextures();

// Class id 0x26.
class CTextureManager : public CResourceManager {
public:

    CTextureManager();
    ~CTextureManager() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    const char* GetFolderName() override;    // "textures"
    // Rebirth-patched. Also deletes the composed textures and adds the archives "textures.res", "redress.res"
    // and "menus.res"; 1 only when every one opened.
    void Create() override;
    // As the destructor up to CResourceManager::Destroy (without SetDevice).
    void Destroy() override;
    // flags_ by type (TextureKind): TextureFlag::Type3E, Type3F, Type40, any other OtherType (no null check).
    CEngineObject* CreateObject(int type, int id) override;
    // textures\<name>.bmp on disk if present, else <name>.mmp from the archives, else the composed texture
    // `name` (Compose).
    int LoadTexture(CTexture* texture);
    // From the composed texture `name`, else from the archives. Returns 0 (and an empty buffer) when neither
    // has it.
    int LoadMmpExtra(const char* name, std::vector<std::uint8_t>* out);
    // A texture the game fills on the CPU, in the format of `kind` (Blank565, Blank1555, Blank4444;
    // another kind keeps a zero format), square at `width`. FatalError's result when the device has no such
    // format.
    CTexture* NewTexture(int width, int height, TextureKind kind);
    // A registered texture with a TextureFlag::Cacheable bit goes into the cache, any other is destroyed and kept as a
    // spare. Returns 0.
    int ReleaseObject(CEngineObject* object) override;
    CEngineObject* NewObject(int type) override;
    // Without a device: textures limited to 511 pixels and mip level 2. Else the mip level from the texture
    // quality setting, the limit when the device's largest texture is at most 256 pixels (or GetCaps fails),
    // and a tenth of the local video memory for textures.
    void SetDevice(CTextureDevice* device);
    // Taken out of the cache and registered again; null when there is none.
    CTexture* TakeFromCache(int type, std::string_view name);
    // The registered texture, else a cached one taken back as TakeFromCache does (but cachedBytes_ is not
    // lowered).
    CTexture* FindTexture(std::string_view name);
    CTexture* FindTypedTexture(int type, std::string_view name);
    void FreeCache();
    // Each layer a `width` x `width` .mmp from the archives, its name into `found`; a missing one is skipped,
    // one of another size is a fatal error (its result returned). Returns the layers found.
    int FindLayers(const std::vector<std::string>* names, int width, std::vector<std::string>* found);
    // The composed texture `name` (`fallback` when `name` has none) made of the request's layers, the name used
    // into `out`: 1 when it was made of them already or is now, 0 when there is nothing to draw.
    int Compose(std::string* out, const SkinRequest* request, const std::string* name,
                const std::string* fallback, unsigned mipCount, std::uint32_t format);
    // The composed texture `name` (any case), or null.
    const ComposedTexture* FindComposed(std::string_view name);

    MmpHeader* GetFileData(std::string_view name);   // an .mmp file's data (CloseFile frees it)
    CTexture* NewTextureOfType(int type);
    // Given `name` either way (no null check). Not loaded.
    CTexture* FindOrCreate(int type, std::string_view name);
    // FindTexture(name) as it is; else FindOrCreate's texture, loaded.
    CTexture* FindOrLoad(int type, std::string_view name);
    // A tenth of the video memory the game saw on the card it ran on (dgVoodoo's 256 MB): the most the cache of
    // released textures holds.
    static constexpr int kCacheLimit = (256 << 20) / 10;
    // Deletes the oldest cached textures while the cache holds more than kCacheLimit.
    void TrimCache();

    std::list<CTexture*> cache_;   // released textures kept for reuse
    int cachedBytes_;           // the cached textures' CTexture::uploadResult_
    CTextureDevice* device_;
    std::uint16_t mipLevel_;    // first mip level to load
    int limitTo512_;            // nonzero: textures are loaded at most 511 pixels wide and high
};
inline CTextureManager* CTexture::Manager() const { return static_cast<CTextureManager*>(owner_); }
