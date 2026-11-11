// ei-convert --effect-check: the original particle types' sources against the reference recorded from their C++
// emitters (effect_check.h; docs/notes/effects.md, "Checks").
#include "effect_check.h"

#include "effect_files/reference.h"
#include "game/app/singletons.h"
#include "game/core/random.h"
#include "game/effects/effects.h"
#include "game/effects/particle_data.h"
#include "game/models/texture.h"
#include "game/objects/map_object.h"
#include "game/terrain/terrain.h"
#include "game/world/world.h"

#include "lib/bit_flags.h"
#include "lib/maths.h"

#include <bit>
#include <cstdint>
#include <cstdio>
#include <format>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace effect_check {

namespace {

// What a case does to its source or carrier before a tick.
enum class Event : std::uint8_t {
    Scale,          // ScaleSize(value), as an effect object's new size does
    StopEmitting,
    StartEmitting,
    Stop,
    Remove,         // the carrier leaves the world (the tick lets go of it)
    Attach,         // the source follows the carrier (back in the world)
    Velocity,       // the carrier's (or a free source's) movement per tick from now
    Alpha,          // the source's alpha and typeParam, as game code sets them (a zone exit's half extents)
    Param,
    Turn,           // turnCos and turnSin of the angle `value` (a zone exit's)
    Extent,         // the source's extent (a zone exit's red: x not 0)
};

struct Step {
    int tick = 0;
    Event event = Event::Scale;
    float value = 0.0f;
    Vector3 vector;
};

struct Case {
    const char* name;
    ParticleType type;
    bool carried;           // attached to the carrier at the start
    float size;             // the effect object's size (ScaleSize(size / baseSize), as EffectParticle::Spawn)
    int ticks;
    std::vector<Step> steps;
};

const Vector3 kStill = {0.0f, 0.0f, 0.0f};

// Each type's cases: carried and free, moving (a fast carrier gives distance tries), rescaled, stopped and started
// again, the carrier gone and back.
const std::vector<Case>& Cases() {
    using enum ParticleType;
    using enum Event;
    static const std::vector<Case> cases = {
        {"FireStar on a unit", FireStar, true, 1.25f, 8, {}},
        {"FireStar on a fast unit", FireStar, true, 1.0f, 8, {{1, Velocity, 0.0f, {1600.0f, 0.0f, 0.0f}}, {3, Velocity, 0.0f, kStill}}},
        {"FireStar free", FireStar, false, 1.0f, 4, {}},
        {"FireStar started again", FireStar, true, 0.8f, 12, {{5, StartEmitting}, {9, Remove}, {10, StartEmitting}}},
        {"AcidStar on a unit", AcidStar, true, 0.7f, 6, {}},
        {"CurseStars on a walking unit", CurseStars, true, 1.0f, 60,
         {{1, Velocity, 0.0f, {0.05f, 0.02f, 0.0f}}, {30, Remove}}},
        {"CurseStars rescaled", CurseStars, true, 2.0f, 70,
         {{10, Scale, 0.5f}, {15, Scale, 3.0f}, {25, StopEmitting}, {30, StartEmitting}, {40, Stop}}},
        {"CurseStars free, then carried", CurseStars, false, 1.0f, 50,
         {{4, Attach}, {6, Velocity, 0.0f, {-0.03f, 0.04f, 0.01f}}, {20, Remove}, {25, Attach}, {30, StartEmitting}}},
        {"CurseStars stopped, let go, carried again", CurseStars, true, 1.5f, 70,
         {{10, StopEmitting}, {35, Remove}, {40, Attach}, {45, StartEmitting}}},
        {"PortalStar free", PortalStar, false, 1.0f, 26,
         {{10, StopEmitting}, {14, StartEmitting}, {16, Scale, 2.0f}, {20, Stop}}},
        {"PortalStar on a moving carrier", PortalStar, true, 0.6f, 16,
         {{1, Velocity, 0.0f, {0.2f, -0.1f, 0.0f}}, {10, Remove}}},
        {"Sparks on a unit", Sparks, true, 1.2f, 10, {}},
        {"Sparks on a moving unit, started again", Sparks, true, 0.9f, 16,
         {{1, Velocity, 0.0f, {1.5f, 0.4f, 0.0f}}, {3, StartEmitting}, {4, Velocity, 0.0f, kStill}, {11, StartEmitting}}},
        {"Sparks let go", Sparks, true, 1.0f, 10, {{3, Remove}}},
        {"FeebleMind on a unit", FeebleMind, true, 1.0f, 40, {}},
        {"FeebleMind rescaled", FeebleMind, true, 1.6f, 60,
         {{6, Scale, 0.5f}, {12, Scale, 2.5f}, {20, StopEmitting}, {26, StartEmitting}, {40, Stop}}},
        {"FeebleMind on a walking unit, let go", FeebleMind, true, 0.7f, 45,
         {{1, Velocity, 0.0f, {0.05f, -0.03f, 0.0f}}, {20, Remove}, {26, Attach}}},
        {"ZoneExit", ZoneExit, false, 1.0f, 30, {{1, Alpha, 3.0f}, {1, Param, 1.5f}, {1, Turn, 0.7f}}},
        {"ZoneExit red, resized", ZoneExit, false, 1.0f, 40,
         {{1, Alpha, 2.0f}, {1, Param, 4.0f}, {1, Turn, -2.1f}, {1, Extent, 0.0f, {1.0f, 0.0f, 0.0f}}, {12, Alpha, 6.5f},
          {12, Param, 7.0f}, {25, StopEmitting}, {30, StartEmitting}, {35, Stop}}},
        {"ZoneExit as made", ZoneExit, false, 1.0f, 20, {}},
        {"FireBall flying", FireBall, false, 1.0f, 30,
         {{1, Velocity, 0.0f, {0.3f, 0.1f, -0.02f}}, {20, Velocity, 0.0f, kStill}}},
        {"FireBall on a fast unit, rescaled", FireBall, true, 1.5f, 30,
         {{1, Velocity, 0.0f, {-0.6f, 0.45f, 0.05f}}, {10, Scale, 0.5f}, {15, StopEmitting}, {18, StartEmitting}, {24, Remove}}},
        {"FireBall still", FireBall, false, 0.8f, 20, {}},
        {"FireArrow flying", FireArrow, false, 1.0f, 30,
         {{1, Velocity, 0.0f, {0.5f, -0.2f, 0.05f}}, {16, StopEmitting}, {20, StartEmitting}}},
        {"FireArrow still, then flying", FireArrow, false, 1.0f, 25,
         {{6, Velocity, 0.0f, {0.0f, 0.4f, 0.0f}}, {14, Scale, 2.0f}, {18, Stop}}},
        {"RikArrowSmoke flying", RikArrowSmoke, false, 0.7f, 25, {{1, Velocity, 0.0f, {-0.35f, -0.35f, 0.1f}}}},
        {"AcidRay flying", AcidRay, false, 1.0f, 30,
         {{1, Velocity, 0.0f, {0.25f, 0.3f, 0.0f}}, {12, Scale, 1.5f}, {22, Velocity, 0.0f, kStill}}},
        {"AcidRay on a rising unit", AcidRay, true, 0.6f, 20, {{1, Velocity, 0.0f, {0.0f, 0.0f, 0.2f}}}},
    };
    return cases;
}

std::uint64_t g_draws = 0;

void CountDraw(IsaacRandom& random, std::uint32_t, const void*) {
    if (&random == &g_particleRandom) ++g_draws;
}

// FNV-1a 64 over 32-bit words.
struct Hash {
    std::uint64_t value = 0xCBF29CE484222325ull;
    void Add(std::uint32_t word) {
        for (int i = 0; i < 4; ++i) {
            value ^= (word >> (8 * i)) & 0xFF;
            value *= 0x100000001B3ull;
        }
    }
    void Add(float f) { Add(std::bit_cast<std::uint32_t>(f)); }
    void Add(const Vector3& v) {
        Add(v.x);
        Add(v.y);
        Add(v.z);
    }
};

constexpr std::uint32_t kSourceFlags = lib::Bits(ParticleSourceFlag::Emitting) | lib::Bits(ParticleSourceFlag::AttachedOnly) |
                                       lib::Bits(ParticleSourceFlag::KeepAlive) | lib::Bits(ParticleSourceFlag::TicksWhilePaused) |
                                       lib::Bits(ParticleSourceFlag::OnPart) | lib::Bits(ParticleSourceFlag::IdlesOffScreen);

std::string Hex(float f) { return std::format("{:08X}", std::bit_cast<std::uint32_t>(f)); }
std::string Hex(const Vector3& v) { return Hex(v.x) + " " + Hex(v.y) + " " + Hex(v.z); }

// One case run: its header line and one line per tick; `dump` (when not null) gets every particle.
std::vector<std::string> Run(const Case& c, int index, std::ofstream* dump) {
    std::vector<std::string> lines;
    IsaacRandom seed;
    for (int i = 0; i < 256; ++i) seed.memory[i] = static_cast<std::uint32_t>(i) * 0x9E3779B9u + static_cast<std::uint32_t>(index);
    g_particleRandom = IsaacRandom();
    g_particleRandom.InitFrom(seed);

    g_clientWorld.particleManager = std::make_unique<ParticleManager>();
    g_clientWorld.particleManager->Init(&g_clientWorld);
    auto* carrier = new CStone();   // no manager: deleted at the end, as lua_check's stones
    carrier->localPosition = {12.5f, -7.25f, 3.0f};
    carrier->bounds.flags |= lib::Bits(BoundsFlag::Sphere);
    carrier->bounds.radius = 0.55f;
    carrier->bounds.center = {0.0f, 0.0f, 0.9f};

    ParticleSource* s = g_clientWorld.particleManager->CreateSource(static_cast<int>(c.type));
    if (!s) {
        lines.push_back(std::format("case {}: no source", c.name));
    } else {
        s->AddRef();
        s->localPosition = c.carried ? kStill : Vector3{3.0f, 4.0f, 0.5f};
        s->ScaleSize(c.size / s->baseSize);
        s->Attach(nullptr);
        if (c.carried) s->Attach(carrier);
        lines.push_back(std::format("case {}: texture {} frames {} columns {} {} max {} flags {:X} part {}", c.name,
                                    s->texture ? s->texture->name : "", s->frameCount, s->frameColumns,
                                    s->additiveBlend ? "additive" : "alpha", s->maxParticles, s->flags & kSourceFlags,
                                    static_cast<int>(s->attachPoint)));
        Vector3 velocity = kStill;
        for (int tick = 1; tick <= c.ticks; ++tick) {
            for (const Step& step : c.steps) {
                if (step.tick != tick) continue;
                switch (step.event) {
                case Event::Scale: s->ScaleSize(step.value); break;
                case Event::StopEmitting: s->StopEmitting(); break;
                case Event::StartEmitting: s->StartEmitting(); break;
                case Event::Stop: s->Stop(); break;
                case Event::Remove: carrier->flags |= lib::Bits(MapObjectFlag::Removed); break;
                case Event::Attach:
                    carrier->flags &= ~lib::Bits(MapObjectFlag::Removed);
                    s->Attach(carrier);
                    break;
                case Event::Velocity: velocity = step.vector; break;
                case Event::Alpha: s->alpha = step.value; break;
                case Event::Param: s->typeParam = step.value; break;
                case Event::Turn:
                    s->turnCos = lib::Cos(step.value);
                    s->turnSin = lib::Sin(step.value);
                    break;
                case Event::Extent: s->extent = step.vector; break;
                }
            }
            carrier->localPosition += velocity;
            if (!s->attachedTo) s->localPosition += velocity;
            g_draws = 0;
            const bool alive = s->RunTick();
            Hash hash;
            for (const Particle* p : s->particles) {
                hash.Add(p->pos);
                hash.Add(p->size);
                hash.Add(p->prevPos);
                hash.Add(p->prevSize);
                hash.Add(p->color);
                hash.Add(p->prevColor);
                hash.Add(static_cast<std::uint32_t>(p->frame));
                hash.Add(static_cast<std::uint32_t>(p->prevFrame));
                hash.Add(static_cast<std::uint32_t>(p->dead));
            }
            hash.Add(s->origin);
            hash.Add(s->clip.center);
            hash.Add(s->clip.boxMin);
            hash.Add(s->clip.boxMax);
            lines.push_back(std::format("{} {} flags {:X} {} particles {} draws {} hash {:016X}", tick,
                                        alive ? "alive" : "done", s->flags & kSourceFlags,
                                        s->attachedTo ? "attached" : "free", s->particles.size(), g_draws, hash.value));
            if (dump) {
                *dump << c.name << " " << lines.back() << "\n";
                *dump << "  origin " << Hex(s->origin) << " bounds " << Hex(s->clip.center) << " " << Hex(s->clip.boxMin)
                      << " " << Hex(s->clip.boxMax) << "\n";
                int i = 0;
                for (const Particle* p : s->particles)
                    *dump << std::format("  {} pos {} size {} color {:08X} frame {} prev {} {} {:08X} {} dead {}\n", i++,
                                         Hex(p->pos), Hex(p->size), p->color, p->frame, Hex(p->prevPos), Hex(p->prevSize),
                                         p->prevColor, p->prevFrame, p->dead);
            }
        }
        s->Attach(nullptr);
        s->Release();
    }
    g_clientWorld.particleManager.reset();
    delete carrier;
    return lines;
}

std::vector<std::string> Lines(std::string_view text) {
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string_view::npos) end = text.size();
        std::string_view line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.empty() && line.front() != '#') lines.emplace_back(line);
        at = end + 1;
    }
    return lines;
}

} // namespace

int Main(const std::string& record, const std::string& dump) {
    effectdata::g_effects.Reset();
    // The sources take their textures by name (no archive opened: the check reads no game data).
    g_clientWorld.textureManager = std::make_unique<TextureManager>();
    g_clientWorld.textureManager->SetContext(&g_clientWorld);
    // A terrain without sectors: the ground is 0 everywhere (the effects that spawn on the ground).
    g_clientWorld.terrain = std::make_unique<terrain::Terrain>();
    IsaacRandom::SetDrawObserver(CountDraw);
    std::ofstream dumpFile;
    if (!dump.empty()) dumpFile.open(dump, std::ios::binary);

    std::vector<std::string> output;
    const std::vector<Case>& cases = Cases();
    for (std::size_t i = 0; i < cases.size(); ++i) {
        std::vector<std::string> lines = Run(cases[i], static_cast<int>(i), dumpFile.is_open() ? &dumpFile : nullptr);
        output.insert(output.end(), lines.begin(), lines.end());
    }
    IsaacRandom::SetDrawObserver(nullptr);
    g_clientWorld.textureManager.reset();
    g_clientWorld.terrain.reset();

    if (!record.empty()) {
        std::ofstream file(record, std::ios::binary);
        file << "# ei-convert --effect-check's reference (tools/convert/effect_check.h): each case's ticks as the type's C++\n"
                "# emitter ran them. Recorded once per type, before the type moved to data; never rewritten for a type\n"
                "# already on data.\n";
        for (const std::string& line : output) file << line << "\n";
        std::printf("effect_check: %zu cases recorded into %s\n", cases.size(), record.c_str());
        return file ? 0 : 1;
    }

    const std::vector<std::string> reference =
        Lines(std::string_view(reinterpret_cast<const char*>(kReference), sizeof(kReference)));
    int failures = 0;
    std::string current;
    std::string failedCase;
    std::size_t matched = 0;
    for (std::size_t i = 0; i < output.size() || i < reference.size(); ++i) {
        const std::string& got = i < output.size() ? output[i] : std::string("(none)");
        const std::string& want = i < reference.size() ? reference[i] : std::string("(none)");
        if (got.starts_with("case ")) current = got;
        if (got == want) {
            ++matched;
            continue;
        }
        if (current != failedCase) {
            std::printf("effect_check: %s\n  reference: %s\n  this build: %s\n", current.c_str(), want.c_str(), got.c_str());
            failedCase = current;
            ++failures;
        }
    }
    std::printf("effect_check: %zu cases, %zu of %zu lines as the reference, %d cases differ\n", cases.size(), matched,
                reference.size(), failures);
    return failures ? 1 : 0;
}

}   // namespace effect_check
