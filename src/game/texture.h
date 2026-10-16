#pragma once

#include "engine_object.h"
#include "resource_manager.h"
#include "lib/containers.h"
#include "lib/stlp.h"

#include <windows.h>

// Pixel format of a texture surface (32 bytes), as filled in by the texture device.
struct PixelFormat {
    std::uint32_t words[8];
};
static_assert(sizeof(PixelFormat) == 32);

struct CDXSurface;
class CTexture;
class CTransformPipeline;
class CDeviceMaterial;   // device_material.h
struct NamedTexture;   // ground_print.h
struct Direct3DDeviceRecord;   // device.h
struct TLVertex;               // clip.h
namespace devicebuffers { struct ProjectedVertex; }   // device_buffers.h
struct _DDSURFACEDESC2;        // ddraw.h (DDSURFACEDESC2)

// A vertex buffer's holder, as the device's batches keep them (counted; slot 0 the deleting
// destructor). Nothing makes one (004E7820 gives no buffer); only what the flushes read.
struct CVertexBufferHolder {
    void* vtable;
    int refCount;               // +0x04
    long result;                // +0x08 the last Direct3D result
    std::uint8_t unused_0C[0x10];
    void* buffer;               // +0x1C IDirect3DVertexBuffer7*

    // Drops a reference; the deleting destructor (vtable slot 0, with 1) at zero.
    void Release() {
        if (--refCount != 0) return;
        using Destroy = void(__fastcall*)(CVertexBufferHolder*, void*, int);
        (*static_cast<Destroy*>(vtable))(this, nullptr, 1);
    }
};
static_assert(offsetof(CVertexBufferHolder, buffer) == 0x1C);

// One of the device's two vertex-buffer batches (0x20 bytes): triangles gathered in a locked
// vertex buffer and drawn in one call (004E7840 / 004E7A40). Never used: no buffer is ever locked.
struct DeviceVertexBatch {
    stlp::list<CVertexBufferHolder*> used;      // +0x00 the buffers drawn (counted)
    CVertexBufferHolder* buffer;                // +0x04 the locked buffer (counted)
    void* vertices;                             // +0x08 its memory
    int vertexCount;                            // +0x0C vertices written (added to appended indices)
    lib::AlignedArray<std::uint16_t> indices;   // +0x10
};
static_assert(sizeof(DeviceVertexBatch) == 0x20 && offsetof(DeviceVertexBatch, indices) == 0x10);

// Owns the Direct3D texture formats; held by CTextureManager. Name provisional; not rewritten yet.
// The same object is the renderer's device object (the global at 007AF080): its DirectDraw
// interface, the primary surface's and the render target's holders and the screen rectangle.
class CTextureDevice {
public:
    // 0x004EB170 (device_states.cpp).
    int SetMaterial(CDeviceMaterial* material);
    // 0x004EA4A0 (device_shadow.cpp).
    int DrawFlatBlocks(const void* positions, const char* uvs, int blocks, const std::uint16_t* indices, int indexCount,
                 std::uint32_t colour, std::uint32_t specular, std::uint32_t last, float zScale);
    // 0x004E9A50 (device_mesh.cpp).
    int DrawMeshBlocks2(void* firstTexture, void* secondTexture, const void* positions, int blocks, int unused5,
                  int colourCount, std::uint32_t* colours, std::uint32_t* speculars, const void* uvs, int unused10,
                  const void* corners, int cornerCount, int unused13, int unused14, std::uint16_t* indices,
                  int indexCount, float uOffset, float vOffset, int alpha, int flags);
    // 0x004E9010 (device_mesh.cpp).
    int DrawMeshBlocks(const void* positions, int blocks, int unused5, int colourCount, std::uint32_t* colours,
                 std::uint32_t* speculars, const void* uvs, int unused10, const void* corners, int cornerCount,
                 int unused13, int unused14, std::uint16_t* indices, int indexCount, float uOffset, float vOffset,
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
    // pixel_formats.cpp (A-S11).
    // 004E7C40 (RET 8). The texture format for an .mmp file's header: the format its id names
    // (formats_); for a DXT id the device does not have, the format its channel bit counts name
    // (5:6:5, 5:5:5, 1:5:5:5, 4:4:4:4, 8:8:8:8; anything else looks id 0 up). 1 with `format`
    // filled, 0 with it untouched.
    int ParseMmpHeader(const void* data, PixelFormat* format);
    // 004E7DA0 (RET 8). The texture format registered under `id` (0x5650, 0x5551, 0x4444, 0x8888,
    // a DXT four-character code): 1 with `format` filled, 0 with it untouched.
    int FindFormat(int id, PixelFormat* format);
    // 004E7820 (RET 4; device_helpers.cpp). A vertex buffer for a batch of `count` vertices: always
    // null (the batch below is never used).
    void* LockVertexBuffer(int count);
    // 004E7E00 (RET 8; device_helpers.cpp). Appends `count` indices to the batch (batch_.indices),
    // each raised by batch_.vertexCount when that is not 0, then sets the array's size (the indices
    // are written before the array grows: the caller has made room).
    void AppendBatchIndices(const std::uint16_t* indices, int count);

    // device_binds.cpp (A-S13).
    // 004E7840 / 004E7A40 (plain RET). A batch drawn: its vertex buffer unlocked (the device lock,
    // held since the buffer's Lock, left; a failure is "Can't unlock vertex buffer."), the indexed
    // triangles drawn from it (DrawIndexedPrimitiveVB under the device lock, the result in
    // lastLoadResult_, the frame's counts raised), the buffer moved to the batch's list and the
    // batch emptied. Nothing without a locked buffer, which is always.
    void FlushBatch();
    void FlushBatch2();
    // 004EB300 (RET 8). `holder`'s surface (or none) on texture stage `stage`, when the stage has
    // another holder: the batch flushed, SetTexture (the result in lastLoadResult_), the change
    // counted. Returns 1.
    int BindSurface(int stage, CDXSurface* holder);
    // 004EBAF0 (RET 8). The same for a texture's surface holder (a null texture: none).
    int BindTexture(int stage, CTexture* texture);
    // 004EB1D0 (RET 8). The same for a texture kept by name: one not looked up yet is found by
    // its name among the texture manager's registered and cached textures (FindTexture; an empty
    // name is not looked up) and kept, counted.
    int BindNamedTexture(int stage, NamedTexture* named);

    // directdraw_holder.cpp (A-S13): the object's DirectDraw base class (vtable 0073D770, the
    // members up to +0x50). A failed DirectDraw call throws the game's error object (dx_errors.h).
    // 004EDD70 (RET 0x24). The base's constructor: the processor's CPUID words into 007CAED0 (no
    // MMX: a fatal error and exit), 2DintMMX.dll loaded and its graphics processor taken, the
    // DirectDraw object of `driver` made with its display modes (CreateDirectDraw), the
    // cooperative level, for full screen the display mode, the primary and back surfaces, and the
    // YUV formats the driver can make an off-screen and an overlay surface of. Returns this.
    CTextureDevice* ConstructDirectDraw(HWND window, GUID* driver, int fullScreen, int width, int height,
                                        int bitsPerPixel, int refreshRate, unsigned backBuffers, int for3D);
    // 004EE330 (plain RET). The base's destructor body: the display mode restored (full screen),
    // the library freed, the clipper, both surface holders and the DirectDraw object released, the
    // display modes freed.
    void DestructDirectDraw();
    // 004EEC60 (plain RET). The display mode restored, in full screen only.
    void RestoreDisplayMode();
    // 004EECF0 (RET 8). The cooperative level of the window: exclusive full screen or normal, with
    // DDSCL_FPUSETUP | DDSCL_MULTITHREADED; `screen` (or an empty rectangle) becomes screen_.
    void SetCooperativeLevel(int fullScreen, const RECT* screen);

    // device_object.cpp (A-S13): the device object itself (vtable 0073D760).
    // 004E5E50 (RET 0x28). The constructor: the base (one back buffer asked as the caller says,
    // for 3D), IDirect3D7 from the DirectDraw object, its devices listed, the HAL device made on
    // the render target, the texture formats (BuildFormatTable) and depth formats listed, the
    // depth buffer (CreateDepthBuffer), the render target and viewport set, and four state blocks
    // of stage 0's colour and alpha operations recorded (007AEDD0 .. 007AEDDC). Returns this.
    CTextureDevice* Construct(HWND window, GUID* driver, int fullScreen, int width, int height, int bitsPerPixel,
                              int refreshRate, unsigned backBuffers, int depthBits, int stencilBits);
    // 004E68A0 (plain RET). The destructor body: the four state blocks deleted, the batches'
    // buffers, the depth surfaces and the pipeline dropped, the Direct3D device and object
    // released, the members destroyed, then the base's destructor body.
    void Destruct();
    // 004E6EE0 (plain RET). The texture formats: the Direct3D device's formats listed again and
    // formats_ filled from them: DXT1 .. DXT5 by their four-character codes, 5:6:5, 5:5:5 (0x5550),
    // 1:5:5:5, 4:4:4:4 and 8:8:8:8 by their ids; largestFormatId_ is the largest id stored.
    void BuildFormatTable();

    // Holders count references to it (the cursor): one more, or one less with the deleting
    // destructor (vtable slot 0, with 1) at zero.
    void AddRef() { ++refs_; }
    void Release() {
        if (--refs_ != 0) return;
        using Destroy = void(__fastcall*)(CTextureDevice*, void*, int);
        (*static_cast<Destroy*>(vtable_))(this, nullptr, 1);
    }

    void* vtable_;
    int refs_;                  // +0x04
    int lastLoadResult_;        // +0x08 result of the last IDirect3DDevice7::Load (and other calls)
    void* ddraw_;               // +0x0C IDirectDraw7*
    GUID* driver_;              // +0x10 the driver the DirectDraw object was made for
    CDXSurface* primary_;       // +0x14 the primary surface's holder
    CDXSurface* renderTarget_;  // +0x18 the render target's holder
    void* clipper_;             // +0x1C IDirectDrawClipper* of the window (004EF510)
    std::uint32_t flags_;       // +0x20 the cooperative level; bit 0: full screen (the present flips)
    RECT screen_;               // +0x24 the screen rectangle
    HWND window_;               // +0x34
    stlp::list<_DDSURFACEDESC2> modes_;   // +0x38 the driver's display modes
    std::uint32_t videoFourCC_;     // +0x3C the YUV format an off-screen video-memory surface can have (0: none)
    std::uint32_t overlayFourCC_;   // +0x40 the YUV format an overlay can have (0: none)
    bool overlayStretch_;       // +0x44 DDCAPS_OVERLAYSTRETCH
    std::uint8_t unused_45[3];
    HMODULE converterLibrary_;  // +0x48 2DintMMX.dll
    void* converter_;           // +0x4C 2DintMMX's graphics processor (pixel conversions)
    void* d3d_;                 // +0x50 IDirect3D7*
    void* d3dDevice_;           // +0x54 IDirect3DDevice7*
    CDXSurface* depth_;         // +0x58 the depth surface's holder (restored with the primary, 00504780)
    CDXSurface* depth2_;        // +0x5C a second depth surface like it (not attached)
    std::uint32_t zEnable_;     // +0x60 the z-buffer setting render state 7 goes back to (00569E40)
    std::uint8_t unused_64[0x7C - 0x64];
    GUID deviceGuid_;           // +0x7C the Direct3D device's GUID
    // +0x8C the depth buffer's format (a DDPIXELFORMAT). The frame clear (00501A40) clears the
    // stencil too when its flags (words[1]) have DDPF_STENCILBUFFER and its stencil bits
    // (words[4]) are nonzero.
    PixelFormat depthFormat_;
    CTransformPipeline* pipeline_;   // +0xAC the scene renderer's pipeline (model.h)
    stlp::list<Direct3DDeviceRecord> devices_;   // +0xB0 the Direct3D devices of the driver
    stlp::list<PixelFormat> depthFormats_;   // +0xB4 the enumerated depth buffer formats (004E6C30 picks one)
    stlp::list<PixelFormat> textureFormats_; // +0xB8 the enumerated texture formats
    // +0xBC the texture formats by id (filled by 004E6EE0 from the Direct3D device's enumeration);
    // the values point at textureFormats_'s elements.
    stlp::hash_map<int, PixelFormat*, stlp::IntHash> formats_;
    std::uint32_t largestFormatId_;  // +0xD0 the largest id in formats_
    DeviceVertexBatch batch_;        // +0xD4 the vertex-buffer batch (never used: 004E7820 gives no buffer)
    DeviceVertexBatch batch2_;       // +0xF4 the second one
    // +0x114: a third set the constructor makes and the destructor frees; nothing else reads it.
    stlp::list<CVertexBufferHolder*> spareBuffers_;   // +0x114 counted holders
    CVertexBufferHolder* spareBuffer_;                // +0x118 counted
    lib::AlignedArray<std::uint8_t> spareArray_;      // +0x11C
    CDXSurface* stageSurfaces_[8];   // +0x12C the surface holder bound to each texture stage (00501AF0, 004EBAF0)
    CDeviceMaterial* material_;      // +0x14C the current material (004EB170; device_material.h)
    std::uint32_t colour_;           // +0x150 the renderer's draw colour (00501CC0; mesh draw flag 4)
    std::uint8_t unused_154[4];
    // +0x158 the draws fog their screen vertices by distance (the transform object's FogVertices)
    // when set; cleared around the sky and hit point draws (00569E40), else 1.
    int distanceFog_;
    RECT drawnRect_;            // +0x15C the screen rectangle of the last mesh draw (004E9010 / 004E9A50)
    // +0x16C .. +0x178: the frame's counts, cleared by the renderer's BeginScene (00503F40).
    int vertexCount_;           // +0x16C vertices drawn
    int primitiveCount_;        // +0x170 primitives drawn
    int drawCount_;             // +0x174 draw calls
    int textureChanges_;        // +0x178 texture bindings changed (00501AF0)
};
static_assert(offsetof(CTextureDevice, drawnRect_) == 0x15C && offsetof(CTextureDevice, vertexCount_) == 0x16C);
static_assert(offsetof(CTextureDevice, lastLoadResult_) == 0x08);
static_assert(offsetof(CTextureDevice, primitiveCount_) == 0x170 && offsetof(CTextureDevice, drawCount_) == 0x174);
static_assert(offsetof(CTextureDevice, textureChanges_) == 0x178);
static_assert(sizeof(CTextureDevice) == 0x17C);
static_assert(offsetof(CTextureDevice, zEnable_) == 0x60);
static_assert(offsetof(CTextureDevice, distanceFog_) == 0x158);
static_assert(offsetof(CTextureDevice, deviceGuid_) == 0x7C && offsetof(CTextureDevice, depthFormat_) == 0x8C);
static_assert(offsetof(CTextureDevice, window_) == 0x34 && offsetof(CTextureDevice, converterLibrary_) == 0x48);
static_assert(offsetof(CTextureDevice, devices_) == 0xB0 && offsetof(CTextureDevice, largestFormatId_) == 0xD0);
static_assert(offsetof(CTextureDevice, spareBuffers_) == 0x114 && offsetof(CTextureDevice, spareArray_) == 0x11C);
static_assert(offsetof(CTextureDevice, stageSurfaces_) == 0x12C && offsetof(CTextureDevice, colour_) == 0x150);
static_assert(offsetof(CTextureDevice, depthFormats_) == 0xB4 && offsetof(CTextureDevice, formats_) == 0xBC);
static_assert(offsetof(CTextureDevice, batch_) == 0xD4 && offsetof(CTextureDevice, batch2_) == 0xF4);

// The renderer's device object (the global pointer at 007AF080).
inline CTextureDevice* RenderDeviceObject() { return *reinterpret_cast<CTextureDevice* const*>(0x007AF080); }
static_assert(offsetof(CTextureDevice, ddraw_) == 0x0C);
static_assert(offsetof(CTextureDevice, converter_) == 0x4C);
static_assert(offsetof(CTextureDevice, primary_) == 0x14);
static_assert(offsetof(CTextureDevice, renderTarget_) == 0x18);
static_assert(offsetof(CTextureDevice, flags_) == 0x20);
static_assert(offsetof(CTextureDevice, screen_) == 0x24);
static_assert(offsetof(CTextureDevice, d3dDevice_) == 0x54);
static_assert(offsetof(CTextureDevice, pipeline_) == 0xAC);

// A Direct3D driver of the device list (the STLport list at 007AEE64, filled by the original's
// 004EE4A0, which the image does not run: it stays empty; 004EE6D0 returns the one with an index).
// 0x30 bytes.
struct DirectDrawDriver {
    GUID guidValue;             // +0x00 the driver's GUID (unset for the primary display driver)
    GUID* guid;                 // +0x10 for DirectDrawCreateEx: &guidValue, or null (the primary display driver)
    stlp::string name;          // +0x14
    stlp::string description;   // +0x20
    HMONITOR monitor;           // +0x2C
};
static_assert(offsetof(DirectDrawDriver, guid) == 0x10 && offsetof(DirectDrawDriver, description) == 0x20);
static_assert(sizeof(DirectDrawDriver) == 0x30);
// The driver list (007AEE64).
inline stlp::list<DirectDrawDriver>& DirectDrawDrivers() {
    return *reinterpret_cast<stlp::list<DirectDrawDriver>*>(0x007AEE64);
}
// 004EE6D0 (the index in ECX; render_helper.cpp). The driver `index` steps after the list's first;
// past the last: a fatal error ("Can't find driver by index.") and null.
DirectDrawDriver* __fastcall DriverAt(int index);

// A DirectDraw surface wrapper (the original's 004EFA60 constructs it from a DDSURFACEDESC2, 004EF7B0
// for an off-screen surface of the screen's format; 0x20 bytes): vtable (slot 0 the deleting
// destructor), the reference count, the last DirectDraw result, the device object, the
// IDirectDrawSurface7* (always null in the image: the pixels are the new renderer's memory,
// dx_surface.h), width, height and bits per pixel.
struct CDXSurface {
    CDXSurface* DeleteScalar(unsigned flags);
    // 004EF7B0 (dx_surface.cpp, thiscall). The constructor for an off-screen surface of the screen's
    // format; returns this. Throws the game's error object when the surface cannot be made.
    CDXSurface* Construct(CTextureDevice* device, int width, int height);
    // 004EFC60 (dx_surface.cpp, thiscall, RET 0xC): a holder whose surface shows an .mmp picture.
    CDXSurface* ConstructFromPicture(CTextureDevice* device, const void* picture, int flag);
    // 004F0060 (dx_surface.cpp, thiscall, RET 8): loads the picture data into the surface.
    int LoadPictureData(const void* data, int flag);
    void* vtable;
    int refCount;               // +0x04
    long result;                // +0x08
    CTextureDevice* device;     // +0x0C
    void* surface;              // +0x10 (null: no holder has a DirectDraw surface)
    int width;                  // +0x14
    int height;                 // +0x18
    int bitsPerPixel;           // +0x1C

    // 004E6BA0: drops a reference; the deleting destructor (vtable slot 0, with 1) at zero.
    void Release() {
        if (--refCount != 0) return;
        using Destroy = void(__fastcall*)(CDXSurface*, void*, int);
        (*static_cast<Destroy*>(vtable))(this, nullptr, 1);
    }
};
static_assert(offsetof(CDXSurface, result) == 0x08);
static_assert(offsetof(CDXSurface, device) == 0x0C);
static_assert(offsetof(CDXSurface, surface) == 0x10);
static_assert(offsetof(CDXSurface, bitsPerPixel) == 0x1C);
static_assert(sizeof(CDXSurface) == 0x20);

// The start of the target the pixel converter's triangle rasterisers (slots 8 / 9) read: a shadow
// triangle's three corners, each a screen and a texture point (moved as qwords), and the keyed
// texture's texels; the surface follows (map_object_shadow.cpp fills it).
struct RasterTriangle {
    struct Point {
        int x;
        int y;
    };
    struct Corner {
        Point screen;
        Point texture;
    };
    Corner corners[3];
    // +0x30 a keyed texture's texels (CTexture::block_), or null. Set: the figure meshes (005A0E80)
    // rasterise through slot 9, which samples them as 16-bit 1:5:5:5 texels at the corners' texture
    // points (wrapping at the row pitch, RasterTarget +0x38) and shadows only where the top bit is set.
    const std::uint8_t* keyTexels;
};
static_assert(sizeof(RasterTriangle::Point) == 8 && offsetof(RasterTriangle, keyTexels) == 0x30);

// 2DintMMX's graphics processor, the pixel converter (*007AF06C, also CTextureDevice::converter_).
// Its code is in the DLL; only the slots the rewritten code calls are named. Never constructed here.
class CPixelConverter {
public:
    virtual void Unknown0();
    // 1: the 16-bit format slot 2 writes for `kind` (2 1:5:5:5, 1 5:5:5, 0 5:6:5; -1 none of them)
    // and its channel masks (dx_surface.cpp, the picture copy into a surface).
    virtual void SetChannelMasks(int kind, std::uint32_t alphaMask, std::uint32_t redMask, std::uint32_t greenMask,
                                 std::uint32_t blueMask);
    // 2: 32-bit pixels to 16 bits; `kind` 3 4:4:4:4, 2 1:5:5:5, 0 5:6:5.
    virtual void ConvertTo16(std::uint16_t* out, const std::uint32_t* in, int kind, unsigned count);
    virtual void Unknown3();
    virtual void Unknown4();
    // 5: an .mmp file's pixels (after its header and extra block) into 32-bit pixels; `pnt3` for a
    // PNT3 file (MmpHeader::format "PNT3").
    virtual void UnpackTo32(std::uint32_t* out, const void* in, unsigned count, int pnt3);
    virtual void Unknown6();
    // 7: halves a square image (`width` is the new width).
    virtual void Halve(std::uint32_t* out, const std::uint32_t* in, unsigned width);
    // 8: rasterises one triangle of a shadow: `target` starts with its three corners (a screen and
    // a texture point each, pairs of ints) and goes on with the surface (004F4850, 005A0E80).
    virtual void RasteriseTriangle(void* target);
    // 9: the same, masked by a keyed texture's texels (RasterTriangle::keyTexels; 005A0E80).
    virtual void RasteriseKeyedTriangle(void* target);
    // 10: decodes a DXT block image into 16-bit pixels (texture_fill.cpp).
    virtual void DecodeDxt(void* target, const void* source, int mode, std::uint32_t width, std::uint32_t height);
    // 11 / 12: the two passes of the shadow's blur over a 16-bit image of `width` x `height` pixels
    // (shadow mode 2 runs both on the shadow texture's pixels; map_object_shadow.cpp).
    virtual void BlurShadow(void* pixels, int width, int height);
    virtual void BlurShadow2(void* pixels, int width, int height);
};
inline CPixelConverter* PixelConverter() { return *reinterpret_cast<CPixelConverter**>(std::uintptr_t(0x007AF06C)); }
constexpr std::uint32_t kMmpFormatPnt3 = 0x33544E50;   // "PNT3"

// A byte buffer aligned to 32 bytes (the engine's aligned array; its out-of-line SetSize is 00694300).
using AlignedBuffer = lib::AlignedArray<std::uint8_t>;

// Header of an .mmp file (0x4C bytes); the extra block of +0x48 bytes follows it, then the mips.
// Each channel: its mask, the shift of its lowest bit and its bit count.
struct MmpHeader {
    std::uint32_t magic;        // +0x00 "MMP"
    std::uint32_t width;        // +0x04
    std::uint32_t height;       // +0x08
    std::uint32_t mipCount;     // +0x0C
    std::uint32_t format;       // +0x10 pixel format id (0x8888, 0x4444, 0x5551, 0x5650, ...)
    std::uint32_t bitsPerPixel; // +0x14
    std::uint32_t alphaMask;    // +0x18
    std::uint32_t alphaShift;   // +0x1C
    std::uint32_t alphaKind;    // +0x20 the alpha bit count: 0, 1, 4 or 8 (texture flags 0x4000..0x20000)
    std::uint32_t redMask;      // +0x24
    std::uint32_t redShift;     // +0x28
    std::uint32_t redBits;      // +0x2C
    std::uint32_t greenMask;    // +0x30
    std::uint32_t greenShift;   // +0x34
    std::uint32_t greenBits;    // +0x38
    std::uint32_t blueMask;     // +0x3C
    std::uint32_t blueShift;    // +0x40
    std::uint32_t blueBits;     // +0x44
    std::uint32_t extraSize;    // +0x48

    // 006908E0. Offset of mip level `mip`'s pixels after the header: the extra block plus the
    // sizes of the larger levels (a geometric sum computed in 32-bit integers).
    std::uint32_t MipOffset(unsigned mip) const;
    // What 0068FA10 returns when it fills a texture surface of `surfaceFormat` from this file
    // (texture_mmp.cpp; the texture cache counts it as the texture's size, CTexture +0x28): per
    // level of the surface (from `mip`, `levels` of them, no further than the file's last) width x
    // height times the bits it writes, / 8: the file's own bits when it copies (00690970: DXT into
    // the same DXT format, any other format), the surface's for 8888 converted to 16 bits
    // (00690B30), 16 for DXT decoded to 16 bits (00690D30); 32-bit arithmetic.
    std::uint32_t FilledBytes(const PixelFormat& surfaceFormat, unsigned mip, unsigned levels) const;
    // 00693820 / 00693850 / 006938B0 / 00693910 (texture_slots.cpp, A-H25). The channel fields
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

// A DDPIXELFORMAT's channels as mask, bit count and shift (0x34 bytes); pixel_formats.cpp.
struct ChannelLayout {
    // +0x00 the bits per pixel the texture upload counts with (texture_blank.cpp); Split zeroes it
    // and never writes it, so it is always 0.
    std::uint32_t bitCount;
    std::uint32_t alphaMask, alphaBits, alphaShift;   // +0x04
    std::uint32_t redMask, redBits, redShift;         // +0x10
    std::uint32_t greenMask, greenBits, greenShift;   // +0x1C
    std::uint32_t blueMask, blueBits, blueShift;      // +0x28
    // 004EDAA0 (RET 4). Zeroes the layout, then splits `format`'s alpha, red, green and blue masks
    // in that order. 0 for a null format, for one without DDPF_RGB (logged: "Format have no RGB
    // information") and at the first channel wider than 8 bits (the channels before it stay
    // filled); else 1.
    int Split(const void* format);
    // 004EDC00 (RET 4). An A8R8G8B8 colour into a pixel of this layout, each channel's top bits
    // at its shift.
    std::uint32_t ToNative(std::uint32_t argb) const;
    // 004EDC70 (RET 4). A pixel of this layout as A8R8G8B8: each channel's bits moved to the top
    // of its byte (the low bits stay 0).
    std::uint32_t FromNative(std::uint32_t pixel) const;
};
static_assert(sizeof(ChannelLayout) == 0x34);

// vtable 00743D24, class id 6. 0x68 bytes (00691810 makes it).
class CTexture : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743D24);

    // 00690430 (texture_mmp.cpp). Creates the surface from .mmp data, starting at mip level `mip`
    // (the original: a system-memory surface filled from the file by 0068FA10 and loaded into a
    // managed texture surface; ours: a holder for the levels the new renderer decodes); copies the
    // file's extra block. Returns 1.
    int CreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);
    // 00690260 (texture_fill.cpp). Loads the .bmp `path` with its alpha from "a_<file>" beside it
    // (LoadBmpFiles), the path's pieces trimmed. 1 without loading when this texture is loaded and
    // named `path`.
    int LoadBmp(CTextureDevice* device, const char* path);
    // 0068F6C0. A surface with no pixels yet, as `header` describes it (texture_blank.cpp): square,
    // header->width wide (the height is not read), header->mipCount levels, the texture's format_,
    // the original's managed texture (system memory with flag 0x400); ours only the new renderer's
    // memory (NewMemoryTextureHolder, dx_surface.h; one level). Returns 1.
    int CreateBlank(CTextureDevice* device, const MmpHeader* header);
    // 00690FC0 (texture_shadow.cpp). Enters the device lock, which stays held until UnlockPixels,
    // and describes the holder's memory (the original: Lock) into `desc` (a DDSURFACEDESC2 as
    // dwords): pitch, pixels, pixel format. 1; 0 without a holder (no lock) or without memory.
    int LockPixels(std::uint32_t* desc);
    // 00691030 (texture_shadow.cpp). Leaves the device lock (the original: and unlocks); nothing
    // without a holder.
    void UnlockPixels();
    // 0051CE60's copy of the shadow's render surface into its texture (texture_shadow.cpp; the
    // original: the device's Load): the memory's rows copied, the device's lastLoadResult_ 0.
    void LoadPixelsFrom(CTextureDevice* device, const CTexture* source);
    // 0068FBF0 (texture_bmp.cpp). Loads a 24-bit .bmp (`path`) and, when `alphaPath` is an 8-bit
    // .bmp, its alpha into a texture of the .bmp's size in the texture's format (format_). 1 (also
    // when already loaded); 0 when the .bmp cannot be opened.
    int LoadBmpFiles(CTextureDevice* device, const char* path, const char* alphaPath);

    // texture_slots.cpp (A-H25).
    // 2: 00691A00. Back to the constructed state: no name, the surface released, sizes and upload
    // result 0, an empty format of size 0x20, the extra block freed; block_ is cleared without
    // being freed. Returns 1.
    int Create() override;
    // 3: 00691A70. The block freed, the surface released, +0x64 and flags_ 0.
    void Destroy() override;
    // 00690FA0. Frees block_ (an aligned block) and clears it.
    void FreeBlock();
    // 00691950. The destructor body: Destroy's work, the extra block freed, the name destroyed,
    // CEngineObject's destructor body. 00691930: slot 0.
    void DestructBody();
    CTexture* DeletingDestruct(unsigned flags);

    mfc::CString* Name() { return reinterpret_cast<mfc::CString*>(&name_); }

    char* name_;                // +0x1C CString
    std::uint32_t width_;       // +0x20
    std::uint32_t height_;      // +0x24
    int uploadResult_;          // +0x28 result of 0068FA10 (MmpHeader::FilledBytes); 0 after CreateBlank
    AlignedBuffer extra_;       // +0x2C the .mmp file's extra block
    PixelFormat format_;        // +0x3C
    CDXSurface* surface_;       // +0x5C the texture surface (counted)
    // +0x60 an aligned block (raw pointer before the data), freed when a surface is made (0068F6C0,
    // 0068F860) and by 00690FA0. For a keyed texture (flags_ 0x2000, a type 0x35 object's skin) the
    // shadow rasterises through it as 16-bit texels (RasterTriangle::keyTexels); nothing in this
    // exe allocates it, so it is null and the keyed path never runs.
    std::uint8_t* block_;
    std::uint32_t spare64_;     // +0x64 cleared by the constructor, slot 2 and slot 3; nothing reads it
};
static_assert(offsetof(CTexture, extra_) == 0x2C);
static_assert(offsetof(CTexture, format_) == 0x3C);
static_assert(offsetof(CTexture, surface_) == 0x5C);

// renderer_lifetime.cpp (A-U11). Counted texture holders (a CTexture* the user keeps).
// 00500970. Makes sure the held texture is loaded: a null one is a fatal error ("Final loading data to
// NULL texture!", its result returned); one with flags_ bit 3 gives 1; otherwise the texture
// manager's LoadTexture (006922A0) and its result.
int __fastcall TextureHolderEnsureLoaded(CTexture** holder);
// 00500A00 (RET 8). The holder drops its texture (set to null, then released) and takes the texture
// manager's registered or cached texture of `kind` named `name`, or a new one of `kind`
// (CreateObject(kind, -1)), named `name` (one more reference).
void __fastcall TextureHolderLoad(CTexture** holder, void*, int kind, const char* name);
static_assert(offsetof(CTexture, block_) == 0x60);
static_assert(sizeof(CTexture) == 0x68);

constexpr std::uint32_t kTextureLoaded = 0x08;    // CEngineObject::flags_

// 006916F0 (texture_cache.cpp; cdecl). Deletes every composed-skin cache file
// (<data path>textures\*.mmp); a file that cannot be deleted is reported through the debug log
// with its name and GetLastError.
void __cdecl DeleteComposedTextures();

// vtable 00743CB0, class id 0x26. Slots 16-22 are CResourceManager's.
class CTextureManager : public CResourceManager {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743CB0);
    static constexpr std::uintptr_t kCacheVtableAddress = std::uintptr_t(0x00743D18);   // cache_'s CPtrList

    // texture_manager_lifetime.cpp (A-H23).
    // 00691140. CResourceManager's constructor body, an empty cache (blocks of 10), no shadow
    // surface or device, +0xD8 / +0xE8 / +0xEC 0. Returns this.
    CTextureManager* Construct();
    // 00691250. The destructor body: the shadow surface released, every cached texture destroyed
    // (slot 3) and deleted, the cache emptied, the composed textures deleted, the device dropped,
    // CResourceManager::Destroy, SetDevice(null), then CResourceManager's destructor body.
    // 00691230: slot 0.
    void Destruct();
    CTextureManager* DeletingDestruct(unsigned flags);
    // 2: 00691500 (A-H26; Rebirth-patched, hooked in the vanilla launch only). CBaseManager's
    // Create (0 when it fails), the composed textures deleted, then the three texture archives of
    // the text resources' folder added (slot 16): "textures.res", "redress.res", "menus.res". All
    // three are tried; 1 only when every one opened.
    int Create() override;
    // 3: 00691630. As the destructor up to CResourceManager::Destroy (without SetDevice).
    void Destroy() override;
    // 23: 00691AA0. CBaseManager::CreateObject, then flags_ by type: 0x3E 0x80, 0x3F 0x100, 0x40
    // 0x200, any other 0x40 (no null check).
    CEngineObject* CreateObject(int type, int id) override;
    CEngineObject* CreateObjectImpl(int type, int id);   // body of the slot above (registered by member pointer)
    // 006922A0. Loads a texture: textures\<name>.bmp on disk if present, else <name>.mmp from the
    // archives, else <data path>textures\<name>.mmp on disk.
    int LoadTexture(CTexture* texture);
    // 006926F0. Loads <directory><name>.mmp from disk.
    int LoadTextureFrom(CTexture* texture, const char* directory);
    // 006928C0. Reads the extra block of <name>.mmp into `out` (sized to it, old contents not
    // kept): from <data path>textures\ on disk, else from the archives. Returns 0 (and an empty
    // buffer) when neither has the file.
    int LoadMmpExtra(const char* name, AlignedBuffer* out);
    // 00691D80. A new texture (CreateObject(kind, -1)) the game fills on the CPU (texture_blank.cpp):
    // `width` x `height` in the format of `kind` (0x1E 5:6:5, 0x20 1:5:5:5, 0x21 4:4:4:4; another
    // kind keeps a zero format), its surface square at `width` (CreateBlank): only the new
    // renderer's memory. Null when CreateBlank fails;
    // FatalError's result when the device has no such format.
    CTexture* NewTexture(int width, int height, int kind);
    // 00692010 (texture_shadow.cpp). A new 64x64 4:4:4:4 texture for an object's shadow (kind 0x25,
    // flag 0x200); only the new renderer's memory. Null when
    // its surface cannot be made; FatalError's result when the device has no 4:4:4:4 format.
    CTexture* NewShadowTexture();
    // 00692140 (texture_shadow.cpp). The texture the shadows are rendered into (shadowSurface_, made
    // on first use as NewShadowTexture's, but flag 0x400: in system memory).
    CTexture* ShadowRenderSurface();
    // 24: 00691B00 (texture_cache.cpp). A texture without references leaves the manager: a
    // registered one with a flag of 0x38 goes into the cache (cache_, cachedBytes_; the original also
    // sets its surface's priority to 0), any other is destroyed and kept as a spare. Returns 0.
    int ReleaseObject(CEngineObject* object) override;
    int ReleaseObjectImpl(CEngineObject* object);   // body of the slot above (registered by member pointer)
    // 14: 00691810 (texture.cpp). A new CTexture of `type`: the constructor and the reset inlined
    // (no name, no surface, an empty extra block, a pixel format of size 0x20 and nothing else).
    // Null when the allocation fails.
    CEngineObject* NewObject(int type) override;
    CEngineObject* NewObjectImpl(int type);   // body of the slot above (registered by member pointer)
    // 006913A0 (texture.cpp; RET 4). Holds `device` (counted). Without one: textures limited to 511
    // pixels and mip level 2. Else the mip level from the texture quality setting (0, 1, 2, else 3),
    // the limit when the Direct3D device's largest texture is at most 256 pixels wide or high (or
    // its GetCaps fails and the DirectX report comes back with 0), and a tenth of the total local
    // video memory for textures.
    void SetDevice(CTextureDevice* device);
    // 00691BF0 (texture_cache.cpp). The cached texture of `type` named `name`, taken out of the
    // cache and registered again (its surface's priority 100); null when there is none.
    CTexture* TakeFromCache(int type, const char* name);
    // 00693C60 (texture_cache.cpp). The registered texture named `name`, else a cached one taken
    // back as TakeFromCache does (but cachedBytes_ is not lowered); null.
    CTexture* FindTexture(const char* name);
    // 00692D30 (texture_cache.cpp; RET 8). The registered texture of `type` named `name`, else
    // TakeFromCache's; null when neither has it.
    CTexture* FindTypedTexture(int type, const char* name);
    // 00692F00 (texture_cache.cpp). Deletes every cached texture (slot 0 with 1) and empties the
    // cache; cachedBytes_ 0.
    void FreeCache();
    // 00693010 (texture_compose.cpp; RET 0xC). Draws the layer textures `names` (each a
    // `width` x `width` .mmp from the archives, GetFile / CloseFile) into the 32-bit `image`
    // through the pixel converter's slot 5. A missing one goes to the debug log and is skipped;
    // one of another size is a fatal error (its result returned). Returns the layers drawn.
    int DrawLayers(std::uint32_t* image, const mfc::CStringArray* names, int width);

    // texture_slots.cpp (A-H25).
    // 00691CF0 (RET 4). GetFile(name) without the size.
    void* GetFileData(const char* name);
    // 00691D40 (RET 4). CreateObject(type, -1).
    CTexture* NewTextureOfType(int type);
    // 00691D00 (RET 8). FindTypedTexture(type, name), else a new texture of `type`; it is given
    // `name` either way (no null check) and returned. Not loaded.
    CTexture* FindOrCreate(int type, const char* name);
    // 00693F10 (RET 8). FindTexture(name) as it is; else FindOrCreate's texture, loaded
    // (LoadTexture). Returns it.
    CTexture* FindOrLoad(int type, const char* name);
    // 00693F60 (sets only AL). Every registered and every cached texture loses its loaded flag and
    // is loaded again (after a lost device, 00504780); false when any LoadTexture fails.
    bool ReloadAll();
    // 00694020. While the cache holds more bytes than videoMemoryTenth_ (signed), its oldest
    // texture is taken out and deleted (the world client, every 32 ticks).
    void TrimCache();

    mfc::CTypedPtrList<CTexture> cache_;   // +0xBC released textures kept for reuse (vtable 00743D18)
    int cachedBytes_;           // +0xD8 the cached textures' byte sizes (CTexture::uploadResult_)
    std::uint32_t videoMemoryTenth_;   // +0xDC a tenth of the local video memory for textures (SetDevice)
    CTexture* shadowRenderSurface_;   // +0xE0 the shadows' render surface (00692140; counted)
    CTextureDevice* device_;    // +0xE4
    std::uint16_t mipLevel_;    // +0xE8 first mip level to load (texture quality setting)
    std::uint16_t unused_EA;
    int limitTo512_;            // +0xEC nonzero: textures are loaded at most 511 pixels wide and high
};
static_assert(offsetof(CTextureManager, cache_) == 0xBC);
static_assert(offsetof(CTextureManager, cachedBytes_) == 0xD8);
static_assert(offsetof(CTextureManager, shadowRenderSurface_) == 0xE0);
static_assert(offsetof(CTextureManager, device_) == 0xE4);
static_assert(offsetof(CTextureManager, limitTo512_) == 0xEC);
// 00691210 / 00691220 (slots 11 / 12): "CTextureManager", class id 0x26. 00691200 (slot 25): the
// archive folder, "textures".
const char* __fastcall TextureManagerClassName(CTextureManager*);
int __fastcall TextureManagerClassId(CTextureManager*);
const char* __fastcall TextureManagerFolder(CTextureManager*);
