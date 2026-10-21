#pragma once

// The game-state digest of harness runs (verify build only; harness_digest.cpp; ticket SV2,
// docs/notes/standalone-verification.md).
//
// A record is the world server's state at one moment, as named fields: the world's clock, weather,
// alarms and counts ("world"), the generators ("random": ISAAC and the CRT's rand), each player with
// its party ("player<id>": money, members, variables) and every world object ("obj<id>": class, flags,
// position, orientation, scale, effects; map objects' hit points and mana; units' state, motion and
// controller; the AI brain's action, target, alertness and modes). Digest 2 (SV2a) adds, as groups
// of their own: the timed objects' own state ("timed<id>": the effects' position, velocity, start
// tick and their class's fields), the missiles ("missile<n>", by place in the list: the spell, its
// state, position, caster, target and timer), the AI event table ("aievents": per side a count and a
// hash), the mission script ("script": instances, waiting runs, global floats, its effects and
// countdowns), and the client world ("client": its clock and counts; "cunit<id>": each unit's place,
// animation and packed vitals). No pointers: objects are named by their ids, AI modes by name (their
// game.exe vtables when not known). The title line names the groups beyond digest 1's, so
// tools/golden.py compares a newer digest with an older golden on the groups the golden has.
//
// digest.txt in the run's folder holds the records: each a header line
//   @ <record> update <n> frame <n> tick <n> zone <name> hash <h> world <h> random <h> players <h> objects <h>
//     timed <h> missiles <h> events <h> script <h> client <h> n <entities>
// then, against the record before it, the entities that appeared ("+ <key> <description>", all their
// fields follow), the fields that changed ("<key> <field> <value>"; "-" for a field gone) and the
// entities gone ("- <key>"). The first record lists everything, so every record's whole state can be
// rebuilt (tools/golden.py). Floats are written with %.9g (exact) and always with a '.', 'e', "inf"
// or "nan"; integers without; bit patterns as 0x........
//
// Records are taken every N world updates (the scenario's "digest <N>" step, or EI_DIGEST=<N> for a
// whole run from the first update), after the update (the main loop's server update, which the
// harness already wraps), and by the step "digest" alone at that moment. A scenario's "digest water"
// line (WithWater) adds the group "water": the cached sectors' water layers as the waves move them. "dump <name>" writes the full
// state to dump_<name>.txt. With "randtrace", each record also goes into randtrace.txt as a note.
// Reading the state calls no game code except each object's GetClassId, the player list's iterator
// and the CRT's per-thread data (the rand() state); nothing the game sees changes.

#include <string>

namespace ei::verify::digest {

// `frames`: the harness's frame count (shown in the records). Reads EI_DIGEST.
void Init(const unsigned* frames);

// After every world update (the server world `server`): a record when the period says so.
void AfterWorldUpdate(void* server);

// The scenario's "digest <N>|off" (every N world updates from now; off or 0: none) and "digest" (one
// record now). "" on success, else the error.
std::string Step(const std::string& argument);

// The scenario's "digest water" line (read with the scenario, before the first record): the group
// "water" in every record and dump.
void WithWater();

// "dump <name>": the full state now into dump_<name>.txt. "" on success, else the error.
std::string Dump(const std::string& name);

// For the harness's waits on the world ("waitidle", "waitdead", "waitaward"; SV5d5): a server world unit now,
// read as the digest reads it.
struct UnitNow {
    bool found = false;     // the server world holds a unit with that id
    bool dead = false;
    bool idle = false;      // its controller has no action (no walk, no attack) and it has no path
    float experience = 0;
};
UnitNow Unit(int id);
// Player 0's (the local player's) first party member's unit id, or -1; `money` its party's money.
int LocalLeader(unsigned* money = nullptr);

// The digest's line for the report ("" when none was taken), and the file flushed.
std::string Summary();
void Flush();

} // namespace ei::verify::digest
