"""Writes a Rebirth mod for the S19d scenarios (the mod-folder patches the S19b scenario leaves unchecked).

usage: python tools/make_mod_s19d.py <saves dir> stream|archives|maps|loose|toml|databases|png|gltf [--zip <name> [--top]]
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
  archives  ResOverride's lists (tests/scenarios/rebirth_mod_archives.txt), config.reg [RES]:
              texts.res    = [Res\\s19d_texts_a.res, Res\\s19d_texts_b.res]   two mod archives, the first one wins
              outro.res    = [Res\\s19d_outro.res, "-"]                       the game's archive switched off
              figures.res  = Res\\s19d_figures.res                            extra figure archive (before the game's)
              textures.res = Res\\s19d_textures.res                           also overrides the game's archive
              menus.res    = Res\\s19d_menus.res                              extra figure (before) and texture (after)
  maps      Maps (tests/scenarios/rebirth_mod_maps.txt, rebirth_mod_zone_table.txt)
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
              Res\\database\\unanhoco.adb.toml  the cow's animation "cidle01" with another chance, alone
  databases game databases as TOML (tests/scenarios/mod_toml_databases.txt; docs/notes/mods.md, "Databases as TOML"):
              Res\\database\\items.idb.toml   the weapon "stone axe" with another damage; a weapon "mf5 axe" added
  png       textures as PNG (tests/scenarios/mod_png_texture.txt; docs/notes/mods.md, "Textures as PNG"):
              Res\\textures\\Zone1000.png .. Zone1007.png   zone 1's terrain pages as four coloured quadrants
                                                       (render_loose_bmp's), 256 x 256, with no settings chunk
  gltf      models as glTF (tests/scenarios/mod_gltf_model.txt; docs/notes/mods.md, "Models as glTF"):
              Res\\figures\\unhuma.glb   the hero's figure (unhuma.mod and .bon of the install's figures.res) as
                                        ei-convert writes it, then as another tool would leave it: the meshes'
                                        and the scene's `extras` dropped (the nodes' kept: their bones' versions)
                                        and the figure 1.5 times as large (the positions, the morph targets'
                                        displacements, the nodes' translations and bones scaled); ei-convert is the
                                        run's build's (EI_BUILD_DIR, else build\\verify); a converted install's
                                        own unhuma.glb when it has one
Rebirth.txt and a config.reg with [MOD] Title are written in every kind (as the shipped mods have them; toml: its
TOML).
"""
import argparse, json, os, shutil, struct, subprocess, sys, tempfile, zipfile, zlib

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


def archives(root):
    res = {
        'texts.res': ['Res\\s19d_texts_a.res', 'Res\\s19d_texts_b.res'],
        'outro.res': ['Res\\s19d_outro.res', '-'],
        'figures.res': 'Res\\s19d_figures.res',
        'textures.res': 'Res\\s19d_textures.res',
        'menus.res': 'Res\\s19d_menus.res',
    }
    config(root, res)
    out = lambda name, files: write(os.path.join(root, 'Res', name), mod.archive(files))
    out('s19d_texts_a.res', {'s19d.txt': b'first\r\n'})                                    # 7 bytes
    out('s19d_texts_b.res', {'s19d.txt': b'second file\r\n', 's19d_b.txt': b'only in b\r\n'})   # 13 and 11
    out('s19d_outro.res', {'outro_mod.txt': b'outro of the mod\r\n'})                      # 18
    # unmodr.anm is the dragon's animation, in the game's figures.res (and Rebirth's), loaded only when a dragon appears.
    out('s19d_figures.res', {'s19d.fig': b'a figure of the mod', 'unmodr.anm': b'dragon, mod'})   # 19 and 11
    out('s19d_textures.res', {'s19d_t2.mmp': b'texture'})                                  # 7
    # armytent01.mmp is in the game's textures.res: the game's three archives come first for the texture manager.
    out('s19d_menus.res', {'s19d_tex.mmp': b'a texture of the mod, 30 bytes', 'armytent01.mmp': b'mod copy'})   # 30 and 8


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
    # chance (its other fields stay).
    write(os.path.join(root, 'Res', 'database', 'acks.db.toml'),
          b'[[unit]]\nname = "Haburu"\nscenario = [\n'
          b'    { sound = "Acks\\\\Haburu\\\\Scenario\\\\MF3.wav", chance = 100, text = "MF3", name = "s24" },\n]\n\n'
          b'[[unit]]\nname = "Human Mercenary Warrior"\nattack = [\n'
          b'    { sound = "Acks\\\\HMW\\\\Attack\\\\MF3a.wav", chance = 50 },\n]\n')
    write(os.path.join(root, 'Res', 'database', 'prints.db.toml'), b'[[blood]]\nname = "stone"\nwet = { alpha = 0.25 }\n')
    write(os.path.join(root, 'Res', 'database', 'unanhoco.adb.toml'), b'[[animation]]\nname = "cidle01"\nchance = 37\n')


# A mod's items.idb.toml holds only what it changes: the game's "stone axe" keeps every field but the two given, and
# "mf5 axe" (a name the game lacks) is a new weapon at the end of the table (the fields left out are 0).
DATABASE_ITEMS = b'''[[weapon]]
name = "stone axe"
damage = 9.5
damage_spread = 1.25

[[weapon]]
name = "mf5 axe"
family = "axe"
type = 1
material_class = "Stone"
number = 1
price = 500.0
weight = 4.0
charge = 30.0
magic_level = 6
condition = 4378.0
material_units = 4
class_mask = 0x1
buildable = 1
attack_interval = 33
damage = 7.0
damage_spread = 0.5
damage_types = [0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0]
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


GLTF_SCALE = 1.5


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


def glb_scaled(glb, scale):
    """A .glb with every `extras` but the nodes' dropped, and its positions, morph displacements and node translations
    and bones scaled."""
    json_length, = struct.unpack_from('<I', glb, 12)
    doc = json.loads(glb[20:20 + json_length])
    bin_length, = struct.unpack_from('<I', glb, 20 + json_length)
    binary = bytearray(glb[28 + json_length:28 + json_length + bin_length])

    def drop_extras(value):
        if isinstance(value, dict):
            value.pop('extras', None)
            for item in value.values():
                drop_extras(item)
        elif isinstance(value, list):
            for item in value:
                drop_extras(item)
    nodes = doc.pop('nodes', [])
    drop_extras(doc)
    doc['nodes'] = nodes
    scaled = set()
    for mesh in doc.get('meshes', []):
        for primitive in mesh['primitives']:
            for attributes in [primitive['attributes']] + primitive.get('targets', []):
                scaled.add(attributes['POSITION'])
    for index in sorted(scaled):
        accessor = doc['accessors'][index]
        if 'bufferView' in accessor:   # (an accessor without a view is all zero)
            at = doc['bufferViews'][accessor['bufferView']].get('byteOffset', 0) + accessor.get('byteOffset', 0)
            count = accessor['count'] * 3
            values = [v * scale for v in struct.unpack_from(f'<{count}f', binary, at)]
            struct.pack_into(f'<{count}f', binary, at, *values)
        for bound in ('min', 'max'):
            if bound in accessor:
                accessor[bound] = [single(single(v) * scale) for v in accessor[bound]]   # as the floats are
    for node in doc.get('nodes', []):
        if 'translation' in node:
            node['translation'] = [v * scale for v in node['translation']]
        ei = node.get('extras', {}).get('ei', {})
        if 'bone' in ei:
            ei['bone'] = [[v * scale for v in version] for version in ei['bone']]
    text = json.dumps(doc, separators=(',', ':')).encode()
    text += b' ' * (-len(text) % 4)
    chunks = struct.pack('<II', len(text), 0x4E4F534A) + text + struct.pack('<II', len(binary), 0x4E4942) + bytes(binary)
    return struct.pack('<III', 0x46546C67, 2, 12 + len(chunks)) + chunks


def unhuma_glb():
    """The hero's figure as ei-convert writes it: a converted install's own (run_all --converted), else made from the
    install's unhuma.mod and unhuma.bon by the run's build's ei-convert."""
    converted = os.path.join(os.path.splitext(vanilla('Res', 'figures.res'))[0], 'unhuma.glb')
    if os.path.exists(converted):
        with open(converted, 'rb') as f:
            return f.read()
    build = os.environ.get('EI_BUILD_DIR') or os.path.join(exe_path.ROOT, 'build', 'verify')
    convert = os.path.join(build, 'ei-convert.exe' if os.name == 'nt' else 'ei-convert')
    files = install_entries(('Res', 'figures.res'), ('unhuma.mod', 'unhuma.bon'))
    with tempfile.TemporaryDirectory() as scratch:
        archive = os.path.join(scratch, 'figures.res')
        write(archive, mod.archive(files))
        subprocess.run([convert, archive, os.path.join(scratch, 'out')], check=True, capture_output=True)
        with open(os.path.join(scratch, 'out', 'figures', 'unhuma.glb'), 'rb') as f:
            return f.read()


def gltf(root):
    config(root)
    write(os.path.join(root, 'Res', 'figures', 'unhuma.glb'), glb_scaled(unhuma_glb(), GLTF_SCALE))


KINDS = {'stream': stream, 'archives': archives, 'maps': maps, 'loose': loose, 'toml': toml, 'databases': databases, 'png': png,
         'gltf': gltf}


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
