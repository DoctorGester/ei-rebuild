r"""The stub game data (H41; docs/notes/linux.md, "The stub data"): a copy of an install that plays the harness's
scenarios with the same goldens, but holds no picture, sound or movie of the game, so that it is small (tens of MB
packed) and can go where the install cannot (a private repository the CI job fetches).

usage: python tools/stub_data.py build <install> <out folder> [--force]
       python tools/stub_data.py pack <out.tar.zst> <stub folder>...
       python tools/stub_data.py reads <runs folder> [<install>...]
       python tools/stub_data.py check <stub folder> <install> <runs folder>

  build   writes the stub set of <install> (read only) into <out folder> (under build\, never in git: the files are the
          game's own). Every file the runs can read keeps its name, size and format, so the game opens it as it opens
          the install's (no loader path of its own):
            .mmp textures, loose .bmp pictures   the header (size, format, levels) and the extra block kept, the pixels 0
                                                 (but KEPT below: the few whose pixels a frame check reads, whole)
            .wav sounds                          every chunk kept, the samples 0 (the same length: a voice ends by its
                                                 bytes over its byte rate; sound.md, "The rules the sound keeps")
            .mp3 speech and music                every frame header kept (the same frames: ScanMp3 counts them), the
                                                 frames' data 0 (silence), an info frame (Xing / Info / VBRI) kept whole
            .bik movies                          the header, the tracks and the frame index (count, size, rate,
                                                 keyframes) kept, every frame emptied: no sound packet and no video
                                                 packet (the decoder shows the frame before); smaller files
          inside .res archives at any depth (.res, .mpr, .mod, .anm are the same format) by the entries' names, the
          archive's layout untouched. Everything else is copied as it is: maps, models, skeletons and animations,
          databases, scripts, texts, registry files, fonts, camera paths, the Rebirth mods' files. Left out:
          LEFT_OUT below (the install's saves\: a run reads its own folder's; what the game never opens; logs). The
          programs (.exe, .dll, .asi) are empty files: no run starts them, the start-up only looks whether
          starter\addon.dll or EI_Plugin.asi is there (starter::RebirthData).
  pack    one .tar.zst of the stub folders (each under its folder's name; pack_options), for the private repository
  reads   the install files the runs in <runs folder> read (their harness_reads.txt: EI_READTRACE=1), relative to the
          installs given (default run_all's two), with the number of runs that read each; "-" before a path the run
          looked for and did not find
  check   whether every install file those runs read is in the stub set (the same relative path), and the stub
          set's files of the same size as the install's (all but the movies)
"""
import argparse, collections, fnmatch, os, shutil, struct, sys, tarfile, time

RES_MAGIC = 0x019CE23C
RES_HEADER = struct.Struct('<IIII')        # magic, count, table offset, names size (src/engine/data/resource.h)
RES_ENTRY = struct.Struct('<iIIIHI')       # next, size, offset, time, name length, name offset
MMP_HEADER = 76                            # MmpHeader (src/engine/models/texture.h): 19 dwords, the last the extra block's size
MMP_MAGIC = b'MMP\0'
PROGRAMS = ('.exe', '.dll', '.asi')        # stand-ins: empty files
# Not copied (paths below the install, lower case): the install's saves (a run reads and writes its own folder's), what
# the game never opens (ResBuild's work folder, the uninstaller, the Rebirth starter's own program, data and crash
# dumps), logs (a player's; a run starts its own) and zips (a mod's download left in the starter's Mods folder, 760 MB
# on 2026-11-05; the game opens the unpacked folders).
LEFT_OUT = ('saves', 'cache', 'uninstall', 'starter/dumps', 'starter/engine', 'starter/eirelements.db')
LEFT_OUT_TYPES = ('.log', '.zip')
# Textures kept whole, by their file or archive entry name (lower case, in any archive or folder): the few whose pixels
# a frame check reads, which fail on blanks (H51b, H51c; Windows and Linux alike). Their pixels reach no golden. Most of
# it is Rebirth's 2048 x 2048 ground and tree.
KEPT = (
    ('cursor_*.mmp', "render_cursor_present: a shape needs opaque and clear pixels, and must change with the gamma"),
    ('sm_cursor_*.mmp', 'render_cursor_present: the same pictures when the small cursors are on'),
    ('*mouth*.mmp', "render_blink: the faces' open and closed rectangles (the briefing speakers', the heroes')"),
    ('basegipat00[01].mmp', "render_shadows_outside: bz1g's ground under most of the gate's shadow (000 and 001 "
                            'take 85% of it; the shadow must lie on lit ground)'),
    ('basegipat005.mmp', "render_weather_roofs: bz1g's ground under the gateway's floor (wet against dry needs lit ground: "
                         '72.7% of the rectangle differs against at least 80% with the page blank, 95.5% with it kept; of the '
                         "other pages 002 and 003 raise it to 74.7%, 004, 006 and 007 change nothing). Rebirth's is 2.8 MB"),
    ('dgunbirdruins00.mmp', "render_smooth: the ruins in view at zone1's start (the msaa and anisotropy checks want "
                            'pixels that change with the filter: blank ones give 0.03% against at least 0.2%)'),
    ('dguntorchruins01.mmp', 'render_smooth: the same, the torch among the ruins'),
    ('ruins00.mmp', 'render_smooth: the same, the ruins themselves'),
    ('tree02.mmp', "render_smooth: the tree in view (Rebirth's is 2048 x 2048 DXT5, 5.4 MB; with the ruins alone Rebirth's "
                   'anisotropy check reaches 0.47% against at least 0.5%, with the tree alone msaa 2 0.13% against 0.2%)'),
    ('rainsnow.mmp', "the Shower and Falling particles' texture (particle_create.cpp) and the snowflakes' pictures "
                     '(weather_particles.cpp; render_weather_particles: blank ones leave the snow clear)'),
    ('footprints*.mmp', "render_shadows_decals: the ground prints' textures (ground_print.cpp); blank ones are clear"),
    ('bloodprints.mmp', 'render_shadows_decals: the same, the blood splats'),
    ('fireprints.mmp', 'render_shadows_decals: the same, the scorch marks'),
    ('zone200[0-7].mmp', "render_shadows_decals: gz2g's ground, zone2's pages (printlight compares the prints' brightness with the ground's under them: black ground gives ratios of 1.4 to 2.3)"),
    ('unhumaskin_00.mmp', "render_weather_panel: the hero Zak's skin and gear layers in the HUD panel (the wet figure is darker than the dry one: a black figure shows no difference)"),
    ('unhumaskin_07.mmp', 'render_weather_panel: the same'),
    ('unhumabt_00.le.0.mmp', 'render_weather_panel: the same'),
    ('unhumabt_02.le.0.mmp', 'render_weather_panel: the same'),
    ('unhumabt_12.cw.0.mmp', 'render_weather_panel: the same'),
    ('unhumadg_03.br.1.mmp', 'render_weather_panel: the same'),
    ('unhumagl_00.le.0.mmp', 'render_weather_panel: the same'),
    ('unhumagl_12.cw.0.mmp', 'render_weather_panel: the same'),
    ('unhumapt_00.le.0.mmp', 'render_weather_panel: the same'),
    ('unhumapt_02.lt.0.mmp', 'render_weather_panel: the same'),
    ('unhumapt_12.cw.0.mmp', 'render_weather_panel: the same'),
    ('unhumash_00.le.0.mmp', 'render_weather_panel: the same'),
    ('unhumash_12.cw.0.mmp', 'render_weather_panel: the same'),
)

# Kept in the vanilla install's stub set only (C159, H51d): render_water passes on Rebirth's set without it, and its
# copies would add 0.5 MB. Same rule as KEPT.
KEPT_VANILLA = (
    ('zone100[0-2].mmp', "render_water: zone1's ground pages (the shore's sand and the sea bed; the water layer draws the same pages)"),
)


class Counts:
    """What the stubbing did, by kind: files, bytes in, bytes zeroed or dropped."""

    def __init__(self):
        self.kinds = collections.defaultdict(lambda: [0, 0, 0])
        self.warnings = []

    def add(self, kind, size, cut):
        k = self.kinds[kind]
        k[0] += 1
        k[1] += size
        k[2] += cut

    def report(self):
        for kind, (files, size, cut) in sorted(self.kinds.items(), key=lambda kv: -kv[1][1]):
            print(f'  {kind:15} {files:6} files  {size / 2**20:9.1f} MB  {cut / 2**20:9.1f} MB blanked or dropped')
        for w in self.warnings[:20]:
            print(f'  warning: {w}')


def zero(buf, start, end):
    if end > start:
        buf[start:end] = bytes(end - start)
    return max(0, end - start)


def res_entries(buf, base, size):
    """A .res archive's entries at buf[base:base+size] as (name, start, size) with starts in buf, or None when it is
    not one (or its tables do not fit)."""
    if size < RES_HEADER.size:
        return None
    magic, count, table, names_size = RES_HEADER.unpack_from(buf, base)
    if magic != RES_MAGIC or table + count * RES_ENTRY.size + names_size > size:
        return None
    names = base + table + count * RES_ENTRY.size
    out = []
    for i in range(count):
        _, length, offset, _, name_length, name_offset = RES_ENTRY.unpack_from(buf, base + table + i * RES_ENTRY.size)
        if offset + length > size or name_offset + name_length > names_size:
            return None
        name = bytes(buf[names + name_offset:names + name_offset + name_length]).decode('cp1251')
        out.append((name, base + offset, length))
    return out


def stub_mmp(buf, start, size):
    if size < MMP_HEADER or bytes(buf[start:start + 4]) != MMP_MAGIC:
        return 0
    extra = struct.unpack_from('<I', buf, start + MMP_HEADER - 4)[0]
    return zero(buf, min(start + MMP_HEADER + extra, start + size), start + size)


def stub_bmp(buf, start, size):
    if size < 14 or bytes(buf[start:start + 2]) != b'BM':
        return 0
    bits = struct.unpack_from('<I', buf, start + 10)[0]
    return zero(buf, min(start + bits, start + size), start + size)


def stub_wav(buf, start, size):
    """The 'data' chunk's bytes zeroed, every chunk and its size kept."""
    end = start + size
    if size < 12 or bytes(buf[start:start + 4]) != b'RIFF' or bytes(buf[start + 8:start + 12]) != b'WAVE':
        return 0
    at, cut = start + 12, 0
    while at + 8 <= end:
        tag = bytes(buf[at:at + 4])
        length = struct.unpack_from('<I', buf, at + 4)[0]
        body = at + 8
        if tag == b'data':
            cut += zero(buf, body, min(body + length, end))
        at = body + length + (length & 1)
    return cut


MP3_RATES = ((44100, 48000, 32000), (22050, 24000, 16000), (11025, 12000, 8000))
MP3_KBPS = ((0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320),
            (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160))
INFO_TAGS = (b'Xing', b'Info', b'VBRI', b'LAME')


def stub_mp3(buf, start, size):
    """Each layer 3 frame's data zeroed after its header (and CRC), walked as the game's ScanMp3 walks them
    (src/audio/audio.cpp), so the frames and their samples stay; an ID3 tag, an info frame and trailing bytes kept."""
    end = start + size
    at, cut = start, 0
    if size >= 10 and bytes(buf[at:at + 3]) == b'ID3':
        p = buf[at + 6:at + 10]
        at += 10 + ((p[0] & 0x7F) << 21 | (p[1] & 0x7F) << 14 | (p[2] & 0x7F) << 7 | (p[3] & 0x7F))
    while at + 4 <= end:
        h = struct.unpack_from('>I', buf, at)[0]
        version, layer, rate_index, sample_index = h >> 19 & 3, h >> 17 & 3, h >> 12 & 15, h >> 10 & 3
        if (h >> 21) != 0x7FF or layer != 1 or version == 1 or rate_index in (0, 15) or sample_index == 3:
            at += 1
            continue
        mpeg1 = version == 3
        rate = MP3_RATES[0 if mpeg1 else 1 if version == 2 else 2][sample_index]
        length = (144000 if mpeg1 else 72000) * MP3_KBPS[0 if mpeg1 else 1][rate_index] // rate + (h >> 9 & 1)
        if at + length > end:
            break
        data = at + 4 + (0 if h >> 16 & 1 else 2)     # the protection bit clear: a 16-bit CRC follows the header
        if not any(tag in bytes(buf[data:min(data + 64, at + length)]) for tag in INFO_TAGS):
            cut += zero(buf, data, at + length)
        at += length
    return cut


def stub_in_place(buf, start, size, name, counts, depth=0):
    """Blanks the pictures and sounds of the file `name` at buf[start:start+size], keeping its size: by its name's
    extension, and into a .res archive's entries (any name: .mpr maps, .mod / .anm figures are archives too)."""
    entries = res_entries(buf, start, size)
    if entries is not None:
        # Entries may share their bytes under two names (Rebirth's textures.res: Zone2000 and bz2g000 are one range,
        # H62e): the other name of a kept entry's range is kept with it, or its blanking would zero the kept pixels.
        shared = {(at, length) for entry, at, length in entries if kept(entry)}
        for entry, at, length in entries:
            if (at, length) in shared and not kept(entry):
                counts.add('kept', length, 0)
                continue
            stub_in_place(buf, at, length, entry, counts, depth + 1)
        if depth == 0:
            counts.add('.res archives', size, 0)
        return
    ext = os.path.splitext(name)[1].lower()
    if kept(name):
        counts.add('kept', size, 0)
        return
    kind = {'.mmp': stub_mmp, '.bmp': stub_bmp, '.wav': stub_wav, '.mp3': stub_mp3}.get(ext)
    cut = kind(buf, start, size) if kind else 0
    if ext == '.bik':
        counts.warnings.append(f'a movie inside an archive kept as it is: {name}')
    if depth == 0 or kind:
        counts.add(ext.lstrip('.') or '(none)', size, cut)


rebirth_install = False        # set by build(): the install being stubbed has Rebirth's starter folder


def kept(name):
    base = name.replace('\\', '/').rsplit('/', 1)[-1].lower()
    kept_here = KEPT if rebirth_install else KEPT + KEPT_VANILLA
    return any(fnmatch.fnmatchcase(base, pattern) for pattern, _ in kept_here)


def stub_bik(data):
    """The movie with every frame emptied: header, tracks and frame index kept (frame count, size, rate, flags,
    keyframe bits), each frame then only its tracks' packet sizes, 0 (no sound), and no video packet (fewer than 4
    bytes: src/video/bink_video.cpp's Decode keeps the frame before). The file size and the largest frame's size in
    the header follow."""
    if len(data) < 44 or data[:4] != b'BIKi':
        return None
    frames, tracks = struct.unpack_from('<I', data, 8)[0], struct.unpack_from('<I', data, 40)[0]
    index_at = 44 + 12 * tracks
    if index_at + 4 * (frames + 1) > len(data):
        return None
    old = struct.unpack_from(f'<{frames + 1}I', data, index_at)
    frame_bytes = 4 * tracks
    first = index_at + 4 * (frames + 1)
    index = [first + i * frame_bytes | (old[i] & 1) for i in range(frames)] + [first + frames * frame_bytes]
    out = bytearray(data[:index_at])
    out += struct.pack(f'<{frames + 1}I', *index)
    out += bytes(frames * frame_bytes)
    struct.pack_into('<I', out, 4, len(out) - 8)
    struct.pack_into('<I', out, 12, frame_bytes)
    return bytes(out)


def build(install, out, force=False):
    install = os.path.abspath(install)
    out = os.path.abspath(out)
    def inside(a, b):
        return os.path.normcase(a + os.sep).startswith(os.path.normcase(b + os.sep))
    if inside(install, out) or inside(out, install):
        sys.exit('stub_data: the stub folder and the install must not hold each other')
    if os.path.exists(out):
        if not force:
            sys.exit(f'stub_data: {out} exists (--force replaces it)')
        remove_tree(out)
    global rebirth_install
    rebirth_install = os.path.isdir(os.path.join(install, 'starter'))
    counts = Counts()
    for folder, dirs, files in os.walk(install):
        relative = os.path.relpath(folder, install)

        def left_out(name):
            path = os.path.normpath(os.path.join(relative, name)).replace(os.sep, '/').lower()
            return path in LEFT_OUT or name.lower().endswith(LEFT_OUT_TYPES)
        dirs[:] = [d for d in dirs if not left_out(d)]
        files = [f for f in files if not left_out(f)]
        for d in dirs:
            if os.path.islink(os.path.join(folder, d)):
                counts.warnings.append(f'a link left out: {os.path.join(relative, d)}')
        dirs[:] = [d for d in dirs if not os.path.islink(os.path.join(folder, d))]
        target = os.path.join(out, relative)
        os.makedirs(target, exist_ok=True)
        for name in files:
            source, dest = os.path.join(folder, name), os.path.join(target, name)
            ext = os.path.splitext(name)[1].lower()
            size = os.path.getsize(source)
            if ext in PROGRAMS:
                open(dest, 'wb').close()
                counts.add('program', size, size)
                continue
            with open(source, 'rb') as f:
                data = f.read()
            if ext == '.bik':
                small = stub_bik(data)
                if small is None:
                    counts.warnings.append(f'not a Bink 1 movie, kept: {os.path.join(relative, name)}')
                    small = data
                counts.add('bik', size, size - len(small))
                data = small
            else:
                buf = bytearray(data)
                stub_in_place(buf, 0, len(buf), name, counts)
                data = buf
            with open(dest, 'wb') as f:
                f.write(data)
            shutil.copystat(source, dest)
    print(f'stub_data: {install} -> {out}')
    counts.report()
    total = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
    print(f'  {total / 2**20:.1f} MB on disk')


def remove_tree(path):
    """Removes a stub folder the tool wrote; refuses one holding a link (never through a junction into an install)."""
    for folder, dirs, files in os.walk(path):
        for name in dirs + files:
            p = os.path.join(folder, name)
            if os.path.islink(p) or (hasattr(os.path, 'isjunction') and os.path.isjunction(p)):
                sys.exit(f'stub_data: {p} is a link; remove it by hand first')
    shutil.rmtree(path)


def pack_options():
    """The packed set's compression: zstd at level 22 with a 128 MB window and long-distance matching (zstd -22 --ultra
    --long=27; unpacking needs no flag, 128 MB is zstd's default limit), so that one install's copy of a file right after
    the other's costs next to nothing (pack orders them so). Measured on the 2026-11-03 sets: 74.5 MB against xz -9's
    65.0 MB (256 MB window), unpacked in 1 s against 7 to 9 s. Python 3.14's own zstd."""
    try:
        from compression import zstd
    except ImportError:
        sys.exit('stub_data pack: needs Python 3.14 or later (compression.zstd)')
    p = zstd.CompressionParameter
    return zstd, {p.compression_level: 22, p.window_log: 27, p.enable_long_distance_matching: 1}


def pack(archive, folders):
    """The stub folders in one .tar.zst, each under its folder's name, the files of the same path in each folder next to
    each other (the two installs share most of their models, maps and databases). A modification time past now (some
    install files carry the largest one Windows has) is written as now."""
    zstd, options = pack_options()
    now = int(time.time())
    members = []
    for folder in folders:
        top = os.path.basename(os.path.normpath(folder))
        for d, dirs, files in os.walk(folder):
            for name in dirs + files:
                relative = os.path.relpath(os.path.join(d, name), folder).replace(os.sep, '/')
                members.append((relative.lower(), top, relative, os.path.join(d, name)))
    members.sort()
    with zstd.open(archive, 'wb', options=options) as packed, \
            tarfile.open(fileobj=packed, mode='w', format=tarfile.PAX_FORMAT) as tar:
        for _, top, relative, path in members:
            info = tar.gettarinfo(path, arcname=f'{top}/{relative}')
            info.mtime = min(int(info.mtime), now)
            if info.isfile():
                with open(path, 'rb') as f:
                    tar.addfile(info, f)
            else:
                tar.addfile(info)
    print(f'stub_data: {archive}: {os.path.getsize(archive) / 2**20:.1f} MB')


def default_installs():
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import run_all
    return [run_all.install_dir('vanilla'), run_all.install_dir('rebirth')]


def reads_of(runs, installs):
    """{(install index, relative path in lower case): [runs that read it, found]} over the runs' harness_reads.txt."""
    roots = [os.path.normcase(os.path.abspath(i)).rstrip('\\/') for i in installs]
    seen = {}
    for folder, _, files in os.walk(runs):
        if 'harness_reads.txt' not in files:
            continue
        with open(os.path.join(folder, 'harness_reads.txt'), encoding='utf-8', errors='replace') as f:
            for line in f:
                mark, path = line[:1], line[2:].strip()
                full = os.path.normcase(path)
                for i, root in enumerate(roots):
                    if full.startswith(root + os.sep):
                        key = (i, full[len(root) + 1:])
                        entry = seen.setdefault(key, [0, mark == '+'])
                        entry[0] += 1
                        break
    return seen


def reads(runs, installs):
    installs = installs or default_installs()
    seen = reads_of(runs, installs)
    for (i, relative), (count, found) in sorted(seen.items()):
        print(f'{"+" if found else "-"} {count:4}  {installs[i]}{os.sep}{relative}')


def check(stub, install, runs):
    seen = reads_of(runs, [install])
    missing = [r for (_, r), (_, found) in seen.items() if found and not os.path.exists(os.path.join(stub, r))]
    extra = [r for (_, r), (_, found) in seen.items() if not found and os.path.exists(os.path.join(stub, r))]
    sizes = []
    for folder, _, files in os.walk(stub):
        for name in files:
            s = os.path.join(folder, name)
            o = os.path.join(install, os.path.relpath(s, stub))
            if not name.lower().endswith(PROGRAMS + ('.bik',)) and os.path.exists(o) and \
                    os.path.getsize(o) != os.path.getsize(s):
                sizes.append(os.path.relpath(s, stub))
    for r in missing:
        print(f'read by the runs, not in the stub set: {r}')
    for r in extra:
        print(f'missing for the runs, in the stub set: {r}')
    for r in sizes:
        print(f'another size than the install\'s: {r}')
    print(f'stub_data check: {len(seen)} paths read, {len(missing)} missing, {len(extra)} extra, {len(sizes)} sizes differ')
    return 1 if missing or extra or sizes else 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='command', required=True)
    b = sub.add_parser('build')
    b.add_argument('install')
    b.add_argument('out')
    b.add_argument('--force', action='store_true')
    k = sub.add_parser('pack')
    k.add_argument('archive')
    k.add_argument('folders', nargs='+')
    r = sub.add_parser('reads')
    r.add_argument('runs')
    r.add_argument('installs', nargs='*')
    c = sub.add_parser('check')
    c.add_argument('stub')
    c.add_argument('install')
    c.add_argument('runs')
    a = p.parse_args(argv)
    if a.command == 'build':
        build(a.install, a.out, a.force)
    elif a.command == 'pack':
        pack(a.archive, a.folders)
    elif a.command == 'reads':
        reads(a.runs, a.installs)
    else:
        return check(a.stub, a.install, a.runs)
    return 0


if __name__ == '__main__':
    sys.exit(main())
