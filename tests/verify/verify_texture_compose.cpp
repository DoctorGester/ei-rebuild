// Checks for the texture manager's composed textures (texture_compose.cpp).
#include "verify.h"

#include "../../src/game/texture.h"
#include "../../src/game/unit_client_models.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

int __fastcall ComposeTexture(void* manager, void*, mfc::CString* out, const SkinRequest* request,
                              const mfc::CString* name, const mfc::CString* fallback, unsigned mipCount,
                              std::uint32_t fill, std::uint32_t format);

namespace {

// Whether <game folder>textures\<name>.mmp holds `request`'s layer bytes after its header.
bool CacheHolds(const mfc::CString& name, const SkinRequest* request) {
    const auto& folder = *reinterpret_cast<const mfc::CString*>(0x007AF460);
    mfc::CString path = folder + "textures\\" + name + ".mmp";
    mfc::CFile file;
    if (!file.Open(path, mfc::CFile::modeRead, nullptr)) return false;
    MmpHeader header;
    bool ok = file.Read(&header, sizeof(header)) == sizeof(header) &&
              static_cast<int>(header.extraSize) == request->layerBytes && header.magic == 0x504D4D;
    for (int done = 0; ok && done < request->layerBytes;) {
        std::uint8_t chunk[256];
        unsigned want = static_cast<unsigned>(request->layerBytes - done);
        if (want > sizeof(chunk)) want = sizeof(chunk);
        ok = file.Read(chunk, want) == want && std::memcmp(chunk, request->layers + done, want) == 0;
        done += static_cast<int>(want);
    }
    file.Close();
    return ok;
}

} // namespace

// 00693180 reads and writes files: ours only. A result of 1 leaves a cache file for the returned
// name that holds the request's layer bytes; 0 only without layer textures or when drawing fails.
static void* s_compose;
static int __fastcall ComposeTextureVerify(void* manager, void*, mfc::CString* out, const SkinRequest* request,
                                           const mfc::CString* name, const mfc::CString* fallback, unsigned mipCount,
                                           std::uint32_t fill, std::uint32_t format) {
    int result = ComposeTexture(manager, nullptr, out, request, name, fallback, mipCount, fill, format);
    bool ok = result == 0 || result == 1;
    if (result == 1) ok = CacheHolds(*out, request);
    ei::verify::Check("ComposeTexture", true, ok, "%s / %s -> %s result %d layers %d textures %d", static_cast<const char*>(*name),
                      static_cast<const char*>(*fallback), static_cast<const char*>(*out), result, request->layerBytes,
                      request->textures.GetSize());
    return result;
}
EI_VERIFY(0x00693180, ComposeTextureVerify, 0, s_compose);

// 00693010: PUSH -1 (2), PUSH imm32 (5). The original draws into a copy of the image (same starting
// pixels), ours into the image; results and pixels compared. GetFile / CloseFile only read the
// archives. Reached by ComposeTexture (party skins: every scenario with a new game).
static int(__fastcall* s_drawLayers)(CTextureManager*, void*, std::uint32_t*, const mfc::CStringArray*, int);
static int __fastcall DrawLayersVerify(CTextureManager* self, void*, std::uint32_t* image, const mfc::CStringArray* names,
                                       int width) {
    const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(width) * 4;
    auto* copy = static_cast<std::uint32_t*>(gamecrt::operator_new(bytes));
    std::memcpy(copy, image, bytes);
    const int expected = s_drawLayers(self, nullptr, copy, names, width);
    const int actual = self->DrawLayers(image, names, width);
    const bool ok = expected == actual && std::memcmp(copy, image, bytes) == 0;
    ei::verify::Check("CTextureManager::DrawLayers", true, ok, "%d layers, width %d: %d / %d", names->GetSize(), width,
                      expected, actual);
    gamecrt::operator_delete(copy);
    return expected;
}
EI_VERIFY(0x00693010, DrawLayersVerify, 7, s_drawLayers);
