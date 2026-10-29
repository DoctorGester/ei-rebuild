// The landscape's ambient sounds: every frame with LandscapeFlag::Ambience (Terrain::RunTick) the 3x3 sectors
// around the sound listener choose up to three sound classes, which keep looping "circle" sounds and now and
// then play a positioned one-shot. The time of day for the one-shots' folder comes from the client world
// (CWorldClient::AmbienceTimeOfDay).
#include "terrain.h"
#include "terrain_sector.h"
#include "game_singletons.h"
#include "random.h"
#include "sound.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "lib/str.h"
#include "lib/vecmath.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <string>

using terrain::kSectorCells;

namespace {

// A sound class (indexed by the sectors' texture classes).
struct AmbientSoundClass {
    const char* name;       // the folder ("Grass", "water", ...)
    int variants;           // one-shots to pick from (0: none); counted per map (CountAmbientVariants)
    float weight;           // per texture of the class in a sector
    int countsAs;           // the class whose vote it adds to, -1 its own
    float panLimit;
    int chance;             // percent per frame for a one-shot
};
constexpr int kClasses = 16;
AmbientSoundClass g_soundClasses[kClasses] = /*007969D0*/ {
    {"Grass", -1, 1.0f, -1, 30.0f, 6},
    {"Ground", -1, 1.0f, -1, 30.0f, 6},
    {"Stone", -1, 1.0f, -1, 30.0f, 6},
    {"sand", -1, 1.0f, -1, 30.0f, 6},
    {"rock", -1, 1.5f, -1, 100.0f, 4},
    {"field", -1, 1.0f, -1, 30.0f, 6},
    {"water", -1, 3.0f, -1, 100.0f, 6},
    {"road", -1, 3.0f, -1, 30.0f, 6},
    {"", 0, 0.0f, -1, 100.0f, 0},
    {"snow", -1, 1.0f, -1, 30.0f, 6},
    {"Ice", -1, 3.0f, -1, 30.0f, 6},
    {"DryGrass", -1, 1.0f, -1, 30.0f, 6},
    {"SnowBall", -1, 1.0f, -1, 30.0f, 6},
    {"lava", -1, 3.0f, -1, 100.0f, 0},
    {"swamp", -1, 3.0f, -1, 100.0f, 7},
    {"rock", -1, 1.5f, -1, 100.0f, 4},
};

// One of the 3x3 sectors around the listener.
struct Cell {
    int soundClass;         // -1 where the sector is missing
    float weight;           // 100 at the listener .. 0 at the radius
    float x, y, z;          // the sector's centre; z the higher of ground and water
    Vector3 offset;         // from the listener
};

// A sound class's votes, sorted by weight.
struct ClassVotes {
    int soundClass;
    Vector3 offset;         // the last voting cell's offset, then the weighted offsets added
    float weightSum;        // sum of the cells' squared weights
    int volume;
    int pan;
    int count;
    int sound;
};

constexpr float kRadiusCells = 48.0f;
constexpr float kSpotCells = 16.0f;         // a one-shot lands within +-16 cells of its sector's offset
constexpr float kMinWeight = 0.001f;
constexpr float kPercent = 100.0f;
constexpr double kTwoToMinus32 = 2.3283064370807974e-10;
constexpr int kMaxVolume = 100;

constexpr float kHalfPi = /*007C8930*/ 1.5707964f;

// The sector containing `v` (v rounded to nearest, then / 32 towards zero).
int SectorOf(float v) { return x87::fistp(v) / kSectorCells; }

// By weight, highest first (an unordered pair sorts "after").
int ByWeight(const void* a, const void* b) {
    float wa = static_cast<const ClassVotes*>(a)->weightSum;
    float wb = static_cast<const ClassVotes*>(b)->weightSum;
    if (wa > wb) return -1;
    if (wa == wb) return 0;
    return 1;
}

// The pan for a class's summed offset: 0 straight ahead, up to +-100 at the sides (behind
// mirrors in front), negative on the left.
float PanOf(const Vector3& offset, const Vector3& axis) {
    Vector2 flat = {offset.x, offset.y};
    Vector2 forward = {axis.x, axis.y};
    float length = Length2D(flat);
    float along = Dot2D(forward, flat);
    if (along > length) return 0.0f;
    float c = static_cast<float>(along / length);
    if (c > 1.0f) c = 1.0f;
    else c = c >= -1.0f ? c : -1.0f;   // -1 also for NaN
    double angle = libcrt::CiAcos(c);
    if (angle > kHalfPi) angle = kPi - angle;
    angle = kPercent / kHalfPi * angle;
    if (axis.x * offset.y > axis.y * offset.x) angle = -angle;
    return static_cast<float>(angle);
}
} // namespace

void Terrain::PlayAmbience() {
    CSoundManager* sounds = g_clientWorld.sounds_;
    Vector3 listener = {sounds->listener.x, sounds->listener.y, sounds->listener.z};
    Vector3 axis = {sounds->listenerAxis.x, sounds->listenerAxis.y, sounds->listenerAxis.z};
    int centreX = SectorOf(listener.x);
    int centreY = SectorOf(listener.y);
    float radius = kRadiusCells;
    float radiusSquared = static_cast<float>(radius * radius);

    // Each sector votes for the class with the most weighted textures (the first of equals).
    ClassVotes votes[kClasses];
    for (int i = 0; i < kClasses; ++i) {
        votes[i].soundClass = i;
        votes[i].weightSum = 0.0f;
        votes[i].count = 0;
    }
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
            cell.x = static_cast<float>((sx + 0.5f) * kSectorCells);
            cell.y = static_cast<float>((sy + 0.5f) * kSectorCells);
            float ground = HeightAt(cell.x, cell.y, 0);
            float water = HeightAt(cell.x, cell.y, 1);
            cell.z = water >= ground ? water : ground;
            cell.offset = {static_cast<float>(cell.x - listener.x), static_cast<float>(cell.y - listener.y),
                           static_cast<float>(cell.z - listener.z)};
            float weights[kClasses] = {};
            for (int i = 0; i < kClasses; ++i) {
                int target = g_soundClasses[i].countsAs < 0 ? i : g_soundClasses[i].countsAs;
                weights[target] = static_cast<float>(sector->classCounts[i] * g_soundClasses[i].weight + weights[target]);
            }
            float best = -1.0f;
            for (int i = 0; i < kClasses; ++i) {
                EI_NAN_AUDIT(weights[i]);
                if (best < weights[i]) {
                    best = weights[i];
                    cell.soundClass = i;
                }
            }
            float distance = static_cast<float>(LengthSquared2D(Vector2{cell.offset.x, cell.offset.y}));
            float within = distance >= radiusSquared ? radiusSquared : distance;
            cell.weight = static_cast<float>((radiusSquared - within) / radiusSquared * kPercent);
            ClassVotes& vote = votes[cell.soundClass];
            vote.weightSum = static_cast<float>(cell.weight * cell.weight + vote.weightSum);
            vote.offset = cell.offset;
            ++vote.count;
        }
    }
    libcrt::Qsort(votes, kClasses, sizeof(ClassVotes), ByWeight);

    int heard = 0;
    for (; heard < kAmbientLoops && votes[heard].weightSum > kMinWeight; ++heard) {   // a NaN weight ends it
        ClassVotes& vote = votes[heard];
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const Cell& cell = cells[column][row];
                if (cell.soundClass != vote.soundClass) continue;
                float dx = static_cast<float>(cell.weight * cell.offset.x);
                float dy = static_cast<float>(cell.weight * cell.offset.y);
                double dz = cell.weight * cell.offset.z;
                vote.offset.x = static_cast<float>(dx + vote.offset.x);
                vote.offset.y = static_cast<float>(dy + vote.offset.y);
                vote.offset.z = static_cast<float>(dz + vote.offset.z);
            }
        }
        vote.volume = x87::fistp(static_cast<float>(std::sqrt(vote.weightSum)));
        if (vote.volume > kMaxVolume) vote.volume = kMaxVolume;
        const AmbientSoundClass& soundClass = g_soundClasses[vote.soundClass];
        float pan = PanOf(vote.offset, axis);
        float limit = soundClass.panLimit;
        float low = static_cast<float>(-limit);
        pan = pan >= low ? pan : low;
        pan = limit >= pan ? pan : limit;
        vote.pan = x87::fistp(pan);

        // A one-shot now and then, in a random sector of the class (3x3 draws until one matches).
        if (static_cast<int>(g_random.Next() % 100 + 1) > soundClass.chance) continue;
        const Cell* pick;
        do {
            std::uint32_t column = g_random.Next() % 3;
            std::uint32_t row = g_random.Next() % 3;
            pick = &cells[column][row];
        } while (pick->soundClass != vote.soundClass);
        if (soundClass.variants == 0) continue;
        std::uint32_t variant = g_random.Next();
        const char* time = g_clientWorld.AmbienceTimeOfDay();
        const std::string name = std::format("ambient\\{}\\{}\\{}\\{}.wav", ambientSet, soundClass.name, time,
                                             variant % static_cast<std::uint32_t>(soundClass.variants) + 1);
        Vector3 position = listener + pick->offset;
        float spot = kSpotCells;
        float spotLow = static_cast<float>(-spot);
        position.x = static_cast<float>((spot - spotLow) * x87::FromUnsigned(g_random.Next()) * kTwoToMinus32 + spotLow + position.x);
        position.y = static_cast<float>((spot - spotLow) * x87::FromUnsigned(g_random.Next()) * kTwoToMinus32 + spotLow + position.y);
        sounds->Play3D(name, 1, &position.x, static_cast<float>(spot + spot), radius, 0);
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
            const std::string name = std::format("ambient\\{}\\{}\\circle.wav", ambientSet, g_soundClasses[vote.soundClass].name);
            vote.sound = sounds->Play2D(name, 1000, vote.volume, vote.pan, 1, 1, 0, 0);
        }
    }
    for (AmbientLoop& loop : ambientLoops) {
        if (loop.soundClass < 0) continue;
        loop.sound = sounds->StopSound(loop.sound);
        loop.soundClass = -1;
    }
    for (int i = 0; i < heard; ++i) ambientLoops[i] = {votes[i].sound, votes[i].soundClass};
}

void Terrain::StopAmbience() {
    landscapeFlags &= ~LandscapeFlag::Ambience;
    StopAmbientLoops();
}

void Terrain::StopAmbientLoops() {
    for (AmbientLoop& loop : ambientLoops) {
        loop.sound = g_clientWorld.sounds_->StopSound(loop.sound);
        loop.soundClass = -1;
    }
}

// The map loader's part: a class that votes for another keeps its count.
void terrain::CountAmbientVariants(const char* set) {
    for (AmbientSoundClass& soundClass : g_soundClasses) {
        if (soundClass.countsAs >= 0) continue;
        soundClass.variants = 0;
        for (;;) {
            char name[260];
            std::snprintf(name, sizeof(name), "ambient\\%s\\%s\\day\\%i.wav", set, soundClass.name, soundClass.variants + 1);
            if (g_clientWorld.sounds_->LoadWaveFile(name, nullptr, nullptr) == nullptr) break;
            ++soundClass.variants;
        }
    }
}
