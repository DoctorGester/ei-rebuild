// Texture loading creates Direct3D surfaces, so it cannot run twice. These wrappers only count calls
// and check that a texture reported as loaded has its pixel format filled in.
#include "verify.h"

#include "../../src/game/texture.h"

namespace {
void* s_unused;

bool FormatSet(const CTexture* t) {
    for (std::uint32_t w : t->format_.words)
        if (w) return true;
    return false;
}
} // namespace

static int __fastcall LoadTextureVerify(CTextureManager* self, void*, CTexture* texture) {
    bool wasLoaded = texture && (texture->flags_ & kTextureLoaded);
    int result = self->LoadTexture(texture);
    bool ok = !texture || wasLoaded || result == 0 || FormatSet(texture);
    ei::verify::Check("CTextureManager::LoadTexture", true, ok, "\"%s\" result %d", texture ? texture->GetName() : "(null)", result);
    return result;
}
EI_VERIFY(0x006922A0, LoadTextureVerify, 0, s_unused);

static int __fastcall LoadTextureFromVerify(CTextureManager* self, void*, CTexture* texture, const char* dir) {
    int result = self->LoadTextureFrom(texture, dir);
    ei::verify::Check("CTextureManager::LoadTextureFrom", true, true, "");
    return result;
}
EI_VERIFY(0x006926F0, LoadTextureFromVerify, 0, s_unused);
