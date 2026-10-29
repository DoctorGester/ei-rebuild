// The fonts built into the executable (render_text.h, FontForFace): third_party/fonts' Liberation Serif and Liberation
// Mono, written as byte arrays into fonts/ in the build folder at build time (CMakeLists.txt, src/render/embed.cmake).
// Their licence, the SIL Open Font License, is in the build's third_party_notices.txt.
#include "render_text.h"

#include <algorithm>

namespace render::text {

namespace {

#include "fonts/LiberationMono.h"
#include "fonts/LiberationSerif.h"

} // namespace

std::span<const unsigned char> FontForFace(std::string_view face) {
    constexpr std::string_view kCourierNew = "Courier New";
    const bool courier = face.size() == kCourierNew.size() &&
                         std::equal(face.begin(), face.end(), kCourierNew.begin(), [](char a, char b) { return (a | 0x20) == (b | 0x20); });
    return courier ? std::span<const unsigned char>(kLiberationMono) : std::span<const unsigned char>(kLiberationSerif);
}

} // namespace render::text
