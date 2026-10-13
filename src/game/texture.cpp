#include "texture.h"

#include "app.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <ddraw.h>

#include <cstring>
#include <new>

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

__declspec(naked) int CTexture::CallCreateFromMmp(CTextureDevice*, const void*, unsigned) EI_ORIGINAL(0x00690430)

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
        return texture->CallLoadBmp(device_, path);
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
        result = texture->CallCreateFromMmp(device_, data, mip);
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
    texture->CallCreateFromMmp(device_, buffer, mipLevel_);
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
    texture->CallCreateFromMmp(device_, buffer, mipLevel_);
    gamecrt::operator_delete(buffer);
    return 1;
}

namespace {
// The texture quality setting (0 best .. 3).
int TextureQuality() { return *reinterpret_cast<const int*>(0x0079BCDC); }
const char* CantGetDeviceDescriptionText() { return reinterpret_cast<const char*>(0x0078EE20); }

// IDirect3DDevice7::GetCaps (slot 3) into a D3DDEVICEDESC7, whose largest texture size is at +0x84 / +0x88.
constexpr int kDeviceGetCaps = 3;
struct DeviceDescription {
    std::uint8_t unknown_00[0x84];
    std::uint32_t maxTextureWidth;    // +0x84
    std::uint32_t maxTextureHeight;   // +0x88
    std::uint8_t unknown_8C[0x100 - 0x8C];
};
long DeviceGetCaps(void* device, DeviceDescription* desc) {
    using Fn = long(__stdcall*)(void*, DeviceDescription*);
    return reinterpret_cast<Fn>((*static_cast<void***>(device))[kDeviceGetCaps])(device, desc);
}
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
    DeviceDescription desc;
    long result = DeviceGetCaps(device_->d3dDevice_, &desc);
    device_->lastLoadResult_ = result;
    if (result != 0 && app::FatalErrorCode(result, CantGetDeviceDescriptionText()) == 0)
        limitTo512_ = 1;
    else
        limitTo512_ = desc.maxTextureWidth > 0x100 && desc.maxTextureHeight > 0x100 ? 0 : 1;
    DDSCAPS2 caps;
    std::memset(&caps, 0, sizeof(caps));
    caps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_LOCALVIDMEM;   // 0x10001000
    DWORD total = 0, available = 0;
    static_cast<IDirectDraw7*>(device_->ddraw_)->GetAvailableVidMem(&caps, &total, &available);
    videoMemoryTenth_ = total / 10;
}
EI_REPLACE_METHOD(0x006913A0, CTextureManager::SetDevice);

CEngineObject* CTextureManager::NewObject(int type) {
    void* memory = gamecrt::operator_new(sizeof(CTexture));
    if (!memory) return nullptr;
    auto* texture = static_cast<CTexture*>(memory);
    texture->ClearFields();
    auto* name = new (&texture->name_) mfc::CString;
    std::memset(&texture->extra_, 0, sizeof(texture->extra_));
    texture->surface_ = nullptr;
    ei::SetVtable(texture, CTexture::kVtableAddress);
    name->Empty();
    // 004EBE30, the counted surface pointer set to null.
    CDXSurface* old = texture->surface_;
    texture->surface_ = nullptr;
    if (old) old->Release();
    texture->block_ = nullptr;
    texture->height_ = 0;
    texture->width_ = 0;
    texture->uploadResult_ = 0;
    std::memset(&texture->format_, 0, sizeof(texture->format_));
    texture->format_.words[0] = sizeof(PixelFormat);   // DDPIXELFORMAT dwSize
    texture->unknown_64 = 0;
    texture->extra_.SetSize(0, -1);
    texture->type_ = type;
    return texture;
}
CEngineObject* __fastcall TextureNewObjectHook(CTextureManager* self, void*, int type) {
    return self->CTextureManager::NewObject(type);
}
EI_REPLACE(0x00691810, TextureNewObjectHook);
// The extra block's out-of-line SetSize.
EI_REPLACE_METHOD(0x00694300, lib::AlignedArray<std::uint8_t>::SetSize);

EI_REPLACE_METHOD(0x006922A0, CTextureManager::LoadTexture);
EI_REPLACE_METHOD(0x006926F0, CTextureManager::LoadTextureFrom);
