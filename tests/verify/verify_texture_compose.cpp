// Checks for the texture manager's composed textures (texture_compose.cpp).
#include "verify.h"

#include "../../src/game/texture.h"
#include "../../src/game/unit_client_models.h"
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
