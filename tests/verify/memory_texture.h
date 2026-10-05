#pragma once

// Verify helpers for texture holders with only the new renderer's memory (the switch-over with
// "cpuwindows", ticket R8: the textures of the texture manager's 00691D80). The originals that write
// such a texture lock its DirectDraw surface; to run one next to ours, the holder is lent a temporary
// DirectDraw texture (system memory, the memory's size and pixel format, filled from the memory) for
// the original's run and is memory-only again afterwards. verify_texture_blank.cpp.

struct CDXSurface;
class CTexture;
class CTextureManager;

namespace memory_texture {

// The game's count of surface holders (007AEE94).
int HolderCount();
// What is wrong with a texture the manager just made for the CPU (00691D80, the shadow textures
// 00692010 / 00692140), or null: `width` x `height`, the device's format `formatId`, a holder of
// `width` square in it (the holder count one above `holdersBefore`), the byte size 0, no block; in
// the switch-over with "cpuwindows" only the new renderer's memory of that size and those channel
// masks, otherwise a DirectDraw texture and no memory.
const char* BlankTextureProblem(CTextureManager* manager, const CTexture* texture, int width, int height, int formatId,
                                int holdersBefore);

// The texture's holder (CTexture +0x5C), or null.
CDXSurface* HolderOf(const void* texture);
// The holder has the new renderer's memory and no DirectDraw surface.
bool MemoryOnly(const CDXSurface* holder);
// A memory-only holder gets a temporary DirectDraw surface holding its memory's pixels. False when
// the holder is not memory-only or DirectDraw refused (logged).
bool Lend(CDXSurface* holder);
// The lent surface is released and the holder is memory-only again; with `keep` the surface's
// pixels are copied into the memory first.
void TakeBack(CDXSurface* holder, bool keep);

// While one is open, every texture 00691D80 makes (its verify wrapper calls Made) is lent a surface;
// when it closes they are taken back with their pixels. For an original that makes a texture and
// writes it itself (005EC020, 00655D40).
class LendNewTextures {
public:
    LendNewTextures();
    ~LendNewTextures();
    LendNewTextures(const LendNewTextures&) = delete;
    LendNewTextures& operator=(const LendNewTextures&) = delete;
    static void Made(CDXSurface* holder);
};

} // namespace memory_texture
