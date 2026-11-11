#pragma once

// A particle host without the game's world (particle_host.h): a world of its own for a program that shows effects
// alone (the effect viewer's exe; effect_check's world-less case). See docs/notes/effects.md, "The host".

#include "engine/core/ref.h"
#include "engine/particles/particle_host.h"

#include <vector>

namespace particlehost {

// The ground flat at `groundHeight` (no water, no platforms), `wind` (none until set), its own tick fraction; scorch
// marks and shakes are dropped. Its carriers are figures alone (FigureInstance): its own list holds each carried figure
// and the sources on it (a reference each, as a map object's effect sources). A figure counts as a unit standing on its
// "ll3" / "rl3" parts (`carrier` says what it is), its sources' parts are its parts by name (FigureInstance's), its
// bounds those of its parts' points.
class FigureHost final : public Host {
public:
    ~FigureHost() override;

    float GroundHeight(float x, float y) override;
    float PlatformPlane(float x, float y, int which) override;
    Wind CurrentWind() override;
    float TickFraction() override;
    void AddScorchMark(float x, float y, float radiusX, float radiusY, float angle, float alpha) override;
    void ShakeCamera(const Vector3& source, float duration, float amplitude, float decay) override;

    void Follow(FigureInstance& carrier, ParticleSource* source) override;
    void LetGo(FigureInstance& carrier, ParticleSource* source) override;
    bool Removed(const FigureInstance& carrier) override;
    bool Hidden(FigureInstance& carrier) override;
    // A part by its name (AttachPointPart; "bd" when the figure lacks it, else the figure's position raised by its
    // parts' height); EffectPoint the right hand ("rh3", else as a missing part); AboveHead 1 higher.
    Vector3 PartPoint(FigureInstance& carrier, AttachPoint part) override;
    Measure MeasureCarrier(FigureInstance& carrier) override;
    CarrierState DescribeCarrier(FigureInstance& carrier) override;

    // `figure` leaves the world: its sources let go at their next tick (Removed), as a map object's do. Back with
    // Restore (and a source's Attach).
    void Remove(const FigureInstance& figure);
    void Restore(const FigureInstance& figure);
    // Every source on `figure` lets go now, and the figure leaves the list (before the figure is destroyed).
    void LetGoAll(FigureInstance& figure);
    // The sources `figure` carries (none for a figure not in the list).
    std::vector<ParticleSource*> SourcesOn(const FigureInstance& figure) const;

    float groundHeight = 0.0f;
    Wind wind;
    float tickFraction = 0.0f;
    // What every carried figure is to its effects (the bounds' radius and the feet are its own).
    bool unit = true;
    bool running = false;
    bool dead = false;
    float unitRadius = 0.5f;

private:
    struct Carried {
        const FigureInstance* figure = nullptr;
        bool removed = false;
        std::vector<HeldRef<ParticleSource>> sources;
    };
    Carried* Find(const FigureInstance& figure);
    const Carried* Find(const FigureInstance& figure) const;

    std::vector<Carried> carried;
};

} // namespace particlehost
