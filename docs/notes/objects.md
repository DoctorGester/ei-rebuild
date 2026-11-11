# World objects, diplomacy, lightning, map database and players

Code: `object*.cpp`, `object_diplomacy.*`, `lightning*.cpp`, `map_database.*`, `map_spec*.cpp`,
`unit_record.*`, `world/world_loot.cpp`, `player*.cpp`. Scenarios: `objects_more` (lightning bolts, then the lightning
spell on a wolf); every scenario that loads a zone reaches the map database, unit records and the player messages.
The party is in `items_spells.md`, options and tables in `app.md`.

## Engine objects

- `EngineObject`: reference count, id (-1 when free), prototype id (items: index << 16 | material; spells: index << 16;
  units: the plain index), type, flags, owner (the manager). `ManagerContext` is the manager's context (World or
  Party). `ObjectListIterator` walks a manager's registered list.
- Map object classes come from `MapObjectManager::NewObject`; an unknown class gives null. The original's fake unit
  (class 0x39) and its scene loader were dead in game.exe (no data makes one) and are not in ours.

## Diplomacy

`Diplomacy` (a global, 0x100 bytes, saved): `allied[32]` and `hostile[32]`, bit q of row p. Relation codes 0 allied,
1 neutral, 2 hostile (script GetDiplomacy maps them to 1 / 0 / -1). A unit's hostile mask has bit p set when it is
hostile to player p (also flag 0x80000).
Kept: SetRelation with a relation outside 0..2 leaves the table alone but still clears the bit in the units' hostile
masks; player indices are unchecked; OnAttack stops at the first party entry with a null unit and sets the victim's
0x80000 flag a second time.

## Lightning

`Lightning` (class 0x1E) and `LightningManager`. The noise table (32 rows of 400 floats) is loaded from
`gfx.res\lightning.dat` on the first manager's Create (missing: the fatal error "Unable to open file
gfx.res\lightning.dat"). The manager's second list holds the bolts registered since the last frame; `DrawAll` (from
the renderer) draws and drops them. Callers: the script's `CreateLightning` and the weather create a client object
(class 0x4D) whose update moves the endpoints every frame while the bolt follows units. Textures "SmtgLight" /
"Lightning" or "DemonLight" / "DemonLightning".
Kept: Animate reads the curve value before the first one when a segment has no points; SampleNoise caps the keys added
per call at 100 but leaves the key time behind by the rest.

## Map database

`MapDatabase` (map/map_database.h): allods, maps and exits from map.txt, then map-LMP.txt, read line by line through the
text manager and split into words. A directive word sets the current record:

    #allod <name>, then 5 lines with one word each (the allod's other names)
    #zone <name> <allod> <game|brief|edge>       a new map, the current one from here
    #res, #camera, #maps, #figure (each followed by a line of words), #weather, #cage, #restrict,
    #sky, #position                              fields of the current map
    #exit (then "<target> <n>")                  a new exit of the current map
    #deploy, #remove, #passtime, #view, #deployangle   fields of the current exit
    #quest <name>, then its position line and its places' line   a quest's global map place (MapQuestRecord)

Kept: the map index continues from the last map of the earlier file (lines before the first "#zone" go to it), the
exit index restarts with every "#zone"; "#allod" reads word 0 of its five lines even without words; "#view" /
"#deployangle" append a "0" word. Then a mod's `Res\texts\map.toml` merges over the lists by name (mods.md, "Zones
from a mod").

## Zone files and unit records

`map_spec*.cpp`: the zone file's (.mob) records: base, object, flame, magic trap, lever, unit with behaviours /
patrols / waypoints, particle source, light, sound; chunk readers and writers, the zone diplomacy table.
`UnitRecord::ApplyTo` applies a .mob unit record to a new unit: stats (chunk BBBB0004), controller fields, protection
from the prototype, the five item lists, spells, armour, weapons (chunk BBBB000A); a record without stats gives only
the items. The records can come from a TOML beside the .mob (`game/map/mob_toml.h`; mods.md, "Map objects as TOML"):
both readers fill one `MobContents`, which `mapspec::LoadMob` applies.

## Players

- `Player` (`player.h`): its link, id, the map objects and timed objects its client knows (`lib::HashMap`s,
  each counted once), its units, an embedded Party, the given items, `sessionKind`, god mode, the recharge list.
  `PlayerConnection` is the base of `PlayerServer` and `PlayerClient`; `Player::Server()` and
  `WorldClient::LocalView()` are the casts of `connection`.
- **Server to client**: each player's server connection queues messages in a MemoryStream (a type byte and a fixed
  payload), compressed per packet. Dirty bits on the player (`PlayerDirty`) mark the state messages 0x02, 0x70
  (weather), 0x71, 0x72, 0x73 and the game time 0x07; the world tick sends those whose bits are set. Variables: 0x81
  set, 0x82 delete (every player's "gtime" too). Objects: 0x40 / 0x41 / 0x42 map objects added / changed / removed,
  0x50 / 0x51 / 0x52 timed objects; the "changed" senders compare the object's stamp with the world's update count, a
  pending delta written first. Party: 0x60 party state, 0x80 the party (refresh timer 900..1800), 0x83 party update,
  0x05 / 0x06 items' states and items given (money, flag byte, ids, -1), 0x0E a path (through a DeltaField), 0x09 +
  0x0B the leader died (single player), 0x08 zone change, 0x0D jingle, 0x13 a string event, 0x15 / 0x18 experience
  numbers.
- **Client to server**: 0x00 login (0x88 bytes: version 0x64, two strings of up to 0x3F; a wrong version gets
  "SrvErrMsg" / "WrongClientVersi..." as message 0x11 and state 3), 0x01 leave, 0x02 refresh party, 0x03 take exit,
  0x04 joined (state 2), 0x05 joined, 0x06 leave to zone, 0x07 briefing done, 0x08 new party, 0x09 resume, 0x10 pause,
  0x30 walk, 0x31 attack, 0x32 / 0x33 a spell or quick item at a point / on an object (a potion counted off at once),
  0x34 wanted weapon, 0x35 movement style, 0x36 use order (the nearest unit, checked from its path end), 0x39 follow,
  0x3A go to, 0x3B fight at will, 0x40 the camera anchor, 0x50 party update (SaveParty draws an ISAAC key). Orders
  are refused for a unit flagged 0x800000 (blocked) or dead. A new party or party update naming items or spells the
  server's databases lack gets message 0x11 with the reason and state 3 too (the player is dropped; ours, not the
  original's); our client leaves the server with the text (the original had no handler: the fatal error).
- **Dispatch**: each side runs a received packet's messages through a switch on the type byte (`PlayerServer::Receive`
  over its own stream `in`, `PlayerClient::DispatchMessages`); a type without a case or a handler returning false is
  the fatal error.
- **Leaving the server** (ours): a client handler that cannot take the server's data (message 0x11; an item or spell
  block or a party naming records this game's databases lack, `items_spells.md`) calls
  `PlayerClient::LeaveAfterTick(reason)` and returns true; the dispatch drops the packet's other messages. At the end
  of `WorldClient::RunTick` the player disconnects (`Player::Disconnect`) and the reason goes to
  `Interface3D::disconnectReason`; the next pass's idle check (`RunPass`: `IsLocalPlayerIdle`) recreates a
  multiplayer client's session, the interface shows the disconnect box with the reason as its text, and its OK goes
  to the main menu (`kLeaveGame`), as for a lost connection; a local game goes to the menu (`CloseSessions`). The
  loaders empty what they refused first, so no item or spell without its records stays readable meanwhile.
- **Client handlers** of note: 0x04 places the camera (`SetDistancePitch` with 23 m and 30 degrees in zone gz20g, else
  RestoreView 6 or 7); 0x0C reads (id, float) pairs until id -1 (an unknown id leaves its float to be read as the next
  id, kept); 0x19 by kind: control a unit, set / clear its flag 0x100, clear the selection; 0x60 old party ids lose
  `MapObjectFlag::InParty`, new ones gain it and fade in, the pause byte goes to the client world.
- `LeaderDied` (single player 0x09 + 0x0B); a network game revives a one-hero party (money / experience shares, a new
  id, the party redeployed).
- The login's text buffers and 0x06's flag byte went out with stack garbage in the original; ours sends zeros.

Kept: Player::Clear's unit loop stops at the first map entry without a unit; `HasLoot` reads every carried item as if
it had the field; Connect / Disconnect / GiveItems / TakeAllItems call the connection without a null test;
OnNewObject leaves an object it cannot read made and unreleased.

## Original behaviour kept elsewhere in this area

- Options: camera flips stored as -1 / 1 floats; "Difficulty Level" goes to two fields and is written from the second.
  The settings the modern renderer made fixed (C81) are constants in options.h: the lights, the fog distances, the
  wind, the clock's scale, the lighting coefficient.
- Item's base slots 17-20 return an empty text, 21 zeroes the caller's ItemUse (every class but the quest item).
