"""Makes the example mod of docs/modding.md from examples/example_mod and the install's own files.

usage: python tools/make_example_mod.py <folder> [--install <game folder>] [--convert <ei-convert>]
  <folder>     the mod is written to <folder>\\example_mod (a scenario's prepare line: the run's harness_saves, which
               the scenario selects with "mod harness_saves\\example_mod"; a player: the game's starter\\Mods)
  --install    the game folder to read (read only); default the vanilla install (EI_GAME_VANILLA), as an unpacked or
               converted copy too (tools/unpack_install.py, convert_install.py)
  --convert    ei-convert (default: EI_BUILD_DIR's, else build\\verify's, else build\\release's)

The mod (tests/scenarios/mod_example.txt checks each part):
  config.reg.toml, readme.txt, Res\\texts\\texts.toml, Res\\database\\items.idb.toml
                        examples/example_mod's files, copied as they are (our own, hand-written)
  Res\\textures\\Zone1000.png .. Zone1007.png
                        zone 1's ground pages as ei-convert writes them (a converted install's own PNGs), recoloured:
                        the green channel becomes the red one (RED_FROM_GREEN), so the grass is red; the PNG keeps
                        its "ei-texture" settings chunk, so the game makes it into a DXT1 .mmp as the game's
  Maps\\zone1.mob.toml   zone 1's objects as ei-convert writes them (a converted install's own) with
                        examples/example_mod_additions/Maps/zone1.mob.toml's records appended
Nothing of the install is committed: the made parts exist only where the tool writes them.
"""
import argparse, os, shutil, struct, subprocess, sys, tempfile, zlib

import exe_path
import make_test_mod
import unpack_install

NAME = 'example_mod'
EXAMPLES = os.path.join(exe_path.ROOT, 'examples')
PAGES = [f'zone1{i:03d}' for i in range(8)]   # zone 1's ground pages in textures.res (512 x 512 DXT1)

# The recolour, a table per channel: red from the green (scaled up), green from the red, blue from the blue (both
# darker). Grass (green over red and blue) comes out red, as capture::HueShares counts red (twice green and blue).
RED_FROM_GREEN = bytes(min(255, round(v * 1.15)) for v in range(256))
GREEN_FROM_RED = bytes(round(v * 0.45) for v in range(256))
BLUE_FROM_BLUE = bytes(round(v * 0.45) for v in range(256))


def find(folder, *names):
    """folder/names..., each name matched without case (an install copied to Linux keeps Windows' spellings); the
    path as named when no entry matches."""
    path = folder
    for name in names:
        exact = os.path.join(path, name)
        if not os.path.exists(exact) and os.path.isdir(path):
            match = [entry for entry in os.listdir(path) if entry.lower() == name.lower()]
            exact = os.path.join(path, match[0]) if match else exact
        path = exact
    return path


def ei_convert(path):
    if path:
        return path
    exe = 'ei-convert.exe' if os.name == 'nt' else 'ei-convert'
    builds = [os.environ.get('EI_BUILD_DIR')] + [os.path.join(exe_path.ROOT, 'build', b) for b in ('verify', 'release')]
    for build in builds:
        if build and os.path.isfile(os.path.join(build, exe)):
            return os.path.join(build, exe)
    sys.exit('make_example_mod: no ei-convert (build.cmd release builds build\\release\\ei-convert.exe)')


def archive_entries(install, archive, names):
    """The entries `names` (lower case) of the install's archive Res\\<archive>.res: from the archive, or from its
    folder in an unpacked copy."""
    source = find(install, 'Res', archive + '.res')
    if not os.path.exists(source):
        folder = find(install, 'Res', archive)
        return {name: open(find(folder, name), 'rb').read() for name in names}
    with open(source, 'rb') as f:
        data = f.read()
    found = {name.lower(): (offset, size) for name, offset, size, _ in unpack_install.entries(data)}
    missing = [name for name in names if name not in found]
    if missing:
        sys.exit(f'make_example_mod: {source} has no {", ".join(missing)}')
    return {name: data[found[name][0]:found[name][0] + found[name][1]] for name in names}


# --- PNG -----------------------------------------------------------------------------------------------------------

def png_chunks(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        sys.exit('make_example_mod: not a PNG')
    at, out = 8, []
    while at + 8 <= len(data):
        length, kind = struct.unpack_from('>I4s', data, at)
        out.append((kind, data[at + 8:at + 8 + length]))
        at += 12 + length
    return out


def unfilter(raw, width, height, step):
    """The rows of 8-bit samples (`step` bytes a pixel) of a PNG's inflated image data, each row's filter undone."""
    stride = width * step
    pixels = bytearray(stride * height)
    prior = bytearray(stride)
    for y in range(height):
        at = y * (stride + 1)
        kind, line = raw[at], bytearray(raw[at + 1:at + 1 + stride])
        if kind == 1:
            for i in range(step, stride):
                line[i] = (line[i] + line[i - step]) & 0xFF
        elif kind == 2:
            line = bytearray(bytes((a + b) & 0xFF for a, b in zip(line, prior)))
        elif kind == 3:
            for i in range(stride):
                line[i] = (line[i] + ((line[i - step] if i >= step else 0) + prior[i]) // 2) & 0xFF
        elif kind == 4:
            for i in range(stride):
                left = line[i - step] if i >= step else 0
                up, corner = prior[i], prior[i - step] if i >= step else 0
                p = left + up - corner
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - corner)
                line[i] = (line[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else corner)) & 0xFF
        elif kind != 0:
            sys.exit(f'make_example_mod: PNG filter {kind} unknown')
        pixels[y * stride:(y + 1) * stride] = line
        prior = line
    return pixels


def recoloured_png(data):
    """The 8-bit RGB or RGBA PNG `data` recoloured (the tables above, alpha kept), its other chunks (the ei-texture
    settings) kept; the rows written unfiltered."""
    chunks = png_chunks(data)
    width, height, bits, colour, _, _, interlace = struct.unpack('>IIBBBBB', chunks[0][1])
    if bits != 8 or colour not in (2, 6) or interlace:
        sys.exit(f'make_example_mod: the PNG is not 8-bit RGB or RGBA ({bits} bits, colour type {colour})')
    step = 3 if colour == 2 else 4
    pixels = unfilter(zlib.decompress(b''.join(body for kind, body in chunks if kind == b'IDAT')), width, height, step)
    red, green, blue = pixels[0::step], pixels[1::step], pixels[2::step]
    pixels[0::step] = green.translate(RED_FROM_GREEN)
    pixels[1::step] = red.translate(GREEN_FROM_RED)
    pixels[2::step] = blue.translate(BLUE_FROM_BLUE)
    stride = width * step
    raw = b''.join(b'\0' + pixels[y * stride:(y + 1) * stride] for y in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    out, image = b'\x89PNG\r\n\x1a\n', False
    for kind, body in chunks:
        if kind != b'IDAT':
            out += chunk(kind, body)
        elif not image:   # the image in one chunk where the first was
            out += chunk(b'IDAT', zlib.compress(raw, 6))
            image = True
    return out


# --- The parts ------------------------------------------------------------------------------------------------------

def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)


def ground_pages(install, convert, scratch):
    """Zone 1's ground pages as ei-convert's PNGs by name: a converted install's own, else written by ei-convert from
    an archive of the install's .mmp entries."""
    converted = {page: find(install, 'Res', 'textures', page + '.png') for page in PAGES}
    if all(os.path.isfile(path) for path in converted.values()):
        return {page: open(path, 'rb').read() for page, path in converted.items()}
    entries = archive_entries(install, 'textures', [page + '.mmp' for page in PAGES])
    archive = os.path.join(scratch, 'textures.res')
    write(archive, make_test_mod.archive(entries))
    subprocess.run([convert, archive, os.path.join(scratch, 'out')], check=True, capture_output=True)
    return {page: open(find(scratch, 'out', 'textures', page + '.png'), 'rb').read() for page in PAGES}


def zone1_objects(install, convert, scratch):
    """Zone 1's objects as ei-convert's TOML: a converted install's own, else written by ei-convert from a copy of the
    install's zone1.mob."""
    converted = find(install, 'Maps', 'zone1.mob.toml')
    if not os.path.isfile(converted):
        source = os.path.join(scratch, 'in', 'Maps', 'zone1.mob')
        os.makedirs(os.path.dirname(source))
        shutil.copyfile(find(install, 'Maps', 'zone1.mob'), source)
        subprocess.run([convert, os.path.join(scratch, 'in'), os.path.join(scratch, 'out')], check=True,
                       capture_output=True)
        converted = os.path.join(scratch, 'out', 'Maps', 'zone1.mob.toml')
    with open(converted, 'r', encoding='utf-8', newline='') as f:
        return f.read().replace('\r\n', '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('folder')
    parser.add_argument('--install', help='the game folder to read (default: the vanilla install)')
    parser.add_argument('--convert', help='ei-convert to run')
    args = parser.parse_args()
    install = args.install or exe_path.install_file('vanilla')
    convert = ei_convert(args.convert)
    root = os.path.join(args.folder, NAME)
    if os.path.exists(root):
        shutil.rmtree(root)
    shutil.copytree(os.path.join(EXAMPLES, NAME), root)
    os.makedirs(args.folder, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=args.folder) as scratch:
        for page, png in ground_pages(install, convert, scratch).items():
            write(os.path.join(root, 'Res', 'textures', page.capitalize() + '.png'), recoloured_png(png))
        with open(os.path.join(EXAMPLES, 'example_mod_additions', 'Maps', 'zone1.mob.toml'), encoding='utf-8') as f:
            additions = f.read()
        objects = zone1_objects(install, convert, scratch).rstrip('\n') + '\n\n' + additions
        write(os.path.join(root, 'Maps', 'zone1.mob.toml'), objects.encode('utf-8'))
    print(f'{root}: the example mod')


if __name__ == '__main__':
    main()
