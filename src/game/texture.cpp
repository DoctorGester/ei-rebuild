#include "texture.h"

#include "app.h"
#include "device.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "game_data.h"
#include "game_singletons.h"
#include "options.h"

#include <ddraw.h>

#include <cstring>
#include <new>

namespace {
// Game-wide paths set at startup. Names provisional.
const mfc::CString& GameRoot() { return ei::GameData<const mfc::CString>(0x0079BC90); }

// Format id for a .bmp texture, by texture kind.
int BmpFormatFor(int type) {
    switch (type) {
    case 1: case 2: case 3: case 4: case 8: return 0x5551;   // 1-bit alpha
    case 6: case 13: return 0x4444;                         // 4-bit alpha
    default: return 0x5650;                                 // no alpha
    }
}
} // namespace

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
    if (void* data = GetFile(path, nullptr)) {
        auto* header = static_cast<const MmpHeader*>(data);
        if (!device_->ParseMmpHeader(data, &format) &&
            !(header->format == 0x8888 && device_->FindFormat(0x4444, &format)))
            return game::FatalError("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
                                    texture->GetName());
        texture->format_ = format;
        unsigned mip = mipLevel_;
        if (mip >= header->mipCount) mip = header->mipCount - 1;
        result = texture->CreateFromMmp(device_, data, mip);
        CloseFile(data);
        return result;
    }

    // Not in any archive: look in the data directory.
    mfc::CFile file;
    path.Format("%stextures\\%s.mmp", DataFolderText(), texture->GetName());
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

namespace {
// The texture quality setting (0 best .. 3).
int TextureQuality() { return options::TheOptions()->textureQuality_; }
const char* CantGetDeviceDescriptionText() { return "Can't get device description.\n"; }
} // namespace

void CTextureManager::SetDevice(CTextureDevice* device) {
    CTextureDevice* old = device_;
    device_ = device;
    if (device) device->AddRef();
    if (old) old->Release();
    if (!device_) {
        limitTo512_ = 1;
        mipLevel_ = 2;
        return;
    }
    switch (TextureQuality()) {
    case 0: mipLevel_ = 0; break;
    case 1: mipLevel_ = 1; break;
    case 2: mipLevel_ = 2; break;
    default: mipLevel_ = 3; break;
    }
    D3DDEVICEDESC7 desc;
    long result = device::GetCaps(device::D3D(device_), &desc);
    device_->lastLoadResult_ = result;
    if (result != 0 && app::FatalErrorCode(result, CantGetDeviceDescriptionText()) == 0)
        limitTo512_ = 1;
    else
        limitTo512_ = desc.dwMaxTextureWidth > 0x100 && desc.dwMaxTextureHeight > 0x100 ? 0 : 1;
    DDSCAPS2 caps;
    std::memset(&caps, 0, sizeof(caps));
    caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_LOCALVIDMEM;   // 0x10001000
    DWORD total = 0, available = 0;
    device::GetAvailableVidMem(device::DDraw(device_), &caps, &total, &available);
    videoMemoryTenth_ = total / 10;
}
EI_REWRITTEN(0x006913A0, CTextureManager::SetDevice);

CEngineObject* CTextureManager::NewObjectImpl(int type) {
    void* memory = gamecrt::operator_new(sizeof(CTexture));
    if (!memory) return nullptr;
    auto* texture = new (memory) CTexture();
    texture->type_ = type;
    return texture;
}
EI_REWRITTEN(0x00691810, CTextureManager::NewObjectImpl);
// The extra block's out-of-line SetSize.
EI_REWRITTEN(0x00694300, lib::AlignedArray<std::uint8_t>::SetSize);

EI_REWRITTEN(0x006922A0, CTextureManager::LoadTexture);
EI_REWRITTEN(0x006926F0, CTextureManager::LoadTextureFrom);
