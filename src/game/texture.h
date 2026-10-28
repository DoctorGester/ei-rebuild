#pragma once

#include "engine_object.h"
#include "resource_manager.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/static_object.h"
#include "renderer_base.h"
#include "surface.h"
#include "render/render_state.h"

#include <list>
#include <string>
#include <vector>
#include <windows.h>

#undef GetClassName

class CPixelConverter;
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

// The device object: the screen (its window, size and colour depth), 2DintMMX's pixel converter, the transform pipeline, the texture stages' holders and the
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
    int DrawTwoTextures(Surface* first, Surface* second, int vertexCount, unsigned indexCount);
    int DrawIndexed(int vertexCount, unsigned indexCount);
    int BoundsOfProjected(const std::vector<devicebuffers::ProjectedVertex>* positions,
                          const std::uint8_t* outcodes);
    int BoundsOfVertices(const std::vector<TLVertex>* vertices, const std::uint8_t* outcodes);
    int DrawTlTriangles(TLVertex* vertices, int vertexCount, const std::uint16_t* indices, int indexCount);
    // Only when the stage has another holder (the change counted). Returns 1.
    int BindSurface(int stage, Surface* holder);
    // A null texture binds none.
    int BindTexture(int stage, CTexture* texture);
    // A texture not looked up yet is found by its name among the texture manager's registered and cached
    // textures (an empty name is not looked up) and kept, counted.
    int BindNamedTexture(int stage, NamedTexture* named);

    // Needs MMX (else a fatal error and exit); loads `workPath`'s 2DintMMX.dll (fatal when missing; the game's only
    // load, freed with the device) and starts the new renderer (its failure ends the game). `width` x `height`
    // pixels of `bitsPerPixel` (16 or 32) on `window`.
    CTextureDevice(const std::string& workPath, HWND window, bool fullScreen, int width, int height, int bitsPerPixel);
    ~CTextureDevice() override;

    bool fullScreen_ = false;   // the window covers the monitor
    RECT screen_ = {};          // (0, 0, width, height): the screen's size
    HWND window_ = nullptr;
    int bitsPerPixel_ = 32;     // the screen's colour depth (16 or 32): the surfaces' format (ScreenFormat)
    HMODULE converterLibrary_ = nullptr;     // 2DintMMX.dll
    CPixelConverter* converter_ = nullptr;   // 2DintMMX's graphics processor (pixel conversions): the only pointer to it
    CTransformPipeline* pipeline_ = nullptr;
    Surface* stageSurfaces_[8] = {};   // the holder bound to each texture stage
    CDeviceMaterial* material_ = nullptr;
    std::uint32_t colour_ = 0xFF000000;   // the renderer's draw colour (mesh draw flag 4)
    // The draws fog their screen vertices by distance when set; cleared around the sky and hit point draws.
    int distanceFog_ = 0;
    RECT drawnRect_ = {};       // the screen rectangle of the last mesh draw
    // The frame's counts, cleared by the renderer's BeginScene.
    int vertexCount_ = 0;
    int primitiveCount_ = 0;
    int drawCount_ = 0;
    int textureChanges_ = 0;
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

// 2DintMMX's graphics processor, the pixel converter. Its code is in the DLL; the slots are in the DLL's order.
// Never constructed here.
class CPixelConverter {
public:
    virtual void Unused0();
    // The 16-bit format ConvertTo16 writes for `kind` (2 1:5:5:5, 1 5:5:5, 0 5:6:5; -1 none of them).
    virtual void SetChannelMasks(int kind, std::uint32_t alphaMask, std::uint32_t redMask, std::uint32_t greenMask,
                                 std::uint32_t blueMask);
    // `kind` 3 4:4:4:4, 2 1:5:5:5, 0 5:6:5.
    virtual void ConvertTo16(std::uint16_t* out, const std::uint32_t* in, int kind, unsigned count);
    virtual void Unused3();
    virtual void Unused4();
    // An .mmp file's pixels (after its header and extra block) into 32-bit pixels.
    virtual void UnpackTo32(std::uint32_t* out, const void* in, unsigned count, int pnt3);
    virtual void Unused6();
    // A square image; `width` is the new width.
    virtual void Halve(std::uint32_t* out, const std::uint32_t* in, unsigned width);
    // The DLL's two triangle rasterisers (the former shadow rasteriser; no caller since the new renderer draws the
    // shadows), kept so DecodeDxt is slot 10.
    virtual void Unused8();
    virtual void Unused9();
    // Into 16-bit pixels.
    virtual void DecodeDxt(void* target, const void* source, int mode, std::uint32_t width, std::uint32_t height);
};
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
    std::uint32_t alphaKind;    // the alpha bit count: 0, 1, 4 or 8 (texture flags 0x4000..0x20000)
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
    void SetFormat8888();
};
static_assert(offsetof(MmpHeader, alphaKind) == 0x20);
static_assert(offsetof(MmpHeader, blueBits) == 0x44);
static_assert(sizeof(MmpHeader) == 0x4C);

// The texture formats, one fixed list: the RGB ids 0x5650 (5:6:5), 0x5551 (1:5:5:5), 0x4444 and 0x8888 and the
// compressed DXT1 .. DXT5 by their four-character codes (texture.cpp).
bool IsTextureFormat(std::uint32_t id);
// The pixel format of an RGB texture format; none (all zero) for a compressed or unknown one.
PixelFormat TextureFormat(std::uint32_t id);

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
    // The alpha kind from the header (flags 0x4000..0x20000), and the texture loaded.
    void MarkLoaded(const MmpHeader* header);
    // With its alpha from "a_<file>" beside it. 1 without loading when this texture is loaded and named `path`.
    int LoadBmp(CTextureDevice* device, const char* path);
    // A square surface header->width wide (the height is not read) in the new renderer's memory, one level.
    // Returns 1.
    int CreateBlank(CTextureDevice* device, const MmpHeader* header);
    // A 24-bit .bmp and, when `alphaPath` is an 8-bit .bmp, its alpha. 1 (also when already loaded); 0 when
    // the .bmp cannot be opened.
    int LoadBmpFiles(CTextureDevice* device, const char* path, const char* alphaPath);

    // Back to the constructed state. Returns 1.
    int Create() override;
    void Destroy() override;
    const char* GetName() override;
    const char* GetClassName() override;
    int GetClassId() override;

    std::string name_;
    std::uint32_t width_;
    std::uint32_t height_;
    int uploadResult_;          // the surface fill's byte count (MmpHeader::FilledBytes); 0 after CreateBlank
    std::vector<std::uint8_t> extra_;       // the .mmp file's extra block
    std::uint32_t format_;      // the texture format it is held in (TextureFormat); 0 none
    Surface* surface_;       // counted

    CTextureManager* Manager() const;
};

// A null texture is a fatal error (its result returned); a loaded one gives 1.
int TextureHolderEnsureLoaded(struct HeldTexture* holder);

constexpr std::uint32_t kTextureLoaded = 0x08;    // CEngineObject::flags_

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
    const char* GetClassName() override;
    int GetClassId() override;
    const char* GetFolderName() override;    // "textures"
    // Rebirth-patched. Also deletes the composed textures and adds the archives "textures.res", "redress.res"
    // and "menus.res"; 1 only when every one opened.
    int Create() override;
    // As the destructor up to CResourceManager::Destroy (without SetDevice).
    void Destroy() override;
    // flags_ by type: 0x3E 0x80, 0x3F 0x100, 0x40 0x200, any other 0x40 (no null check).
    CEngineObject* CreateObject(int type, int id) override;
    // textures\<name>.bmp on disk if present, else <name>.mmp from the archives, else the composed texture
    // `name` (Compose).
    int LoadTexture(CTexture* texture);
    // From the composed texture `name`, else from the archives. Returns 0 (and an empty buffer) when neither
    // has it.
    int LoadMmpExtra(const char* name, std::vector<std::uint8_t>* out);
    // A texture the game fills on the CPU, in the format of `kind` (0x1E 5:6:5, 0x20 1:5:5:5, 0x21 4:4:4:4;
    // another kind keeps a zero format), square at `width`. FatalError's result when the device has no such
    // format.
    CTexture* NewTexture(int width, int height, int kind);
    // A registered texture with a flag of 0x38 goes into the cache, any other is destroyed and kept as a
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
