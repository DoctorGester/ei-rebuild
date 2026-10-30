// ei_text_check --titles: the titles' rich text (src/render/render_rich_text) against RichEdit 2.0, the
// control the original formatted outro.rtf with (EM_STREAMIN, EM_FORMATRANGE on a memory DC; RichEdit only here).
//
//   ei_text_check --titles [--res <outro.res>] [--width <pixels>] [--height <pixels>] [--dpi-unaware]
//                          [--picture <out.bmp> <offset>...]
//
// Prints both layouts' line bottoms and the height each needs to fit the whole text (what the scroll's end follows:
// UiScrollingSurface::RenderText), at the system DPI the game sees (the process is made DPI aware as the game is; --dpi-unaware: 96).
// --picture writes the surface's page at each scroll offset (twips), RichEdit's on the left and ours on the right,
// white where nothing is drawn (the colour key). The default size is the 1280x720 surface (1248 x 360).
#define NOMINMAX
#include <windows.h>
#include <richedit.h>

#include "render/render_rich_text.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace render::text;
using render::Rect;

namespace {

constexpr int kTwipsPerInch = 1440;
constexpr std::uint32_t kResMagic = 0x019CE23C;
constexpr int kExtraTwipsBelow = 1000;   // RenderText formats this far below the surface

// The file `name` of a .res archive (header magic, count, table offset; 22-byte entries; the names after the table).
std::string ReadResFile(const std::string& path, const std::string& name) {
    std::FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) return {};
    std::vector<unsigned char> data;
    std::fseek(file, 0, SEEK_END);
    data.resize(static_cast<std::size_t>(std::ftell(file)));
    std::fseek(file, 0, SEEK_SET);
    const std::size_t read = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    if (read != data.size() || data.size() < 16) return {};
    auto u32 = [&](std::size_t at) {
        std::uint32_t value = 0;
        if (at + 4 <= data.size()) std::memcpy(&value, &data[at], 4);
        return value;
    };
    if (u32(0) != kResMagic) return {};
    const std::uint32_t count = u32(4), table = u32(8);
    const std::size_t names = table + static_cast<std::size_t>(count) * 22;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = table + static_cast<std::size_t>(i) * 22;
        if (entry + 22 > data.size()) break;
        const std::uint32_t size = u32(entry + 4), offset = u32(entry + 8);
        std::uint16_t length = 0;
        std::memcpy(&length, &data[entry + 16], 2);
        const std::uint32_t nameAt = u32(entry + 18);
        if (names + nameAt + length > data.size() || offset + size > data.size()) continue;
        const std::string entryName(reinterpret_cast<const char*>(&data[names + nameAt]), length);
        if (_stricmp(entryName.c_str(), name.c_str()) == 0) return std::string(reinterpret_cast<const char*>(&data[offset]), size);
    }
    return {};
}

struct Page {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    std::uint32_t* pixels = nullptr;
    int width = 0, height = 0;

    Page(int w, int h) : width(w), height(h) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        old = SelectObject(dc, bitmap);
        pixels = static_cast<std::uint32_t*>(bits);
    }
    ~Page() {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;
};

struct Source {
    const std::string* data;
    std::size_t at;
};

DWORD CALLBACK ReadSource(DWORD_PTR cookie, LPBYTE buffer, LONG wanted, LONG* read) {
    Source* source = reinterpret_cast<Source*>(cookie);
    const std::size_t count = std::min(static_cast<std::size_t>(wanted), source->data->size() - source->at);
    std::memcpy(buffer, source->data->data() + source->at, count);
    source->at += count;
    *read = static_cast<LONG>(count);
    return 0;
}

// RichEdit's first character not fitting a page `widthTwips` wide whose rectangle is `rc` (twips): EM_FORMATRANGE.
LRESULT FormatRange(HWND edit, HDC dc, const RECT& rc, bool draw) {
    FORMATRANGE range{};
    range.hdc = dc;
    range.hdcTarget = dc;
    range.rc = rc;
    range.rcPage = rc;
    range.chrg.cpMin = 0;
    range.chrg.cpMax = -1;
    const LRESULT fitted = SendMessageA(edit, EM_FORMATRANGE, draw ? 1 : 0, reinterpret_cast<LPARAM>(&range));
    SendMessageA(edit, EM_FORMATRANGE, 0, 0);
    return fitted;
}

void WriteBmp(const std::string& path, const std::vector<std::uint32_t>& pixels, int width, int height) {
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    info.biSize = sizeof info;
    info.biWidth = width;
    info.biHeight = -height;
    info.biPlanes = 1;
    info.biBitCount = 32;
    const DWORD bytes = static_cast<DWORD>(pixels.size() * 4);
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof file + sizeof info;
    file.bfSize = file.bfOffBits + bytes;
    std::FILE* out = nullptr;
    if (fopen_s(&out, path.c_str(), "wb") != 0 || !out) return;
    std::fwrite(&file, sizeof file, 1, out);
    std::fwrite(&info, sizeof info, 1, out);
    std::fwrite(pixels.data(), bytes, 1, out);
    std::fclose(out);
}

} // namespace

int Titles(int argc, char** argv) {
    std::string res = "F:\\games\\EvilIslands\\Res\\outro.res";
    int width = 1248, height = 360;
    std::string picture;
    std::vector<int> offsets;
    bool aware = true;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--res" && i + 1 < argc) res = argv[++i];
        else if (arg == "--dpi-unaware") aware = false;
        else if (arg == "--width" && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (arg == "--height" && i + 1 < argc) height = std::atoi(argv[++i]);
        else if (arg == "--picture" && i + 1 < argc) {
            picture = argv[++i];
            while (i + 1 < argc && argv[i + 1][0] != '-') offsets.push_back(std::atoi(argv[++i]));
        } else {
            std::printf("usage: ei_text_check --titles [--res outro.res] [--width px] [--height px] [--dpi-unaware] [--picture out.bmp offset...]\n");
            return 2;
        }
    }
    if (aware) SetProcessDPIAware();
    const std::string rtf = ReadResFile(res, "outro.rtf");
    if (rtf.empty()) {
        std::printf("cannot read outro.rtf from %s\n", res.c_str());
        return 2;
    }
    Page page(width, height);
    const int dpi = GetDeviceCaps(page.dc, LOGPIXELSY);
    const int widthTwips = MulDiv(width, kTwipsPerInch, GetDeviceCaps(page.dc, LOGPIXELSX));
    const int heightTwips = MulDiv(height, kTwipsPerInch, dpi);

    // RichEdit, as UiScrollingSurface made it.
    HMODULE library = LoadLibraryA("Riched20.dll");
    HWND edit = CreateWindowExA(0, "RichEdit20A", nullptr, WS_POPUP | ES_MULTILINE, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!library || !edit) {
        std::printf("no RichEdit\n");
        return 2;
    }
    Source source{&rtf, 0};
    EDITSTREAM stream{reinterpret_cast<DWORD_PTR>(&source), 0, ReadSource};
    const LRESULT length = SendMessageA(edit, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    auto fits = [&](int bottom) { return FormatRange(edit, page.dc, RECT{0, 0, widthTwips, bottom}, false); };
    // Each line's bottom: the least height (twips) at which RichEdit fits more of the text. RichEdit puts the first line
    // on any page, however low, so its bottom is not measured (-1).
    std::vector<int> richBottoms;
    int low = 0;
    LRESULT fitted = fits(0);
    if (fitted > 0) richBottoms.push_back(-1);
    while (fitted < length) {
        int high = low + 1;
        while (fits(high) <= fitted) high *= 2;
        while (high - low > 1) {
            const int middle = (low + high) / 2;
            (fits(middle) > fitted ? high : low) = middle;
        }
        richBottoms.push_back(high);
        fitted = fits(high);
        low = high;
    }
    const int richFit = richBottoms.empty() ? 0 : richBottoms.back();

    const RichText text = ReadRtf(rtf);
    const RichLayout ours = LayoutRichText(text, TwipsToPixels(widthTwips, dpi), dpi, false);
    std::printf("dpi %d, page %d x %d px (%d x %d twips), %lld characters, %zu paragraphs\n", dpi, width, height, widthTwips, heightTwips,
                static_cast<long long>(length), text.paragraphs.size());
    std::printf("RichEdit: %zu lines, the whole text fits in %d twips (%d px)\n", richBottoms.size(), richFit, TwipsToPixels(richFit, dpi));
    // Ours fits when the page's bottom in pixels reaches the text's (UiScrollingSurface::RenderText).
    int ourFit = MulDiv(ours.height, kTwipsPerInch, dpi);
    while (ourFit > 0 && TwipsToPixels(ourFit - 1, dpi) >= ours.height) --ourFit;
    while (TwipsToPixels(ourFit, dpi) < ours.height) ++ourFit;
    std::printf("ours:     %zu lines, %d px high, the whole text fits in %d twips (%+d)\n", ours.lines.size(), ours.height, ourFit,
                ourFit - richFit);
    const std::size_t lines = std::max(richBottoms.size(), ours.lines.size());
    int differ = 0;
    for (std::size_t i = 0; i < lines; ++i) {
        if (i < richBottoms.size() && richBottoms[i] < 0) continue;
        const int rich = i < richBottoms.size() ? TwipsToPixels(richBottoms[i], dpi) : -1;
        const int mine = i < ours.lines.size() ? ours.lines[i].top + ours.lines[i].height : -1;
        if (rich != mine && differ++ < 20)
            std::printf("  line %zu: RichEdit's bottom %d px (%d twips), ours %d px\n", i, rich, i < richBottoms.size() ? richBottoms[i] : -1, mine);
    }
    std::printf("%d of %zu line bottoms differ\n", differ, lines);
    const int failures = differ + (ourFit != richFit ? 1 : 0);

    if (!picture.empty()) {
        const int panel = width + 8;
        std::vector<std::uint32_t> out(static_cast<std::size_t>(2 * panel) * static_cast<std::size_t>(height) * offsets.size(), 0x404040);
        for (std::size_t n = 0; n < offsets.size(); ++n) {
            const int offset = offsets[n];
            std::fill_n(page.pixels, static_cast<std::size_t>(width) * height, 0xFFFFFF);
            SendMessageA(edit, EM_SETBKGNDCOLOR, 0, RGB(255, 255, 255));
            FormatRange(edit, page.dc, RECT{0, -offset, widthTwips, kExtraTwipsBelow + heightTwips}, true);
            GdiFlush();
            std::vector<std::uint32_t> mine(static_cast<std::size_t>(width) * height, 0xFFFFFF);
            for (const RichDraw& draw : RichDraws(ours, 0, -TwipsToPixels(offset, dpi), Rect{0, 0, width, height})) {
                GlyphAtlas& atlas = draw.font->Atlas();
                const std::uint32_t rgb = ((draw.colour & 0xFF) << 16) | (draw.colour & 0xFF00) | ((draw.colour >> 16) & 0xFF);
                for (const GlyphQuad& q : draw.layout.quads)
                    for (int y = 0; y < q.height; ++y)
                        for (int x = 0; x < q.width; ++x) {
                            const int px = q.x + x, py = q.y + y;
                            if (px < 0 || py < 0 || px >= width || py >= height) continue;
                            if (atlas.Pixels()[static_cast<std::size_t>(q.atlasY + y) * atlas.Width() + q.atlasX + x])
                                mine[static_cast<std::size_t>(py) * width + px] = rgb;
                        }
            }
            for (int y = 0; y < height; ++y) {
                std::uint32_t* row = &out[(n * height + y) * 2 * panel];
                std::copy_n(page.pixels + static_cast<std::size_t>(y) * width, width, row);
                std::copy_n(&mine[static_cast<std::size_t>(y) * width], width, row + panel);
            }
        }
        WriteBmp(picture, out, 2 * panel, height * static_cast<int>(offsets.size()));
        std::printf("wrote %s\n", picture.c_str());
    }
    DestroyWindow(edit);
    FreeLibrary(library);
    return failures == 0 ? 0 : 1;
}
