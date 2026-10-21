// The landscape's ambient sounds (006CB360-006CBD0A): every frame with flag 8 (Terrain::RunTick)
// the 3x3 sectors around the sound listener choose up to three sound classes, which keep looping
// "circle" sounds and now and then play a positioned one-shot. The time of day for the one-shots'
// folder comes from the client world (CWorldClient::AmbienceTimeOfDay).
#include "terrain.h"
#include "terrain_sector.h"
#include "random.h"
#include "sound.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "lib/vecmath.h"
#include "originals_library.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <cmath>
#include <cstdint>
#include <new>

namespace {

// A sound class (16 entries at 007969D0, writable data; indexed by the sectors' texture classes).
struct AmbientSoundClass {
    const char* name;       // +0x00 the folder ("Grass", "water", ...)
    int variants;           // +0x04 one-shots to pick from (0: none)
    float weight;           // +0x08 per texture of the class in a sector
    int countsAs;           // +0x0C the class whose vote it adds to, -1 its own
    float panLimit;         // +0x10
    int chance;             // +0x14 percent per frame for a one-shot
};
static_assert(sizeof(AmbientSoundClass) == 0x18);
const AmbientSoundClass* SoundClasses() { return ei::GamePointer<const AmbientSoundClass>(0x007969D0); }
constexpr int kClasses = 16;

// One of the 3x3 sectors around the listener.
struct Cell {
    int soundClass;         // -1 where the sector is missing
    float weight;           // 100 at the listener .. 0 at the radius
    float x, y, z;          // the sector's centre; z the higher of ground and water
    Vector3 offset;         // from the listener
};

// A sound class's votes, sorted by weight (qsort on 0x24-byte records, as the original).
struct ClassVotes {
    int soundClass;
    Vector3 offset;         // the last voting cell's offset, then the weighted offsets added
    float weightSum;        // sum of the cells' squared weights
    int volume;
    int pan;
    int count;
    int sound;
};
static_assert(sizeof(ClassVotes) == 0x24);

constexpr float kRadiusCells = 48.0f;       // 0074420C
constexpr float kSectorCells = 32.0f;       // 0073CA90
constexpr float kSpotCells = 16.0f;         // 0073CA98: a one-shot lands within +-16 cells of its sector's offset
constexpr float kMinWeight = 0.001f;        // 0073CCF4
constexpr float kPercent = 100.0f;          // 0073BAB0
constexpr float kPi = 3.14159274f;          // 007441C8
constexpr double kTwoToMinus32 = 2.3283064370807974e-10;   // 0073C468

float CellSize() { return 1.0f /*007441D0*/; }
// pi / 2 (007C8930, a static initialiser from 007441C8 * 0.5).
constexpr float kHalfPi = /*007C8930*/ 1.5707964f;   // a static initialiser: pi * 0.5

// The sector containing `v` (FISTP of v / cell, then / 32 towards zero).
int SectorOf(float v) { return x87::fistp(static_cast<float>(v / CellSize())) / 32; }

// 006CB360: by weight, highest first (an unordered pair sorts "after").
int __cdecl ByWeight(const void* a, const void* b) {
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
    double angle = Acos(c);
    if (angle > kHalfPi) angle = kPi - angle;
    angle = kPercent / kHalfPi * angle;
    if (axis.x * offset.y > axis.y * offset.x) angle = -angle;
    return static_cast<float>(angle);
}

} // namespace

mfc::CString* CWorldClient::AmbienceTimeOfDay(mfc::CString* out) {
    float hour = static_cast<float>(std::fmod(static_cast<double>(tick_) * secondsPerTick_ + timeBase_, 24.0));
    const char* name;
    if (hour < 6.0f || hour > 18.0f)
        name = hour < 22.0f && hour > 2.0f ? (hour < 6.0f ? "morning" : "evening") : "night";
    else
        name = "day";   // also NaN
    return new (out) mfc::CString(name);
}

void Terrain::PlayAmbience() {
    CSoundManager* sounds = TheSoundManager();
    Vector3 listener = {sounds->listener.x, sounds->listener.y, sounds->listener.z};
    Vector3 axis = {sounds->listenerAxis.x, sounds->listenerAxis.y, sounds->listenerAxis.z};
    int centreX = SectorOf(listener.x);
    int centreY = SectorOf(listener.y);
    float radius = static_cast<float>(CellSize() * kRadiusCells);
    float radiusSquared = static_cast<float>(radius * radius);
    const AmbientSoundClass* classes = SoundClasses();

    // Each sector votes for the class with the most weighted textures (the first of equals).
    ClassVotes votes[kClasses];
    for (int i = 0; i < kClasses; ++i) {
        votes[i].soundClass = i;
        votes[i].weightSum = 0.0f;
        votes[i].count = 0;
    }
    Cell cells[9];   // [column * 3 + row]
    for (int row = 0; row < 3; ++row) {
        int sy = centreY + row - 1;
        for (int column = 0; column < 3; ++column) {
            int sx = centreX + column - 1;
            Cell& cell = cells[column * 3 + row];
            auto* sector = SectorAt(sx, sy, 2, 1);
            if (!sector) {
                cell.soundClass = -1;
                cell.weight = 0.0f;
                continue;
            }
            cell.x = static_cast<float>((sx + 0.5f) * CellSize() * kSectorCells);
            cell.y = static_cast<float>((sy + 0.5f) * CellSize() * kSectorCells);
            float ground = HeightAt(cell.x, cell.y, 0);
            float water = HeightAt(cell.x, cell.y, 1);
            cell.z = water >= ground ? water : ground;
            cell.offset = {static_cast<float>(cell.x - listener.x), static_cast<float>(cell.y - listener.y),
                           static_cast<float>(cell.z - listener.z)};
            float weights[kClasses] = {};
            for (int i = 0; i < kClasses; ++i) {
                int target = classes[i].countsAs < 0 ? i : classes[i].countsAs;
                weights[target] = static_cast<float>(sector->classCounts[i] * classes[i].weight + weights[target]);
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

    IsaacRandom& random = IsaacRandom::Global();
    int heard = 0;
    for (; heard < 3 && votes[heard].weightSum > kMinWeight; ++heard) {   // a NaN weight ends it
        ClassVotes& vote = votes[heard];
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const Cell& cell = cells[column * 3 + row];
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
        if (vote.volume > 100) vote.volume = 100;
        const AmbientSoundClass& kind = classes[vote.soundClass];
        float pan = PanOf(vote.offset, axis);
        float limit = kind.panLimit;
        float low = static_cast<float>(-limit);
        pan = pan >= low ? pan : low;
        pan = limit >= pan ? pan : limit;
        vote.pan = x87::fistp(pan);

        // A one-shot now and then, in a random sector of the class (3x3 draws until one matches).
        if (static_cast<int>(random.Next() % 100 + 1) > kind.chance) continue;
        int pick;
        do {
            std::uint32_t column = random.Next() % 3;
            std::uint32_t row = random.Next() % 3;
            pick = static_cast<int>(column * 3 + row);
        } while (cells[pick].soundClass != vote.soundClass);
        if (kind.variants == 0) continue;
        std::uint32_t variant = random.Next();
        char name[256];
        {
            mfc::CString time;   // the empty string, replaced (the original passes raw stack storage)
            TheClientWorld()->AmbienceTimeOfDay(&time);
            gamecrt::sprintf(name, "ambient\\%s\\%s\\%s\\%i.wav", static_cast<const char*>(textAE0), kind.name, static_cast<const char*>(time),
                             variant % static_cast<std::uint32_t>(kind.variants) + 1);
        }
        Vector3 position = listener + cells[pick].offset;
        float half = static_cast<float>(CellSize() * kSpotCells);
        float low2 = static_cast<float>(-half);
        position.x = static_cast<float>((half - low2) * x87::FromUnsigned(random.Next()) * kTwoToMinus32 + low2 + position.x);
        position.y = static_cast<float>((half - low2) * x87::FromUnsigned(random.Next()) * kTwoToMinus32 + low2 + position.y);
        sounds->Play3D(name, 1, &position.x, static_cast<float>(half + half), radius, 0, 0);
    }

    // The loops: a class already playing keeps its sound (volume and pan updated), a new one starts.
    for (int i = 0; i < heard; ++i) {
        ClassVotes& vote = votes[i];
        int slot = 0;
        while (slot < 3 && ambientClasses[slot] != vote.soundClass) ++slot;
        if (slot < 3) {
            ambientClasses[slot] = -1;
            vote.sound = ambientSounds[slot];
            sounds->SetSoundVolume(vote.sound, vote.volume, vote.pan);
        } else {
            char name[256];
            gamecrt::sprintf(name, "ambient\\%s\\%s\\circle.wav", static_cast<const char*>(textAE0), classes[vote.soundClass].name);
            vote.sound = sounds->Play2D(name, 1000, vote.volume, vote.pan, 1, 1, 0, 0);
        }
    }
    for (int slot = 0; slot < 3; ++slot) {
        if (ambientClasses[slot] < 0) continue;
        ambientSounds[slot] = sounds->StopSound(ambientSounds[slot]);
        ambientClasses[slot] = -1;
    }
    for (int i = 0; i < heard; ++i) {
        ambientSounds[i] = votes[i].sound;
        ambientClasses[i] = votes[i].soundClass;
    }
}

void Terrain::StopAmbience() {
    landscapeFlags &= ~8u;
    for (int slot = 0; slot < 3; ++slot) {
        ambientSounds[slot] = TheSoundManager()->StopSound(ambientSounds[slot]);
        ambientClasses[slot] = -1;
    }
}

// The map loader's part (006C68B0, A-H26). The table is the game's own writable data; a class
// that votes for another keeps its count.
void terrain::CountAmbientVariants(const char* set) {
    auto* classes = const_cast<AmbientSoundClass*>(SoundClasses());
    for (int i = 0; i < kClasses; ++i) {
        AmbientSoundClass& kind = classes[i];
        if (kind.countsAs >= 0) continue;
        kind.variants = 0;
        for (;;) {
            char name[260];
            gamecrt::sprintf(name, "ambient\\%s\\%s\\day\\%i.wav", set, kind.name, kind.variants + 1);
            if (TheSoundManager()->LoadWaveFile(name, nullptr, nullptr) == nullptr) break;
            ++kind.variants;
        }
    }
}
int terrain::AmbientVariants(int soundClass) { return SoundClasses()[soundClass].variants; }

EI_REPLACE_METHOD(0x006CB390, Terrain::PlayAmbience);
EI_REPLACE_METHOD(0x006CBC00, CWorldClient::AmbienceTimeOfDay);
