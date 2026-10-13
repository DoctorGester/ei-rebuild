#pragma once

#include "engine_object.h"
#include "resource_manager.h"
#include "lib/containers.h"

#include <windows.h>

// Pixel format of a texture surface (32 bytes), as filled in by the texture device.
struct PixelFormat {
    std::uint32_t words[8];
};
static_assert(sizeof(PixelFormat) == 32);

struct CDXSurface;
class CTransformPipeline;

// Owns the Direct3D texture formats; held by CTextureManager. Name provisional; not rewritten yet.
// The same object is the renderer's device object (the global at 007AF080): its DirectDraw
// interface, the primary surface's and the render target's holders and the screen rectangle.
class CTextureDevice {
public:
    int ParseMmpHeader(const void* data, PixelFormat* format);   // 004E7C40
    int FindFormat(int id, PixelFormat* format);                 // 004E7DA0

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
    std::uint8_t unknown_10[4];
    CDXSurface* primary_;       // +0x14 the primary surface's holder
    CDXSurface* renderTarget_;  // +0x18 the render target's holder
    std::uint8_t unknown_1C[4];
    std::uint8_t flags_;        // +0x20 bit 0: full screen (the present flips)
    std::uint8_t unknown_21[3];
    RECT screen_;               // +0x24 the screen rectangle
    std::uint8_t unknown_34[0x4C - 0x34];
    void* converter_;           // +0x4C 2DintMMX's graphics processor (pixel conversions)
    std::uint8_t unknown_50[4];
    void* d3dDevice_;           // +0x54 IDirect3DDevice7*
    CDXSurface* depth_;         // +0x58 the depth surface's holder (restored with the primary, 00504780)
    std::uint8_t unknown_5C[4];
    std::uint32_t zEnable_;     // +0x60 the z-buffer setting render state 7 goes back to (00569E40)
    std::uint8_t unknown_64[0x90 - 0x64];
    // +0x90 / +0x9C: read as the depth format's DDPIXELFORMAT flags and stencil bits (the frame clear,
    // 00501A40, clears the stencil too when DDPF_STENCILBUFFER is set and the bits are nonzero).
    std::uint32_t depthFormatFlags_;   // +0x90
    std::uint8_t unknown_94[8];
    std::uint32_t stencilBits_;        // +0x9C
    std::uint8_t unknown_A0[0xAC - 0xA0];
    CTransformPipeline* pipeline_;   // +0xAC the scene renderer's pipeline (model.h)
    std::uint8_t unknown_B0[0x12C - 0xB0];
    CDXSurface* stageSurfaces_[8];   // +0x12C the surface holder bound to each texture stage (00501AF0, 004EBAF0)
    void* material_;                 // +0x14C the current material (004EB170)
    std::uint32_t colour_;           // +0x150 the renderer's draw colour (00501CC0; mesh draw flag 4)
    std::uint8_t unknown_154[4];
    int unknown_158;            // +0x158 cleared around the sky and hit point draws (00569E40), else 1
    RECT drawnRect_;            // +0x15C the screen rectangle of the last mesh draw (004E9010 / 004E9A50)
    // +0x16C .. +0x178: the frame's counts, cleared by the renderer's BeginScene (00503F40).
    int vertexCount_;           // +0x16C vertices drawn
    int primitiveCount_;        // +0x170 primitives drawn
    int drawCount_;             // +0x174 draw calls
    int textureChanges_;        // +0x178 texture bindings changed (00501AF0)
};
static_assert(offsetof(CTextureDevice, drawnRect_) == 0x15C && offsetof(CTextureDevice, vertexCount_) == 0x16C);
static_assert(sizeof(CTextureDevice) == 0x17C);
static_assert(offsetof(CTextureDevice, zEnable_) == 0x60);
static_assert(offsetof(CTextureDevice, unknown_158) == 0x158);
static_assert(offsetof(CTextureDevice, depthFormatFlags_) == 0x90 && offsetof(CTextureDevice, stencilBits_) == 0x9C);
static_assert(offsetof(CTextureDevice, stageSurfaces_) == 0x12C && offsetof(CTextureDevice, colour_) == 0x150);

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

// A Direct3D driver of the device list (the STLport list at 007AEE64, filled by 004EE4A0; 004EE5D0
// returns the first hardware one, 004EE6D0 the one with an index). Only what the renderer reads.
struct DirectDrawDriver {
    std::uint8_t unknown_00[0x10];
    GUID* guid;                 // +0x10 for DirectDrawCreateEx
};
static_assert(offsetof(DirectDrawDriver, guid) == 0x10);

// A DirectDraw surface wrapper (004EFA60 constructs it from a DDSURFACEDESC2, 004EF7B0 for an
// off-screen surface of the screen's format; 0x20 bytes): vtable (slot 0 the deleting destructor),
// the reference count, the last DirectDraw result, the device object, the IDirectDrawSurface7*,
// width, height and bits per pixel.
struct CDXSurface {
    void* vtable;
    int refCount;               // +0x04
    long result;                // +0x08
    CTextureDevice* device;     // +0x0C
    void* surface;              // +0x10
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

// 2DintMMX's graphics processor, the pixel converter (*007AF06C, also CTextureDevice::converter_).
// Its code is in the DLL; only the slots the rewritten code calls are named. Never constructed here.
class CPixelConverter {
public:
    virtual void Unknown0();
    virtual void Unknown1();
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

// 004EDAA0 (thiscall, RET 4) fills it: a DDPIXELFORMAT's channels as mask, bit count and shift
// (0x34 bytes, zeroed first; it stops early for a format without DDPF_RGB, logging "Format have no
// RGB information", or with a channel wider than 8 bits). Returns 1 when the four were split.
struct ChannelLayout {
    std::uint32_t unknown_00;                         // zeroed, never written
    std::uint32_t alphaMask, alphaBits, alphaShift;   // +0x04
    std::uint32_t redMask, redBits, redShift;         // +0x10
    std::uint32_t greenMask, greenBits, greenShift;   // +0x1C
    std::uint32_t blueMask, blueBits, blueShift;      // +0x28
    int Split(const void* format);
    // 004EDC00's conversion (texture_bmp.cpp; 004EDC00 itself is not hooked): an A8R8G8B8 colour
    // into a pixel of this layout, each channel's top bits at its shift.
    std::uint32_t ToNative(std::uint32_t argb) const;
};
static_assert(sizeof(ChannelLayout) == 0x34);

// vtable 00743D24, class id 6. 0x68 bytes (00691810 makes it).
class CTexture : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743D24);

    // 00690430. Creates the surface from .mmp data, starting at mip level `mip`: a system-memory
    // surface filled from the file (0068FA10) and loaded into a managed texture surface; copies
    // the file's extra block. Returns 1.
    int CreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);
    int CallCreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);   // through 00690430
    // 00690260 (texture_fill.cpp). Loads the .bmp `path` with its alpha from "a_<file>" beside it
    // (LoadBmpFiles), the path's pieces trimmed. 1 without loading when this texture is loaded and
    // named `path`.
    int LoadBmp(CTextureDevice* device, const char* path);
    int CallLoadBmp(CTextureDevice* device, const char* path);   // through 00690260
    // 0068FA10 (texture_fill.cpp). Fills `surface`'s levels with the .mmp `data` from level `mip`
    // on: copied (00690970) when the formats match, 8888 converted to 16 bits (00690B30), DXT
    // decoded to 16 bits (00690D30). Returns the bytes written (MmpHeader::FilledBytes).
    int FillSurface(CDXSurface* surface, const void* data, int mip);
    int CallFillSurface(CDXSurface* surface, const void* data, int mip);   // through 0068FA10
    // 00690970, 00690B30, 00690D30 (thiscall, the texture unused by the first and last): each locks
    // a level, writes it, unlocks it and moves to the attached level (not released, as in the
    // original) until the file's last level or the chain's end. Bytes written, or the DirectX
    // report's result when a lock or unlock fails. `kind` is the converter's decode mode.
    int CopyToSurface(CDXSurface* surface, const MmpHeader* header, int mip);
    int ConvertToSurface(CDXSurface* surface, const MmpHeader* header, int mip);
    int UnpackToSurface(CDXSurface* surface, const MmpHeader* header, int mip, int kind);
    // 0068F6C0. A surface with no pixels yet, as `header` describes it (texture_blank.cpp): square,
    // header->width wide (the height is not read), header->mipCount levels, the texture's format_,
    // a managed texture (system memory with flag 0x400). Returns 1.
    int CreateBlank(CTextureDevice* device, const MmpHeader* header);
    // CreateBlank's body; with `memoryOnly` the holder gets no DirectDraw surface in the switch-over,
    // only the new renderer's memory (NewMemoryTextureHolder, dx_surface.h).
    int MakeBlank(CTextureDevice* device, const MmpHeader* header, bool memoryOnly);
    // 00690FC0 (texture_shadow.cpp). Locks the surface for writing (waiting) and enters the device
    // lock, which stays held until UnlockPixels: `desc` (a DDSURFACEDESC2 as dwords) receives the
    // pitch, the pixels and the pixel format. A holder with only the new renderer's memory is
    // described from it (DirectPixels). 1; 0 without a holder (no lock); a failed Lock: the DirectX
    // report's result (the lock still held).
    int LockPixels(std::uint32_t* desc);
    // 00691030 (texture_shadow.cpp). Leaves the device lock and unlocks the surface (the failure
    // reported); nothing without a holder.
    void UnlockPixels();
    // 0051CE60's copy of the shadow's render surface into its texture (texture_shadow.cpp): the
    // device's Load of `source`'s surface into this one, the result in the device's
    // lastLoadResult_; when both have only the new renderer's memory, its rows copied.
    void LoadPixelsFrom(CTextureDevice* device, const CTexture* source);
    // 0068FBF0 (texture_bmp.cpp). Loads a 24-bit .bmp (`path`) and, when `alphaPath` is an 8-bit
    // .bmp, its alpha into a texture of the .bmp's size in the texture's format (format_). 1 (also
    // when already loaded); 0 when the .bmp cannot be opened or the surface locked or unlocked and
    // the report says stop.
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
    int uploadResult_;          // +0x28 result of 0068FA10; 0 after CreateBlank
    AlignedBuffer extra_;       // +0x2C the .mmp file's extra block
    PixelFormat format_;        // +0x3C
    CDXSurface* surface_;       // +0x5C the texture surface (counted)
    std::uint8_t* block_;       // +0x60 an aligned block (raw pointer before the data), freed when a
                                // surface is made (0068F6C0, 0068F860) and by 00690FA0
    std::uint32_t unknown_64;   // +0x64 cleared by the constructor, slot 2 and slot 3
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
    // 3: 00691630. As the destructor up to CResourceManager::Destroy (without SetDevice).
    void Destroy() override;
    // 23: 00691AA0. CBaseManager::CreateObject, then flags_ by type: 0x3E 0x80, 0x3F 0x100, 0x40
    // 0x200, any other 0x40 (no null check).
    CEngineObject* CreateObject(int type, int id) override;
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
    // kind keeps a zero format), its surface square at `width` (CreateBlank). In the switch-over
    // with "cpuwindows" the surface is only the new renderer's memory. Null when CreateBlank fails;
    // FatalError's result when the device has no such format.
    CTexture* NewTexture(int width, int height, int kind);
    // 00692010 (texture_shadow.cpp). A new 64x64 4:4:4:4 texture for an object's shadow (kind 0x25,
    // flag 0x200); in the switch-over with "cpuwindows" only the new renderer's memory. Null when
    // its surface cannot be made; FatalError's result when the device has no 4:4:4:4 format.
    CTexture* NewShadowTexture();
    // 00692140 (texture_shadow.cpp). The texture the shadows are rendered into (shadowSurface_, made
    // on first use as NewShadowTexture's, but flag 0x400: in system memory).
    CTexture* ShadowRenderSurface();
    // 24: 00691B00 (texture_cache.cpp). A texture without references leaves the manager: a
    // registered one with a flag of 0x38 goes into the cache (cache_, cachedBytes_; its surface's
    // priority 0), any other is destroyed and kept as a spare. Returns 0.
    int ReleaseObject(CEngineObject* object) override;
    // 14: 00691810 (texture.cpp). A new CTexture of `type`: the constructor and the reset inlined
    // (no name, no surface, an empty extra block, a pixel format of size 0x20 and nothing else).
    // Null when the allocation fails.
    CEngineObject* NewObject(int type) override;
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
    std::uint16_t unknown_EA;
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
