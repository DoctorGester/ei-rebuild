#pragma once

// The native graphics operations sokol leaves to its host. Shared rendering owns every pass, pipeline,
// buffer and image; a selected backend owns the device, window drawable, transfers and diagnostic queries.
#include "sokol_gfx.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform::graphics {

struct Region {
    int left, top, right, bottom;
};

bool CreateEnvironment(sg_environment* environment, bool removable, void (*flush)());
void PrepareDevice(bool removable);
void DropDevice();
const char* DeviceError();
int SampleCount();
bool PollDeviceLoss(long* result, long* reason);
bool Removable();
bool RequestRemoval();
void RemoveIfAsked();
int Removals();
constexpr long kDeviceRemoved = static_cast<std::int32_t>(0x887A0005u);

// The window's client area in pixels: the drawable AcquireDrawable gives has the same size.
bool WindowSize(void* window, int* width, int* height);
bool AcquireDrawable(void* window, bool vsync, bool allowTearing, sg_swapchain* drawable);
bool PresentDrawable(bool vsync);
void DropWindow();
bool CanPresentOnWorker();
bool ReadDrawable(unsigned x, unsigned y, unsigned width, unsigned height, std::vector<std::uint8_t>* bytes,
                  std::size_t* pitch);

void CopyImage(sg_image from, sg_image to, Region region, int x, int y);
void WriteImage(sg_image image, Region region, const void* bytes, int pitch);
void GenerateMipmaps(sg_image image);
bool ReadImage(sg_image image, unsigned x, unsigned y, unsigned width, unsigned height,
               std::vector<std::uint8_t>* bytes, std::size_t* pitch);
void ReleaseReadbacks();
// An image whose mip levels GenerateMipmaps can fill.
sg_image MakeMipmappedImage(const sg_image_desc& desc);

std::uint32_t SaveFloatingPointState();
void RestoreFloatingPointState(std::uint32_t state);

// Nonblocking GPU timestamps. Begin lazily creates resources; unsupported backends return false.
// Read consumes a completed set and returns relative milliseconds (empty for disjoint samples).
class Timestamps {
public:
    Timestamps() = default;
    Timestamps(const Timestamps&) = delete;
    Timestamps& operator=(const Timestamps&) = delete;
    bool Begin();
    void Stamp();
    void End();
    bool Read(std::vector<double>* milliseconds);
    void Release();

private:
    void* state = nullptr;
};

}   // namespace platform::graphics
