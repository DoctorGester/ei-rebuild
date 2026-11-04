// The save pictures (src/game/save_picture.h) and their QOI codec (src/lib/qoi.h): round trips, the shot.sav layout
// the original reads, files without or with a broken trailer.
#include "../../src/game/save_picture.h"
#include "../../src/lib/qoi.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
    ++g_checks;
    if (ok) return;
    std::printf("save_picture: %s\n", what);
    ++g_failures;
}

// A picture with flat areas (runs longer than QOI's 62), gradients (its small differences) and noise (its literals).
save_picture::Picture TestPicture(int width, int height, bool alpha) {
    save_picture::Picture p;
    p.width = width;
    p.height = height;
    p.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    std::uint32_t seed = 12345;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            std::uint8_t* o = &p.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            seed = seed * 1103515245u + 12345u;
            if (y < height / 3) {
                o[0] = 40, o[1] = 90, o[2] = 200;
            } else if (y < 2 * height / 3) {
                o[0] = static_cast<std::uint8_t>(x * 3), o[1] = static_cast<std::uint8_t>(y * 2), o[2] = static_cast<std::uint8_t>(x + y);
            } else {
                o[0] = static_cast<std::uint8_t>(seed >> 24), o[1] = static_cast<std::uint8_t>(seed >> 16), o[2] = static_cast<std::uint8_t>(seed >> 8);
            }
            o[3] = alpha ? static_cast<std::uint8_t>(x * 7) : 255;
        }
    return p;
}

void CheckQoi() {
    for (int channels : {3, 4}) {
        const save_picture::Picture p = TestPicture(211, 97, channels == 4);
        const std::vector<std::uint8_t> file = lib::EncodeQoi(p.rgba.data(), p.width, p.height, channels);
        int w = 0, h = 0;
        std::vector<std::uint8_t> back;
        Check(lib::DecodeQoi(file.data(), file.size(), 4096, &w, &h, &back) && w == p.width && h == p.height && back == p.rgba,
              "QOI round trip");
        if (channels == 3) Check(file.size() < p.rgba.size() * 3 / 4, "QOI smaller than the pixels");
        Check(!lib::DecodeQoi(file.data(), file.size() / 2, 4096, &w, &h, &back), "a cut QOI file refused");
        Check(!lib::DecodeQoi(file.data(), file.size(), 100, &w, &h, &back), "a QOI file over the size limit refused");
    }
    // One opaque black pixel: the header, a run of one (the start colour), the end.
    const std::uint8_t black[4] = {0, 0, 0, 255};
    const std::vector<std::uint8_t> one = lib::EncodeQoi(black, 1, 1, 3);
    const std::vector<std::uint8_t> want = {'q', 'o', 'i', 'f', 0, 0, 0, 1, 0, 0, 0, 1, 3, 0, 0xC0, 0, 0, 0, 0, 0, 0, 0, 1};
    Check(one == want, "QOI of one black pixel");
}

void CheckShot() {
    // Shrink: 4x2 to 2x1 averages each 2x2 block.
    save_picture::Picture small;
    small.width = 4;
    small.height = 2;
    small.rgba = {0, 0, 0, 255, 10, 20, 30, 255, 100, 100, 100, 255, 0, 0, 0, 255,
                  20, 40, 60, 255, 30, 60, 90, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    const save_picture::Picture shrunk = save_picture::Shrink(small, 2, 1);
    Check(shrunk.width == 2 && shrunk.height == 1 &&
              shrunk.rgba == std::vector<std::uint8_t>{15, 30, 45, 255, 25, 25, 25, 255},
          "Shrink averages");
    Check(save_picture::Fit(small, 10, 10).rgba == small.rgba, "Fit keeps a smaller picture");

    const save_picture::Picture screen = TestPicture(640, 480, false);
    const save_picture::Picture shown = save_picture::Shrink(screen, 290, 220);
    const std::vector<std::uint8_t> file = save_picture::ShotFile(screen, shown);
    Check(file.size() > save_picture::kOldBytes + 20, "the trailer follows the old picture");
    // The old picture: pixel (x, y) is the screen's under its centre, 5:6:5.
    for (int y : {0, 100, 191})
        for (int x : {0, 77, 255}) {
            const int sx = (2 * x + 1) * 640 / 512, sy = (2 * y + 1) * 480 / 384;
            const std::uint8_t* s = &screen.rgba[(static_cast<std::size_t>(sy) * 640 + sx) * 4];
            const unsigned want = ((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3);
            const std::size_t at = (static_cast<std::size_t>(y) * 256 + x) * 2;
            Check(static_cast<unsigned>(file[at] | (file[at + 1] << 8)) == want, "the old picture's pixels");
        }
    const save_picture::Picture read = save_picture::ReadShot(file.data(), file.size());
    Check(read.width == 290 && read.height == 220 && read.rgba == shown.rgba, "the trailer's picture read back");
    // The original's file (no trailer), a broken trailer, a short file: the old picture.
    const save_picture::Picture old = save_picture::ReadShot(file.data(), save_picture::kOldBytes);
    Check(old.width == 256 && old.height == 192, "a file without a trailer shows the old picture");
    Check(save_picture::OldPicture(file.data(), file.size()).rgba == old.rgba, "the old picture ignores the trailer");
    std::vector<std::uint8_t> broken = file;
    broken[save_picture::kOldBytes] = 'X';
    Check(save_picture::ReadShot(broken.data(), broken.size()).rgba == old.rgba, "another magic shows the old picture");
    broken = file;
    broken.resize(broken.size() - 30);
    Check(save_picture::ReadShot(broken.data(), broken.size()).rgba == old.rgba, "a cut trailer shows the old picture");
    const save_picture::Picture cut = save_picture::ReadShot(file.data(), 256 * 2 * 10);
    Check(cut.height == 192 && cut.rgba[(11 * 256) * 4] == 0 && cut.rgba[(11 * 256) * 4 + 3] == 255,
          "a short file's missing rows are black");
}

} // namespace

int RunSavePictureChecks() {
    CheckQoi();
    CheckShot();
    std::printf("ei_unit_check: save pictures: %d checks, %d failed\n", g_checks, g_failures);
    return g_failures;
}
