#pragma once

// The effect viewer's state, shared by what plays the effect (effect_viewer.cpp) and its panel
// (effect_viewer_panel.cpp). docs/notes/effects.md, "The effect viewer".

#include "tools/effect_document.h"

#include "engine/core/random.h"
#include "lib/vecmath.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class ParticleManager;
class ParticleSource;
class Unit;

namespace tools::effect_viewer {

// Where an effect comes from: an original type (its built-in file), or a mod's effect (Res\effects through the
// layers) or one made in the viewer.
enum class Origin : std::uint8_t { Builtin, Mod };

struct EffectEntry {
    std::string name;
    std::int32_t id = 0;   // the source's type: an original type's number or a data effect's id
    Origin origin = Origin::Builtin;
    std::string madeText;  // a new effect's text (made in the viewer), played from it
};

// What the viewer did to the source at a tick: made again at that tick by every replay (a restart, a scrub, an edit).
enum class ChangeKind : std::uint8_t {
    StartEmitting, StopEmitting, Stop,   // the game's calls
    Value,                               // a float, bool or enum input set by number (`name`, value.x)
    Vector,                              // a vector input (`name`, value)
    Fire,                                // an event input (`name`)
    Alpha, Param,                        // the source's values game code sets (value.x)
    Extent,                              // (value)
    Turn,                                // turnCos and turnSin of the angle value.x (degrees)
};
struct Change {
    int tick = 0;   // the ticks run before it
    ChangeKind kind = ChangeKind::StartEmitting;
    std::string name;
    Vector3 value;
};

class Viewer {
public:
    // The effects there are: the 83 original types, then the mods' (Res\effects through the layers).
    void Scan();
    // The entry named `name` (any case), or -1.
    int Find(std::string_view name) const;

    // The entry `index` played from its start (its document loaded, or started from its made text).
    void Pick(int index);
    // A new effect `name` from the template, picked; false (`status` says why) for a name taken or not a file's stem.
    bool NewEffect(std::string_view name);
    // The source made again and run silently up to `target` ticks, the timeline's changes at their ticks.
    void Restart(int target);
    // One tick: the timeline's changes due first.
    void Step();
    // `change` made on the source now, and (`record`) kept on the timeline at this tick.
    void Make(const Change& change, bool record);
    // The source let go (it goes back to the manager's spares).
    void Drop();
    // The drawn particles held where they are (prev = current), for a paused source and its sub-effects.
    void Freeze();
    // A sub-effect of the source (StartEffects through effectdata::g_startOwnClock): made on the viewer's clock.
    ParticleSource* StartChild(ParticleManager& owner, int type);
    // The client world ticked: the source ticks with it while it plays.
    void ClientTicked();

    // On the unit `id` (a client world object), or free at `at`; the source made again at its tick.
    void PlaceOnUnit(int id);
    void PlaceFree(const Vector3& at);
    // The unit the source is put on (null when free, or gone).
    Unit* Carrier() const;
    // The client world's units near the camera's focus, the local party's first, then by distance.
    std::vector<Unit*> NearbyUnits() const;
    // The first unit of the local party, or null.
    Unit* PartyUnit() const;

    // The document's text or tree changed: read again and, when it loads, played from the start up to this tick
    // (the registry takes the record: Registry::Put).
    void Edited(bool fromTree);
    // The document saved into saveFolder; `status` says where or why not.
    void Save();

    bool open = false;
    std::vector<EffectEntry> entries;
    int picked = -1;
    std::string pendingPick;               // Open's name, picked at the first frame with a world
    ParticleManager* manager = nullptr;    // the client world's, which made the source
    ParticleSource* source = nullptr;      // one reference of the viewer's; the manager does not hold or tick it
    std::vector<ParticleSource*> children; // the effects it started (and theirs), held and ticked as it is
    bool hasDocument = false;
    EffectDocument document;
    int tick = 0;                          // ticks run since the start
    int furthest = 0;                      // the most ticks run since the pick
    bool playing = true;
    bool loop = true;
    bool sourceEnded = false;              // the source's tick said it is done (its sub-effects may play on)
    bool ended = false;                    // the source and its sub-effects are done
    int endedTicks = 0;                    // client ticks since it ended (Loop starts it again after a few)
    float size = 1.0f;                     // the effect object's size (ScaleSize(size / baseSize), as a spawn's)
    bool placed = false;                   // a place given (else the first pick puts it on the party's unit)
    bool onUnit = false;
    int unitId = -1;
    Vector3 freeAt;
    std::vector<Change> timeline;
    IsaacRandom start;                     // the particles' generator at the start: every replay draws the same
    IsaacRandom random;                    // the source's own, swapped in around its ticks
    std::string saveFolder;
    std::string newName;                   // the panel's name for a new effect
    std::string status;
    int documentTab = 0;                   // 0 fields, 1 text (the harness's "tab" picks it)
};

extern Viewer g_viewer;

// The viewer's panel (imgui), once a frame.
void DrawPanel(Viewer& viewer);

} // namespace tools::effect_viewer
