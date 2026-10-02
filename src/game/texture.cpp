#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

namespace {
// Game-wide paths set at startup. Names provisional.
const mfc::CString& GameRoot() { return *reinterpret_cast<const mfc::CString*>(0x0079BC90); }
const char* DataPath() { return *reinterpret_cast<const char* const*>(0x007AF460); }

// Manager vtable slots used here (CResourceManager::GetFile / CloseFile, possibly overridden).
void* ManagerGetFile(CTextureManager* m, const char* name) {
    return ei::VCall<void*>(m, 19, name, static_cast<std::uint32_t*>(nullptr));
}
void ManagerCloseFile(CTextureManager* m, void* data) { ei::VCall<int>(m, 20, data); }

// Format id for a .bmp texture, by texture kind.
int BmpFormatFor(int type) {
    switch (type) {
    case 1: case 2: case 3: case 4: case 8: return 0x5551;   // 1-bit alpha
    case 6: case 13: return 0x4444;                         // 4-bit alpha
    default: return 0x5650;                                 // no alpha
    }
}
} // namespace

__declspec(naked) int CTextureDevice::ParseMmpHeader(const void*, PixelFormat*) EI_ORIGINAL(0x004E7C40)
__declspec(naked) int CTextureDevice::FindFormat(int, PixelFormat*) EI_ORIGINAL(0x004E7DA0)
__declspec(naked) int CTexture::CreateFromMmp(CTextureDevice*, const void*, unsigned) EI_ORIGINAL(0x00690430)
__declspec(naked) int CTexture::LoadBmp(CTextureDevice*, const char*) EI_ORIGINAL(0x00690260)

int CTextureManager::LoadTexture(CTexture* texture) {
    if (!texture) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't load texture to NULL pointer.\n");
        return 0;
    }
    if (texture->flags_ & kTextureLoaded) {
        game::DebugLog("Load already loaded texture \"%s\".\n", texture->GetName());
        return 1;
    }

    mfc::CFileFind finder;
    mfc::CString path = GameRoot() + "textures\\" + texture->GetName() + ".bmp";
    PixelFormat format;
    int result;

    if (finder.FindFile(path, 0)) {
        // A loose .bmp overrides the archived texture.
        if (!device_->FindFormat(BmpFormatFor(texture->type_), &format))
            return game::FatalError(
                "CTextureManager::LoadTexture.\n Can't find desired texture format %d for texture \"%s\".\n", 0,
                texture->GetName());
        texture->format_ = format;
        return texture->LoadBmp(device_, path);
    }

    path = mfc::CString(texture->GetName()) + ".mmp";
    if (void* data = ManagerGetFile(this, path)) {
        auto* header = static_cast<const MmpHeader*>(data);
        if (!device_->ParseMmpHeader(data, &format) &&
            !(header->format == 0x8888 && device_->FindFormat(0x4444, &format)))
            return game::FatalError("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
                                    texture->GetName());
        texture->format_ = format;
        unsigned mip = mipLevel_;
        if (mip >= header->mipCount) mip = header->mipCount - 1;
        result = texture->CreateFromMmp(device_, data, mip);
        ManagerCloseFile(this, data);
        return result;
    }

    // Not in any archive: look in the data directory.
    mfc::CFile file;
    path.Format("%stextures\\%s.mmp", DataPath(), texture->GetName());
    if (!file.Open(path, mfc::CFile::modeRead, nullptr)) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't find texture \"%s\"\n", texture->GetName());
        return 1;
    }
    void* buffer = gamecrt::operator_new(file.GetLength());
    file.Read(buffer, file.GetLength());
    file.Close();
    if (!device_->ParseMmpHeader(buffer, &format))   // the original leaks `buffer` here
        return game::FatalError("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
                                texture->GetName());
    texture->format_ = format;
    texture->CreateFromMmp(device_, buffer, mipLevel_);
    gamecrt::operator_delete(buffer);
    return 1;
}

int CTextureManager::LoadTextureFrom(CTexture* texture, const char* directory) {
    if (!texture) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't load texture to NULL pointer.\n");
        return 0;
    }
    if (texture->flags_ & kTextureLoaded) {
        game::DebugLog("Load already loaded texture \"%s\".\n", texture->GetName());
        return 1;
    }

    mfc::CString path;
    mfc::CFile file;
    path.Format("%s%s.mmp", directory, texture->GetName());
    if (!file.Open(path, mfc::CFile::modeRead, nullptr)) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't find texture \"%s\" in directory \"%s\"\n",
                       texture->GetName(), directory);
        return 1;
    }
    void* buffer = gamecrt::operator_new(file.GetLength());
    file.Read(buffer, file.GetLength());
    file.Close();
    PixelFormat format;
    if (!device_->ParseMmpHeader(buffer, &format))   // the original leaks `buffer` here
        return game::FatalError("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
                                texture->GetName());
    texture->format_ = format;
    texture->CreateFromMmp(device_, buffer, mipLevel_);
    gamecrt::operator_delete(buffer);
    return 1;
}

EI_REPLACE_METHOD(0x006922A0, CTextureManager::LoadTexture);
EI_REPLACE_METHOD(0x006926F0, CTextureManager::LoadTextureFrom);
