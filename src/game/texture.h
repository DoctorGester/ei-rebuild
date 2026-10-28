#pragma once

#include "engine_object.h"
#include "resource_manager.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/static_object.h"
#include "raster_target.h"
#include "renderer_base.h"

#include <list>
#include <string>
#include <unordered_map>
#include <vector>
#include <windows.h>

struct IDirect3D7;
struct IDirect3DDevice7;
struct IDirectDraw7;

#undef GetClassName

// Pixel format of a texture surface (a DDPIXELFORMAT).
struct PixelFormat {
    std::uint32_t words[8];
};
static_assert(sizeof(PixelFormat) == 32);

struct CDXSurface;
class CPixelConverter;
struct SurfacePixels;
class CTexture;
class CTextureManager;
struct ClipBounds;
class CTransformPipeline;
class CDeviceMaterial;
struct NamedTexture;
struct SkinRequest;
struct MmpHeader;
struct Direct3DDeviceRecord;
struct TLVertex;
struct VertexPacket;
struct NormalPacket;
struct TexCoord;
struct MeshCorner;
namespace devicebuffers { struct ProjectedVertex; }
struct _DDSURFACEDESC2;

// The device object's DirectDraw base. A failed DirectDraw call throws the game's error object (dx_errors.h).
class CDXDirectDraw : public CRendererBase {
public:
    // Needs MMX (else a fatal error and exit); loads 2DintMMX.dll.
    CDXDirectDraw(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel, int refreshRate,
                  unsigned backBuffers, int for3D);
    ~CDXDirectDraw() override;

    void RestoreDisplayMode();
    // With DDSCL_FPUSETUP | DDSCL_MULTITHREADED; `screen` (or an empty rectangle) becomes screen_.
    void SetCooperativeLevel(int fullScreen, const RECT* screen);

    int lastLoadResult_ = 0;    // result of the last IDirect3DDevice7::Load (and other calls)
    IDirectDraw7* ddraw_;
    GUID* driver_;
    CDXSurface* primary_;
    CDXSurface* renderTarget_;
    void* clipper_;             // IDirectDrawClipper* of the window
    std::uint32_t flags_;       // the cooperative level; bit 0: full screen (the present flips)
    RECT screen_;
    HWND window_;
    std::list<_DDSURFACEDESC2> modes_;
    std::uint32_t videoFourCC_;     // the YUV format an off-screen video-memory surface can have (0: none)
    std::uint32_t overlayFourCC_;   // the YUV format an overlay can have (0: none)
    bool overlayStretch_;       // DDCAPS_OVERLAYSTRETCH
    HMODULE converterLibrary_;  // 2DintMMX.dll
    CPixelConverter* converter_;   // 2DintMMX's graphics processor (pixel conversions)
};

// Owns the Direct3D texture formats; held by CTextureManager. The same object is the renderer's device object:
// its DirectDraw interface, the primary surface's and the render target's holders and the screen rectangle.
class CTextureDevice final : public CDXDirectDraw {
public:
    int SetMaterial(CDeviceMaterial* material);
    int DrawFlatBlocks(const void* positions, const void* uvs, int blocks, const std::uint16_t* indices, int indexCount,
                 std::uint32_t colour, std::uint32_t specular, const ClipBounds* bounds, float zScale);
    int DrawMeshBlocks2(CDXSurface* firstTexture, CDXSurface* secondTexture, const VertexPacket* positions, int blocks,
                  const NormalPacket* normals, int colourCount, std::uint32_t* colours, std::uint32_t* speculars,
                  const TexCoord* uvs, int unused10, const MeshCorner* corners, int cornerCount,
                  const std::uint32_t* words, int wordCount, std::uint16_t* indices, int indexCount, float uOffset,
                  float vOffset, int alpha, int flags);
    int DrawMeshBlocks(const VertexPacket* positions, int blocks, const NormalPacket* normals, int colourCount,
                 std::uint32_t* colours, std::uint32_t* speculars, const TexCoord* uvs, int unused10,
                 const MeshCorner* corners, int cornerCount, const std::uint32_t* words, int wordCount,
                 std::uint16_t* indices, int indexCount, float uOffset, float vOffset, int alpha, int flags);
    int DrawTwoTextures(CDXSurface* first, CDXSurface* second, int primitive, int fvf, void* vertices,
                        int vertexCount, const std::uint16_t* indices, unsigned indexCount, int flags);
    int DrawIndexed(int primitive, int fvf, void* vertices, int vertexCount, const std::uint16_t* indices,
                    unsigned indexCount, int flags);
    int BoundsOfProjected(const std::vector<devicebuffers::ProjectedVertex>* positions,
                          const std::uint8_t* outcodes);
    int BoundsOfVertices(const std::vector<TLVertex>* vertices, const std::uint8_t* outcodes);
    int DrawTlTriangles(TLVertex* vertices, int vertexCount, std::uint16_t* indices, int indexCount);
    int DrawIndexedDirect(int primitive, int fvf, void* vertices, int vertexCount, std::uint16_t* indices,
                          int indexCount, int flags);
    // The format the header's id names; for a DXT id the device does not have, the one its channel bit counts
    // name (anything else looks id 0 up). 1 with `format` filled, 0 with it untouched.
    int ParseMmpHeader(const MmpHeader* header, PixelFormat* format);
    // `id`: 0x5650, 0x5551, 0x4444, 0x8888 or a DXT four-character code. 1 with `format` filled, 0 with it
    // untouched.
    int FindFormat(int id, PixelFormat* format);
    // Only when the stage has another holder (the change counted). Returns 1.
    int BindSurface(int stage, CDXSurface* holder);
    // A null texture binds none.
    int BindTexture(int stage, CTexture* texture);
    // A texture not looked up yet is found by its name among the texture manager's registered and cached
    // textures (an empty name is not looked up) and kept, counted.
    int BindNamedTexture(int stage, NamedTexture* named);

    // Also records the four stage 0 state blocks (g_stageBlocks).
    CTextureDevice(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel, int refreshRate,
                   unsigned backBuffers, int depthBits, int stencilBits);
    ~CTextureDevice() override;
    // formats_ from the Direct3D device's formats: DXT1 .. DXT5 by their four-character codes, 5:6:5, 5:5:5
    // (0x5550), 1:5:5:5, 4:4:4:4 and 8:8:8:8 by their ids.
    void BuildFormatTable();

    IDirect3D7* d3d_;
    IDirect3DDevice7* d3dDevice_;
    CDXSurface* depth_;         // restored with the primary
    CDXSurface* depth2_;        // a second depth surface like it (not attached)
    std::uint32_t zEnable_;     // the z-buffer setting render state 7 goes back to
    GUID deviceGuid_;
    // The frame clear clears the stencil too when its flags (words[1]) have DDPF_STENCILBUFFER and its
    // stencil bits (words[4]) are nonzero.
    PixelFormat depthFormat_;
    CTransformPipeline* pipeline_;
    std::list<Direct3DDeviceRecord> devices_;
    std::list<PixelFormat> textureFormats_;
    // The values point at textureFormats_'s elements.
    std::unordered_map<int, PixelFormat*> formats_;
    std::uint32_t largestFormatId_;
    CDXSurface* stageSurfaces_[8];   // the holder bound to each texture stage
    CDeviceMaterial* material_;
    std::uint32_t colour_;           // the renderer's draw colour (mesh draw flag 4)
    // The draws fog their screen vertices by distance when set; cleared around the sky and hit point draws.
    int distanceFog_;
    RECT drawnRect_;            // the screen rectangle of the last mesh draw
    // The frame's counts, cleared by the renderer's BeginScene.
    int vertexCount_;
    int primitiveCount_;
    int drawCount_;
    int textureChanges_;
};

// The four state blocks of texture stage 0: the scene's (texture x diffuse, alpha diffuse), texture x diffuse
// with the texture's alpha, the alpha passes' (texture x diffuse for colour and alpha) and the terrain's
// (texture x current, alpha texture).
enum StageBlock { kSceneBlock, kTextureAlphaBlock, kAlphaPassBlock, kTerrainBlock, kStageBlockCount };
extern DWORD g_stageBlocks[kStageBlockCount];

// Once a frame; returns the DirectDraw result (0).
long DevicePresent(CTextureDevice* device);

// A DirectDraw surface wrapper. The surface pointer is always null: the pixels are the new renderer's memory
// (dx_surface.h).
struct CDXSurface final : CRendererBase {
    explicit CDXSurface(CTextureDevice* device);
    // Of the screen's format. Throws the game's error object when the surface cannot be made.
    CDXSurface(CTextureDevice* device, int width, int height);
    // Shows an .mmp picture.
    CDXSurface(CTextureDevice* device, const MmpHeader* picture, int flag);
    ~CDXSurface() override;

    int LoadPictureData(const MmpHeader* picture, int flag);

    long result = 0;
    CTextureDevice* device;
    void* surface = nullptr;    // always null
    int width = 0;
    int height = 0;
    int bitsPerPixel = 0;
};

// 2DintMMX's graphics processor, the pixel converter. Its code is in the DLL; the slots are in the DLL's order.
// Never constructed here.
class CPixelConverter {
public:
    virtual void Unknown0();
    // The 16-bit format ConvertTo16 writes for `kind` (2 1:5:5:5, 1 5:5:5, 0 5:6:5; -1 none of them).
    virtual void SetChannelMasks(int kind, std::uint32_t alphaMask, std::uint32_t redMask, std::uint32_t greenMask,
                                 std::uint32_t blueMask);
    // `kind` 3 4:4:4:4, 2 1:5:5:5, 0 5:6:5.
    virtual void ConvertTo16(std::uint16_t* out, const std::uint32_t* in, int kind, unsigned count);
    virtual void Unknown3();
    virtual void Unknown4();
    // An .mmp file's pixels (after its header and extra block) into 32-bit pixels.
    virtual void UnpackTo32(std::uint32_t* out, const void* in, unsigned count, int pnt3);
    virtual void Unknown6();
    // A square image; `width` is the new width.
    virtual void Halve(std::uint32_t* out, const std::uint32_t* in, unsigned width);
    // `target` starts with the triangle's three corners (a screen and a texture point each, pairs of ints)
    // and goes on with the surface.
    virtual void RasteriseTriangle(RasterTarget* target);
    // The DLL's slot 9 for a keyed texture's texels (RasterTriangle::keyTexels); nothing builds one, never called.
    virtual void RasteriseKeyedTriangle(RasterTarget* target);
    // Into 16-bit pixels.
    virtual void DecodeDxt(void* target, const void* source, int mode, std::uint32_t width, std::uint32_t height);
    // The two passes of the shadow's blur over a 16-bit image (shadow mode 2 runs both).
    virtual void BlurShadow(void* pixels, int width, int height);
    virtual void BlurShadow2(void* pixels, int width, int height);
};
CPixelConverter* PixelConverter();
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
    // What filling a texture surface of `surfaceFormat` from this file returns (the texture cache counts it
    // as the texture's size): per level width x height times the bits written, / 8 (the file's own bits when
    // it copies, the surface's for 8888 converted to 16 bits, 16 for DXT decoded); 32-bit arithmetic.
    std::uint32_t FilledBytes(const PixelFormat& surfaceFormat, unsigned mip, unsigned levels) const;
    void SetNoFormat();
    void SetFormat5551();
    void SetFormat4444();
    void SetFormat8888();
};
static_assert(offsetof(MmpHeader, alphaKind) == 0x20);
static_assert(offsetof(MmpHeader, blueBits) == 0x44);
static_assert(sizeof(MmpHeader) == 0x4C);

// A DDPIXELFORMAT's channels as mask, bit count and shift.
struct ChannelLayout {
    // The bits per pixel the texture upload counts with; Split zeroes it and never writes it, so it is
    // always 0.
    std::uint32_t bitCount;
    std::uint32_t alphaMask, alphaBits, alphaShift;
    std::uint32_t redMask, redBits, redShift;
    std::uint32_t greenMask, greenBits, greenShift;
    std::uint32_t blueMask, blueBits, blueShift;
    // 0 for a null format, one without DDPF_RGB, and at the first channel wider than 8 bits (the channels
    // before it stay filled); else 1.
    int Split(const void* format);
    // Each channel's top bits at its shift.
    std::uint32_t ToNative(std::uint32_t argb) const;
    // Each channel's bits moved to the top of its byte (the low bits stay 0).
    std::uint32_t FromNative(std::uint32_t pixel) const;
};
static_assert(sizeof(ChannelLayout) == 0x34);

// Class id 6, "CTexture".
class CTexture : public CEngineObject {
public:
    CTexture();
    ~CTexture() override;

    // From mip level `mip` on: a holder for the levels the new renderer decodes; copies the file's extra
    // block. Returns 1.
    int CreateFromMmp(CTextureDevice* device, const MmpHeader* data, unsigned mip);
    // With its alpha from "a_<file>" beside it. 1 without loading when this texture is loaded and named `path`.
    int LoadBmp(CTextureDevice* device, const char* path);
    // A square surface header->width wide (the height is not read) in the new renderer's memory, one level.
    // Returns 1.
    int CreateBlank(CTextureDevice* device, const MmpHeader* header);
    // Enters the device lock, held until UnlockPixels. 0 without a holder (no lock) or without memory.
    int LockPixels(SurfacePixels* pixels);
    // Nothing without a holder.
    void UnlockPixels();
    // The shadow's render surface into its texture.
    void LoadPixelsFrom(CTextureDevice* device, const CTexture* source);
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
    PixelFormat format_;
    CDXSurface* surface_;       // counted

    CTextureManager* Manager() const;
};

// A null texture is a fatal error (its result returned); a loaded one gives 1.
int TextureHolderEnsureLoaded(struct HeldTexture* holder);

constexpr std::uint32_t kTextureLoaded = 0x08;    // CEngineObject::flags_

// Deletes every composed-skin cache file (<data path>textures\*.mmp).
void DeleteComposedTextures();

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
    // textures\<name>.bmp on disk if present, else <name>.mmp from the archives, else <data
    // path>textures\<name>.mmp on disk.
    int LoadTexture(CTexture* texture);
    int LoadTextureFrom(CTexture* texture, const char* directory);
    // From <data path>textures\ on disk, else from the archives. Returns 0 (and an empty buffer) when neither
    // has the file.
    int LoadMmpExtra(const char* name, std::vector<std::uint8_t>* out);
    // A texture the game fills on the CPU, in the format of `kind` (0x1E 5:6:5, 0x20 1:5:5:5, 0x21 4:4:4:4;
    // another kind keeps a zero format), square at `width`. FatalError's result when the device has no such
    // format.
    CTexture* NewTexture(int width, int height, int kind);
    // 64x64 4:4:4:4 (kind 0x25, flag 0x200). FatalError's result when the device has no 4:4:4:4 format.
    CTexture* NewShadowTexture();
    // Made on first use as NewShadowTexture's, but flag 0x400.
    CTexture* ShadowRenderSurface();
    // A registered texture with a flag of 0x38 goes into the cache, any other is destroyed and kept as a
    // spare. Returns 0.
    int ReleaseObject(CEngineObject* object) override;
    CEngineObject* NewObject(int type) override;
    // Without a device: textures limited to 511 pixels and mip level 2. Else the mip level from the texture
    // quality setting, the limit when the device's largest texture is at most 256 pixels (or GetCaps fails),
    // and a tenth of the local video memory for textures.
    void SetDevice(CTextureDevice* device);
    // Taken out of the cache and registered again; null when there is none.
    CTexture* TakeFromCache(int type, const char* name);
    // The registered texture, else a cached one taken back as TakeFromCache does (but cachedBytes_ is not
    // lowered).
    CTexture* FindTexture(const char* name);
    CTexture* FindTypedTexture(int type, const char* name);
    void FreeCache();
    // Each layer a `width` x `width` .mmp from the archives; a missing one is skipped, one of another size is
    // a fatal error (its result returned). Returns the layers drawn.
    int DrawLayers(std::uint32_t* image, const std::vector<std::string>* names, int width);
    // The skin composed into the cached texture `name` (or `fallback`), the name used into `out`: 1 when
    // cached or written, 0 when nothing was drawn.
    int Compose(std::string* out, const SkinRequest* request, const std::string* name,
                const std::string* fallback, unsigned mipCount, std::uint32_t fill, std::uint32_t format);

    MmpHeader* GetFileData(const char* name);   // an .mmp file's data (CloseFile frees it)
    CTexture* NewTextureOfType(int type);
    // Given `name` either way (no null check). Not loaded.
    CTexture* FindOrCreate(int type, const char* name);
    // FindTexture(name) as it is; else FindOrCreate's texture, loaded.
    CTexture* FindOrLoad(int type, const char* name);
    // Deletes the oldest cached textures while the cache holds more than videoMemoryTenth_ (signed).
    void TrimCache();

    std::list<CTexture*> cache_;   // released textures kept for reuse
    int cachedBytes_;           // the cached textures' CTexture::uploadResult_
    std::uint32_t videoMemoryTenth_;
    CTexture* shadowRenderSurface_;   // counted
    CTextureDevice* device_;
    std::uint16_t mipLevel_;    // first mip level to load
    int limitTo512_;            // nonzero: textures are loaded at most 511 pixels wide and high
};
inline CTextureManager* CTexture::Manager() const { return static_cast<CTextureManager*>(owner_); }
