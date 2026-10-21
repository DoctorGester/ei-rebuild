// The interface's two paths compared (verify build only; ticket N3): see harness_ui.h.
//
// After each frame (Frame): the mirror feeds its canvases first (so a surface whose memory just changed is
// compared with its fed canvas, not the one before), then every canvas whose version (render::ui::CanvasInfo)
// or memory changed since its last check is read back and compared with its holder's memory, widened to
// 8 bits a channel by the same rule as the canvas (render::ui::Widen). A surface's name comes from the
// window that owns it (g_uiSurfaceOwnerObserver) or the interface's own fields.
#define NOMINMAX
#include "harness_ui.h"
#include "harness_capture.h"

#include "../../src/game/console.h"
#include "../../src/game/game_singletons.h"
#include "../../src/game/ui.h"
#include "../../src/hook/log.h"
#include "../../src/render/render.h"
#include "../../src/render/render_ui.h"
#include "game/game_data.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <unordered_map>
#include <vector>

namespace ei::verify::uicompare {

namespace {

namespace ui = render::ui;
using render::Rect;

struct Settings {
    int tolerance = 0;
    int text = 24;
    int block = 1;
    double limit = 0.0;
    double textLimit = 0.01;
    int dumps = 8;
};
Settings g_settings;

// The windows' surfaces (g_uiSurfaceOwnerObserver), in the order they were added.
std::map<const CUiSurface*, const CUiWindow*> g_owners;
void (*g_previousObserver)(const CUiWindow*, const CUiSurface*, bool) = nullptr;

void OnOwner(const CUiWindow* window, const CUiSurface* surface, bool added) {
    if (added) g_owners[surface] = window;
    else g_owners.erase(surface);
    if (g_previousObserver) g_previousObserver(window, surface, added);
}

struct Checked {
    unsigned version = 0;
    std::uint64_t memoryHash = 0;
};
std::unordered_map<const void*, Checked> g_checked;   // by holder
struct Fed {
    std::uint64_t memoryHash = 0;
    bool stale = false;
};
std::unordered_map<const void*, Fed> g_fed;   // the mirror's canvases, by holder
bool g_mirror = false;

unsigned g_checks = 0, g_differing = 0, g_settled = 0, g_dumped = 0;
std::map<std::string, unsigned> g_checkedNames;   // name: checks
std::vector<std::string> g_differences;           // the first few, for the report
std::string g_firstUnsettled;                     // the first difference since the last uidiffcheck

std::string Hex(std::uintptr_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%08X", static_cast<unsigned>(value));
    return text;
}

// "W<window vtable>#<index in its surfaces>".
std::string WindowName(const CUiWindow* window, const CUiSurface* surface) {
    std::uintptr_t vtable = *reinterpret_cast<const std::uintptr_t*>(window);
    int index = -1;
    for (int i = 0; i < window->surfaces_.size; ++i)
        if (window->surfaces_.data[i] == surface) index = i;
    return "W" + Hex(ei::OriginalAddress(vtable)) + "#" + std::to_string(index);
}

// Every surface holder that can be named now.
std::unordered_map<const void*, std::string> NamedHolders() {
    std::unordered_map<const void*, std::string> out;
    auto add = [&](const CUiSurface* surface, const std::string& name) {
        if (surface && surface->holder_) out.emplace(surface->holder_, name);
    };
    const CInterface3D* ui = TheInterface();
    add(ui->backdrop_, "backdrop");
    add(ui->secondBackdrop_, "backdrop2");
    add(ui->fpsSurface_, "fps");
    add(ui->statusSurface_, "status");
    if (const console::ConsoleWindow* console = ui->console_) add(static_cast<const CUiSurface*>(console->surface_), "console");
    for (const auto& [surface, window] : g_owners) add(surface, WindowName(window, surface));
    return out;
}

std::string NameOf(const std::unordered_map<const void*, std::string>& names, const void* holder) {
    auto it = names.find(holder);
    return it != names.end() ? it->second : "holder" + Hex(reinterpret_cast<std::uintptr_t>(holder));
}

ui::PixelFormat FormatOf(const render::SurfaceMemory& m) { return ui::PixelFormat{m.bitsPerPixel, m.redMask, m.greenMask, m.blueMask}; }

bool Readable(const render::SurfaceMemory* m) { return m && m->bits && (m->bitsPerPixel == 16 || m->bitsPerPixel == 32); }

std::uint64_t HashMemory(const render::SurfaceMemory& m) {
    std::uint64_t h = 1469598103934665603ull;
    const std::size_t rowBytes = static_cast<std::size_t>(m.width) * static_cast<std::size_t>(m.bitsPerPixel / 8);
    for (int y = 0; y < m.height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(m.bits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(m.pitch);
        for (std::size_t i = 0; i < rowBytes; ++i) h = (h ^ row[i]) * 1099511628211ull;
    }
    return h;
}

// The memory as 0x00RRGGBB rows, widened as the canvas holds it.
std::vector<std::uint32_t> Widened(const render::SurfaceMemory& m) {
    const ui::PixelFormat format = FormatOf(m);
    std::vector<std::uint32_t> rgb(static_cast<std::size_t>(m.width) * static_cast<std::size_t>(m.height));
    for (int y = 0; y < m.height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(m.bits) + static_cast<std::size_t>(y) * static_cast<std::size_t>(m.pitch);
        std::uint32_t* out = rgb.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(m.width);
        for (int x = 0; x < m.width; ++x)
            out[x] = ui::Widen(m.bitsPerPixel == 16 ? reinterpret_cast<const std::uint16_t*>(row)[x] : reinterpret_cast<const std::uint32_t*>(row)[x],
                               format);
    }
    return rgb;
}

int ChannelDifference(std::uint32_t a, std::uint32_t b) {
    int d = 0;
    for (int shift = 0; shift < 24; shift += 8)
        d = std::max(d, std::abs(static_cast<int>((a >> shift) & 0xFF) - static_cast<int>((b >> shift) & 0xFF)));
    return d;
}

std::string FileName(const std::string& name) {
    std::string out;
    for (char c : name) out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
    return out;
}

void Dump(const std::string& name, int width, int height, const std::vector<std::uint32_t>& gdi, const std::vector<std::uint32_t>& canvas) {
    if (g_dumped >= static_cast<unsigned>(g_settings.dumps)) return;
    ++g_dumped;
    const std::string base = "uidiff" + std::to_string(g_dumped) + "_" + FileName(name);
    for (int which = 0; which < 2; ++which) {
        const std::vector<std::uint32_t>& rgb = which ? canvas : gdi;
        std::vector<std::uint8_t> bgr(rgb.size() * 3);
        for (std::size_t i = 0; i < rgb.size(); ++i) {
            bgr[3 * i] = static_cast<std::uint8_t>(rgb[i]);
            bgr[3 * i + 1] = static_cast<std::uint8_t>(rgb[i] >> 8);
            bgr[3 * i + 2] = static_cast<std::uint8_t>(rgb[i] >> 16);
        }
        std::string written;
        capture::WriteFrame((base + (which ? "_canvas" : "_gdi")).c_str(), width, height, bgr, &written);
        Log("harness: uidiff: wrote %s", written.c_str());
    }
}

// One surface: its canvas against its memory.
void Compare(const ui::CanvasInfo& canvas, const render::SurfaceMemory& memory, const std::string& name) {
    ++g_checks;
    ++g_checkedNames[name];
    std::vector<std::uint32_t> drawn;
    int w = 0, h = 0;
    std::string verdict;
    if (memory.width != canvas.width || memory.height != canvas.height || !ui::ReadPixels(canvas.holder, &drawn, &w, &h)) {
        char text[200];
        std::snprintf(text, sizeof(text), "%s: canvas %dx%d, memory %dx%d%s", name.c_str(), canvas.width, canvas.height, memory.width,
                      memory.height, memory.width == canvas.width && memory.height == canvas.height ? ", not read back" : "");
        verdict = text;
    } else {
        const std::vector<std::uint32_t> gdi = Widened(memory);
        std::vector<std::uint8_t> isText(drawn.size(), 0);
        for (const Rect& r : canvas.text)
            for (int y = std::max(0, r.top); y < std::min(h, r.bottom); ++y)
                for (int x = std::max(0, r.left); x < std::min(w, r.right); ++x) isText[static_cast<std::size_t>(y) * w + x] = 1;
        long long plain = 0, plainDiffer = 0, text = 0, textDiffer = 0;
        int largest = 0;
        for (std::size_t i = 0; i < drawn.size(); ++i) {
            if (isText[i]) continue;
            ++plain;
            int d = ChannelDifference(gdi[i], drawn[i]);
            largest = std::max(largest, d);
            if (d > g_settings.tolerance) ++plainDiffer;
        }
        // The text: `block` x `block` cells, the averages of their text pixels.
        const int b = std::max(1, g_settings.block);
        for (int by = 0; by < h; by += b)
            for (int bx = 0; bx < w; bx += b) {
                long long sumA[3] = {0, 0, 0}, sumB[3] = {0, 0, 0}, n = 0;
                for (int y = by; y < std::min(h, by + b); ++y)
                    for (int x = bx; x < std::min(w, bx + b); ++x) {
                        std::size_t i = static_cast<std::size_t>(y) * w + x;
                        if (!isText[i]) continue;
                        ++n;
                        for (int c = 0; c < 3; ++c) {
                            sumA[c] += (gdi[i] >> (8 * c)) & 0xFF;
                            sumB[c] += (drawn[i] >> (8 * c)) & 0xFF;
                        }
                    }
                if (!n) continue;
                text += n;
                long long worst = 0;
                for (int c = 0; c < 3; ++c) worst = std::max(worst, std::llabs(sumA[c] - sumB[c]));
                if (worst > static_cast<long long>(g_settings.text) * n) textDiffer += n;
            }
        const bool differs = static_cast<double>(plainDiffer) > g_settings.limit * static_cast<double>(plain) ||
                             static_cast<double>(textDiffer) > g_settings.textLimit * static_cast<double>(text);
        if (differs) {
            char line[240];
            std::snprintf(line, sizeof(line), "%s %dx%d %d-bit: %lld of %lld pixels differ by more than %d (largest %d), text %lld of %lld",
                          name.c_str(), w, h, memory.bitsPerPixel, plainDiffer, plain, g_settings.tolerance, largest, textDiffer, text);
            verdict = line;
            Dump(name, w, h, gdi, drawn);
        }
    }
    if (verdict.empty()) return;
    ++g_differing;
    Log("harness: uidiff: %s", verdict.c_str());
    if (g_differences.size() < 16) g_differences.push_back(verdict);
    if (g_firstUnsettled.empty()) g_firstUnsettled = verdict;
    auto fed = g_fed.find(canvas.holder);
    if (fed != g_fed.end()) fed->second.stale = true;   // the mirror feeds it again: the next check passes
}

// The mirror: every named surface's canvas fed from its memory when that changed (or it was marked stale).
void Mirror(const std::unordered_map<const void*, std::string>& names) {
    for (auto it = g_fed.begin(); it != g_fed.end();)
        it = ui::HasCanvas(it->first) && names.count(it->first) ? std::next(it) : g_fed.erase(it);
    for (const auto& [holder, name] : names) {
        if (ui::HasCanvas(holder) && !g_fed.count(holder)) continue;   // a native surface's (src/game/ui_native.h)
        const render::SurfaceMemory* m = render::SurfaceMemoryOf(holder);
        if (!Readable(m) || !ui::Canvas(holder, m->width, m->height, FormatOf(*m))) continue;
        const std::uint64_t hash = HashMemory(*m);
        auto found = g_fed.find(holder);
        if (found != g_fed.end() && found->second.memoryHash == hash && !found->second.stale) continue;
        const std::vector<std::uint32_t> rgb = Widened(*m);
        ui::Image(holder, Rect{0, 0, m->width, m->height}, rgb.data(), m->width);
        g_fed[holder] = Fed{hash, false};
    }
}

} // namespace

void Install() {
    g_previousObserver = g_uiSurfaceOwnerObserver;
    g_uiSurfaceOwnerObserver = &OnOwner;
}

void Frame() {
    if (!render::Active() || ui::CurrentMode() != ui::Mode::Both) return;
    static bool logged = false;
    if (!logged) Log("harness: uiboth: the interface's canvases compared with GDI's drawing after each frame");
    logged = true;
    const std::unordered_map<const void*, std::string> names = NamedHolders();
    if (g_mirror) Mirror(names);
    std::vector<ui::CanvasInfo> canvases;
    ui::Canvases(&canvases);
    std::sort(canvases.begin(), canvases.end(), [&](const ui::CanvasInfo& a, const ui::CanvasInfo& b) {
        return NameOf(names, a.holder) < NameOf(names, b.holder);
    });
    for (auto it = g_checked.begin(); it != g_checked.end();)
        it = ui::HasCanvas(it->first) ? std::next(it) : g_checked.erase(it);
    for (const ui::CanvasInfo& canvas : canvases) {
        const render::SurfaceMemory* m = render::SurfaceMemoryOf(canvas.holder);
        if (!Readable(m)) continue;
        const std::uint64_t hash = HashMemory(*m);
        auto found = g_checked.find(canvas.holder);
        if (found != g_checked.end() && found->second.version == canvas.version && found->second.memoryHash == hash) continue;
        g_checked[canvas.holder] = Checked{canvas.version, hash};
        Compare(canvas, *m, NameOf(names, canvas.holder));
    }
}

bool Step(const std::string& verb, const std::string& argument, std::string* error) {
    if (verb != "uipath" && verb != "uimask" && verb != "uidiff" && verb != "uidiffcheck" && verb != "uimirror" && verb != "uifault")
        return false;
    if (!render::Active()) {
        Log("harness: %s: skipped in a run of the original code", verb.c_str());
        return true;
    }
    if (verb == "uimask") {
        std::vector<std::uint8_t> bgr;
        int width = 0, height = 0;
        if (argument.empty() || !render::ReadFrame(&width, &height, &bgr)) {
            *error = "uimask: " + std::string(argument.empty() ? "needs a name" : "no frame drawn");
            return true;
        }
        std::vector<Rect> rects;
        ui::DrawnCanvasRects(&rects);
        std::fill(bgr.begin(), bgr.end(), std::uint8_t{0});
        for (const Rect& r : rects)
            for (int y = std::max(0, r.top); y < std::min(height, r.bottom); ++y)
                for (int x = std::max(0, r.left); x < std::min(width, r.right); ++x)
                    std::fill_n(bgr.begin() + (static_cast<std::ptrdiff_t>(y) * width + x) * 3, 3, std::uint8_t{0xFF});
        std::string written;
        if (!capture::WriteFrame((argument + ".uimask").c_str(), width, height, bgr, &written)) *error = "uimask: cannot write " + written;
        else Log("harness: uimask: %u canvas blit(s) in %s", unsigned(rects.size()), written.c_str());
        return true;
    }
    if (ui::CurrentMode() != ui::Mode::Both) {
        *error = verb + ": needs the canvases drawn beside GDI (\"render ...,uiboth\")";
        return true;
    }
    if (verb == "uipath") {
        if (argument != "cpu" && argument != "native") *error = "uipath needs cpu or native";
        else ui::SetPath(argument == "cpu" ? ui::Path::Cpu : ui::Path::Native);
    } else if (verb == "uimirror") {
        g_mirror = argument == "on";
        if (!g_mirror) g_fed.clear();
    } else if (verb == "uifault") {
        const std::unordered_map<const void*, std::string> names = NamedHolders();
        const void* target = nullptr;
        std::string name;
        for (const auto& [holder, fed] : g_fed)
            if (ui::HasCanvas(holder) && (!target || NameOf(names, holder) < name)) target = holder, name = NameOf(names, holder);
        if (!target) *error = "uifault: no mirrored canvas (\"uimirror on\" and a frame first)";
        else ui::Fill(target, Rect{0, 0, 4, 4}, RGB(255, 0, 255));
        if (target) Log("harness: uifault: a magenta square on %s", name.c_str());
    } else if (verb == "uidiff") {
        char key[16] = "";
        const char* p = argument.c_str();
        int used = 0;
        double value = 0;
        while (sscanf_s(p, "%15s %lf%n", key, unsigned(sizeof(key)), &value, &used) == 2) {
            p += used;
            std::string k = key;
            if (k == "tolerance") g_settings.tolerance = static_cast<int>(value);
            else if (k == "text") g_settings.text = static_cast<int>(value);
            else if (k == "block") g_settings.block = static_cast<int>(value);
            else if (k == "limit") g_settings.limit = value;
            else if (k == "textlimit") g_settings.textLimit = value;
            else if (k == "dumps") g_settings.dumps = static_cast<int>(value);
            else *error = "uidiff: unknown setting " + k;
        }
    } else {   // uidiffcheck
        unsigned least = 0, most = 0;
        int n = sscanf_s(argument.c_str(), "%u %u", &least, &most);
        if (n < 2) most = least;
        const unsigned seen = g_differing - g_settled;
        g_settled = g_differing;
        g_firstUnsettled.clear();
        Log("harness: uidiffcheck: %u surface check(s) differed (expected %u..%u); %u checks so far", seen, least, most, g_checks);
        if (n < 1) *error = "uidiffcheck needs <least> [most]";
        else if (seen < least || seen > most)
            *error = "uidiffcheck: " + std::to_string(seen) + " surface check(s) differed, expected " + std::to_string(least) + ".." + std::to_string(most);
    }
    return true;
}

void Report(FILE* f) {
    if (!g_checks) return;
    std::fprintf(f, "\ninterface surfaces compared (uiboth): %u checks of %u surfaces, %u differed, %u of them expected\n", g_checks,
                 unsigned(g_checkedNames.size()), g_differing, g_settled);
    for (const auto& [name, count] : g_checkedNames) std::fprintf(f, "  %s: %u\n", name.c_str(), count);
    for (const std::string& d : g_differences) std::fprintf(f, "  differed: %s\n", d.c_str());
}

std::string Failure() {
    if (g_differing == g_settled) return {};
    return "uidiff: " + std::to_string(g_differing - g_settled) + " interface surface check(s) differed from GDI's drawing" +
           ", the first: " + g_firstUnsettled;
}

} // namespace ei::verify::uicompare
