#include "platform/graphics.h"
#include "render/render_internal.h"
namespace render::detail {
bool StagingCopy::Map(sg_image image) {
    return Map(image, 0, 0, sg_query_image_width(image), sg_query_image_height(image));
}

bool StagingCopy::Map(sg_image image, unsigned x, unsigned y, unsigned width, unsigned height) {
    Unmap();
    if (!platform::graphics::ReadImage(image, x, y, width, height, &storage, &pitch)) return false;
    data = storage.data();
    mappedWidth = width;
    mappedHeight = height;
    return true;
}

void StagingCopy::Unmap() {
    data = nullptr;
    pitch = 0;
    mappedWidth = mappedHeight = 0;
}

void StagingCopy::Release() {
    Unmap();
    platform::graphics::ReleaseReadbacks();
    storage.clear();
}

bool StagingCopy::MapDrawable(unsigned x, unsigned y, unsigned width, unsigned height) {
    Unmap();
    if (!platform::graphics::ReadDrawable(x, y, width, height, &storage, &pitch)) return false;
    data = storage.data();
    mappedWidth = width;
    mappedHeight = height;
    return true;
}
}
