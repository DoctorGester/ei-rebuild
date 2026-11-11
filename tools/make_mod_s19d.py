"""Writes a Rebirth mod for the S19d scenarios (the mod-folder patches the S19b scenario leaves unchecked).

usage: python tools/make_mod_s19d.py <saves dir> stream|maps|loose|toml|databases|png|heights|anim|mobs
                                    [--zip <name> [--top]]
  <saves dir>   the run's harness_saves folder; the mod goes to <saves dir>\\s19d_mod, which the scenario selects with
                the harness step "mod harness_saves\\s19d_mod" (the settings' Mod)
  --zip <name>  the mod as one zip instead (docs/notes/mods.md, "Zip mods"), <saves dir>\\<name>: "s19d_mod.zip" for the
                step "mod harness_saves\\s19d_mod.zip"; "starter\\Mods\\<name>.zip" puts it in the run's overlay of the
                game folder's starter\\Mods, where the main menu's list finds it
  --top         the zip's files below one folder s19d_mod\\ (as a zip of the mod's folder), not at its root

Every file is a copy or a repack of the vanilla install's own data (EI_GAME_VANILLA; read only) or a few made-up bytes;
nothing is committed. Kinds (docs/notes/rebirth.md, "Resources and paths"):
  stream    StreamPatch and MoviesPatch (tests/scenarios/rebirth_mod_stream_movies.txt)
              Stream\\s19d.mp3      a copy of the install's murder.mp3: a track the game has no file of
              Stream\\bravery.mp3   a copy of rage.mp3: a track the game has, in the mod another one
              Movies\\crdtfin.bik   a copy of the install's progres.bik under the name of a movie the game has
  maps      Maps (tests/scenarios/rebirth_mod_maps.txt)
              Maps\\zone2.mpr       a copy of the install's: zone2's map from the mod, its .mob from the game's folder
              Maps\\z9q2.mq         a copy of the install's: the zone table and the quest files read the mod's folder
              Maps\\s19d\\note.txt   a file of a subfolder, listed as "s19d\\note.txt"
  loose     loose files over archives (tests/scenarios/mod_loose_files.txt; docs/notes/mods.md, "Loose files"):
              Res\\s19d_texts_a.res   the mod's texts.res ([RES]): s19d.txt, s19d_c.txt, s19d_d.txt
              Res\\texts\\            s19d.txt over the mod's archive, Sub\\S19D_Sub.TXT in a sub-folder
              Res\\s19d_texts_a\\     s19d_d.txt: the mod's archive's own folder over its entry
              Res\\outro\\outro.rtf   over the game's outro.res (no [RES] line for it)
              Maps\\s19d_loose\\      a folder that stands for s19d_loose.mpr
  toml      records as TOML (tests/scenarios/mod_toml_records.txt; docs/notes/mods.md, "Records as TOML"):
              config.reg.toml                 the mod's config.reg as TOML (no config.reg)
              Res\\texts\\texts.toml          one string of the game's texts.res changed ("string quit")
              Res\\database\\acks.db.toml     two records of the game's acks.db changed (Haburu's scenario speeches; the
                                              mercenary warrior's attack speeches alone)
              Res\\database\\prints.db.toml   the stone blood print's wet alpha alone
              Res\\database\\unanhoco.adb.toml  the cow's animation "cidle01" with another chance, alone; "cidle02"
                                              with another chance and a move no animation has (the record left out)
  databases game databases as TOML (tests/scenarios/mod_toml_databases.txt; docs/notes/mods.md, "Databases as TOML"):
              Res\\database\\items.idb.toml   the weapon "stone axe" with another damage; a weapon "mf5 axe" added
  png       textures as PNG (tests/scenarios/mod_png_texture.txt; docs/notes/mods.md, "Textures as PNG"):
              Res\\textures\\Zone1000.png .. Zone1007.png   zone 1's terrain pages as four coloured quadrants
                                                       (render_loose_bmp's), 256 x 256, with no settings chunk
  heights   a map's heights as a PNG (tests/scenarios/mod_map_heights.txt; docs/notes/mods.md, "Maps as folders"):
              Maps\\zone1.mpr             a copy of the install's (packed from its folder on an unpacked or converted one)
              Maps\\zone1\\heights.png    its heights (from the sectors, or from the converted folder's heights.png) with
                                        a plateau of 60000 raised around HILL: over the archive's map, the rest its own
  anim      animations as glTF (tests/scenarios/mod_gltf_animation.txt; docs/notes/mods.md, "Animations as glTF"):
              Res\\figures\\unhuma.glb   the hero's figure (unhuma.mod, .bon and .anm of the install's figures.res) as
                                        ei-convert writes it (the run's build's: EI_BUILD_DIR, else
                                        build\\verify; a converted install's own unhuma.glb when it has one),
                                        its idle "cidle18" twice as slow: its keys' times doubled and its `extras` dropped (so the loader samples
                                        the glTF's rotations at every tick: 211 keys for 106)
              Res\\database\\unhuma.adb.toml  "cidle18" with twice its frames (210), alone
  mobs      a map's objects as TOML (tests/scenarios/mod_map_objects.txt; docs/notes/mods.md, "Map objects as TOML"):
              Maps\\zone1.mob.toml   the game's zone1.mob as ei-convert writes it (a converted install's own, else made by
                                    the run's build's ei-convert, EI_BUILD_DIR, else build\\verify) with the pig PigF0-1947
                                    moved to MOVED_PIG and a pig MF9aPig added at ADDED_PIG, then two bad records the
                                    load leaves out with a warning (MF10a: a unit without a class, one with a
                                    tree's); no .mob (the game's gives the scripts)
Rebirth.txt and a config.reg with [MOD] Title are written in every kind (as the shipped mods have them; toml: its
TOML).
"""
import argparse, json, os, re, shutil, struct, subprocess, sys, tempfile, zipfile, zlib

import exe_path
import make_test_mod as mod
import unpack_install

MOD = 's19d_mod'


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def copy(source, target):
    os.makedirs(os.path.dirname(target), exist_ok=True)
    shutil.copyfile(source, target)


def copy_archive(source, target):
    """The install's archive at source copied to target; from an unpacked install (tools/unpack_install.py) the folder
    of its name packed into one."""
    if os.path.exists(source):
        return copy(source, target)
    folder = os.path.splitext(source)[0]
    files = {}
    for path, _, names in os.walk(folder):
        for name in sorted(names):
            full = os.path.join(path, name)
            with open(full, 'rb') as f:
                files[os.path.relpath(full, folder).replace(os.sep, '\\')] = f.read()
    write(target, mod.archive(files))


def vanilla(*names):
    return exe_path.install_file('vanilla', *names)


def config(root, res=None):
    sections = {'MOD': {'Title': 'S19d test mod'}}
    if res:
        sections['RES'] = res
    write(os.path.join(root, 'config.reg'), mod.registry(sections))
    write(os.path.join(root, 'Rebirth.txt'), b'')


def stream(root):
    config(root)
    copy(vanilla('Stream', 'murder.mp3'), os.path.join(root, 'Stream', 's19d.mp3'))
    copy(vanilla('Stream', 'rage.mp3'), os.path.join(root, 'Stream', 'bravery.mp3'))
    copy(vanilla('Movies', 'progres.bik'), os.path.join(root, 'Movies', 'crdtfin.bik'))


def maps(root):
    config(root)
    copy_archive(vanilla('Maps', 'zone2.mpr'), os.path.join(root, 'Maps', 'zone2.mpr'))
    copy_archive(vanilla('Maps', 'z9q2.mq'), os.path.join(root, 'Maps', 'z9q2.mq'))
    write(os.path.join(root, 'Maps', 's19d', 'note.txt'), b'a file of a subfolder\r\n')


def loose(root):
    config(root, {'texts.res': 'Res\\s19d_texts_a.res'})
    write(os.path.join(root, 'Res', 's19d_texts_a.res'), mod.archive({
        's19d.txt': b'first\r\n',                      # 7 bytes
        's19d_c.txt': b'only in the archive\r\n',      # 21
        's19d_d.txt': b'archive copy\r\n'}))           # 14
    write(os.path.join(root, 'Res', 'texts', 's19d.txt'), b'the mod folder\r\n')             # 16
    write(os.path.join(root, 'Res', 'texts', 'Sub', 'S19D_Sub.TXT'), b'sub file\n')          # 9
    write(os.path.join(root, 'Res', 's19d_texts_a', 's19d_d.txt'), b'own folder\r\n')        # 12
    write(os.path.join(root, 'Res', 'outro', 'outro.rtf'), b'loose credits\r\n')             # 15
    write(os.path.join(root, 'Maps', 's19d_loose', 's19d_loose.mp'), b'a map folder\r\n')


def toml(root):
    write(os.path.join(root, 'config.reg.toml'), b'[MOD]\nTitle = "S19d test mod"\n')
    write(os.path.join(root, 'Rebirth.txt'), b'')
    write(os.path.join(root, 'Res', 'texts', 'texts.toml'), b'"string quit" = "MF3 mod text"\n')
    # Records merge by field (MF3a). Haburu's record gives his scenario list, which replaces the game's whole list: s24
    # gets another sound, s25 is gone; his other lists stay. The mercenary warrior's gives his attack list alone (his
    # move list stays), the stone blood print's one wet value (the rest of it stays) and the cow's "cidle01" its
    # chance (its other fields stay). The cow's "cidle02" has a name its field does not have (MF14b): a load error of
    # the record, which is left out whole (its chance stays the game's).
    write(os.path.join(root, 'Res', 'database', 'acks.db.toml'),
          b'[[unit]]\nname = "Haburu"\nscenario = [\n'
          b'    { sound = "Acks\\\\Haburu\\\\Scenario\\\\MF3.wav", chance = 100, text = "MF3", name = "s24" },\n]\n\n'
          b'[[unit]]\nname = "Human Mercenary Warrior"\nattack = [\n'
          b'    { sound = "Acks\\\\HMW\\\\Attack\\\\MF3a.wav", chance = 50 },\n]\n')
    write(os.path.join(root, 'Res', 'database', 'prints.db.toml'), b'[[blood]]\nname = "stone"\nwet = { alpha = 0.25 }\n')
    write(os.path.join(root, 'Res', 'database', 'unanhoco.adb.toml'),
          b'[[animation]]\nname = "cidle01"\nchance = 37\n\n[[animation]]\nname = "cidle02"\nchance = 11\nmove = "trot"\n')


# A mod's items.idb.toml holds only what it changes: the game's "stone axe" keeps every field but the two given, and
# "mf5 axe" (a name the game lacks) is a new weapon at the end of the table (the fields left out are 0).
DATABASE_ITEMS = b'''[[weapon]]
name = "stone axe"
damage = 9.5
damage_spread = 1.25

[[weapon]]
name = "mf5 axe"
family = "axe"
type = "axe"
material_class = "Stone"
number = 1
price = 500.0
weight = 4.0
charge = 30.0
magic_level = 6
condition = 4378.0
material_units = 4
sold_by = ["gipat_smith"]
attack_interval = 33
damage = 7.0
damage_spread = 0.5
damage_types = { slashing = 1.0 }
defence_bonus = -5.0
'''


def databases(root):
    config(root)
    write(os.path.join(root, 'Res', 'database', 'items.idb.toml'), DATABASE_ITEMS)


def png_file(width, height, rgb_at):
    """A PNG of 8-bit RGB with no chunk but the image's: rgb_at(x, y) the texel at x, y from the top left."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + b''.join(bytes(rgb_at(x, y)) for x in range(width)) for y in range(height))
    header = struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')


def png(root):
    config(root)
    quadrants = [[(255, 0, 0), (0, 255, 0)], [(0, 0, 255), (255, 255, 0)]]   # top row first
    size = 256
    picture = png_file(size, size, lambda x, y: quadrants[y * 2 // size][x * 2 // size])
    for i in range(8):
        write(os.path.join(root, 'Res', 'textures', f'Zone1{i:03d}.png'), picture)


# heights: the plateau's middle (a vertex of zone 1, away from where a new game starts), its flat radius and the radius
# where it meets the ground, in cells; its height in heights.png's units (60000 of 65534: 20.1 of zone 1's 21.95).
HILL = (40, 100)
HILL_TOP, HILL_FOOT = 4.0, 12.0
HILL_HEIGHT = 60000


def png_chunks(data):
    """The chunks of the PNG `data` as (type, bytes)."""
    at, out = 8, []
    while at + 8 <= len(data):
        length, kind = struct.unpack_from('>I4s', data, at)
        out.append((kind, data[at + 8:at + 8 + length]))
        at += 12 + length
    return out


def png_grey16(data):
    """A 16-bit grey PNG (the converted folder's heights.png): width, height and the samples row by row."""
    chunks = png_chunks(data)
    width, height, bits, colour = struct.unpack_from('>IIBB', chunks[0][1])
    if bits != 16 or colour != 0:
        sys.exit(f'make_mod_s19d: heights.png is not 16-bit grey ({bits} bits, colour type {colour})')
    raw = zlib.decompress(b''.join(body for kind, body in chunks if kind == b'IDAT'))
    stride, step = width * 2, 2
    rows, prior = [], bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            left = line[i - step] if i >= step else 0
            up, corner = prior[i], prior[i - step] if i >= step else 0
            if kind == 1: line[i] = (line[i] + left) & 0xFF
            elif kind == 2: line[i] = (line[i] + up) & 0xFF
            elif kind == 3: line[i] = (line[i] + (left + up) // 2) & 0xFF
            elif kind == 4:
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 0xFF
        rows.append(line)
        prior = line
    return width, height, [struct.unpack('>%dH' % width, bytes(line)) for line in rows]


def png_grey16_file(width, height, rows):
    """A 16-bit grey PNG of the rows (lists of samples), each row unfiltered."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    raw = b''.join(b'\0' + struct.pack('>%dH' % width, *row) for row in rows)
    header = struct.pack('>IIBBBBB', width, height, 16, 0, 0, 0, 0)
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')


def map_heights(files):
    """The vertex grid's heights of a map archive's files (lower-case names): its heights.png, or its map file's and
    sectors' (a vertex two sectors share from the earlier one, as the folder's grid has it)."""
    if 'heights.png' in files:
        return png_grey16(files['heights.png'])
    mp_name = next(name for name in files if name.endswith('.mp'))
    _, _, sectors_x, sectors_y = struct.unpack_from('<IfII', files[mp_name], 0)
    stem = mp_name[:-3]
    width, height = sectors_x * 32 + 1, sectors_y * 32 + 1
    rows = [[0] * width for _ in range(height)]
    for sy in range(sectors_y - 1, -1, -1):
        for sx in range(sectors_x - 1, -1, -1):
            sector = files[f'{stem}{sx:03d}{sy:03d}.sec']
            for r in range(33):
                for c in range(33):
                    rows[sy * 32 + r][sx * 32 + c] = struct.unpack_from('<H', sector, 5 + (r * 33 + c) * 8 + 2)[0]
    return width, height, rows


def heights(root):
    import unpack_install
    config(root)
    archive = os.path.join(root, 'Maps', 'zone1.mpr')
    copy_archive(vanilla('Maps', 'zone1.mpr'), archive)
    with open(archive, 'rb') as f:
        data = f.read()
    files = {name.lower(): data[offset:offset + size] for name, offset, size, _ in unpack_install.entries(data)}
    width, height, rows = map_heights(files)
    rows = [list(row) for row in rows]
    for y in range(height):
        for x in range(width):
            distance = ((x - HILL[0]) ** 2 + (y - HILL[1]) ** 2) ** 0.5
            if distance >= HILL_FOOT:
                continue
            share = 1.0 if distance <= HILL_TOP else (HILL_FOOT - distance) / (HILL_FOOT - HILL_TOP)
            rows[y][x] = max(rows[y][x], round(rows[y][x] + (HILL_HEIGHT - rows[y][x]) * share))
    write(os.path.join(root, 'Maps', 'zone1', 'heights.png'), png_grey16_file(width, height, rows))





def install_entries(archive, names):
    """The entries `names` of the vanilla install's archive (its path's names; packed, or the folder of an unpacked
    one) by name."""
    source = vanilla(*archive)
    if not os.path.exists(source):
        folder = os.path.splitext(source)[0]
        return {name: open(os.path.join(folder, name), 'rb').read() for name in names}
    with open(source, 'rb') as f:
        data = f.read()
    found = {name.lower(): (offset, size) for name, offset, size, _ in unpack_install.entries(data)}
    return {name: data[found[name][0]:found[name][0] + found[name][1]] for name in names}


def single(value):
    """value rounded to a 32-bit float."""
    return struct.unpack('<f', struct.pack('<f', value))[0]


def glb_parts(glb):
    """A .glb's JSON document and its binary chunk."""
    json_length, = struct.unpack_from('<I', glb, 12)
    doc = json.loads(glb[20:20 + json_length])
    bin_length, = struct.unpack_from('<I', glb, 20 + json_length)
    return doc, bytearray(glb[28 + json_length:28 + json_length + bin_length])


def glb_of(doc, binary):
    """The .glb of a JSON document and its binary chunk (padded, the buffer's length its own)."""
    binary = bytes(binary) + b'\0' * (-len(binary) % 4)
    doc['buffers'][0]['byteLength'] = len(binary)
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    chunks = struct.pack('<II', len(text), 0x4E4F534A) + text + struct.pack('<II', len(binary), 0x4E4942) + binary
    return struct.pack('<III', 0x46546C67, 2, 12 + len(chunks)) + chunks


def glb_slower(glb, animation, factor):
    """A .glb with the animation of that name `factor` times as slow: its samplers' key times multiplied (a new
    accessor: the times are shared) and its `extras` dropped."""
    doc, binary = glb_parts(glb)
    found = [a for a in doc['animations'] if a['name'] == animation]
    if len(found) != 1:
        sys.exit(f'make_mod_s19d: {animation}: {len(found)} animations of the name')
    anim = found[0]
    anim.pop('extras', None)
    made = {}
    for sampler in anim['samplers']:
        index = sampler['input']
        if index not in made:
            accessor = doc['accessors'][index]
            at = doc['bufferViews'][accessor['bufferView']].get('byteOffset', 0) + accessor.get('byteOffset', 0)
            times = [single(t * factor) for t in struct.unpack_from(f'<{accessor["count"]}f', binary, at)]
            binary += b'\0' * (-len(binary) % 4)
            doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': 4 * len(times)})
            binary += struct.pack(f'<{len(times)}f', *times)
            doc['accessors'].append({'bufferView': len(doc['bufferViews']) - 1, 'componentType': 5126,
                                     'count': len(times), 'type': 'SCALAR', 'min': [times[0]], 'max': [times[-1]]})
            made[index] = len(doc['accessors']) - 1
        sampler['input'] = made[index]
    return glb_of(doc, binary)


def unhuma_glb():
    """The hero's figure as ei-convert writes it: a converted install's own (run_all --converted), else made from the
    install's unhuma.mod, unhuma.bon and unhuma.anm by the run's build's ei-convert."""
    converted = os.path.join(os.path.splitext(vanilla('Res', 'figures.res'))[0], 'unhuma.glb')
    if os.path.exists(converted):
        with open(converted, 'rb') as f:
            return f.read()
    build = os.environ.get('EI_BUILD_DIR') or os.path.join(exe_path.ROOT, 'build', 'verify')
    convert = os.path.join(build, 'ei-convert.exe' if os.name == 'nt' else 'ei-convert')
    files = install_entries(('Res', 'figures.res'), ('unhuma.mod', 'unhuma.bon', 'unhuma.anm'))
    with tempfile.TemporaryDirectory() as scratch:
        archive = os.path.join(scratch, 'figures.res')
        write(archive, mod.archive(files))
        subprocess.run([convert, archive, os.path.join(scratch, 'out')], check=True, capture_output=True)
        with open(os.path.join(scratch, 'out', 'figures', 'unhuma.glb'), 'rb') as f:
            return f.read()


ANIM_SLOWER = 2   # the idle's keys' times multiplied; its .adb frames likewise (105 for its 106 keys: 210)


def anim(root):
    config(root)
    write(os.path.join(root, 'Res', 'figures', 'unhuma.glb'), glb_slower(unhuma_glb(), 'cidle18', ANIM_SLOWER))
    write(os.path.join(root, 'Res', 'database', 'unhuma.adb.toml'),
          f'[[animation]]\nname = "cidle18"\nframes = {105 * ANIM_SLOWER}\n'.encode())


# mobs: where the pig PigF0-1947 (id 1947, placed at 190.79, 78.52) is moved and the pig MF9aPig is added: map units
# near the party's start of the new game.
MOVED_PIG = (50.0, 138.0)
ADDED_PIG = (50.0, 142.0)
PIG_ID = 1947
PIG_POSITION = '[190.78964, 78.51906, 0.0]'


def zone1_toml():
    """The game's zone1.mob as TOML (ei-convert's): a converted install's own Maps\\zone1.mob.toml, else written by the
    run's build's ei-convert from a copy of the install's zone1.mob."""
    converted = vanilla('Maps', 'zone1.mob.toml')
    if os.path.exists(converted):
        with open(converted, 'r', encoding='utf-8', newline='') as f:
            return f.read()
    build = os.environ.get('EI_BUILD_DIR') or os.path.join(exe_path.ROOT, 'build', 'verify')
    convert = os.path.join(build, 'ei-convert.exe' if os.name == 'nt' else 'ei-convert')
    with tempfile.TemporaryDirectory() as scratch:
        copy(vanilla('Maps', 'zone1.mob'), os.path.join(scratch, 'in', 'Maps', 'zone1.mob'))
        subprocess.run([convert, os.path.join(scratch, 'in'), os.path.join(scratch, 'out')], check=True, capture_output=True)
        with open(os.path.join(scratch, 'out', 'Maps', 'zone1.mob.toml'), 'r', encoding='utf-8', newline='') as f:
            return f.read()


def mobs(root):
    config(root)
    text = zone1_toml().replace('\r\n', '\n')
    records = text.split('[[object]]\n')   # the header, then each record with its blank line after it
    start = next(i for i, r in enumerate(records) if r.startswith(f'kind = "unit"\nid = {PIG_ID}\n'))
    pig = records[start]
    if PIG_POSITION not in pig:
        sys.exit(f'make_mod_s19d: the pig {PIG_ID} of zone1.mob.toml is not where it was placed')
    at = lambda xy: f'[{xy[0]}, {xy[1]}, 0.0]'
    # The new pig is a copy of the moved one's record under a new id and name; its position and its guard behaviour's
    # place (the same point) are the new place.
    top = max(int(m) for m in re.findall(r'^id = (\d+)$', text, re.M))
    added = pig.replace(f'id = {PIG_ID}\n', f'id = {top + 1}\n', 1)
    added = added.replace(f'name = "PigF0-{PIG_ID}"', 'name = "MF9aPig"', 1).replace(PIG_POSITION, at(ADDED_PIG))
    records[start] = pig.replace(PIG_POSITION, at(MOVED_PIG))
    records[-1] = records[-1].rstrip('\n') + '\n\n'
    records.append(added.rstrip('\n') + '\n\n')
    # MF10a: two records the zone's load leaves out with a warning: a unit without a class, one of a tree's class.
    for n, (name, parent) in enumerate((('MF10aNoClass', ''), ('MF10aWrongClass', 'class = "tree"\n')), 2):
        bad = added.replace(f'id = {top + 1}\n', f'id = {top + n}\n', 1).replace('name = "MF9aPig"', f'name = "{name}"', 1)
        bad = re.sub(r'^class = "\w+"\n', parent, bad, count=1, flags=re.M)
        records.append(bad.rstrip('\n') + '\n' + ('\n' if n == 2 else ''))
    write(os.path.join(root, 'Maps', 'zone1.mob.toml'), '[[object]]\n'.join(records).encode('utf-8'))


KINDS = {'stream': stream, 'maps': maps, 'loose': loose, 'toml': toml, 'databases': databases, 'png': png,
         'heights': heights, 'anim': anim, 'mobs': mobs}


def zip_folder(root, target, top):
    """The mod folder root packed into the zip target: its files at the zip's root, or below one top folder of the
    mod's name with an entry for each folder (as Rebirth's LostInAstral.zip has them). The .res archives stored, the
    rest deflated, so both of the reader's ways are taken."""
    os.makedirs(os.path.dirname(target), exist_ok=True)
    with zipfile.ZipFile(target, 'w') as z:
        if top:
            z.writestr(MOD + '/', b'')
        for path, folders, names in os.walk(root):
            folders.sort()
            inside = os.path.relpath(path, root).replace(os.sep, '/')
            prefix = (MOD + '/' if top else '') + ('' if inside == '.' else inside + '/')
            if top and inside != '.':
                z.writestr(prefix, b'')
            for name in sorted(names):
                method = zipfile.ZIP_STORED if name.lower().endswith('.res') else zipfile.ZIP_DEFLATED
                z.write(os.path.join(path, name), prefix + name, compress_type=method)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('saves')
    parser.add_argument('kind', choices=sorted(KINDS))
    parser.add_argument('--zip', metavar='NAME', help='the mod as <saves dir>\\NAME (a .zip), not as a folder')
    parser.add_argument('--top', action='store_true', help='in the zip below one folder of the mod\'s name')
    args = parser.parse_args()
    if not args.zip:
        root = os.path.join(args.saves, MOD)
        os.makedirs(root, exist_ok=True)
        KINDS[args.kind](root)
        print(f'{root}: {args.kind}')
        return
    target = os.path.join(args.saves, args.zip.replace('\\', os.sep))
    os.makedirs(args.saves, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.saves) as scratch:
        KINDS[args.kind](scratch)
        zip_folder(scratch, target, args.top)
    print(f'{target}: {args.kind}{" below " + MOD if args.top else ""}')


if __name__ == '__main__':
    main()
