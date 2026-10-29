// The read-backs' staging copies (render_internal.h, StagingCopy): the frame, the canvases, the composed levels and
// the harness build's checks.
#include "render_internal.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

namespace render::detail {

bool StagingCopy::Map(ID3D11DeviceContext* context, ID3D11Texture2D* source) {
    if (!source) return false;
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    return Map(context, source, 0, 0, desc.Width, desc.Height);
}

bool StagingCopy::Map(ID3D11DeviceContext* context, ID3D11Texture2D* source, unsigned left, unsigned top, unsigned width,
                      unsigned height) {
    Unmap();
    if (!context || !source || width == 0 || height == 0) return false;
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (!texture_ || width_ < width || height_ < height || format_ != static_cast<unsigned>(desc.Format)) {
        Release();
        ID3D11Device* device = nullptr;
        source->GetDevice(&device);
        if (!device) return false;
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture_);
        device->Release();
        if (FAILED(hr)) {
            texture_ = nullptr;
            return false;
        }
        width_ = width;
        height_ = height;
        format_ = static_cast<unsigned>(desc.Format);
    }
    D3D11_BOX box{left, top, 0, left + width, top + height, 1};
    context->CopySubresourceRegion(texture_, 0, 0, 0, 0, source, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(texture_, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    context_ = context;
    data_ = static_cast<const std::uint8_t*>(mapped.pData);
    pitch_ = mapped.RowPitch;
    mappedWidth_ = width;
    mappedHeight_ = height;
    return true;
}

void StagingCopy::Unmap() {
    if (context_) context_->Unmap(texture_, 0);
    context_ = nullptr;
    data_ = nullptr;
    pitch_ = 0;
    mappedWidth_ = mappedHeight_ = 0;
}

void StagingCopy::Release() {
    Unmap();
    if (texture_) texture_->Release();
    texture_ = nullptr;
    width_ = height_ = format_ = 0;
}

} // namespace render::detail
