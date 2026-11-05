// weather::Field::Place (src/game/weather_field.h): the rain's and the snow's particles move smoothly while they show. Over
// 600 steps of a third of a tick (60 frames a second for ten seconds of the game), with the view's centre panning and
// the wind changing on the way, no particle (every seventh is followed) that shows in two frames in a row moves
// further between them than its speed takes it, and none appears or vanishes at once (its alpha changes by little in
// a frame). Field::BinBounds: every particle that shows lies in its bin's boxes.
#include "../../src/game/weather_field.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void Expect(bool ok, const char* what, int index, double time, float got, float allowed) {
    ++checks;
    if (ok) return;
    if (++failures <= 10)
        std::printf("ei_unit_check: weather field: %s: particle %d at tick %.2f moved %g (allowed %g)\n", what, index, time,
                    got, allowed);
}

void CheckKind(weather::Kind kind, const char* name) {
    const int count = weather::Field(kind, weather::FieldView{}).Count();
    constexpr double kStep = 0.3;
    constexpr int kSteps = 600;
    constexpr int kEvery = 7;
    weather::FieldView view;
    view.time = 123456.0;   // the world's time some hours in
    view.wind = {0.05f, -0.02f, 0.0f};
    view.density = 1.0f;
    std::vector<weather::Particle> last(static_cast<std::size_t>(count));
    int shown = 0;
    for (int step = 0; step <= kSteps; ++step) {
        if (step == kSteps / 2) view.wind = {-0.15f, 0.1f, -0.02f};   // a gust: the carry goes on from where it was
        if (step > 0) {
            view.time += kStep;
            view.carry = weather::Carry(view.carry, view.wind, kStep);
        }
        // The camera pans 0.2 units a frame along x and climbs a little; the box's centre goes with it.
        view.eye = {100.0f + 0.2f * step, 50.0f, 20.0f + 0.002f * step};
        view.centre = {view.eye.x + 15.0f, view.eye.y + 5.0f, view.eye.z - 10.0f};
        const weather::Field field(kind, view);
        for (int i = 0; i < count; i += kEvery) {
            const weather::Particle p = field.Place(i);
            weather::Particle& before = last[static_cast<std::size_t>(i)];
            if (step > 0) {
                const float alphaStep = std::fabs(p.alpha - before.alpha);
                Expect(alphaStep <= 0.2f, "alpha jump", i, view.time, alphaStep, 0.2f);
                if (p.alpha > 0.02f && before.alpha > 0.02f) {
                    const float moved = Distance(p.position, before.position);
                    const float speed = Length(p.velocity) > Length(before.velocity) ? Length(p.velocity) : Length(before.velocity);
                    const float allowed = speed * static_cast<float>(kStep) * 1.05f + 1e-3f;
                    Expect(moved <= allowed, name, i, view.time, moved, allowed);
                }
            }
            if (p.alpha > 0.5f) ++shown;
            before = p;
        }
    }
    // Most of the box shows: the walls' fades take only its rim.
    const double share = static_cast<double>(shown) * kEvery / (static_cast<double>(count) * (kSteps + 1));
    ++checks;
    if (share < 0.3) {
        ++failures;
        std::printf("ei_unit_check: weather field: %s: only %.2f of the particles show\n", name, share);
    }
    // Every particle that shows lies in one of its bin's boxes (the draw passes over the bins whose boxes are out of
    // view), now and some hours of the game later.
    for (const double later : {0.0, 54321.7}) {
        weather::FieldView at = view;
        at.time += later;
        at.carry = weather::Carry(view.carry, view.wind, later);
        const weather::Field field(kind, at);
        for (int bin = 0; bin < weather::kBins; ++bin) {
            weather::Bounds boxes[8];
            const int boxCount = field.BinBounds(bin, 0.0f, boxes);
            const weather::BinRange range = field.Bin(bin);
            for (int i = range.first; i < range.end; ++i) {
                const weather::Particle p = field.Place(i);
                if (p.alpha <= 0.0f) continue;
                bool inside = false;
                for (int b = 0; b < boxCount && !inside; ++b)
                    inside = p.position.x >= boxes[b].low.x && p.position.x <= boxes[b].high.x &&
                             p.position.y >= boxes[b].low.y && p.position.y <= boxes[b].high.y &&
                             p.position.z >= boxes[b].low.z && p.position.z <= boxes[b].high.z;
                Expect(inside, "outside its bin", i, at.time, 0.0f, 0.0f);
            }
        }
    }
    // The density's fade: none at 0, all at 1.
    view.density = 0.0f;
    int atNone = 0;
    const weather::Field none(kind, view);
    for (int i = 0; i < count; ++i) atNone += none.Place(i).alpha > 0.0f;
    ++checks;
    if (atNone != 0) {
        ++failures;
        std::printf("ei_unit_check: weather field: %s: %d particles show at density 0\n", name, atNone);
    }
}

} // namespace

int RunWeatherFieldChecks() {
    failures = 0;
    checks = 0;
    CheckKind(weather::Kind::Rain, "rain");
    CheckKind(weather::Kind::Snow, "snow");
    std::printf("ei_unit_check: weather field: %d checks, %d failed\n", checks, failures);
    return failures;
}
