"""The showcase mod (examples/showcase_mod, docs/notes/mods.md "The showcase mod"): writes its made files, or copies it.

usage: python tools/make_showcase_mod.py <folder>     copies examples/showcase_mod to <folder>\\showcase_mod (a
                                                     scenario's prepare line: the run's harness_saves)
       python tools/make_showcase_mod.py --write     writes the made files into examples/showcase_mod
       python tools/make_showcase_mod.py --check     says whether the made files are the ones --write would write
                                                     (their pixels and glTF data, not the bytes: zlib's output may
                                                     differ between versions)

Every made file comes from this script alone (no game data is read): procedural pixels and boxes, drawn with one
seeded generator, so the same script writes the same files. The hand-written files of the mod (its config, texts,
databases, map objects and script) are in examples/showcase_mod as they are.

Made files:
  Maps\\showcase\\map.toml, heights.png, tiles.png   the island's ground (the mod's own zone; 8 x 6 sectors)
  Res\\textures\\showcase000.png                     its ground page: grass, earth, rock and sand tiles
  Res\\textures\\showcasemap.png, showcasequest.png  its minimap and objectives pictures
  Res\\textures\\scgolem00.png                       the golem's skin
  Res\\figures\\scgolem.glb                          the golem: its parts and 13 animations
  Res\\textures\\scshard00.png                       the golem's shards' skin (the golem's figure, smaller)
  Res\\textures\\chainspark.png                      the Chain Spark's 16 spark pictures (its data effects' texture)
  Res\\textures\\spell0101.png, prototype0101.png     the Chain Spark's pictures (icon 101): its spell items and
                                                     scrolls, the spell maker's prototype
  Res\\figures\\initwehm9.glb, initarbt20.glb         the hammer's and the boots' inventory models
  Res\\figures\\hm_09.glb                            the hammer's worn model: its part in the hand of each figure
                                                     that wears clubs (docs/notes/mods.md, "Worn models")
  Res\\redress\\unhumahm_09.gr.0.png                  the hammer's texture (granite), in the weapons' strip
  Res\\redress\\unhumabt_20.le.0.png                  the boots' layer over the hero's skin (thin leather)
"""
import json, math, os, random, shutil, struct, sys, zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MOD = os.path.join(ROOT, 'examples', 'showcase_mod')
NAME = 'showcase_mod'

TICK = 0.055   # a world tick: the game plays one animation key a tick


# --- PNG ---------------------------------------------------------------------------------------------------------

def png(width, height, kind, pixels):
    """A PNG of `pixels` (a flat list, row by row): kind 'grey16' (ints 0..65535), 'rgb' or 'rgba' (tuples of 0..255)."""
    colour, depth = {'grey16': (0, 16), 'rgb': (2, 8), 'rgba': (6, 8)}[kind]
    rows = []
    for y in range(height):
        row = pixels[y * width:(y + 1) * width]
        if kind == 'grey16':
            rows.append(b'\0' + struct.pack('>%dH' % width, *row))
        else:
            rows.append(b'\0' + bytes(c for p in row for c in p))

    def chunk(tag, data):
        return struct.pack('>I', len(data)) + tag + data + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    header = struct.pack('>IIBBBBB', width, height, depth, colour, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) +
            chunk(b'IEND', b''))


def png_pixels(data):
    """The header and the inflated rows of a PNG this script writes (filter 0), for --check."""
    at, idat, header = 8, b'', None
    while at < len(data):
        length, tag = struct.unpack_from('>I4s', data, at)
        if tag == b'IHDR':
            header = struct.unpack_from('>IIBB', data, at + 8)
        elif tag == b'IDAT':
            idat += data[at + 8:at + 8 + length]
        at += 12 + length
    return header, zlib.decompress(idat)


# --- Noise ---------------------------------------------------------------------------------------------------------

class Noise:
    """Smooth value noise on a lattice that repeats every `period` steps each way."""

    def __init__(self, rng, period):
        self.period = period
        self.values = [rng.random() for _ in range(period * period)]

    def at(self, x, y):
        xi, yi = math.floor(x), math.floor(y)
        fx, fy = x - xi, y - yi
        sx, sy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        p = self.period
        v = self.values
        x0, x1, y0, y1 = xi % p, (xi + 1) % p, (yi % p) * p, ((yi + 1) % p) * p
        a = v[y0 + x0] + (v[y0 + x1] - v[y0 + x0]) * sx
        b = v[y1 + x0] + (v[y1 + x1] - v[y1 + x0]) * sx
        return a + (b - a) * sy


class Fractal:
    """Octaves of Noise over a square of side `size` that repeats: octave k has 2^k x `cells` lattice steps."""

    def __init__(self, rng, cells, octaves, size):
        self.layers = [Noise(rng, cells << k) for k in range(octaves)]
        self.size = size

    def at(self, x, y):
        total, weight, amplitude = 0.0, 0.0, 1.0
        for noise in self.layers:
            scale = noise.period / self.size
            total += amplitude * noise.at(x * scale, y * scale)
            weight += amplitude
            amplitude *= 0.5
        return total / weight


def clamp(v, lo=0.0, hi=1.0):
    return lo if v < lo else hi if v > hi else v


def mix(a, b, t):
    return tuple(int(round(clamp(x + (y - x) * t, 0, 255))) for x, y in zip(a, b))


# --- The ground ----------------------------------------------------------------------------------------------------

SECTORS = (8, 6)                                   # Res\texts\map.toml's size of the zone
WIDTH, HEIGHT = 32 * SECTORS[0], 32 * SECTORS[1]   # cells (world units)
HEIGHT_RANGE = 20.0
START = (54.0, 138.0)                              # the party's arrival: map.toml's exit 1, (49, 133)-(59, 143)
GOLEM = (78.0, 116.0)                              # the golem's guard post (Maps\showcase.mob.toml)
EXIT = (187.0, 114.0)                              # map.toml's exit 2 to the base bz1g, (181, 108)-(193, 120)

# The ground page: 8 x 8 tiles of 64 pixels; the game draws a tile's inner 48 (its UVs keep 8 pixels from each edge).
PAGE, TILE, INNER, MARGIN = 512, 64, 48, 8
KINDS = ['grass', 'earth', 'rock', 'sand']         # two rows of tiles each, 16 tiles a kind, from the bottom up
CLASSES = {'grass': 0, 'earth': 1, 'rock': 2, 'sand': 3}   # the tile classes: footstep sounds and prints
SURFACES = ['grass', 'ground', 'stone', 'sand']         # the names of classes 0..3 (docs/modding.md, "Values by name")
COLOURS = {
    'grass': ((52, 92, 34), (96, 140, 52)),
    'earth': ((92, 70, 46), (140, 112, 76)),
    'rock': ((96, 96, 100), (150, 148, 144)),
    'sand': ((170, 150, 108), (206, 190, 146)),
}


def segment_distance(p, a, b):
    ax, ay = b[0] - a[0], b[1] - a[1]
    t = clamp(((p[0] - a[0]) * ax + (p[1] - a[1]) * ay) / (ax * ax + ay * ay))
    dx, dy = p[0] - (a[0] + ax * t), p[1] - (a[1] + ay * t)
    return math.hypot(dx, dy)


PATH = [START, GOLEM, (130.0, 104.0), EXIT]


def path_distance(x, y):
    return min(segment_distance((x, y), PATH[i], PATH[i + 1]) for i in range(len(PATH) - 1))


def ground_height(hills, x, y):
    """The ground's height in world units at map point (x, y): a valley of low hills inside a ring of high ground."""
    edge = min(x, y, WIDTH - x, HEIGHT - y)
    ring = clamp((24.0 - edge) / 24.0) ** 2 * 11.0           # the rim rises over the last 24 units
    rolling = (hills.at(x, y) - 0.5) * 5.0                    # gentle hills
    flat = clamp(path_distance(x, y) / 14.0)                  # the path and its clearings are level
    clearing = min(math.hypot(x - START[0], y - START[1]), math.hypot(x - GOLEM[0], y - GOLEM[1]))
    flat = min(flat, clamp((clearing - 6.0) / 10.0))
    return 3.0 + ring + rolling * flat


def ground_layers(rng):
    hills = Fractal(rng, 4, 3, 256.0)
    patches = Fractal(rng, 8, 2, 256.0)
    heights = []
    for y in range(HEIGHT + 1):
        for x in range(WIDTH + 1):
            h = ground_height(hills, x, y)
            heights.append(int(round(clamp(h / HEIGHT_RANGE) * 65534)))
    tiles = []
    for ty in range(HEIGHT // 2):
        for tx in range(WIDTH // 2):
            x, y = tx * 2 + 1.0, ty * 2 + 1.0
            h = ground_height(hills, x, y)
            if h > 9.0:
                kind = 'rock'
            elif path_distance(x, y) < 2.2:
                kind = 'earth'
            elif h > 7.0:
                kind = 'sand' if patches.at(x, y) > 0.5 else 'rock'
            else:
                kind = 'earth' if patches.at(x, y) > 0.68 else 'grass'
            texture = KINDS.index(kind) * 16 + rng.randrange(16)
            tiles.append((texture & 0xFF, texture >> 8, 0))   # no turn: a kind's tiles meet without seams
    return heights, tiles


def island_map(heights, tiles, darker):
    """showcasemap / showcasequest (256 x 256, the minimap's and the objectives screen's picture of the island): the
    tiles' colours shaded by the slope, the map's 256 x 192 cells at the picture's top (as zone 1's pictures cover the
    map's width)."""
    size = 256
    pixels = []
    for y in range(size):
        for x in range(size):
            if y >= HEIGHT:
                pixels.append((0, 0, 0))
                continue
            tile = tiles[(y // 2) * (WIDTH // 2) + x // 2]
            kind = KINDS[(tile[0] | tile[1] << 8) // 16]
            dark, light = COLOURS[kind]
            west = heights[y * (WIDTH + 1) + max(x - 1, 0)]
            east = heights[y * (WIDTH + 1) + min(x + 1, WIDTH)]
            shade = clamp(0.6 + (east - west) / 65534.0 * HEIGHT_RANGE * 0.4, 0.2, 1.0) * darker
            pixels.append(mix((0, 0, 0), mix(dark, light, 0.5), shade))
    return pixels


def map_toml():
    classes = [CLASSES[kind] for kind in KINDS for _ in range(16)]
    lines = [
        '# The showcase island (tools/make_showcase_mod.py): the map of the mod\'s own zone (docs/modding.md, "Maps',
        '# as folders"). Its normals are worked out from heights.png; it has no water.',
        'name = "Showcase"',
        'height_range = %r' % HEIGHT_RANGE,
        'sectors = [%d, %d]' % SECTORS,
        'pages = 1',
        'page_size = %d' % PAGE,
        'tile_size = %d' % TILE,
        'water_sectors = [',
    ]
    lines += ['    "%s",' % ('.' * SECTORS[0]) for _ in range(SECTORS[1])]
    lines += [']', '# Per tile texture its class: the ground\'s surface (footsteps and prints).', 'classes = [']
    lines += ['    ' + ', '.join('"%s"' % SURFACES[c] for c in classes[i:i + 8]) + ',' for i in range(0, len(classes), 8)]
    lines += [']', 'animated = []', '']
    return '\n'.join(lines)


def ground_page(rng):
    """showcase000: the four kinds' tiles. Each tile's inner 48 pixels repeat a kind's pattern of period 48, so tiles of
    one kind meet without a seam; each tile adds a few spots of its own inside."""
    patterns = {kind: Fractal(rng, 4, 4, float(INNER)) for kind in KINDS}
    pixels = [None] * (PAGE * PAGE)
    for index in range(64):
        kind = KINDS[index // 16]
        pattern = patterns[kind]
        dark, light = COLOURS[kind]
        spots = [(rng.uniform(12, 52), rng.uniform(12, 52), rng.uniform(1.5, 3.5)) for _ in range(rng.randrange(2, 6))]
        ox, oy = (index % 8) * TILE, (7 - index // 8) * TILE   # the page's tile rows count from its bottom
        for py in range(TILE):
            for px in range(TILE):
                u, v = (px - MARGIN) % INNER, (py - MARGIN) % INNER
                t = pattern.at(u, v)
                if kind == 'grass':
                    t = t * 0.8 + 0.2 * pattern.at(v * 2.0, u * 2.0)
                colour = mix(dark, light, (t - 0.25) * 1.8)
                for sx, sy, r in spots:
                    if math.hypot(px - sx, py - sy) < r:
                        colour = mix(colour, light if kind != 'rock' else dark, 0.5)
                pixels[(oy + py) * PAGE + ox + px] = colour
    return pixels


# --- glTF ------------------------------------------------------------------------------------------------------------

def game_to_gltf(p):
    """The game's (x, y, z), z up, as glTF's (x, z, -y), y up (docs/notes/mods.md, "Models as glTF")."""
    return (p[0], p[2], -p[1])


def box(centre, size, uv):
    """A box's 24 corners and 12 triangles in the game's axes: (positions, normals, uvs, indices). uv: (u0, v0, u1,
    v1), the texture rectangle every face takes."""
    cx, cy, cz = centre
    hx, hy, hz = size[0] / 2, size[1] / 2, size[2] / 2
    faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
             ((0, 1, 0), (-1, 0, 0), (0, 0, 1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (1, 0, 0), (0, -1, 0))]
    positions, normals, uvs, indices = [], [], [], []
    for n, s, t in faces:
        base = len(positions)
        for a, b in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            positions.append((cx + (n[0] + a * s[0] + b * t[0]) * hx, cy + (n[1] + a * s[1] + b * t[1]) * hy,
                              cz + (n[2] + a * s[2] + b * t[2]) * hz))
            normals.append(n)
            uvs.append((uv[0] if a < 0 else uv[2], uv[3] if b < 0 else uv[1]))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, indices


def merge(*shapes):
    out = ([], [], [], [])
    for positions, normals, uvs, indices in shapes:
        base = len(out[0])
        out[0].extend(positions)
        out[1].extend(normals)
        out[2].extend(uvs)
        out[3].extend(i + base for i in indices)
    return out


class Gltf:
    """A glTF 2.0 binary: nodes with one mesh each (in the game's axes, turned to glTF's), animations."""

    def __init__(self):
        self.binary = bytearray()
        self.doc = {'asset': {'version': '2.0', 'generator': 'tools/make_showcase_mod.py'}, 'scene': 0,
                    'scenes': [{'nodes': []}], 'nodes': [], 'meshes': [], 'materials': [], 'accessors': [],
                    'bufferViews': []}

    def accessor(self, kind, component, values, target=None, bounds=False):
        while len(self.binary) % 4:
            self.binary.append(0)
        fmt = {5126: 'f', 5123: 'H'}[component]
        flat = [c for v in values for c in (v if isinstance(v, tuple) else (v,))]
        data = struct.pack('<%d%s' % (len(flat), fmt), *flat)
        view = {'buffer': 0, 'byteOffset': len(self.binary), 'byteLength': len(data)}
        if target:
            view['target'] = target
        self.binary += data
        self.doc['bufferViews'].append(view)
        accessor = {'bufferView': len(self.doc['bufferViews']) - 1, 'componentType': component, 'count': len(values),
                    'type': kind}
        if bounds:
            width = len(values[0]) if isinstance(values[0], tuple) else 1
            accessor['min'] = [min((v if width > 1 else (v,))[i] for v in values) for i in range(width)]
            accessor['max'] = [max((v if width > 1 else (v,))[i] for v in values) for i in range(width)]
        self.doc['accessors'].append(accessor)
        return len(self.doc['accessors']) - 1

    def material(self, name):
        names = [m['name'] for m in self.doc['materials']]
        if name not in names:
            self.doc['materials'].append({'name': name})
            names.append(name)
        return names.index(name)

    def node(self, name, translation, shape, material, parent=None):
        """A part: its mesh (the game's axes, relative to the part; None: a node without one) and its translation from
        its parent."""
        node = {'name': name}
        if shape is not None:
            node['mesh'] = self.mesh(name, shape, material)
        node['translation'] = list(game_to_gltf(translation))
        self.doc['nodes'].append(node)
        index = len(self.doc['nodes']) - 1
        if parent is None:
            self.doc['scenes'][0]['nodes'].append(index)
        else:
            self.doc['nodes'][parent].setdefault('children', []).append(index)
        return index

    def mesh(self, name, shape, material):
        positions, normals, uvs, indices = shape
        primitive = {
            'attributes': {
                'POSITION': self.accessor('VEC3', 5126, [game_to_gltf(p) for p in positions], 34962, True),
                'NORMAL': self.accessor('VEC3', 5126, [game_to_gltf(n) for n in normals], 34962),
                'TEXCOORD_0': self.accessor('VEC2', 5126, uvs, 34962),
            },
            'indices': self.accessor('SCALAR', 5123, indices, 34963),
            'material': self.material(material),
        }
        self.doc['meshes'].append({'name': name, 'primitives': [primitive]})
        return len(self.doc['meshes']) - 1

    def animation(self, name, frames, rotations, translations):
        """One animation of frames + 1 keys a tick apart: rotations {node: [quaternion (x, y, z, w), game axes]},
        translations {node: [position, game axes]}."""
        times = self.accessor('SCALAR', 5126, [round(k * TICK, 6) for k in range(frames + 1)], None, True)
        channels, samplers = [], []
        for node, keys in rotations.items():
            values = [(q[0], q[2], -q[1], q[3]) for q in keys]   # a rotation's axis turned as a point
            samplers.append({'input': times, 'output': self.accessor('VEC4', 5126, values), 'interpolation': 'LINEAR'})
            channels.append({'sampler': len(samplers) - 1, 'target': {'node': node, 'path': 'rotation'}})
        for node, keys in translations.items():
            values = [game_to_gltf(p) for p in keys]
            samplers.append({'input': times, 'output': self.accessor('VEC3', 5126, values), 'interpolation': 'LINEAR'})
            channels.append({'sampler': len(samplers) - 1, 'target': {'node': node, 'path': 'translation'}})
        self.doc.setdefault('animations', []).append({'name': name, 'channels': channels, 'samplers': samplers})

    def glb(self, scene_extras=None):
        doc = dict(self.doc)
        if scene_extras:
            doc['scenes'] = [dict(doc['scenes'][0], extras=scene_extras)]
        while len(self.binary) % 4:
            self.binary.append(0)
        doc['buffers'] = [{'byteLength': len(self.binary)}]
        text = json.dumps(doc, separators=(',', ':')).encode()
        text += b' ' * (-len(text) % 4)
        body = struct.pack('<I4s', len(text), b'JSON') + text + struct.pack('<I4s', len(self.binary), b'BIN\0') + self.binary
        return struct.pack('<4sII', b'glTF', 2, 12 + len(body)) + body


def axis_angle(axis, degrees):
    length = math.sqrt(sum(a * a for a in axis))
    half = math.radians(degrees) / 2
    s = math.sin(half) / length
    return (axis[0] * s, axis[1] * s, axis[2] * s, math.cos(half))


# --- The golem -------------------------------------------------------------------------------------------------------

# Its texture (scgolem00, 128 x 128): stone above, the eyes' panel at the top right.
STONE_UV = (0.0, 0.25, 1.0, 1.0)
FACE_UV = (0.5, 0.0, 1.0, 0.25)

# Parts in the game's axes (z up, the figure facing -y): name, parent, translation from the parent, boxes (centre and
# size relative to the part), all times GOLEM_SCALE. The hips are the root; the feet reach 0.82 below them, so the
# animations hold the hips GOLEM_HIPS above the figure's origin, the ground (the hero's are 0.95 above it).
GOLEM_PARTS = [
    ('hp', None, (0, 0, 0), [((0, 0, 0.02), (0.34, 0.2, 0.16))]),
    ('bd', 'hp', (0, 0, 0.1), [((0, 0, 0.17), (0.46, 0.28, 0.34))]),
    ('hd', 'bd', (0, -0.02, 0.36), [((0, 0, 0.09), (0.2, 0.2, 0.18)), ((0, -0.105, 0.11), (0.16, 0.012, 0.08))]),
    ('lh1', 'bd', (0.28, 0, 0.28), [((0.02, 0, -0.12), (0.14, 0.14, 0.28))]),
    ('lh2', 'lh1', (0.02, 0, -0.26), [((0, 0, -0.12), (0.12, 0.12, 0.26))]),
    ('lh3', 'lh2', (0, 0, -0.26), [((0, 0, -0.06), (0.16, 0.16, 0.14))]),
    ('rh1', 'bd', (-0.28, 0, 0.28), [((-0.02, 0, -0.12), (0.14, 0.14, 0.28))]),
    ('rh2', 'rh1', (-0.02, 0, -0.26), [((0, 0, -0.12), (0.12, 0.12, 0.26))]),
    ('rh3', 'rh2', (0, 0, -0.26), [((0, 0, -0.06), (0.16, 0.16, 0.14))]),
    ('ll1', 'hp', (0.1, 0, -0.04), [((0, 0, -0.17), (0.15, 0.15, 0.34))]),
    ('ll2', 'll1', (0, 0, -0.34), [((0, 0, -0.16), (0.13, 0.13, 0.32))]),
    ('ll3', 'll2', (0, 0, -0.34), [((0, -0.03, -0.04), (0.15, 0.22, 0.08))]),
    ('rl1', 'hp', (-0.1, 0, -0.04), [((0, 0, -0.17), (0.15, 0.15, 0.34))]),
    ('rl2', 'rl1', (0, 0, -0.34), [((0, 0, -0.16), (0.13, 0.13, 0.32))]),
    ('rl3', 'rl2', (0, 0, -0.34), [((0, -0.03, -0.04), (0.15, 0.22, 0.08))]),
]

GOLEM_SCALE = 1.4
GOLEM_HIPS = 0.82 * GOLEM_SCALE

# The animation list (Res\database\scgolem.adb.toml) plays each to its `frames`; the keys below are made for those.
GOLEM_ANIMATIONS = {
    'uattack01': 43, 'uattack02': 29, 'udeath01': 25, 'cdeath02': 21, 'udeath03': 15, 'cidle': 68, 'cwalk': 16,
    'crun': 10, 'uhit01': 33, 'uhit02': 10, 'ucross01': 73, 'ucross02': 15, 'crest': 45,
}


def golem_pose(name, t):
    """{part: (axis, degrees)} and the hips' position at t in 0..1 of animation `name`."""
    wave = math.sin(t * 2 * math.pi)
    pose, hips = {}, (0.0, 0.0, 0.0)
    if name in ('cwalk', 'crun', 'ucross01', 'ucross02'):
        swing = 34 if name == 'crun' else 22
        pose = {'ll1': ((1, 0, 0), swing * wave), 'rl1': ((1, 0, 0), -swing * wave),
                'll2': ((1, 0, 0), -12 * max(0.0, -wave)), 'rl2': ((1, 0, 0), -12 * max(0.0, wave)),
                'lh1': ((1, 0, 0), -swing * 0.7 * wave), 'rh1': ((1, 0, 0), swing * 0.7 * wave),
                'bd': ((0, 0, 1), 4 * wave)}
        hips = (0.0, 0.0, 0.03 * abs(math.sin(t * 2 * math.pi)))
    elif name in ('cidle', 'crest'):
        breathe = math.sin(t * 2 * math.pi)
        pose = {'bd': ((1, 0, 0), 2 * breathe), 'hd': ((0, 0, 1), 12 * math.sin(t * 4 * math.pi) if name == 'cidle' else 0),
                'lh1': ((0, 1, 0), -3 * breathe), 'rh1': ((0, 1, 0), 3 * breathe)}
        hips = (0.0, 0.0, -0.01 * (1 - breathe) if name == 'cidle' else -0.06)
    elif name.startswith('uattack'):
        # Both fists raised over the head, then brought down at the moment of the hit (about 55%).
        lift = math.sin(clamp(t / 0.5) * math.pi / 2)
        strike = clamp((t - 0.45) / 0.15)
        recover = clamp((t - 0.7) / 0.3)
        arm = -150 * lift * (1 - strike) + (-40) * strike * (1 - recover)
        lean = 10 * lift * (1 - strike) - 18 * strike * (1 - recover)
        pose = {'lh1': ((1, 0, 0), arm), 'rh1': ((1, 0, 0), arm), 'bd': ((1, 0, 0), lean),
                'lh2': ((1, 0, 0), -30 * lift * (1 - recover)), 'rh2': ((1, 0, 0), -30 * lift * (1 - recover))}
    elif name.startswith('uhit'):
        knock = math.sin(clamp(t / 0.6) * math.pi)
        pose = {'bd': ((1, 0, 0), 16 * knock), 'hd': ((1, 0, 0), 10 * knock),
                'lh1': ((0, 1, 0), -20 * knock), 'rh1': ((0, 1, 0), 20 * knock)}
        hips = (0.0, 0.06 * knock, 0.0)
    elif 'death' in name:
        # Falls backwards and breaks apart: the hips sink to the ground, the limbs splay.
        fall = clamp(t / 0.8) ** 2
        pose = {'hp': ((1, 0, 0), 84 * fall), 'hd': ((0, 1, 0), 40 * fall),
                'lh1': ((0, 1, 0), -70 * fall), 'rh1': ((0, 1, 0), 70 * fall),
                'll1': ((1, 0, 0), -30 * fall), 'rl1': ((1, 0, 0), -45 * fall)}
        hips = (0.0, 0.25 * fall, -0.62 * fall)
    return pose, (hips[0] * GOLEM_SCALE, hips[1] * GOLEM_SCALE, hips[2] * GOLEM_SCALE + GOLEM_HIPS)


def golem():
    gltf = Gltf()
    index = {}
    k = GOLEM_SCALE
    for name, parent, translation, boxes in GOLEM_PARTS:
        # The head's second box is a plate on its front (-y) that shows the eyes' panel.
        shapes = [box(tuple(c * k for c in centre), tuple(c * k for c in size),
                      FACE_UV if name == 'hd' and i == 1 else STONE_UV)
                  for i, (centre, size) in enumerate(boxes)]
        place = (0.0, 0.0, GOLEM_HIPS) if parent is None else tuple(c * k for c in translation)
        index[name] = gltf.node(name, place, merge(*shapes), 'stone 1', None if parent is None else index[parent])
    for name, frames in GOLEM_ANIMATIONS.items():
        rotations = {i: [] for i in index.values()}
        hips = []
        for k in range(frames + 1):
            pose, position = golem_pose(name, k / frames)
            for part, i in index.items():
                axis, degrees = pose.get(part, ((1, 0, 0), 0.0))
                rotations[i].append(axis_angle(axis, degrees))
            hips.append(position)
        gltf.animation(name, frames, rotations, {index['hp']: hips})
    return gltf.glb()


def golem_texture(rng):
    size = 128
    stone = Fractal(rng, 4, 4, float(size))
    cracks = Fractal(rng, 8, 2, float(size))
    pixels = []
    for y in range(size):
        for x in range(size):
            t = stone.at(x, y)
            colour = mix((70, 66, 72), (150, 140, 128), (t - 0.2) * 1.6)
            if abs(cracks.at(x, y) - 0.5) < 0.015:
                colour = (90, 200, 230)   # glowing veins
            if y < size // 4 and x >= size // 2:
                # The eyes' panel (FACE_UV): two glowing eyes on dark stone.
                colour = mix((40, 38, 44), colour, 0.3)
                for ex in (size * 0.62, size * 0.88):
                    if math.hypot(x - ex, y - size * 0.12) < 5:
                        colour = (120, 230, 255)
            pixels.append(colour)
    return size, pixels


def shard_texture(rng):
    """scshard00: the golem's shards' skin on the golem's layout (STONE_UV, FACE_UV): blue crystal with bright facet
    edges, two white eyes."""
    size = 128
    facets = Fractal(rng, 3, 2, float(size))
    glints = Fractal(rng, 8, 2, float(size))
    pixels = []
    for y in range(size):
        for x in range(size):
            t = facets.at(x, y)
            # Facets: the noise in steps, each step a shade of blue; the steps' edges light up.
            step = math.floor(t * 6) / 6
            colour = mix((30, 70, 140), (120, 190, 250), step * 1.4 - 0.2)
            if abs(t * 6 - round(t * 6)) < 0.06 or glints.at(x, y) > 0.78:
                colour = (210, 240, 255)
            if y < size // 4 and x >= size // 2:
                colour = mix((20, 30, 60), colour, 0.3)
                for ex in (size * 0.62, size * 0.88):
                    if math.hypot(x - ex, y - size * 0.12) < 4:
                        colour = (250, 250, 255)
            pixels.append(colour)
    return size, pixels


# --- The Chain Spark --------------------------------------------------------------------------------------------------

def jagged(rng, start, end, steps, sway):
    """A lightning-like polyline from `start` to `end`: `steps` pieces, the inner points pushed across by up to `sway`."""
    (x0, y0), (x1, y1) = start, end
    length = math.hypot(x1 - x0, y1 - y0) or 1.0
    nx, ny = -(y1 - y0) / length, (x1 - x0) / length
    points = [start]
    for i in range(1, steps):
        t, push = i / steps, rng.uniform(-sway, sway)
        points.append((x0 + (x1 - x0) * t + nx * push, y0 + (y1 - y0) * t + ny * push))
    return points + [end]


def polyline_distance(p, points):
    return min(segment_distance(p, a, b) for a, b in zip(points, points[1:]))


def spark_texture(rng):
    """chainspark: the spark's 16 pictures (ChainSpark and ChainSparkHit), 4 x 4 cells of 32 x 32, row by row from the
    top: a white glow with five jagged rays, other rays in each. White, its brightness in the alpha (the effects tint
    and add it)."""
    cell, columns = 32, 4
    size = cell * columns
    alpha = [0] * (size * size)
    for frame in range(16):
        ox, oy = (frame % columns) * cell, (frame // columns) * cell
        centre = (cell / 2, cell / 2)
        rays = []
        for k in range(5):
            angle = rng.uniform(0, 2 * math.pi)
            reach = rng.uniform(9, 15)
            rays.append(jagged(rng, centre, (centre[0] + math.cos(angle) * reach, centre[1] + math.sin(angle) * reach),
                               4, 2.0))
        for y in range(cell):
            for x in range(cell):
                p = (x + 0.5, y + 0.5)
                glow = clamp(1 - math.hypot(p[0] - centre[0], p[1] - centre[1]) / 9) ** 2
                ray = max(clamp(1 - polyline_distance(p, r) / 1.4) for r in rays)
                fade = clamp(1 - math.hypot(p[0] - centre[0], p[1] - centre[1]) / 15.5)   # nothing at the cell's edge
                alpha[(oy + y) * size + ox + x] = int(round(255 * clamp(glow + ray * fade)))
    return size, [(255, 255, 255, a) for a in alpha]


def spark_icon(background, rim, bolt, glow):
    """A 64 x 64 picture of the spell: three sparks joined by a jagged arc on a round plate. spell0101 (the spell
    items and scrolls) and prototype0101 (the spell maker's loot item) in their own colours."""
    rng = random.Random('showcase:spark icon')   # the same arc in both
    size = 64
    nodes = [(14.0, 46.0), (30.0, 20.0), (50.0, 38.0)]
    arcs = [jagged(rng, a, b, 5, 3.0) for a, b in zip(nodes, nodes[1:])]
    pixels = []
    for y in range(size):
        for x in range(size):
            p = (x + 0.5, y + 0.5)
            r = math.hypot(p[0] - size / 2, p[1] - size / 2)
            colour = mix(background, (0, 0, 0), r / 44)
            if 28 <= r < 31:
                colour = rim
            elif r >= 31:
                colour = mix(rim, (0, 0, 0), 0.6)
            d = min(polyline_distance(p, a) for a in arcs)
            colour = mix(colour, glow, clamp(1 - d / 4) * 0.7)
            if d < 1.2:
                colour = bolt
            for n in nodes:
                k = clamp(1 - math.hypot(p[0] - n[0], p[1] - n[1]) / 6)
                if k > 0:
                    colour = mix(colour, bolt, k * 1.5)
            pixels.append(colour)
    return size, pixels


# --- The hammer and the boots ------------------------------------------------------------------------------------------

# The camp draws an item's inventory model unlit, in one colour per texel (a lying weapon from above, other items from
# the front; docs/notes/mods.md, "The showcase mod"), so the pictures carry the light: each face samples a part of
# the texture painted as that face should look, and the outline does the rest. Its texture is the worn piece's
# (unhuma + the model name): a picture smaller than 256 x 256 sits at the bottom left of a 256 x 256 texture, so the
# inventory model's coordinates are the picture's pixels over 256 (icon_uv), the worn model's over its own size.

def outline_octagon(a, b, half_a, half_b, chamfer):
    """A chamfered rectangle's corners about (a, b), counter-clockwise."""
    c = chamfer
    return [(a + half_a, b - half_b + c), (a + half_a, b + half_b - c), (a + half_a - c, b + half_b),
            (a - half_a + c, b + half_b), (a - half_a, b + half_b - c), (a - half_a, b - half_b + c),
            (a - half_a + c, b - half_b), (a + half_a - c, b - half_b)]


# The two axes across each extrusion axis, so that (across[0], across[1], axis) is right-handed.
ACROSS = {0: (1, 2), 1: (2, 0), 2: (0, 1)}


def loft(axis, low, high, outline_low, outline_high, uv):
    """A solid from a convex outline at `low` along `axis` (0 x, 1 y, 2 z) to another of as many corners at `high`
    (counter-clockwise seen from +axis, in ACROSS[axis]'s two axes): its sides and both ends, each face flat.
    uv(point, normal): the texture coordinates of a face's corner."""
    a, b = ACROSS[axis]

    def point(corner, at):
        p = [0.0, 0.0, 0.0]
        p[a], p[b], p[axis] = corner[0], corner[1], at
        return tuple(p)
    faces = []
    n = len(outline_low)
    for i in range(n):
        j = (i + 1) % n
        faces.append([point(outline_low[i], low), point(outline_low[j], low), point(outline_high[j], high),
                      point(outline_high[i], high)])
    faces.append([point(c, high) for c in outline_high])
    faces.append([point(c, low) for c in reversed(outline_low)])
    positions, normals, uvs, indices = [], [], [], []
    for face in faces:
        e1 = [face[1][k] - face[0][k] for k in range(3)]
        e2 = [face[2][k] - face[0][k] for k in range(3)]
        normal = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
        length = math.sqrt(sum(c * c for c in normal)) or 1.0
        normal = tuple(c / length for c in normal)
        base = len(positions)
        for p in face:
            positions.append(p)
            normals.append(normal)
            uvs.append(uv(p, normal))
        for k in range(1, len(face) - 1):
            indices += [base, base + k, base + k + 1]
    return positions, normals, uvs, indices


def rod(axis, low, high, radius_low, radius_high, centre, uv):
    """An eight-sided rod along `axis` from `low` to `high` about `centre` (its point in ACROSS[axis]'s axes)."""
    return loft(axis, low, high, outline_octagon(centre[0], centre[1], radius_low, radius_low, radius_low * 0.6),
                outline_octagon(centre[0], centre[1], radius_high, radius_high, radius_high * 0.6), uv)


def turned(shape, axis, degrees, scale=1.0, shift=(0.0, 0.0, 0.0)):
    """`shape` turned about `axis` (0 x, 1 y, 2 z) by `degrees`, then scaled and moved."""
    c, s = math.cos(math.radians(degrees)), math.sin(math.radians(degrees))
    a, b = ACROSS[axis]

    def turn(v):
        out = list(v)
        out[a], out[b] = v[a] * c - v[b] * s, v[a] * s + v[b] * c
        return out
    positions = [tuple(x * scale + d for x, d in zip(turn(p), shift)) for p in shape[0]]
    normals = [tuple(turn(n)) for n in shape[1]]
    return positions, normals, shape[2], shape[3]


def planar(rect, across, along, picture):
    """Texture coordinates that lay `rect` (picture pixels x0, y0, x1, y1, half a pixel kept from each edge) over
    the box `across` (axis, from, to) x `along` (axis, from, to): the first along the pixels' x, the second down their
    rows. picture(px, py): the coordinates of a pixel."""
    x0, y0, x1, y1 = rect[0] + 0.5, rect[1] + 0.5, rect[2] - 0.5, rect[3] - 0.5

    def uv(p, normal):
        s = clamp((p[across[0]] - across[1]) / (across[2] - across[1]))
        t = clamp((p[along[0]] - along[1]) / (along[2] - along[1]))
        return picture(x0 + (x1 - x0) * s, y0 + (y1 - y0) * t)
    return uv


def icon_uv(px, py):
    """A pixel of a 128 x 128 picture as the inventory model's coordinates: the picture at the bottom left of 256."""
    return px / 256, (128 + py) / 256


def worn_uv(px, py):
    """A pixel of a 128 x 128 picture as the worn model's coordinates."""
    return px / 128, py / 128


# The hammer's picture (128 x 128): a unit's weapons take its left 0.3 (the helmets the middle; the game's hammers'
# pictures are opaque there alone), so everything is in a strip STRIP wide, the rest clear. Its parts (x0, y0, x1, y1):
STRIP = 38
HANDLE = (0, 0, 9, 96)          # the shaft's wood, along the rows, lit down the middle
GRIP = (0, 96, 9, 128)          # leather wound round the grip
HEAD_TOP = (9, 0, 38, 64)       # the head's face that the light falls on: granite with a blue rune
HEAD_BEVEL = (9, 64, 38, 88)    # its chamfers: darker, a bright edge
HEAD_SIDE = (9, 88, 38, 104)    # its sides, in shadow
HEAD_FACE = (9, 104, 38, 120)   # the striking faces' plates: worn smooth
IRON = (9, 120, 38, 128)        # the collar under the head and the pommel


def hammer_shape(picture):
    """The hammer in its own units, lying with its head towards +y and its top face towards +z (the camp shows a
    weapon from +z): a grip, a shaft, an iron collar and pommel, a chamfered granite head with flared striking faces.
    picture(px, py): the coordinates of a pixel."""
    shapes = []

    def by_x(rect, radius, low, high):
        return planar(rect, (0, -radius, radius), (1, high, low), picture)
    shapes.append(rod(1, -1.27, -1.18, 0.085, 0.085, (0, 0), by_x(IRON, 0.085, -1.27, -1.18)))
    shapes.append(rod(1, -1.18, -0.72, 0.075, 0.07, (0, 0), by_x(GRIP, 0.075, -1.18, -0.72)))
    shapes.append(rod(1, -0.72, 0.52, 0.065, 0.06, (0, 0), by_x(HANDLE, 0.065, -0.72, 0.52)))
    shapes.append(rod(1, 0.4, 0.49, 0.09, 0.085, (0, 0), by_x(IRON, 0.09, 0.4, 0.49)))
    centre, half, chamfer = 0.67, 0.19, 0.075
    # The head: its long side along x; the face it shows by the way the face turns.
    top = planar(HEAD_TOP, (1, centre - half, centre + half), (0, -0.38, 0.38), picture)
    bevel = planar(HEAD_BEVEL, (1, centre - half, centre + half), (0, -0.38, 0.38), picture)
    side = planar(HEAD_SIDE, (2, -half, half), (0, -0.38, 0.38), picture)
    end = planar(HEAD_FACE, (1, centre - half, centre + half), (2, -half, half), picture)

    def head(p, normal):
        if abs(normal[0]) > 0.9:
            return end(p, normal)
        if abs(normal[2]) > 0.9:
            return top(p, normal)
        return bevel(p, normal) if abs(normal[2]) > 0.3 else side(p, normal)
    body = outline_octagon(centre, 0.0, half, half, chamfer)
    shapes.append(loft(0, -0.38, 0.38, body, body, head))
    # The striking faces: plates flaring outwards, worn smooth, their rims dark lines from above.
    face = planar(HEAD_FACE, (1, centre - 0.235, centre + 0.235), (2, -0.235, 0.235), picture)
    inner = outline_octagon(centre, 0.0, 0.205, 0.205, 0.08)
    outer = outline_octagon(centre, 0.0, 0.235, 0.235, 0.09)
    for low, high, outlines in ((0.38, 0.48, (inner, outer)), (-0.48, -0.38, (outer, inner))):
        rim = planar(HEAD_FACE, (1, centre - 0.235, centre + 0.235), (0, low, high), picture)
        shapes.append(loft(0, low, high, outlines[0], outlines[1],
                           lambda p, normal, rim=rim: face(p, normal) if abs(normal[0]) > 0.9 else rim(p, normal)))
    return merge(*shapes)


def hammer_icon():
    """initwehm9: the inventory model of hammer number 9 (a loose figure of one part, "weapon"). The game's lie
    diagonally, the head at the top left, about 2.6 long: the camp turns a weapon about that diagonal."""
    gltf = Gltf()
    centred = turned(hammer_shape(icon_uv), 2, 0, 1.0, (0.0, 0.18, 0.0))   # the middle of its length at the origin
    shape = turned(centred, 2, 45, 0.88)
    gltf.node('weapon', (0, 0, 0), shape, 'stone 1')
    return gltf.glb({'ei': {'layout': 'loose'}})


def hammer_worn():
    """hm_09: the hammer in the hand, the part the game loads for club number 9 (rh3.club09) under the bone the game's
    clubs hang from (rh3.axe00, a node without a mesh here: the figures have it), in the second texture's weapons'
    strip (material "stone 2"). The game's clubs point along -y from the grip, about 0.45 long; this one is 0.58: the
    inventory model's shape turned head down, the hand 0.11 from the pommel."""
    gltf = Gltf()
    scale = 0.273
    shape = turned(hammer_shape(worn_uv), 2, 180, scale, (0.0, 0.11 - 1.27 * scale, 0.0))
    hand = gltf.node('rh3.axe00', (0, 0, 0), None, None)
    gltf.node('rh3.club09', (0, 0, 0), shape, 'stone 2', hand)
    return gltf.glb()


def granite(grain, x, y, light):
    """Grey granite with dark and light flecks, darkened to `light` (1 as lit as the head's top)."""
    t = grain.at(x, y)
    colour = mix((84, 80, 84), (150, 142, 134), (t - 0.2) * 1.5)
    fleck = grain.at(x * 7.3 + 31, y * 7.3 + 17)
    if fleck > 0.68:
        colour = mix(colour, (40, 36, 40), 0.6)
    elif fleck < 0.3:
        colour = mix(colour, (214, 206, 196), 0.45)
    return mix(colour, (0, 0, 0), 1 - light)


def hammer_texture(rng):
    """unhumahm_09.gr.0: the hammer's parts (HANDLE ... IRON) in the weapons' strip, the rest clear (a helmet's picture
    shows there)."""
    size = 128
    grain = Fractal(rng, 4, 3, float(size))
    pixels = []

    def inside(rect, x, y):
        return rect[0] <= x < rect[2] and rect[1] <= y < rect[3]
    for y in range(size):
        for x in range(size):
            if x >= STRIP:
                pixels.append((0, 0, 0, 0))
                continue
            across = (x + 0.5 - 4.5) / 4.5   # -1..1 over the handle's width
            if inside(HANDLE, x, y):
                t = grain.at(x * 4.0, y * 0.5)
                colour = mix(mix((64, 40, 22), (138, 98, 58), t), (0, 0, 0), 0.55 * across * across)
            elif inside(GRIP, x, y):
                band = (x + y * 2) % 8
                colour = (98, 60, 34) if band >= 2 else (48, 28, 16)
                colour = mix(colour, (0, 0, 0), 0.5 * across * across)
            elif inside(HEAD_TOP, x, y):
                u, v = (x - 9 + 0.5) / 29, (y + 0.5) / 64
                edge = max(abs(u - 0.5), abs(v - 0.5)) * 2
                colour = granite(grain, x, y, 1.05 - 0.3 * edge * edge)
                cx, cy = 23.5, 32.0
                ring = abs(math.hypot(x + 0.5 - cx, (y + 0.5 - cy) * 0.75) - 8.5)
                if ring < 1.4 or (abs(x + 0.5 - cx) < 1.3 and abs(y + 0.5 - cy) < 15):
                    colour = (90, 190, 250)
                elif ring < 3.0:
                    colour = mix(colour, (70, 150, 220), 0.35)
            elif inside(HEAD_BEVEL, x, y):
                colour = granite(grain, x, y, 0.72)
                if y < 66:
                    colour = mix(colour, (230, 226, 216), 0.5)   # the lit edge
            elif inside(HEAD_SIDE, x, y):
                colour = granite(grain, x, y, 0.48)
            elif inside(HEAD_FACE, x, y):
                colour = granite(grain, x * 0.5, y * 0.5, 0.85)
                if y in (104, 119) or x in (9, 37):
                    colour = mix(colour, (0, 0, 0), 0.55)
            else:
                colour = (58, 56, 60) if (x - 9) % 7 != 3 or y in (120, 127) else (150, 148, 140)   # iron, rivets
            pixels.append(tuple(int(round(clamp(c / 255) * 255)) for c in colour) + (255,))
    return size, pixels


# The boots' layer: where the hero's skin texture (256 x 256) has its feet and lower legs, the rest transparent. The
# hero's legs lie in it seen from the side, the sole at the top: the feet's rows from 59 / 159, the shins below. Two
# parts of the rectangles no part of the hero's figure takes hold the inventory model's own pictures: the pair seen
# from the front (FRONT, the boot's top in the first row, the sole in the last) and a foot from above (FOOT_TOP, the
# heel in the first row, the toe in the last).
BOOT_RECTS = [(208, 48, 256, 96), (128, 144, 256, 192)]
FRONT = (200, 144, 222, 192)
FOOT_TOP = (208, 48, 221, 96)

# The boot's heights (y, the sole at the bottom) and depths (z, the toe towards the viewer) in its own units.
BOOT_TOP, BOOT_SOLE, SHAFT_FOOT, FOOT_HEEL, FOOT_TOE = 0.74, -0.64, -0.3, -0.22, 0.56
CUFF_LOW, SOLE_TOP = 0.5, -0.58


def boots_texture(rng):
    size = 256
    leather = Fractal(rng, 8, 3, float(size))
    pixels = []
    for y in range(size):
        for x in range(size):
            inside = any(x0 <= x < x1 and y0 <= y < y1 for x0, y0, x1, y1 in BOOT_RECTS)
            if not inside:
                pixels.append((0, 0, 0, 0))
                continue
            t = leather.at(x, y)
            colour = mix((70, 40, 24), (128, 82, 48), t)
            if FRONT[0] <= x < FRONT[2] and FRONT[1] <= y < FRONT[3]:
                colour = boot_front(colour, (x + 0.5 - FRONT[0]) / (FRONT[2] - FRONT[0]),
                                    BOOT_TOP - (y + 0.5 - FRONT[1]) / (FRONT[3] - FRONT[1]) * (BOOT_TOP - BOOT_SOLE))
            elif FOOT_TOP[0] <= x < FOOT_TOP[2] and FOOT_TOP[1] <= y < FOOT_TOP[3]:
                colour = boot_foot_top(colour, (x + 0.5 - FOOT_TOP[0]) / (FOOT_TOP[2] - FOOT_TOP[0]),
                                       FOOT_HEEL + (y + 0.5 - FOOT_TOP[1]) / (FOOT_TOP[3] - FOOT_TOP[1]) *
                                       (FOOT_TOE - FOOT_HEEL))
            elif y % 16 in (0, 1):
                colour = mix(colour, (200, 170, 90), 0.6)   # stitched bands
            pixels.append(tuple(int(round(clamp(c / 255) * 255)) for c in colour) + (255,))
    return size, pixels


def boot_front(leather, u, height):
    """The pixel of the boot seen from the front at `u` (0..1 across) and `height`: the light from the upper left on a
    round shaft, a turned-down cuff, laces up the front, a strap with a buckle at the ankle, the toe, a stitched welt
    and the sole."""
    across = u * 2 - 1
    light = 1.12 - 0.5 * across * across + 0.18 * max(0.0, 1 - abs(across + 0.35) * 3)
    colour = leather
    if height > CUFF_LOW:
        colour = mix(leather, (176, 120, 72), 0.55)                   # the cuff, lighter, turned down
        if abs(height - CUFF_LOW) < 0.035:
            colour = (40, 24, 14)                                      # its shadow
        elif abs(height - (BOOT_TOP - 0.06)) < 0.015 and int(u * 22) % 2 == 0:
            colour = (220, 190, 120)                                   # stitches
    elif height > SHAFT_FOOT + 0.08:
        if abs(across) < 0.1:
            colour = (34, 20, 12)                                      # the laces' slit
        lace = (height * 28) % 2.0
        if abs(across) < 0.32 and abs(abs(across) - abs(lace - 1) * 0.32) < 0.07:
            colour = (214, 188, 140)                                   # the crossed laces
    elif height > SHAFT_FOOT:
        colour = (52, 30, 18)                                          # the ankle strap
        if abs(across) < 0.2:
            colour = (222, 182, 84) if abs(across) > 0.1 or abs(height - SHAFT_FOOT - 0.04) > 0.02 else (90, 64, 30)
    elif height > SOLE_TOP + 0.03:
        colour = mix(leather, (150, 98, 58), 0.4 * (height - SOLE_TOP) / (SHAFT_FOOT - SOLE_TOP))   # the toe
    elif height > SOLE_TOP:
        colour = (210, 180, 110) if int(u * 22) % 2 == 0 else (60, 38, 22)   # the welt's stitches
    else:
        colour = (34, 24, 18)                                          # the sole
    return mix(colour, (0, 0, 0), clamp(1 - light)) if light < 1 else mix(colour, (255, 236, 200), (light - 1) * 0.5)


def boot_foot_top(leather, u, depth):
    """The pixel of the foot seen from above at `u` (0..1 across) and `depth` (heel to toe): the shaft's dark opening
    above the heel, then the instep and the toe, polished where it is round."""
    across = u * 2 - 1
    if depth < 0.16:
        return (30, 18, 12)                                            # into the shaft
    toe = clamp((depth - 0.16) / (FOOT_TOE - 0.16))
    colour = mix(leather, (160, 106, 64), 0.5 * toe)
    light = 1.05 - 0.45 * across * across
    if toe > 0.55 and abs(across + 0.2) < 0.25:
        light += 0.2                                                    # the toe cap's shine
    return mix(colour, (0, 0, 0), clamp(1 - light)) if light < 1 else mix(colour, (255, 236, 200), (light - 1) * 0.5)


def boot_shape(side):
    """One boot in its own units, standing, the toe towards +z: the shaft and its cuff, the foot and the sole. Faces
    turned towards +z or sideways show FRONT, those turned up FOOT_TOP; `side` -1 or 1 mirrors the pictures."""
    def picture(px, py):
        return px / 256, py / 256
    u0, u1 = (-0.26, 0.26) if side > 0 else (0.26, -0.26)
    front = planar(FRONT, (0, u0, u1), (1, BOOT_TOP, BOOT_SOLE), picture)
    above = planar(FOOT_TOP, (0, u0 * 0.9, u1 * 0.9), (2, FOOT_HEEL, FOOT_TOE), picture)

    def uv(p, normal):
        return above(p, normal) if normal[1] > 0.6 else front(p, normal)
    shapes = [rod(1, SHAFT_FOOT - 0.04, CUFF_LOW, 0.16, 0.18, (0.0, 0.0), uv),
              rod(1, CUFF_LOW - 0.02, BOOT_TOP, 0.215, 0.23, (0.0, 0.0), uv)]
    # The foot: a slab from heel to toe, wider than the shaft, its top falling towards the toe.
    foot_low = outline_octagon(0.17, 0.0, 0.39, 0.22, 0.13)    # (z, x) about the foot's middle
    foot_high = outline_octagon(0.06, 0.0, 0.26, 0.17, 0.1)
    shapes.append(loft(1, SOLE_TOP, SHAFT_FOOT + 0.02, foot_low, foot_high, uv))
    sole = outline_octagon(0.17, 0.0, 0.42, 0.24, 0.14)
    shapes.append(loft(1, BOOT_SOLE, SOLE_TOP + 0.005, sole, sole, uv))
    return merge(*shapes)


def boots_icon():
    """initarbt20: the inventory model of boots number 20 (a loose figure of one part, "armor"): a pair of boots
    standing side by side, the toes a little apart and tipped towards the viewer so the feet show from above. The
    camp shows an armour's model from +z and turns it about y."""
    pair = merge(turned(boot_shape(-1), 1, -12, 1.0, (-0.27, 0.0, 0.0)),
                 turned(boot_shape(1), 1, 12, 1.0, (0.27, 0.0, 0.0)))
    gltf = Gltf()
    gltf.node('armor', (0, 0, 0), turned(pair, 0, 26, 0.85, (0.0, 0.04, 0.0)), 'leather 1')
    return gltf.glb({'ei': {'layout': 'loose'}})


# --- Writing ----------------------------------------------------------------------------------------------------------

def made_files():
    """{path below the mod: bytes} of every made file, each from its own seeded generator."""
    files = {}

    def rng(name):
        return random.Random('showcase:' + name)
    heights, tiles = ground_layers(rng('ground'))
    files['Maps/showcase/map.toml'] = map_toml().encode()
    files['Maps/showcase/heights.png'] = png(WIDTH + 1, HEIGHT + 1, 'grey16', heights)
    files['Maps/showcase/tiles.png'] = png(WIDTH // 2, HEIGHT // 2, 'rgb', tiles)
    files['Res/textures/showcase000.png'] = png(PAGE, PAGE, 'rgb', ground_page(rng('page')))
    files['Res/textures/showcasemap.png'] = png(256, 256, 'rgb', island_map(heights, tiles, 1.0))
    files['Res/textures/showcasequest.png'] = png(256, 256, 'rgb', island_map(heights, tiles, 0.8))
    size, pixels = golem_texture(rng('golem skin'))
    files['Res/textures/scgolem00.png'] = png(size, size, 'rgb', pixels)
    files['Res/figures/scgolem.glb'] = golem()
    size, pixels = shard_texture(rng('shard skin'))
    files['Res/textures/scshard00.png'] = png(size, size, 'rgb', pixels)
    size, pixels = spark_texture(rng('spark'))
    files['Res/textures/chainspark.png'] = png(size, size, 'rgba', pixels)
    size, pixels = spark_icon((40, 70, 150), (150, 180, 220), (235, 248, 255), (110, 200, 255))
    files['Res/textures/spell0101.png'] = png(size, size, 'rgb', pixels)
    size, pixels = spark_icon((120, 80, 40), (220, 190, 110), (255, 240, 190), (240, 200, 90))
    files['Res/textures/prototype0101.png'] = png(size, size, 'rgb', pixels)
    files['Res/figures/initwehm9.glb'] = hammer_icon()
    files['Res/figures/hm_09.glb'] = hammer_worn()
    size, pixels = hammer_texture(rng('hammer'))
    files['Res/redress/unhumahm_09.gr.0.png'] = png(size, size, 'rgba', pixels)
    files['Res/figures/initarbt20.glb'] = boots_icon()
    size, pixels = boots_texture(rng('boots'))
    files['Res/redress/unhumabt_20.le.0.png'] = png(size, size, 'rgba', pixels)
    return files


def same(path, data, old):
    if path.endswith('.png'):
        return png_pixels(data) == png_pixels(old)
    return data == old


def main():
    args = sys.argv[1:]
    if args == ['--write'] or args == ['--check']:
        stale = []
        for path, data in made_files().items():
            target = os.path.join(MOD, *path.split('/'))
            old = open(target, 'rb').read() if os.path.exists(target) else None
            if old is not None and same(path, data, old):
                continue
            stale.append(path)
            if args == ['--write']:
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with open(target, 'wb') as f:
                    f.write(data)
        if args == ['--check'] and stale:
            sys.exit('make_showcase_mod: not what --write writes: ' + ', '.join(stale))
        print('make_showcase_mod: %s %d files' % ('wrote' if args == ['--write'] else 'checked', len(stale) if args ==
                                                  ['--write'] else len(made_files())))
        return
    if len(args) != 1 or args[0].startswith('-'):
        sys.exit(__doc__)
    target = os.path.join(args[0], NAME)
    if os.path.isdir(target):
        shutil.rmtree(target)
    shutil.copytree(MOD, target)


if __name__ == '__main__':
    main()
