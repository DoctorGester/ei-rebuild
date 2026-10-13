// Checks for texture_manager_lifetime.cpp (A-H23). The texture manager is built with the client
// world (every scenario), Destroy runs when the client world is torn down, CreateObject for every
// texture made (every zone load). They allocate, release and register, so they run ours only and
// check the state left; the name / id / folder getters run against the original (MOV EAX,imm32).
#include "verify.h"

#include "../../src/game/texture.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t v;
    std::memcpy(&v, object, 4);
    return v;
}

bool Emptied(const CTextureManager* m) {
    return m->cache_.count == 0 && m->cache_.head == nullptr && m->cachedBytes_ == 0 && m->device_ == nullptr &&
           m->shadowRenderSurface_ == nullptr;
}

CTextureManager* __fastcall ConstructVerify(CTextureManager* self) {
    CTextureManager* result = self->Construct();
    const bool ok = result == self && VtableOf(self) == CTextureManager::kVtableAddress && Emptied(self) &&
                    self->cache_.blockSize == 10 && self->mipLevel_ == 0 && self->limitTo512_ == 0;
    ei::verify::Check("CTextureManager::Construct", true, ok, "manager %p", self);
    return result;
}

// Whether `text` ends with `tail`.
bool EndsWith(const stlp::string& text, const char* tail) {
    const std::size_t n = std::strlen(tail);
    return text.size() >= n && std::memcmp(text.data() + text.size() - n, tail, n) == 0;
}

// 00691500 (A-H26; slot 2, the vanilla launch only: Rebirth patches it). The client world's
// managers are created at every start (CBaseManager::Init). Ours only (it opens archives): a
// success leaves the three texture archives in the list, in the original's order (an archive
// already there is not added again).
int __fastcall CreateVerify(CTextureManager* self) {
    const int result = self->CTextureManager::Create();
    const std::size_t count = self->archives_.size();
    bool ok = result == 0 || result == 1;
    if (result == 1) {
        static const char* const kNames[] = {"textures.res", "redress.res", "menus.res"};
        std::size_t found = 0;
        for (std::size_t i = 0; i < count && found < 3; ++i)
            if (EndsWith(self->archives_[i].name, kNames[found])) ++found;
        ok = found == 3;
    }
    ei::verify::Check("CTextureManager::Create", true, ok, "manager %p result %d, %u archives", self, result,
                      static_cast<unsigned>(count));
    return result;
}

void __fastcall DestroyVerify(CTextureManager* self) {
    self->CTextureManager::Destroy();
    ei::verify::Check("CTextureManager::Destroy", true, Emptied(self), "manager %p", self);
}

void __fastcall DestructVerify(CTextureManager* self) {
    self->Destruct();
    ei::verify::Check("CTextureManager::Destruct", true, Emptied(self) && self->limitTo512_ == 1, "manager %p", self);
}

CTextureManager* __fastcall DeleteVerify(CTextureManager* self, void*, unsigned flags) {
    CTextureManager* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CTextureManager::DeletingDestruct", true, result == self && Emptied(self), "manager %p", self);
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

CEngineObject* __fastcall CreateObjectVerify(CTextureManager* self, void*, int type, int id) {
    CEngineObject* object = self->CTextureManager::CreateObject(type, id);
    const std::uint32_t flag = type == 0x3E ? 0x80u : type == 0x3F ? 0x100u : type == 0x40 ? 0x200u : 0x40u;
    ei::verify::Check("CTextureManager::CreateObject", true, (object->flags_ & flag) != 0 && object->owner_ == self,
                      "manager %p type 0x%X -> %p", self, type, object);
    return object;
}

const char*(__fastcall* s_name)(CTextureManager*);
const char* __fastcall NameVerify(CTextureManager* self) {
    const char* expected = s_name(self);
    ei::verify::Check("CTextureManager::GetClassName", expected, TextureManagerClassName(self), "%p", self);
    return expected;
}
int(__fastcall* s_id)(CTextureManager*);
int __fastcall IdVerify(CTextureManager* self) {
    int expected = s_id(self);
    ei::verify::Check("CTextureManager::GetClassId", expected, TextureManagerClassId(self), "%p", self);
    return expected;
}
const char*(__fastcall* s_folder)(CTextureManager*);
const char* __fastcall FolderVerify(CTextureManager* self) {
    const char* expected = s_folder(self);
    ei::verify::Check("CTextureManager::Folder", expected, TextureManagerFolder(self), "%p", self);
    return expected;
}

} // namespace

EI_VERIFY(0x00691140, ConstructVerify, 0, s_unused);
EI_VERIFY(0x00691500, CreateVerify, 0, s_unused);
EI_VERIFY(0x00691630, DestroyVerify, 0, s_unused);
EI_VERIFY(0x00691250, DestructVerify, 0, s_unused);
EI_VERIFY(0x00691230, DeleteVerify, 0, s_unused);
EI_VERIFY(0x00691AA0, CreateObjectVerify, 0, s_unused);
EI_VERIFY(0x00691210, NameVerify, 5, s_name);
EI_VERIFY(0x00691220, IdVerify, 5, s_id);
EI_VERIFY(0x00691200, FolderVerify, 5, s_folder);
