#pragma once

#include "engine_object.h"
#include "resource_manager.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/static_object.h"
#include "game_data.h"
#include "raster_target.h"
#include "renderer_base.h"

#include <windows.h>

struct IDirect3D7;
struct IDirect3DDevice7;
struct IDirectDraw7;

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// Pixel format of a texture surface (32 bytes), as filled in by the texture device.
struct PixelFormat {
    std::uint32_t words[8];
};
static_assert(sizeof(PixelFormat) == 32);

struct CDXSurface;
struct SurfacePixels;
class CTexture;
class CTextureManager;
struct ClipBounds;
class CTransformPipeline;
class CDeviceMaterial;
struct NamedTexture;
struct SkinRequest;
struct Direct3DDeviceRecord;
struct TLVertex;
namespace devicebuffers { struct ProjectedVertex; }   // device_buffers.h
struct _DDSURFACEDESC2;        // ddraw.h (DDSURFACEDESC2)

// The device object's DirectDraw base ("CDXDirectDraw" in its messages; the members up
// to +0x50; directdraw_holder.cpp). A failed DirectDraw call throws the game's error object (dx_errors.h).
class CDXDirectDraw : public CRendererBase {
public:
    // The processor's CPUID words stored (no MMX: a fatal error and exit), 2DintMMX.dll
    // loaded and its graphics processor taken, the DirectDraw object of `driver` made with its display modes
    // (CreateDirectDraw), the cooperative level, for full screen the display mode, the primary and back surfaces,
    // and the YUV formats the driver can make an off-screen and an overlay surface of.
    CDXDirectDraw(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel, int refreshRate,
                  unsigned backBuffers, int for3D);
    ~CDXDirectDraw() override;

    // The display mode restored, in full screen only.
    void RestoreDisplayMode();
    // The cooperative level of the window: exclusive full screen or normal, with
    // DDSCL_FPUSETUP | DDSCL_MULTITHREADED; `screen` (or an empty rectangle) becomes screen_.
    void SetCooperativeLevel(int fullScreen, const RECT* screen);

    int lastLoadResult_ = 0;    // result of the last IDirect3DDevice7::Load (and other calls)
    IDirectDraw7* ddraw_;
    GUID* driver_;              // the driver the DirectDraw object was made for
    CDXSurface* primary_;       // the primary surface's holder
    CDXSurface* renderTarget_;  // the render target's holder
    void* clipper_;             // IDirectDrawClipper* of the window
    std::uint32_t flags_;       // the cooperative level; bit 0: full screen (the present flips)
    RECT screen_;               // the screen rectangle
    HWND window_;
    stlp::list<_DDSURFACEDESC2> modes_;   // the driver's display modes
    std::uint32_t videoFourCC_;     // the YUV format an off-screen video-memory surface can have (0: none)
    std::uint32_t overlayFourCC_;   // the YUV format an overlay can have (0: none)
    bool overlayStretch_;       // DDCAPS_OVERLAYSTRETCH
    std::uint8_t unused_45[3];
    HMODULE converterLibrary_;  // 2DintMMX.dll
    void* converter_;           // 2DintMMX's graphics processor (pixel conversions)
};

// Owns the Direct3D texture formats; held by CTextureManager. Name provisional.
// The same object is the renderer's device object: its
// DirectDraw interface, the primary surface's and the render target's holders and the screen rectangle.
class CTextureDevice final : public CDXDirectDraw {
public:
    // 0x004EB170 (device_states.cpp).
    int SetMaterial(CDeviceMaterial* material);
    // 0x004EA4A0 (device_shadow.cpp).
    int DrawFlatBlocks(const void* positions, const char* uvs, int blocks, const std::uint16_t* indices, int indexCount,
                 std::uint32_t colour, std::uint32_t specular, const ClipBounds* bounds, float zScale);
    // 0x004E9A50 (device_mesh.cpp).
    int DrawMeshBlocks2(void* firstTexture, void* secondTexture, const void* positions, int blocks, const void* normals,
                  int colourCount, std::uint32_t* colours, std::uint32_t* speculars, const void* uvs, int unused10,
                  const void* corners, int cornerCount, const void* words, int wordCount, std::uint16_t* indices,
                  int indexCount, float uOffset, float vOffset, int alpha, int flags);
    // 0x004E9010 (device_mesh.cpp).
    int DrawMeshBlocks(const void* positions, int blocks, const void* normals, int colourCount, std::uint32_t* colours,
                 std::uint32_t* speculars, const void* uvs, int unused10, const void* corners, int cornerCount,
                 const void* words, int wordCount, std::uint16_t* indices, int indexCount, float uOffset, float vOffset,
                 int alpha, int flags);
    // 0x004EB370 (device_draw.cpp).
    int DrawTwoTextures(CDXSurface* first, CDXSurface* second, int primitive, int fvf, void* vertices,
                        int vertexCount, const std::uint16_t* indices, unsigned indexCount, int flags);
    // 0x004EB550 (device_draw.cpp).
    int DrawIndexed(int primitive, int fvf, void* vertices, int vertexCount, const std::uint16_t* indices,
                    unsigned indexCount, int flags);
    // 0x004E8900 (device_bounds.cpp).
    int BoundsOfProjected(const lib::AlignedArray<devicebuffers::ProjectedVertex>* positions,
                          const std::uint8_t* outcodes);
    // 0x004E86B0 (device_bounds.cpp).
    int BoundsOfVertices(const lib::AlignedArray<TLVertex>* vertices, const std::uint8_t* outcodes);
    // 0x00502A20 (renderer_strip.cpp).
    int DrawTlTriangles(TLVertex* vertices, int vertexCount, std::uint16_t* indices, int indexCount);
    // 0x00503680 (renderer_sprites.cpp).
    int DrawIndexedDirect(int primitive, int fvf, void* vertices, int vertexCount, std::uint16_t* indices,
                          int indexCount, int flags);
    // The texture format for an .mmp file's header: the format its id names
    // (formats_); for a DXT id the device does not have, the format its channel bit counts name
    // (5:6:5, 5:5:5, 1:5:5:5, 4:4:4:4, 8:8:8:8; anything else looks id 0 up). 1 with `format`
    // filled, 0 with it untouched.
    int ParseMmpHeader(const void* data, PixelFormat* format);
    // The texture format registered under `id` (0x5650, 0x5551, 0x4444, 0x8888,
    // a DXT four-character code): 1 with `format` filled, 0 with it untouched.
    int FindFormat(int id, PixelFormat* format);
    // `holder`'s surface (or none) on texture stage `stage`, when the stage has
    // another holder: SetTexture (the result in lastLoadResult_), the change
    // counted. Returns 1.
    int BindSurface(int stage, CDXSurface* holder);
    // The same for a texture's surface holder (a null texture: none).
    int BindTexture(int stage, CTexture* texture);
    // The same for a texture kept by name: one not looked up yet is found by
    // its name among the texture manager's registered and cached textures (FindTexture; an empty
    // name is not looked up) and kept, counted.
    int BindNamedTexture(int stage, NamedTexture* named);

    // The base (one back buffer asked as the caller says, for 3D), IDirect3D7 from the
    // DirectDraw object, its devices listed, the HAL device made on the render target, the texture formats
    // (BuildFormatTable) and depth formats listed, the depth buffer (CreateDepthBuffer), the render target and
    // viewport set, and four state blocks of stage 0's colour and alpha operations recorded.
    CTextureDevice(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel, int refreshRate,
                   unsigned backBuffers, int depthBits, int stencilBits);
    ~CTextureDevice() override;
    // The texture formats: the Direct3D device's formats listed again and
    // formats_ filled from them: DXT1 .. DXT5 by their four-character codes, 5:6:5, 5:5:5 (0x5550),
    // 1:5:5:5, 4:4:4:4 and 8:8:8:8 by their ids; largestFormatId_ is the largest id stored.
    void BuildFormatTable();

    // Holders count references to it (the cursor): CRendererBase's AddRef / Release.

    IDirect3D7* d3d_;
    IDirect3DDevice7* d3dDevice_;
    CDXSurface* depth_;         // the depth surface's holder (restored with the primary)
    CDXSurface* depth2_;        // a second depth surface like it (not attached)
    std::uint32_t zEnable_;     // the z-buffer setting render state 7 goes back to
    std::uint8_t unused_64[0x7C - 0x64];
    GUID deviceGuid_;           // the Direct3D device's GUID
    // +0x8C the depth buffer's format (a DDPIXELFORMAT). The frame clear clears the
    // stencil too when its flags (words[1]) have DDPF_STENCILBUFFER and its stencil bits
    // (words[4]) are nonzero.
    PixelFormat depthFormat_;
    CTransformPipeline* pipeline_;   // the scene renderer's pipeline (model.h)
    stlp::list<Direct3DDeviceRecord> devices_;   // the Direct3D devices of the driver
    stlp::list<PixelFormat> depthFormats_;   // the enumerated depth buffer formats (one is picked)
    stlp::list<PixelFormat> textureFormats_; // the enumerated texture formats
    // +0xBC the texture formats by id (filled from the Direct3D device's enumeration);
    // the values point at textureFormats_'s elements.
    stlp::hash_map<int, PixelFormat*, stlp::IntHash> formats_;
    std::uint32_t largestFormatId_;  // the largest id in formats_
    std::uint8_t unused_D4[0x58];    // two vertex-buffer batches and a spare set: never used (4E7820 gives no buffer)
    CDXSurface* stageSurfaces_[8];   // the surface holder bound to each texture stage
    CDeviceMaterial* material_;      // the current material
    std::uint32_t colour_;           // the renderer's draw colour (mesh draw flag 4)
    std::uint8_t unused_154[4];
    // +0x158 the draws fog their screen vertices by distance (the transform object's FogVertices)
    // when set; cleared around the sky and hit point draws, else 1.
    int distanceFog_;
    RECT drawnRect_;            // the screen rectangle of the last mesh draw
    // +0x16C.. +0x178: the frame's counts, cleared by the renderer's BeginScene.
    int vertexCount_;           // vertices drawn
    int primitiveCount_;        // primitives drawn
    int drawCount_;             // draw calls
    int textureChanges_;        // texture bindings changed
};
static_assert(offsetof(CTextureDevice, drawnRect_) == 0x15C && offsetof(CTextureDevice, vertexCount_) == 0x16C);
static_assert(offsetof(CTextureDevice, lastLoadResult_) == 0x08);
static_assert(offsetof(CTextureDevice, primitiveCount_) == 0x170 && offsetof(CTextureDevice, drawCount_) == 0x174);
static_assert(offsetof(CTextureDevice, textureChanges_) == 0x178);
static_assert(sizeof(CTextureDevice) == 0x17C);

// The handles of the four state blocks of texture stage 0 that the device object records at its creation and deletes
// with it (device_object.cpp): the scene's (texture x diffuse, alpha diffuse), texture x diffuse with the texture's
// alpha, the alpha passes' (texture x diffuse for colour and alpha) and the terrain's (texture x current, alpha texture).
enum StageBlock { kSceneBlock, kTextureAlphaBlock, kAlphaPassBlock, kTerrainBlock, kStageBlockCount };
extern DWORD g_stageBlocks[kStageBlockCount];
static_assert(offsetof(CTextureDevice, zEnable_) == 0x60);
static_assert(offsetof(CTextureDevice, distanceFog_) == 0x158);
static_assert(offsetof(CTextureDevice, deviceGuid_) == 0x7C && offsetof(CTextureDevice, depthFormat_) == 0x8C);
static_assert(offsetof(CTextureDevice, window_) == 0x34 && offsetof(CTextureDevice, converterLibrary_) == 0x48);
static_assert(offsetof(CTextureDevice, devices_) == 0xB0 && offsetof(CTextureDevice, largestFormatId_) == 0xD0);
static_assert(offsetof(CTextureDevice, stageSurfaces_) == 0x12C && offsetof(CTextureDevice, colour_) == 0x150);
static_assert(offsetof(CTextureDevice, depthFormats_) == 0xB4 && offsetof(CTextureDevice, formats_) == 0xBC);

// The device object's present, once a frame. Returns the DirectDraw
// result: 0.
long __fastcall DevicePresent(CTextureDevice* device);
static_assert(offsetof(CTextureDevice, ddraw_) == 0x0C);
static_assert(offsetof(CTextureDevice, converter_) == 0x4C);
static_assert(offsetof(CTextureDevice, primary_) == 0x14);
static_assert(offsetof(CTextureDevice, renderTarget_) == 0x18);
static_assert(offsetof(CTextureDevice, flags_) == 0x20);
static_assert(offsetof(CTextureDevice, screen_) == 0x24);
static_assert(offsetof(CTextureDevice, d3dDevice_) == 0x54);
static_assert(offsetof(CTextureDevice, pipeline_) == 0xAC);

// A Direct3D driver of the device list (an STLport list; filled by the game's driver
// enumeration, which the image does not run: it stays empty; the lookup returns the one with an index).
// 0x30 bytes.
struct DirectDrawDriver {
    GUID guidValue;             // the driver's GUID (unset for the primary display driver)
    GUID* guid;                 // for DirectDrawCreateEx: &guidValue, or null (the primary display driver)
    stlp::string name;
    stlp::string description;
    HMONITOR monitor;
};
static_assert(offsetof(DirectDrawDriver, guid) == 0x10 && offsetof(DirectDrawDriver, description) == 0x20);
static_assert(sizeof(DirectDrawDriver) == 0x30);
// The driver list.
extern lib::StaticObject<stlp::list<DirectDrawDriver>> g_directDrawDrivers;   // static_lists.cpp
inline stlp::list<DirectDrawDriver>& DirectDrawDrivers() { return *g_directDrawDrivers; }
// The driver `index` steps after the list's first;
// past the last: a fatal error ("Can't find driver by index.") and null.
DirectDrawDriver* __fastcall DriverAt(int index);

// A DirectDraw surface wrapper (constructed from a
// DDSURFACEDESC2, or for an off-screen surface of the screen's format; 0x20 bytes): counted, the last DirectDraw
// result, the device object, the IDirectDrawSurface7* (always null in the image: the pixels are the new renderer's
// memory, dx_surface.h), width, height and bits per pixel. dx_surface.cpp.
struct CDXSurface final : CRendererBase {
    explicit CDXSurface(CTextureDevice* device);
    // An off-screen surface of the screen's format. Throws the game's error object
    // when the surface cannot be made.
    CDXSurface(CTextureDevice* device, int width, int height);
    // A holder whose surface shows an .mmp picture.
    CDXSurface(CTextureDevice* device, const void* picture, int flag);
    ~CDXSurface() override;

    // Loads the picture data into the surface.
    int LoadPictureData(const void* data, int flag);

    long result = 0;
    CTextureDevice* device;
    void* surface = nullptr;    // (null: no holder has a DirectDraw surface)
    int width = 0;
    int height = 0;
    int bitsPerPixel = 0;
};
static_assert(offsetof(CDXSurface, result) == 0x08);
static_assert(offsetof(CDXSurface, device) == 0x0C);
static_assert(offsetof(CDXSurface, surface) == 0x10);
static_assert(offsetof(CDXSurface, bitsPerPixel) == 0x1C);
static_assert(sizeof(CDXSurface) == 0x20);

// RasterTriangle / RasterTarget: raster_target.h.



// 2DintMMX's graphics processor, the pixel converter (also CTextureDevice::converter_).
// Its code is in the DLL; only the slots the rewritten code calls are named. Never constructed here.
class CPixelConverter {
public:
    virtual void Unknown0();
    // The 16-bit format slot 2 writes for `kind` (2 1:5:5:5, 1 5:5:5, 0 5:6:5; -1 none of them)
    // and its channel masks (dx_surface.cpp, the picture copy into a surface).
    virtual void SetChannelMasks(int kind, std::uint32_t alphaMask, std::uint32_t redMask, std::uint32_t greenMask,
                                 std::uint32_t blueMask);
    // 32-bit pixels to 16 bits; `kind` 3 4:4:4:4, 2 1:5:5:5, 0 5:6:5.
    virtual void ConvertTo16(std::uint16_t* out, const std::uint32_t* in, int kind, unsigned count);
    virtual void Unknown3();
    virtual void Unknown4();
    // An .mmp file's pixels (after its header and extra block) into 32-bit pixels; `pnt3` for a
    // PNT3 file (MmpHeader::format "PNT3").
    virtual void UnpackTo32(std::uint32_t* out, const void* in, unsigned count, int pnt3);
    virtual void Unknown6();
    // Halves a square image (`width` is the new width).
    virtual void Halve(std::uint32_t* out, const std::uint32_t* in, unsigned width);
    // Rasterises one triangle of a shadow: `target` starts with its three corners (a screen and
    // a texture point each, pairs of ints) and goes on with the surface.
    virtual void RasteriseTriangle(RasterTarget* target);
    // The same, masked by a keyed texture's texels (RasterTriangle::keyTexels).
    virtual void RasteriseKeyedTriangle(RasterTarget* target);
    // Decodes a DXT block image into 16-bit pixels (texture_fill.cpp).
    virtual void DecodeDxt(void* target, const void* source, int mode, std::uint32_t width, std::uint32_t height);
    // The two passes of the shadow's blur over a 16-bit image of `width` x `height` pixels
    // (shadow mode 2 runs both on the shadow texture's pixels; map_object_shadow.cpp).
    virtual void BlurShadow(void* pixels, int width, int height);
    virtual void BlurShadow2(void* pixels, int width, int height);
};
// The scene renderer's graphics processor (2DintMMX.dll's; world_singletons.cpp).
CPixelConverter* PixelConverter();
constexpr std::uint32_t kMmpFormatPnt3 = 0x33544E50;   // "PNT3"

// A byte buffer aligned to 32 bytes (the engine's aligned array).
using AlignedBuffer = lib::AlignedArray<std::uint8_t>;

// Header of an.mmp file; the extra block of +0x48 bytes follows it, then the mips.
// Each channel: its mask, the shift of its lowest bit and its bit count.
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

    // Offset of mip level `mip`'s pixels after the header: the extra block plus the
    // sizes of the larger levels (a geometric sum computed in 32-bit integers).
    std::uint32_t MipOffset(unsigned mip) const;
    // What filling a texture surface of `surfaceFormat` from this file returns
    // (texture_mmp.cpp; the texture cache counts it as the texture's size, CTexture +0x28): per
    // level of the surface (from `mip`, `levels` of them, no further than the file's last) width x
    // height times the bits it writes, / 8: the file's own bits when it copies (DXT into
    // the same DXT format, any other format), the surface's for 8888 converted to 16 bits
    // 16 for DXT decoded to 16 bits; 32-bit arithmetic.
    std::uint32_t FilledBytes(const PixelFormat& surfaceFormat, unsigned mip, unsigned levels) const;
    // The channel fields
    // +0x18..+0x44 zeroed with format and bits per pixel 0; or 1:5:5:5, 4:4:4:4 (both 16 bits) or
    // 8:8:8:8 (32 bits) with the format id.
    void SetNoFormat();
    void SetFormat5551();
    void SetFormat4444();
    void SetFormat8888();
};
static_assert(offsetof(MmpHeader, alphaKind) == 0x20);
static_assert(offsetof(MmpHeader, blueBits) == 0x44);
static_assert(sizeof(MmpHeader) == 0x4C);

// A DDPIXELFORMAT's channels as mask, bit count and shift; pixel_formats.cpp.
struct ChannelLayout {
    // +0x00 the bits per pixel the texture upload counts with (texture_blank.cpp); Split zeroes it
    // and never writes it, so it is always 0.
    std::uint32_t bitCount;
    std::uint32_t alphaMask, alphaBits, alphaShift;
    std::uint32_t redMask, redBits, redShift;
    std::uint32_t greenMask, greenBits, greenShift;
    std::uint32_t blueMask, blueBits, blueShift;
    // Zeroes the layout, then splits `format`'s alpha, red, green and blue masks
    // in that order. 0 for a null format, for one without DDPF_RGB (logged: "Format have no RGB
    // information") and at the first channel wider than 8 bits (the channels before it stay
    // filled); else 1.
    int Split(const void* format);
    // An A8R8G8B8 colour into a pixel of this layout, each channel's top bits
    // at its shift.
    std::uint32_t ToNative(std::uint32_t argb) const;
    // A pixel of this layout as A8R8G8B8: each channel's bits moved to the top
    // of its byte (the low bits stay 0).
    std::uint32_t FromNative(std::uint32_t pixel) const;
};
static_assert(sizeof(ChannelLayout) == 0x34);

// Class id 6, "CTexture", 0x68 bytes. A real virtual
// class.
class CTexture : public CEngineObject {
public:
    CTexture();
    ~CTexture() override;

    // Creates the surface from.mmp data, starting at mip level `mip`
    // (a system-memory surface filled from the file and loaded into a
    // managed texture surface; ours: a holder for the levels the new renderer decodes); copies the
    // file's extra block. Returns 1.
    int CreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);
    // Loads the.bmp `path` with its alpha from "a_<file>" beside it
    // (LoadBmpFiles), the path's pieces trimmed. 1 without loading when this texture is loaded and
    // named `path`.
    int LoadBmp(CTextureDevice* device, const char* path);
    // A surface with no pixels yet, as `header` describes it (texture_blank.cpp): square,
    // header->width wide (the height is not read), header->mipCount levels, the texture's format_,
    // the original's managed texture (system memory with flag 0x400); ours only the new renderer's
    // memory (NewMemoryTextureHolder, dx_surface.h; one level). Returns 1.
    int CreateBlank(CTextureDevice* device, const MmpHeader* header);
    // Enters the device lock, which stays held until UnlockPixels,
    // and describes the holder's memory (the original: Lock) into `pixels`. 1; 0 without a holder
    // (no lock) or without memory.
    int LockPixels(SurfacePixels* pixels);
    // Leaves the device lock (and unlocks); nothing
    // without a holder.
    void UnlockPixels();
    // The shadow's render surface copied into its texture (texture_shadow.cpp; the
    // device's Load is not used): the memory's rows copied, the device's lastLoadResult_ 0.
    void LoadPixelsFrom(CTextureDevice* device, const CTexture* source);
    // Loads a 24-bit.bmp (`path`) and, when `alphaPath` is an 8-bit
    // .bmp, its alpha into a texture of the .bmp's size in the texture's format (format_). 1 (also
    // when already loaded); 0 when the .bmp cannot be opened.
    int LoadBmpFiles(CTextureDevice* device, const char* path, const char* alphaPath);

    // Back to the constructed state: no name, the surface released, sizes and upload
    // result 0, an empty format of size 0x20, the extra block freed; block_ is cleared without
    // being freed. Returns 1.
    int Create() override;
    // The block freed, the surface released, +0x64 and flags_ 0.
    void Destroy() override;
    // The name's text.
    const char* GetName() override;
    const char* GetClassName() override;
    int GetClassId() override;
    // Frees block_ (an aligned block) and clears it.
    void FreeBlock();

    mfc::CString* Name() { return &name_; }

    mfc::CString name_;
    std::uint32_t width_;
    std::uint32_t height_;
    int uploadResult_;          // the surface fill's byte count (MmpHeader::FilledBytes); 0 after CreateBlank
    AlignedBuffer extra_;       // the .mmp file's extra block
    PixelFormat format_;
    CDXSurface* surface_;       // the texture surface (counted)
    // +0x60 an aligned block (raw pointer before the data), freed when a surface is made
    // and in Destroy. For a keyed texture (flags_ 0x2000, a type 0x35 object's skin) the
    // shadow rasterises through it as 16-bit texels (RasterTriangle::keyTexels); nothing in this
    // exe allocates it, so it is null and the keyed path never runs.
    std::uint8_t* block_;
    std::uint32_t spare64_;     // cleared by the constructor, slot 2 and slot 3; nothing reads it

    // The texture manager that owns the texture (owner_; below).
    CTextureManager* Manager() const;
};
static_assert(offsetof(CTexture, name_) == 0x1C && offsetof(CTexture, extra_) == 0x2C);
static_assert(offsetof(CTexture, format_) == 0x3C);
static_assert(offsetof(CTexture, surface_) == 0x5C);

// Counted texture holders (a CTexture* the user keeps).
// Makes sure the held texture is loaded: a null one is a fatal error ("Final loading data to
// NULL texture!", its result returned); one with flags_ bit 3 gives 1; otherwise the texture
// manager's LoadTexture and its result.
int __fastcall TextureHolderEnsureLoaded(struct HeldTexture* holder);
// (A counted texture pointer, HeldTexture, is in model.h.)
static_assert(offsetof(CTexture, block_) == 0x60);
static_assert(sizeof(CTexture) == 0x68);

constexpr std::uint32_t kTextureLoaded = 0x08;    // CEngineObject::flags_

// Deletes every composed-skin cache file
// (<data path>textures\*.mmp); a file that cannot be deleted is reported through the debug log
// with its name and GetLastError.
void __cdecl DeleteComposedTextures();

// Class id 0x26. A real virtual class.
class CTextureManager : public CResourceManager {
public:

    CTextureManager();
    ~CTextureManager() override;
    const char* GetClassName() override;
    int GetClassId() override;               // 0x26
    const char* GetFolderName() override;    // "textures"
    // Rebirth-patched, hooked in the vanilla launch only. CBaseManager's
    // Create (0 when it fails), the composed textures deleted, then the three texture archives of
    // the text resources' folder added: "textures.res", "redress.res", "menus.res". All
    // three are tried; 1 only when every one opened.
    int Create() override;
    // As the destructor up to CResourceManager::Destroy (without SetDevice).
    void Destroy() override;
    // CBaseManager::CreateObject, then flags_ by type: 0x3E 0x80, 0x3F 0x100, 0x40
    // 0x200, any other 0x40 (no null check).
    CEngineObject* CreateObject(int type, int id) override;
    // Loads a texture: textures\<name>.bmp on disk if present, else <name>.mmp from the
    // archives, else <data path>textures\<name>.mmp on disk.
    int LoadTexture(CTexture* texture);
    // Loads <directory><name>.mmp from disk.
    int LoadTextureFrom(CTexture* texture, const char* directory);
    // Reads the extra block of <name>.mmp into `out` (sized to it, old contents not
    // kept): from <data path>textures\ on disk, else from the archives. Returns 0 (and an empty
    // buffer) when neither has the file.
    int LoadMmpExtra(const char* name, AlignedBuffer* out);
    // A new texture (CreateObject(kind, -1)) the game fills on the CPU (texture_blank.cpp):
    // `width` x `height` in the format of `kind` (0x1E 5:6:5, 0x20 1:5:5:5, 0x21 4:4:4:4; another
    // kind keeps a zero format), its surface square at `width` (CreateBlank): only the new
    // renderer's memory. Null when CreateBlank fails;
    // FatalError's result when the device has no such format.
    CTexture* NewTexture(int width, int height, int kind);
    // A new 64x64 4:4:4:4 texture for an object's shadow (kind 0x25,
    // flag 0x200); only the new renderer's memory. Null when
    // its surface cannot be made; FatalError's result when the device has no 4:4:4:4 format.
    CTexture* NewShadowTexture();
    // The texture the shadows are rendered into (shadowSurface_, made
    // on first use as NewShadowTexture's, but flag 0x400: in system memory).
    CTexture* ShadowRenderSurface();
    // A texture without references leaves the manager: a
    // registered one with a flag of 0x38 goes into the cache (cache_, cachedBytes_; the original also
    // sets its surface's priority to 0), any other is destroyed and kept as a spare. Returns 0.
    int ReleaseObject(CEngineObject* object) override;
    // A new CTexture of `type`: the constructor and the reset inlined
    // (no name, no surface, an empty extra block, a pixel format of size 0x20 and nothing else).
    // Null when the allocation fails.
    CEngineObject* NewObject(int type) override;
    // Holds `device` (counted). Without one: textures limited to 511
    // pixels and mip level 2. Else the mip level from the texture quality setting (0, 1, 2, else 3),
    // the limit when the Direct3D device's largest texture is at most 256 pixels wide or high (or
    // its GetCaps fails and the DirectX report comes back with 0), and a tenth of the total local
    // video memory for textures.
    void SetDevice(CTextureDevice* device);
    // The cached texture of `type` named `name`, taken out of the
    // cache and registered again (its surface's priority 100); null when there is none.
    CTexture* TakeFromCache(int type, const char* name);
    // The registered texture named `name`, else a cached one taken
    // back as TakeFromCache does (but cachedBytes_ is not lowered); null.
    CTexture* FindTexture(const char* name);
    // The registered texture of `type` named `name`, else
    // TakeFromCache's; null when neither has it.
    CTexture* FindTypedTexture(int type, const char* name);
    // Deletes every cached texture (slot 0 with 1) and empties the
    // cache; cachedBytes_ 0.
    void FreeCache();
    // Draws the layer textures `names` (each a
    // `width` x `width` .mmp from the archives, GetFile / CloseFile) into the 32-bit `image`
    // through the pixel converter's slot 5. A missing one goes to the debug log and is skipped;
    // one of another size is a fatal error (its result returned). Returns the layers drawn.
    int DrawLayers(std::uint32_t* image, const mfc::CStringArray* names, int width);
    // The skin `request` composed into the cached texture
    // `name` (or `fallback`), the name used into `out`: 1 when cached or written, 0 when nothing was
    // drawn (FatalError's result when the file cannot be written).
    int Compose(mfc::CString* out, const SkinRequest* request, const mfc::CString* name,
                const mfc::CString* fallback, unsigned mipCount, std::uint32_t fill, std::uint32_t format);

    // GetFile(name) without the size.
    void* GetFileData(const char* name);
    // CreateObject(type, -1).
    CTexture* NewTextureOfType(int type);
    // FindTypedTexture(type, name), else a new texture of `type`; it is given
    // `name` either way (no null check) and returned. Not loaded.
    CTexture* FindOrCreate(int type, const char* name);
    // FindTexture(name) as it is; else FindOrCreate's texture, loaded
    // (LoadTexture). Returns it.
    CTexture* FindOrLoad(int type, const char* name);
    // Every registered and every cached texture loses its loaded flag and
    // is loaded again (after a lost device); false when any LoadTexture fails.
    bool ReloadAll();
    // While the cache holds more bytes than videoMemoryTenth_ (signed), its oldest
    // texture is taken out and deleted (the world client, every 32 ticks).
    void TrimCache();

    mfc::CTypedPtrList<CTexture> cache_;   // released textures kept for reuse
    int cachedBytes_;           // the cached textures' byte sizes (CTexture::uploadResult_)
    std::uint32_t videoMemoryTenth_;   // a tenth of the local video memory for textures (SetDevice)
    CTexture* shadowRenderSurface_;   // the shadows' render surface (counted)
    CTextureDevice* device_;
    std::uint16_t mipLevel_;    // first mip level to load (texture quality setting)
    std::uint16_t unused_EA;
    int limitTo512_;            // nonzero: textures are loaded at most 511 pixels wide and high
};
inline CTextureManager* CTexture::Manager() const { return static_cast<CTextureManager*>(owner_); }
static_assert(offsetof(CTextureManager, cache_) == 0xBC);
static_assert(offsetof(CTextureManager, cachedBytes_) == 0xD8);
static_assert(offsetof(CTextureManager, shadowRenderSurface_) == 0xE0);
static_assert(offsetof(CTextureManager, device_) == 0xE4);
static_assert(offsetof(CTextureManager, limitTo512_) == 0xEC);
