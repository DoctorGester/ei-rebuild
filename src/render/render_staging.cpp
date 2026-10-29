// The read-backs' staging copies (render_internal.h, StagingCopy): the frame, the canvases, the composed levels and
// the harness build's checks.
#include "render_internal.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

namespace render::detail {

bool StagingCopy::Map(ID3D11DeviceContext* deviceContext, ID3D11Texture2D* source) {
    if (!source) return false;
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    return Map(deviceContext, source, 0, 0, desc.Width, desc.Height);
}

bool StagingCopy::Map(ID3D11DeviceContext* deviceContext, ID3D11Texture2D* source, unsigned left, unsigned top, unsigned copyWidth,
                      unsigned copyHeight) {
    Unmap();
    if (!deviceContext || !source || copyWidth == 0 || copyHeight == 0) return false;
    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    if (!texture || width < copyWidth || height < copyHeight || format != static_cast<unsigned>(desc.Format)) {
        Release();
        ID3D11Device* device = nullptr;
        source->GetDevice(&device);
        if (!device) return false;
        desc.Width = copyWidth;
        desc.Height = copyHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &texture);
        device->Release();
        if (FAILED(hr)) {
            texture = nullptr;
            return false;
        }
        width = copyWidth;
        height = copyHeight;
        format = static_cast<unsigned>(desc.Format);
    }
    D3D11_BOX box{left, top, 0, left + copyWidth, top + copyHeight, 1};
    deviceContext->CopySubresourceRegion(texture, 0, 0, 0, 0, source, 0, &box);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(deviceContext->Map(texture, 0, D3D11_MAP_READ, 0, &mapped))) return false;
    context = deviceContext;
    data = static_cast<const std::uint8_t*>(mapped.pData);
    pitch = mapped.RowPitch;
    mappedWidth = copyWidth;
    mappedHeight = copyHeight;
    return true;
}

void StagingCopy::Unmap() {
    if (context) context->Unmap(texture, 0);
    context = nullptr;
    data = nullptr;
    pitch = 0;
    mappedWidth = mappedHeight = 0;
}

void StagingCopy::Release() {
    Unmap();
    if (texture) texture->Release();
    texture = nullptr;
    width = height = format = 0;
}

} // namespace render::detail
