// The image's thrown exceptions read for the crash report (image_side.h; harness_crash.cpp).
#include "image_side.h"

#include "lib/mfc_exception.h"

#include <cstring>

namespace ei::verify::image {

void ArchiveExceptionFileName(const void* exception, char* out, std::size_t size) {
    const auto* archive = static_cast<const mfc::impl::CArchiveException*>(exception);
    strncpy_s(out, size, archive->fileName.c_str(), _TRUNCATE);
}

} // namespace ei::verify::image
