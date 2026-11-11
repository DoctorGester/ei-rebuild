// The particle host without the game's world (particle_figure_host.h).
#include "engine/particles/particle_figure_host.h"

#include "engine/models/animation.h"
#include "engine/models/figure_instance.h"
#include "engine/particles/particle_source.h"

#include <algorithm>
#include <cmath>

namespace particlehost {

namespace {

constexpr Vector3 kUnscaled = {1.0f, 1.0f, 1.0f};

// The box of `figure`'s parts' points relative to its position; empty (min above max) without parts.
struct PartsBox {
    Vector3 min = {1e30f, 1e30f, 1e30f};
    Vector3 max = {-1e30f, -1e30f, -1e30f};
    bool Empty() const { return min.x > max.x; }
};

PartsBox BoxOf(const FigureInstance& figure) {
    PartsBox box;
    for (const FigureMesh* part : figure.parts) {
        const Vector3 at = figure.PartPoint(part, kUnscaled) - figure.localPosition;
        box.min = {std::min(box.min.x, at.x), std::min(box.min.y, at.y), std::min(box.min.z, at.z)};
        box.max = {std::max(box.max.x, at.x), std::max(box.max.y, at.y), std::max(box.max.z, at.z)};
    }
    return box;
}

// The point of `figure`'s first part named `prefix...`, else of its "bd" part, else its position raised by its parts'
// height.
Vector3 PartPointOf(FigureInstance& figure, const char* prefix) {
    FigureMesh* part = figure.FindPartByPrefix(prefix);
    if (!part) part = figure.FindPartByPrefix("bd");
    if (part) return figure.PartPoint(part, kUnscaled);
    const PartsBox box = BoxOf(figure);
    return figure.localPosition + Vector3{0.0f, 0.0f, box.Empty() ? 0.0f : box.max.z};
}

} // namespace

FigureHost::~FigureHost() = default;

float FigureHost::GroundHeight(float, float) { return groundHeight; }

float FigureHost::PlatformPlane(float, float, int which) {
    switch (which) {
    case 3: return 1.0f;
    case 4: return groundHeight;
    default: return 0.0f;
    }
}

Wind FigureHost::CurrentWind() { return wind; }

float FigureHost::TickFraction() { return tickFraction; }

void FigureHost::AddScorchMark(float, float, float, float, float, float) {}

void FigureHost::ShakeCamera(const Vector3&, float, float, float) {}

FigureHost::Carried* FigureHost::Find(const FigureInstance& figure) {
    for (Carried& entry : carried)
        if (entry.figure == &figure) return &entry;
    return nullptr;
}

const FigureHost::Carried* FigureHost::Find(const FigureInstance& figure) const {
    for (const Carried& entry : carried)
        if (entry.figure == &figure) return &entry;
    return nullptr;
}

void FigureHost::Follow(FigureInstance& carrier, ParticleSource* source) {
    Carried* entry = Find(carrier);
    if (!entry) entry = &carried.emplace_back(Carried{&carrier});
    if (std::ranges::find(entry->sources, source, &Ref<ParticleSource>::Get) == entry->sources.end())
        entry->sources.emplace_back(source);
}

void FigureHost::LetGo(FigureInstance& carrier, ParticleSource* source) {
    Carried* entry = Find(carrier);
    if (!entry) return;
    auto node = std::ranges::find(entry->sources, source, &Ref<ParticleSource>::Get);
    if (node == entry->sources.end()) return;
    HeldRef<ParticleSource> held = std::move(*node);   // released after the detach
    entry->sources.erase(node);
    source->Attach(nullptr);
}

bool FigureHost::Removed(const FigureInstance& carrier) {
    const Carried* entry = Find(carrier);
    return entry && entry->removed;
}

bool FigureHost::Hidden(FigureInstance&) { return false; }

Vector3 FigureHost::PartPoint(FigureInstance& carrier, AttachPoint part) {
    if (!namedPoint.empty()) {
        if (const std::optional<Vector3> at = NamedPointOf(carrier, namedPoint)) return *at;
        return PartPointOf(carrier, "bd");
    }
    if (part == AttachPoint::EffectPoint) return PartPointOf(carrier, "rh3");
    Vector3 at = PartPointOf(carrier, AttachPointPart(part));
    if (part == AttachPoint::AboveHead) at.z = at.z + 1.0f;
    return at;
}

Measure FigureHost::MeasureCarrier(FigureInstance& carrier) {
    const PartsBox box = BoxOf(carrier);
    if (box.Empty()) return {{0.0f, 0.0f, 0.0f}, 0.1f};
    return {{0.0f, 0.0f, (box.min.z + box.max.z) * 0.5f}, std::sqrt(LengthSquared(box.max - box.min)) * 0.5f};
}

CarrierState FigureHost::DescribeCarrier(FigureInstance& carrier) {
    CarrierState state;
    state.boundsRadius = MeasureCarrier(carrier).radius;
    state.unit = unit;
    state.running = running;
    state.dead = dead;
    state.unitRadius = unitRadius;
    state.leftFoot = PartPointOf(carrier, "ll3");
    state.rightFoot = PartPointOf(carrier, "rl3");
    return state;
}

void FigureHost::Remove(const FigureInstance& figure) {
    if (Carried* entry = Find(figure)) entry->removed = true;
}

void FigureHost::Restore(const FigureInstance& figure) {
    if (Carried* entry = Find(figure)) entry->removed = false;
}

void FigureHost::LetGoAll(FigureInstance& figure) {
    while (Carried* entry = Find(figure)) {
        if (entry->sources.empty()) {
            carried.erase(carried.begin() + (entry - carried.data()));
            return;
        }
        LetGo(figure, entry->sources.front().Get());
    }
}

std::optional<Vector3> FigureHost::NamedPointOf(FigureInstance& figure, std::string_view name) {
    if (figure.animation)
        if (const FigureBone* bone = figure.animation->FindBone(name))
            return figure.localRotation.Rotate(bone->position) + figure.localPosition;
    if (const FigureMesh* part = figure.FindPart(name)) return figure.PartPoint(part, kUnscaled);
    return std::nullopt;
}

std::vector<ParticleSource*> FigureHost::SourcesOn(const FigureInstance& figure) const {
    std::vector<ParticleSource*> sources;
    if (const Carried* entry = Find(figure))
        for (const HeldRef<ParticleSource>& source : entry->sources) sources.push_back(source.Get());
    return sources;
}

} // namespace particlehost
