# Comment blocks moved out of source (C22i)

File-header research moved from the source by the comment removal pass; each section is the original
header comment of the named file.

## map_database.cpp

The map database loader: MapDatabase's allods, maps and exits (object_tables.h) filled
from map.txt / map-LMP.txt.
The file is read line by line through the text manager and split into words. Lines
starting with a directive word set the current record:
  #allod <name>, then 5 lines with one word each (the allod's other names)
  #zone <name> <allod> <game|brief|edge>       a new map, the current one from here
  #res, #camera, #maps, #figure (each followed by a line of words), #weather, #cage, #restrict,
  #sky                                         fields of the current map
  #exit (then "<target> <n>")                  a new exit of the current map
  #deploy, #remove, #passtime, #view, #deployangle   fields of the current exit

## mod_folders.h

The standalone resource layer: Rebirth's mod-folder patches, which addon.dll v0.12.4.2 applies
around the selected mod (HKCU\Software\Gipat.Ru\EI_Starter\Settings "AddonPath", a '\' added):
- Maps (10 sites): every maps-folder user names its file first; the maps folder is the
  mod's folder when the mod's Maps\ held that name at start, else the game's.
- ResOverride (25 sites): the mod's config.reg [Res] lists archives that come before (or, with '-',
  instead of) the game's twelve archives, plus extra figure and texture archives; every
  MappedResource::GetFile and ListFiles goes through addon (its vtable slot and all their calls).
- MoviesPatch (the BinkOpen import), CameraPatch (the camera path's CFile::Open), StreamPatch (the stream file's
  path): the mod's Movies\, Camera\ and Stream\ copy of the file when there is one.
In a hooked Rebirth launch addon does all of it (the patched functions stay original there) and a
vanilla launch has no mod, so the layer runs in the standalone image only, behind the flags. With
the Standard mod (no folders, no [Res]) everything passes through, except two things addon changes
for every archive: a failed GetFile of "t\<name>" / "s\<name>" is tried again as "<name>", and
ListFiles appends to the caller's array instead of refilling it. docs/notes/rebirth.md, "Resources
and paths"; addon's code: its scratch disassembly under build.

## net_connection.h

A connection of a network session to one peer (0x1168 bytes; constructor
created by SessionBase::AddClient) and the object replication it runs.
Names are provisional; see docs/notes/network.md.
Replication: every replicated object (a Message, whose version changes with it) is tracked by a
ReplicaEntry in one of 11 priority maps. Each update packet carries, highest priority first, the
entries whose object version differs from the version last sent (a 1 bit, type, id, then the
object's own Write) or whose object went away (a 0 bit, type, id). Every packet is remembered in
the send history with what it carried, so an acknowledgement (or its absence) can mark entries as
delivered or resend them. A reliable byte stream (reliableOut, reliableIn) travels in front of
the entries in chunks of up to 255 bytes ("headers"). The peer keeps the objects it received in
`records` (key type << 16 | id).

## net_link.h

Message links over streams: the endpoints a player's messages go through,
and the pump that keeps a driver link per remote player. Names are
provisional; see docs/notes/network.md and
docs/notes/cleanup-network.md.
Class map (vtables):
  FileStream (0x1C): a ByteStream over a `platform::File` (the original: an MFC CFile). Save files go through it
            (world_game.cpp, ui_saveload.cpp), and the connection screen's address book.
  NetLink (0x28 in game.exe, abstract): the base of the links.
  LoopbackLink (0x30): two of them joined to each other, the single-player
            link between the local client and the server. Send puts a copy of the stream on the
            peer's queue, Receive takes the front of its own.
  FramedLink (0x50, abstract): the received bytes in a memory stream, each message a
            4-byte length and its bytes; Send frames the message and hands it to slot 13.
  DriverLink (0x54): a FramedLink over a network connection's reliable streams.
  Is another FramedLink whose constructors are dead code; its own
            slots are listed in config/dead_code.csv.
The links are real C++ classes, created with net::NewCounted.

## net_messages.cpp

The network message classes (net_messages.h) and the message base: for each class its factory,
constructor, copy (Clone's), slots 2-5 (reset, clone, write, read) and destructor; the same for the lobby's
server entry (net_classes.h; no factory). See docs/notes/network.md and
docs/notes/cleanup-network.md.
The original inlines several STLport copies of the same string and vector code (copy
construction from [first, last) into a fresh block of length + 1 bytes, the 8-byte empty string,
assignment from a C string); they are reproduced here through one helper each, with the same
allocations in the same order. The reset slots end with the inlined placement new's null test
of `this`, which cannot fail here and is left out.

## net_second_master.h

EI_Plugin's NewMaster: the second master server (the plugin's is hat.gipath.org:28004; ours is the
setting ei::rebirth::PluginSecondMaster(), empty by default). Used behind
ei::rebirth::Patch::EiPluginNewMaster by three rewrites, at the plugin's call sites:
- the main menu's Multiplayer button (MainMenu::LeftDown),
- the announcement to the master (ServerSession::AnnounceToMaster),
- the master request of the connection screen (MpConnectScreen::MasterRequest).
With the setting empty none of them changes anything (no thread, no packet, no probe).
See docs/notes/rebirth.md ("EI_Plugin.asi", NewMaster).

## options_construct.cpp

The options object's constructor. The static initialiser runs it on
the global before WinMain (and registers the destructor with atexit), so the
constructor has already run when the hooks install.
These are not the defaults the game plays with: OptionsResetDefaults is called
before the registry is read, and that sets most of them again (640 x 480 and so on). What stays
from here: the "Network Settings" subkey, the CD labels, the font's character set, the keyboard
layout, both lights' radius and a few values the defaults function does not touch. The master server's name
stays empty.

## particle_create.cpp

ParticleSource set-up: Create, Reset and Clear. Names are provisional. Create resets the source, then fills
the per-type parameters by `type` (0x2000-0x2052; the script names are a table in the game's data):
emission rates, lifetime, spread, sizes, the three callbacks (emit, update, element), the element array
some types keep, and the texture. Last it loads the texture (kind 6, by name) and builds one UV rectangle
per texture frame (the frames are a grid of `frameColumns` columns). The callbacks are member pointers to
our rewrites (ParticleSource's EmitCallback, UpdateCallback, ElementCallback: plain code addresses, as in
the original). Floating point follows the original's x87 code: `double` for values the original keeps on
the FPU stack, static_cast<float> where it stores to a float.

## persistent_globals.cpp

Rebirth's PersistentGlobalVars (addon.dll 1001E3C1), reproduced behind its flag: where the game
empties a variable table, the "gvar." entries survive. addon copies them into an
std::unordered_map<std::string, float> of its own compiler (MSVC 2010: FNV-1 over every
(len / 10 + 1)-th character, 8 buckets growing by 8 at a load factor above 1, nodes kept in one
list with each bucket's range in it), lets the game clear the table, then sets the
entries again in the order of that list. The order decides the order of the entries
in the game's hash chains (and so in saves and in listings), so the container is reproduced
here exactly: AddonStringMap below is that unordered_map's insert, find and rehash.
See docs/notes/rebirth.md ("PersistentGlobalVars").

## present_window.h

The game window the renderer presents on (render::StartPresenting): full
screen without a display mode change, and the game's mouse coordinates as frame pixels.
docs/notes/renderer.md, section 9.
The game takes its mouse positions as screen coordinates of a window at 0, 0 the size of its
resolution: GetCursorPos (the cursor, the global map) and SetCursorPos without
converting, WM_MOUSEMOVE and the buttons as client coordinates, WM_MOUSEWHEEL as screen
coordinates. The window is anywhere (windowed it is centred and movable) and the frame may be
scaled into it (render::FrameInWindow), so these are always mapped to frame pixels: the cursor
calls through platform::MapCursorPoints, the events in the input dispatcher (app.cpp).

## registry.h

The game's binary registry files (*.reg: quests, item databases, settings), read through
FileRegistry. Names come from the game's own messages
("FileRegistry::FindData().\nWrong type of entry.").
File layout (all offsets from the start of the file):
  +0x00 u32 magic 0x45AB3EFB
  +0x04 u16 section count = size of the section hash table
  +0x06 the section table, 6 bytes per slot: u16 next slot in the chain (0xFFFF ends it),
        u32 offset of the section
A section: u16 key count (its hash table size), u16 name length, the name (no terminator),
then its key table (6-byte slots like the section table, offsets from the section).
A key: u8 type, u16 name length, the name, then the value. Types: 0 int, 1 float, 2 string
(u16 length + characters), 0x80/0x81/0x82 arrays of them (u16 count first).
A name's home slot is (sum of tolower(c) over its signed characters) % table size; names
compare case-insensitively over their stored length.

## renderer_lines.cpp

The old renderer's two line-strip draws, so the new renderer's state mirror
(src/render/render_state.h) gets the states they set and the new renderer gets the lines
(render::TlDraw). Both draw untextured
one-colour strips: stage 0's colour and alpha operations off around the draw, back to MODULATE
after it. See docs/notes/renderer.md ("Pass state").
- a strip through integer screen points. No caller found in the exe (not reached by the scenarios).
- a strip through points the transform pipeline projected (the device object's draw); the
  renderer's line lists use it (the figure parts' debug bounds
  boxes, the world client's line lists). Debug drawing; not reached by
  the scenarios. Its vertices go to a growing buffer in the exe's data.

## renderer_sprites.cpp

The renderer's sprite draw
with its helpers (indexed draw, a radix sort pass, a
merge sort pass). Every point (x, y, z and a size) becomes a screen-facing quad: the sprite's
colour and texture rectangle go to four TL vertices, and the pipeline
(ProjectSprites) places the corners. Batches of up to 511 sprites are sorted back to front
by the first corner's z (sprites whose z is outside 0..1 are dropped) and drawn as two triangles each;
the GPU clips the ones crossing the screen's edges. Particles draw through it, with additive blending when
their source asks for it. Buffers are function-local statics. See
docs/notes/models.md ("Renderer").

## renderer_strip.cpp

The renderer's ribbon draw
with its helpers (indexed draw of TL vertices, one ribbon cross
section, the direction at a point). A polyline of world points is projected (by the transform
object's point projection, with clipping flags when asked), and every point becomes a
pair of screen vertices offset across the line's screen direction by width * rhw * 15, with the
texture's u running along the ribbon and v across it; consecutive pairs make two triangles. The
ends are transparent (colour 0x00FFFFFF). Used for lightning bolts and similar strips. Buffers are
function-local statics. See docs/notes/models.md ("Renderer").
