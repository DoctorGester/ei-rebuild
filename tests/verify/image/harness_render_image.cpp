// The image's renderer for the harness (image_side.h; harness.cpp, harness_capture.cpp): its present counts, its
// frame placement and its steps.
#include "image_side.h"

#include "../harness_capture.h"

#include "../../../src/hook/log.h"
#include "ei/hook.h"

#include "platform/win32/graphics_win32.h"
#include "render/render.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace ei::verify::image {

CursorPresents CursorPresentCounts() {
    const render::CursorPresentInfo info = render::CursorPresents();
    CursorPresents counts;
    counts.presents = info.presents;
    counts.calls = info.calls;
    for (int i = 0; i < 4; ++i) counts.refused[i] = info.refused[i];
    counts.lastLeft = info.lastTo.left;
    counts.lastTop = info.lastTo.top;
    counts.lastRight = info.lastTo.right;
    counts.lastBottom = info.lastTo.bottom;
    counts.checks = info.checks;
    counts.mismatches = info.mismatches;
    return counts;
}

WindowPresents WindowPresentCounts() {
    const render::WindowPresentInfo info = render::WindowPresents();
    WindowPresents counts;
    counts.presents = info.presents;
    counts.rampChecks = info.rampChecks;
    counts.rampPoints = info.rampPoints;
    counts.rampDiffering = info.rampDiffering;
    counts.cursorChecks = info.cursorChecks;
    counts.cursorMismatches = info.cursorMismatches;
    counts.rampCheckPending = info.rampCheckPending;
    counts.rampIdentity = info.rampIdentity;
    counts.deviceLosses = info.deviceLosses;
    counts.deviceRecoveries = info.deviceRecoveries;
    counts.presentsAtRecovery = info.presentsAtRecovery;
    counts.lastLossCode = info.lastLossCode;
    counts.removable = platform::graphics::Removable();
    counts.removals = platform::graphics::Removals();
    return counts;
}

bool ReadCursorPresent(int* width, int* height, std::vector<std::uint8_t>* bgr) {
    return render::ReadCursorPresent(width, height, bgr);
}
bool FrameInWindow(int* width, int* height) {
    render::Rect placed{};
    return render::FrameInWindow(width, height, &placed);
}
void FrameToWindow(int* x, int* y) { render::FrameToWindow(x, y); }
void WindowToFrame(int* x, int* y) { render::WindowToFrame(x, y); }
// "removable" is the Direct3D backend's (graphics_win32.h); the other words are the renderer's.
void ConfigureRenderer(const char* spec) {
    std::string words;
    std::size_t start = 0;
    const std::string all = spec ? spec : "";
    while (start <= all.size()) {
        std::size_t comma = all.find(',', start);
        if (comma == std::string::npos) comma = all.size();
        const std::string word = all.substr(start, comma - start);
        if (word == "removable") platform::graphics::MakeRemovable();
        else words += (words.empty() ? "" : ",") + word;
        start = comma + 1;
    }
    render::Configure(words.c_str());
}
void SetWaitForVsync(bool on) { render::SetWaitForVsync(on); }
void LoseDevice() { render::LoseDevice(); }
bool RemoveDevice() { return platform::graphics::RequestRemoval(); }
void EndFrame() { render::EndFrame(); }
bool ReadFrame(int* width, int* height, std::vector<std::uint8_t>* bgr) { return render::ReadFrame(width, height, bgr); }

bool RenderStep(const std::string& verb, const std::string& argument, std::vector<std::string>* failures,
                std::vector<std::string>* frames) {
    if (verb == "terrainlevels") {
        if (argument == "on") render::TerrainLevels(render::TerrainLevelMode::Levels);
        else if (argument == "point") render::TerrainLevels(render::TerrainLevelMode::PointMips);
        else if (argument == "off") render::TerrainLevels(render::TerrainLevelMode::Off);
        else failures->push_back("terrainlevels needs on, point or off");
    } else if (verb == "levelsteps") {
        char frame[64] = "";
        double least = 0, most = 1;
        if (sscanf_s(argument.c_str(), "%63s %lf %lf", frame, unsigned(sizeof(frame)), &least, &most) < 3) {
            failures->push_back("levelsteps needs a frame name, a least and a most fraction");
            return true;
        }
        double steps = 0;
        long long pairs = 0;
        const char* why = "";
        if (!capture::LevelSteps(frame, &steps, &pairs, &why)) {
            failures->push_back(std::string("levelsteps ") + frame + ": " + why);
            return true;
        }
        char text[200];
        std::snprintf(text, sizeof(text), "levelsteps %s: %.4f%% of %lld neighbouring pairs step a level (allowed %.4f%% .. %.4f%%)",
                      frame, 100.0 * steps, pairs, 100.0 * least, 100.0 * most);
        Log("harness: %s", text);
        frames->push_back(text);
        if (steps < least || steps > most) failures->push_back(text);
    } else if (verb == "hues") {
        char frame[64] = "";
        double least = 0, most = 1;
        if (sscanf_s(argument.c_str(), "%63s %lf %lf", frame, unsigned(sizeof(frame)), &least, &most) < 3) {
            failures->push_back("hues needs a frame name, a least and a most fraction");
            return true;
        }
        double shares[4] = {0, 0, 0, 0};
        const char* why = "";
        if (!capture::HueShares(frame, shares, &why)) {
            failures->push_back(std::string("hues ") + frame + ": " + why);
            return true;
        }
        char text[200];
        std::snprintf(text, sizeof(text),
                      "hues %s: red %.3f%% green %.3f%% blue %.3f%% yellow %.3f%% (allowed %.3f%% .. %.3f%% each)", frame,
                      100.0 * shares[0], 100.0 * shares[1], 100.0 * shares[2], 100.0 * shares[3], 100.0 * least,
                      100.0 * most);
        Log("harness: %s", text);
        frames->push_back(text);
        for (double share : shares)
            if (share < least || share > most) {
                failures->push_back(text);
                break;
            }
    } else if (verb == "mmplevels") {
        double most = 0;
        if (sscanf_s(argument.c_str(), "%lf", &most) < 1) {
            failures->push_back("mmplevels needs the largest mean difference allowed");
            return true;
        }
        render::MmpLevelCheck check = render::CheckMmpLevels();
        char text[240];
        std::snprintf(text, sizeof(text),
                      "mmplevels: %d textures, %d levels; largest mean difference %.2f (allowed %.2f; level %d of a "
                      "%dx%d texture, format %08X)",
                      check.textures, check.levels, check.worst, most, check.worstLevel, check.worstWidth,
                      check.worstHeight, static_cast<unsigned>(check.worstFormat));
        Log("harness: %s", text);
        frames->push_back(text);
        if (check.levels == 0 || check.worst > most) failures->push_back(text);
    } else if (verb == "msaa" || verb == "anisotropy") {
        int level = 0;
        if (sscanf_s(argument.c_str(), "%d", &level) < 1) failures->push_back(verb + " needs a level");
        else if (verb == "msaa") render::SetMsaa(level);
        else render::SetAnisotropy(level);
    } else if (verb == "smoothcheck") {
        char plain[64] = "", smooth[64] = "";
        double least = 0, most = 1;
        if (sscanf_s(argument.c_str(), "%63s %63s %lf %lf", plain, unsigned(sizeof(plain)), smooth, unsigned(sizeof(smooth)),
                     &least, &most) < 4) {
            failures->push_back("smoothcheck needs two frame names, a least and a most fraction");
            return true;
        }
        render::SmoothInfo info = render::SmoothState();
        double differing = 0;
        int largest = 0;
        const char* why = "";
        bool compared = capture::Compare(plain, smooth, 8, &differing, &largest, &why);
        char text[360];
        std::snprintf(text, sizeof(text),
                      "smoothcheck %s %s: msaa %d, anisotropy %d; %d samples (the device gives %d), last frame %d "
                      "multisampled passes, %d models anisotropic, %d as their stage; %.3f%% of the pixels differ by more "
                      "than 8 (largest %d; allowed %.3f%% .. %.3f%%)",
                      plain, smooth, info.msaa, info.anisotropy, info.targetSamples, info.deviceSamples,
                      info.multisampledPasses, info.anisotropicModels, info.plainModels, 100.0 * differing, largest,
                      100.0 * least, 100.0 * most);
        Log("harness: %s", text);
        frames->push_back(text);
        // The levels asked for are what was drawn: as many samples as asked (at least 2: the suites' devices
        // give 8), anisotropic world models exactly when asked.
        const int samples = info.msaa < info.deviceSamples ? info.msaa : info.deviceSamples;
        const bool drawn = info.msaa > 1 ? samples >= 2 && info.targetSamples == samples && info.multisampledPasses >= 1
                                         : info.targetSamples == 1 && info.multisampledPasses == 0;
        const bool sampled = info.anisotropy > 1 ? info.anisotropicModels >= 1 : info.anisotropicModels == 0;
        if (!compared) failures->push_back(std::string("smoothcheck ") + plain + " " + smooth + ": " + why);
        else if (!drawn || !sampled || differing < least || differing > most)
            failures->push_back(text);
    } else if (verb == "smoothorder") {
        static render::SmoothOrder order = render::SmoothOrder::Game;
        if (argument == "game" || argument == "before" || argument == "split") {
            order = argument == "game" ? render::SmoothOrder::Game
                    : argument == "before" ? render::SmoothOrder::Before : render::SmoothOrder::Split;
            render::SetSmoothOrder(order);
            return true;
        }
        if (argument != "check") {
            failures->push_back("smoothorder needs game, before, split or check");
            return true;
        }
        render::SmoothInfo info = render::SmoothState();
        char text[240];
        std::snprintf(text, sizeof(text),
                      "smoothorder check (%s): msaa %d, %d samples, last frame %d multisampled passes, %d copies into "
                      "them, %d depth copies back",
                      order == render::SmoothOrder::Game ? "game" : order == render::SmoothOrder::Before ? "before" : "split",
                      info.msaa, info.targetSamples, info.multisampledPasses, info.copiesInto, info.depthCopiesBack);
        Log("harness: %s", text);
        frames->push_back(text);
        bool ok = info.msaa > 1 && info.targetSamples >= 2;
        if (order == render::SmoothOrder::Game) ok = ok && info.multisampledPasses >= 1 && info.copiesInto == 0;
        else if (order == render::SmoothOrder::Before) ok = ok && info.multisampledPasses >= 1 && info.copiesInto >= 1;
        else ok = ok && info.multisampledPasses >= 2 && info.copiesInto >= 1 && info.depthCopiesBack >= 2;
        if (!ok) failures->push_back(text);
    } else if (verb == "shadowcasters") {
        if (argument == "view") render::SetOffscreenCasters(render::OffscreenCasters::View);
        else if (argument == "all") render::SetOffscreenCasters(render::OffscreenCasters::All);
        else if (argument == "walked") render::SetOffscreenCasters(render::OffscreenCasters::Walked);
        else failures->push_back("shadowcasters needs view, all or walked");
    } else if (verb == "shadowview" || verb == "shadowshift") {
        if (verb == "shadowshift") render::SetShadowShift(std::atoi(argument.c_str()));
        else if (argument == "on" || argument == "off") render::SetShadowView(argument == "on");
        else failures->push_back("shadowview needs on or off");
    } else if (verb == "terraincache") {
        if (argument == "on" || argument == "off") {
            render::SetTerrainCache(argument == "on");
        } else if (argument == "check") {
            render::TerrainCacheInfo info = render::TerrainCacheState();
            char text[200];
            std::snprintf(text, sizeof(text), "terraincache: %s, %zu pages kept, %llu checks against a fresh build, %llu stale",
                          info.on ? "on" : "off", info.pages, static_cast<unsigned long long>(info.checks),
                          static_cast<unsigned long long>(info.stale));
            Log("harness: %s", text);
            frames->push_back(text);
            if (info.stale > 0) failures->push_back(text);
        } else {
            failures->push_back("terraincache needs on, off or check");
        }
    } else if (verb == "meshcache") {
        if (argument == "on" || argument == "off") {
            render::SetMeshCache(argument == "on");
        } else if (argument == "check") {
            render::MeshCacheInfo info = render::MeshCacheState();
            char text[200];
            std::snprintf(text, sizeof(text), "meshcache: %s, %zu meshes kept, %llu checks against a fresh build, %llu stale",
                          info.on ? "on" : "off", info.meshes, static_cast<unsigned long long>(info.checks),
                          static_cast<unsigned long long>(info.stale));
            Log("harness: %s", text);
            frames->push_back(text);
            if (info.stale > 0) failures->push_back(text);
        } else {
            failures->push_back("meshcache needs on, off or check");
        }
    } else if (verb == "shadowcheck") {
        // shadowcheck <least casters> [<least casters outside the view> (R20)]
        int least = 1, leastOutside = 0;
        if (sscanf_s(argument.c_str(), "%d %d", &least, &leastOutside) < 1) {
            failures->push_back("shadowcheck needs the least number of casters");
            return true;
        }
        render::ShadowInfo info = render::ShadowState();
        char text[420];
        std::snprintf(text, sizeof(text),
                      "shadowcheck: map %s (%d texels over %.0f units), %d casters in %d draws, %d meshes and %d "
                      "ground batches sampling it (at least %d casters); shadowed objects not drawn %d, drawn but "
                      "not cast %d; outside the view %d casters in %d draws (%d corners)",
                      info.made ? "made" : "not made", info.size,
                      static_cast<double>(info.extent), info.casters, info.casterDraws, info.receivingMeshes,
                      info.receivingTerrain, least, info.offScreen, info.lost, info.outsideCasters,
                      info.outsideDraws, info.outsideCorners);
        Log("harness: %s", text);
        frames->push_back(text);
        if (!info.made || info.casters < least || info.receivingMeshes < 1 || info.receivingTerrain < 1 ||
            info.lost > 0 || info.outsideCasters < leastOutside)
            failures->push_back(text);
    } else if (verb == "renderstats") {
        render::FrameStats t = render::TakeFrameStats();
        // The main thread's CPU time (its cycles, in milliseconds by the thread's CPU time so far, as the
        // TIMING lines) and the wall time since the last renderstats (R16h: what a frame costs the main
        // thread, the game's work with the renderer's). Differences of whole counts first: the main thread
        // computes at the game's 24-bit precision.
        static unsigned long long lastCycles = 0;
        static long long lastTicks = 0;
        unsigned long long cycles = 0;
        double msPerCycle = 0.0, msPerTick = 0.0;
        LARGE_INTEGER now{}, frequency{};
        {
            FILETIME created{}, exited{}, kernel{}, user{};
            QueryThreadCycleTime(GetCurrentThread(), &cycles);
            if (cycles && GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user)) {
                auto ms = [](const FILETIME& f) {
                    return static_cast<double>((static_cast<unsigned long long>(f.dwHighDateTime) << 32) | f.dwLowDateTime) / 1e4;
                };
                msPerCycle = (ms(kernel) + ms(user)) / static_cast<double>(cycles);
            }
            QueryPerformanceCounter(&now);
            QueryPerformanceFrequency(&frequency);
            if (frequency.QuadPart) msPerTick = 1000.0 / static_cast<double>(frequency.QuadPart);
        }
        const double frameCpuMs = static_cast<double>(cycles - lastCycles) * msPerCycle;
        const double frameWallMs = static_cast<double>(now.QuadPart - lastTicks) * msPerTick;
        lastCycles = cycles;
        lastTicks = now.QuadPart;
        if (argument == "-" || t.frames == 0) return true;   // a new count begins
        double n = static_cast<double>(t.frames);
        auto kb = [n](std::uint64_t bytes) { return static_cast<double>(bytes) / 1024.0 / n; };
        auto per = [n](std::uint64_t count) { return static_cast<double>(count) / n; };
        using F = render::FrameStats;
        const render::SmoothInfo quality = render::SmoothState();
        char text[2400];
        std::snprintf(
            text, sizeof(text),
            "renderstats %s: %u frames of %dx%d, per frame: hand-over %.0f terrain pages -> %.0f batches, %.0f meshes / %.0f "
            "passes (%.0f flat, %.1f KB), %.0f pre-transformed, %.0f blits, %.0f depth clears, %.0f state reads, "
            "%.0f matrices; against the last frame (same / relit / moved / new): terrain pages %.0f / %.0f / %.0f / %.0f, meshes %.0f / %.0f / "
            "%.0f / %.0f, %.0f meshes share another's geometry; upload KB "
            "terrain %.1f v + %.1f i, meshes %.1f v + %.1f i, uniforms %.1f; %.1f passes, %.0f draws, pipelines %.0f "
            "applied / %.0f changed, bindings %.0f (first texture changed %.0f); images made (count / KB): mmp %.2f / "
            "%.1f, movie %.2f / %.1f, small %.2f / %.1f; "
            "CPU ms terrain %.3f, hand-over %.3f, textures %.3f, end of frame %.3f (counting %.3f), device mesh draws %.3f; "
            "decals projected: %.1f prints %.3f ms (%.0f vertices); flat draws: %.1f, %.3f ms "
            "(%.0f vertices); shadow builds %.1f, %.3f ms; "
            "shadow map %.1f draws, CPU ms %.3f; GPU ms (%u frames) frame %.3f, shadow map %.3f; morphed meshes %.1f "
            "(%.0f corners); main thread ms CPU %.2f / wall %.2f; quality: msaa %d (%d samples, the device gives %d), "
            "anisotropy %d",
            argument.c_str(), t.frames, t.width, t.height, per(t.terrainPages), per(t.terrainBatches), per(t.meshes),
            per(t.meshPasses), per(t.flatMeshes), kb(t.flatVertexBytes),
            per(t.transformedDraws), per(t.blits), per(t.depthClears), per(t.stateReads), per(t.matrices),
            per(t.terrainReuse[F::Same]), per(t.terrainReuse[F::Relit]), per(t.terrainReuse[F::Moved]),
            per(t.terrainReuse[F::New]), per(t.meshReuse[F::Same]), per(t.meshReuse[F::Relit]), per(t.meshReuse[F::Moved]),
            per(t.meshReuse[F::New]), per(t.sharedMeshes),
            kb(t.terrainVertexBytes), kb(t.terrainIndexBytes), kb(t.meshVertexBytes), kb(t.meshIndexBytes), kb(t.uniformBytes),
            per(t.passes), per(t.draws), per(t.pipelineApplies), per(t.pipelineChanges), per(t.bindings),
            per(t.textureChanges), per(t.uploads[F::Mmp]), kb(t.uploadBytes[F::Mmp]),
            per(t.uploads[F::Movie]), kb(t.uploadBytes[F::Movie]), per(t.uploads[F::Small]), kb(t.uploadBytes[F::Small]),
            t.terrainMs / n, t.handOverMs / n, t.textureMs / n, t.endFrameMs / n, t.statsMs / n, t.deviceMeshMs / n,
            per(t.decalPrints), t.decalPrintMs / n, per(t.decalVertices),
            per(t.flatDraws), t.flatMs / n, per(t.flatDrawVertices), per(t.shadowBuilds), t.shadowBuildMs / n,
            per(t.shadowDraws), t.shadowMs / n, t.gpuFrames, t.gpuFrames ? t.gpuFrameMs / t.gpuFrames : 0.0,
            t.gpuFrames ? t.gpuShadowMs / t.gpuFrames : 0.0, per(t.morphMeshes), per(t.morphCorners), frameCpuMs / n,
            frameWallMs / n, quality.msaa, quality.targetSamples, quality.deviceSamples, quality.anisotropy);
        Log("harness: %s", text);
        frames->push_back(text);
    } else {
        return false;
    }
    return true;
}

} // namespace ei::verify::image
