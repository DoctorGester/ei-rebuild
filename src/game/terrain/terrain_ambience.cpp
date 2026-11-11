// The landscape's ambient sounds: every frame with LandscapeFlag::Ambience (Terrain::RunTick) the 3x3 sectors
// around the sound listener choose up to three sound classes, which keep looping "circle" sounds and now and
// then play a positioned one-shot. The time of day for the one-shots' folder comes from the client world
// (WorldClient::AmbienceTimeOfDay).
#include "game/terrain/terrain.h"
#include "game/terrain/terrain_sector.h"
#include "game/app/singletons.h"
#include "engine/core/random.h"
#include "game/sound/sound.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "lib/float_select.h"
#include "lib/maths.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <string>

namespace terrain {
namespace {

// A sound class (indexed by the sectors' texture classes). Its one-shots are counted per map
// (Terrain::ambientVariants).
struct AmbientSoundClass {
    const char* name = nullptr;  // the folder ("Grass", "water", ...)
    float weight = 0.0f;    // per texture of the class in a sector
    float panLimit = 0.0f;
    int chance = 0;         // percent per frame for a one-shot
};
constexpr int kClasses = Terrain::kSoundClasses;
constexpr AmbientSoundClass kAmbientSounds[kClasses] = {
    {"Grass", 1.0f, 30.0f, 6},
    {"Ground", 1.0f, 30.0f, 6},
    {"Stone", 1.0f, 30.0f, 6},
    {"sand", 1.0f, 30.0f, 6},
    {"rock", 1.5f, 100.0f, 4},
    {"field", 1.0f, 30.0f, 6},
    {"water", 3.0f, 100.0f, 6},
    {"road", 3.0f, 30.0f, 6},
    {"", 0.0f, 100.0f, 0},
    {"snow", 1.0f, 30.0f, 6},
    {"Ice", 3.0f, 30.0f, 6},
    {"DryGrass", 1.0f, 30.0f, 6},
    {"SnowBall", 1.0f, 30.0f, 6},
    {"lava", 3.0f, 100.0f, 0},
    {"swamp", 3.0f, 100.0f, 7},
    {"rock", 1.5f, 100.0f, 4},
};

// One of the 3x3 sectors around the listener.
struct Cell {
    int soundClass = 0;     // -1 where the sector is missing
    float weight = 0.0f;    // 100 at the listener .. 0 at the radius
    Vector3 centre;         // the sector's centre; z the higher of ground and water
    Vector3 offset;         // from the listener
};

// A sound class's votes, sorted by weight.
struct ClassVotes {
    int soundClass = 0;
    Vector3 offset;         // the last voting cell's offset, then the weighted offsets added
    float weightSum = 0.0f;  // sum of the cells' squared weights
    int volume = 0;
    int pan = 0;
    int sound = 0;
};

constexpr float kRadiusCells = 48.0f;
constexpr float kSpotCells = 16.0f;         // a one-shot lands within +-16 cells of its sector's offset
constexpr float kMinWeight = 0.001f;
constexpr float kPercent = 100.0f;
constexpr int kMaxVolume = 100;

// The sector containing `v` (v rounded to nearest, then / 32 towards zero).
int SectorOf(float v) { return std::lround(v) / kSectorCells; }

// A loop without a sound has nothing to stop (a terrain that never played, as ei-convert --effect-check's).
void StopLoop(Terrain::AmbientLoop& loop) {
    if (loop.sound != kNoSound) g_clientWorld.sounds->StopSound(loop.sound);
    loop.sound = kNoSound;
    loop.soundClass = -1;
}

// By weight, highest first (an unordered pair sorts "after").
int ByWeight(const ClassVotes& a, const ClassVotes& b) {
    float wa = a.weightSum;
    float wb = b.weightSum;
    if (wa > wb) return -1;
    if (wa == wb) return 0;
    return 1;
}

// The pan for a class's summed offset: 0 straight ahead, up to +-100 at the sides (behind
// mirrors in front), negative on the left.
float PanOf(const Vector3& offset, const Vector3& axis) {
    Vector2 flat = XY(offset);
    Vector2 forward = XY(axis);
    float length = Length2D(flat);
    float along = Dot2D(forward, flat);
    if (along > length) return 0.0f;
    float c = along / length;
    if (c > 1.0f) c = 1.0f;
    else c = MaxKeepB(c, -1.0f);   // -1 also for NaN
    float angle = lib::Acos(c);
    if (angle > kHalfPi) angle = kPi - angle;
    angle = kPercent / kHalfPi * angle;
    if (axis.x * offset.y > axis.y * offset.x) angle = -angle;
    return angle;
}

// Now and then (the class's chance) a one-shot of sound class `soundClass` (of `variants`) in a random sector
// of the class (3x3 draws until one matches), somewhere within kSpotCells of that sector's offset.
void PlayOneShot(SoundManager* sounds, const Cell (&cells)[3][3], int soundClass, int variants, const std::string& set,
                 const Vector3& listener) {
    const AmbientSoundClass& sound = kAmbientSounds[soundClass];
    if (static_cast<int>(g_clientRandom.Next() % 100 + 1) > sound.chance) return;
    const Cell* pick;
    do {
        std::uint32_t column = g_clientRandom.Next() % 3;
        std::uint32_t row = g_clientRandom.Next() % 3;
        pick = &cells[column][row];
    } while (pick->soundClass != soundClass);
    if (variants == 0) return;
    std::uint32_t variant = g_clientRandom.Next();
    const char* time = g_clientWorld.AmbienceTimeOfDay();
    const std::string name = std::format("ambient\\{}\\{}\\{}\\{}.wav", set, sound.name, time,
                                         variant % static_cast<std::uint32_t>(variants) + 1);
    Vector3 position = listener + pick->offset;
    position.x = g_clientRandom.Range(-kSpotCells, kSpotCells) + position.x;
    position.y = g_clientRandom.Range(-kSpotCells, kSpotCells) + position.y;
    sounds->Play3D(name, 1, position, kSpotCells + kSpotCells, kRadiusCells, false);
}

} // namespace

void Terrain::PlayAmbience() {
    SoundManager* sounds = g_clientWorld.sounds.get();
    Vector3 listener = sounds->listener;
    Vector3 axis = sounds->listenerAxis;
    int centreX = SectorOf(listener.x);
    int centreY = SectorOf(listener.y);
    const float radiusSquared = kRadiusCells * kRadiusCells;

    // Each sector votes for the class with the most weighted textures (the first of equals).
    ClassVotes votes[kClasses];
    for (int i = 0; i < kClasses; ++i) votes[i].soundClass = i;
    Cell cells[3][3];   // [column][row]
    for (int row = 0; row < 3; ++row) {
        int sy = centreY + row - 1;
        for (int column = 0; column < 3; ++column) {
            int sx = centreX + column - 1;
            Cell& cell = cells[column][row];
            auto* sector = SectorAt(sx, sy, SectorStage::Built, true);
            if (!sector) {
                cell.soundClass = -1;
                cell.weight = 0.0f;
                continue;
            }
            cell.centre.x = (sx + 0.5f) * kSectorCells;
            cell.centre.y = (sy + 0.5f) * kSectorCells;
            float ground = HeightAt(cell.centre.x, cell.centre.y, Layer::Ground);
            float water = HeightAt(cell.centre.x, cell.centre.y, Layer::Water);
            cell.centre.z = MaxKeepB(water, ground);
            cell.offset = cell.centre - listener;
            float weights[kClasses];
            for (int i = 0; i < kClasses; ++i) weights[i] = sector->classCounts[i] * kAmbientSounds[i].weight;
            float best = -1.0f;
            for (int i = 0; i < kClasses; ++i) {
                if (best < weights[i]) {
                    best = weights[i];
                    cell.soundClass = i;
                }
            }
            float distance = LengthSquared2D(XY(cell.offset));
            float within = MinKeepA(distance, radiusSquared);
            cell.weight = (radiusSquared - within) / radiusSquared * kPercent;
            ClassVotes& vote = votes[cell.soundClass];
            vote.weightSum = cell.weight * cell.weight + vote.weightSum;
            vote.offset = cell.offset;
        }
    }
    libcrt::Qsort(votes, kClasses, ByWeight);

    int heard = 0;
    for (; heard < kAmbientLoops && votes[heard].weightSum > kMinWeight; ++heard) {   // a NaN weight ends it
        ClassVotes& vote = votes[heard];
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const Cell& cell = cells[column][row];
                if (cell.soundClass != vote.soundClass) continue;
                vote.offset = cell.weight * cell.offset + vote.offset;
            }
        }
        vote.volume = std::lround(std::sqrt(vote.weightSum));
        if (vote.volume > kMaxVolume) vote.volume = kMaxVolume;
        float pan = PanOf(vote.offset, axis);
        float limit = kAmbientSounds[vote.soundClass].panLimit;
        float low = -limit;
        pan = MaxKeepB(pan, low);
        pan = MinKeepA(limit, pan);
        vote.pan = std::lround(pan);
        PlayOneShot(sounds, cells, vote.soundClass, ambientVariants[vote.soundClass], ambientSet, listener);
    }

    // The loops: a class already playing keeps its sound (volume and pan updated), a new one starts.
    for (int i = 0; i < heard; ++i) {
        ClassVotes& vote = votes[i];
        int slot = 0;
        while (slot < kAmbientLoops && ambientLoops[slot].soundClass != vote.soundClass) ++slot;
        if (slot < kAmbientLoops) {
            ambientLoops[slot].soundClass = -1;
            vote.sound = ambientLoops[slot].sound;
            sounds->SetSoundVolume(vote.sound, vote.volume, vote.pan);
        } else {
            const std::string name = std::format("ambient\\{}\\{}\\circle.wav", ambientSet, kAmbientSounds[vote.soundClass].name);
            vote.sound = sounds->Play2D(name, 1000, vote.volume, vote.pan, {.loop = true, .heightFade = true});
        }
    }
    for (AmbientLoop& loop : ambientLoops) {
        if (loop.soundClass >= 0) StopLoop(loop);
    }
    for (int i = 0; i < heard; ++i) ambientLoops[i] = {votes[i].sound, votes[i].soundClass};
}

void Terrain::StopAmbience() {
    landscapeFlags &= ~LandscapeFlag::Ambience;
    StopAmbientLoops();
}

void Terrain::StopAmbientLoops() {
    for (AmbientLoop& loop : ambientLoops) StopLoop(loop);
}

void Terrain::CountAmbientVariants() {
    for (int i = 0; i < kClasses; ++i) {
        int& variants = ambientVariants[i];
        variants = 0;
        for (;;) {
            const std::string name =
                std::format("ambient\\{}\\{}\\day\\{}.wav", ambientSet, kAmbientSounds[i].name, variants + 1);
            if (!g_clientWorld.sounds->HasWaveFile(name)) break;
            ++variants;
        }
    }
}

} // namespace terrain
