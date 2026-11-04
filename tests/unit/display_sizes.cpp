// platform::ChooseSizes and AddSize (src/platform/display_sizes.h): the options' resolution row's list from a display's
// modes, which repeat a size for each refresh rate.
#include "../../src/platform/display_sizes.h"

#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void Expect(const char* what, const std::vector<Size>& got, const std::vector<Size>& want) {
    if (got == want) return;
    ++failures;
    std::printf("ei_unit_check: display sizes: %s gave", what);
    for (const Size s : got) std::printf(" %dx%d", s.w, s.h);
    std::printf(", wanted");
    for (const Size s : want) std::printf(" %dx%d", s.w, s.h);
    std::printf("\n");
}

} // namespace

int RunDisplaySizesChecks() {
    failures = 0;
    // A real monitor's list: each size at several refresh rates, in the order the system gave, 640x480 and 720x480
    // below the smallest offered, and the desktop 1920x1080 the largest.
    const std::vector<Size> modes = {{1920, 1080}, {1920, 1080}, {1280, 1024}, {1280, 720},  {1920, 1080}, {640, 480},
                                     {1280, 720},  {800, 600},   {1024, 768},  {1280, 1024}, {720, 480},   {1600, 900},
                                     {1920, 1200}, {2560, 1440}, {1280, 800},  {1600, 1200}, {800, 600}};
    Expect("ordered, deduped, none above the desktop or under 800x600", platform::ChooseSizes(modes, {1920, 1080}),
           {{800, 600}, {1024, 768}, {1280, 720}, {1280, 800}, {1280, 1024}, {1600, 900}, {1920, 1080}});
    // A size above the desktop in one direction only is left out too.
    Expect("a size wider than the desktop", platform::ChooseSizes({{1280, 1024}, {1600, 900}}, {1280, 720}), {});
    Expect("equal widths by height", platform::ChooseSizes({{1280, 1024}, {1280, 720}, {1280, 800}}, {3840, 2160}),
           {{1280, 720}, {1280, 800}, {1280, 1024}});
    Expect("no modes", platform::ChooseSizes({}, {1920, 1080}), {});

    // The window's own size put in its place, once.
    std::vector<Size> sizes = {{800, 600}, {1280, 720}, {1920, 1080}};
    platform::AddSize(sizes, {1000, 800});
    Expect("a size between", sizes, {{800, 600}, {1000, 800}, {1280, 720}, {1920, 1080}});
    platform::AddSize(sizes, {1000, 800});
    Expect("a size already there", sizes, {{800, 600}, {1000, 800}, {1280, 720}, {1920, 1080}});
    platform::AddSize(sizes, {1280, 600});
    Expect("an equal width by height", sizes, {{800, 600}, {1000, 800}, {1280, 600}, {1280, 720}, {1920, 1080}});
    platform::AddSize(sizes, {3000, 2000});
    platform::AddSize(sizes, {640, 480});
    Expect("both ends", sizes,
           {{640, 480}, {800, 600}, {1000, 800}, {1280, 600}, {1280, 720}, {1920, 1080}, {3000, 2000}});
    return failures;
}
