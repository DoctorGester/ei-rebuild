"""Resolves the harness crash log in ei_hook.log to function names (tickets X40, X40a).

The harness logs fatal exceptions with the code addresses found on the stack: game.exe addresses
as is ("stack 006936BE (game.exe)"), ei_hook.dll ones as ei_hook+offset. This names each with the
ei_hook.map symbol (undecorated) or the game function list (tests/coverage/coverage.csv names the
rewritten functions, config/functions.csv the rest).

The raw stack scan also holds words that are no return address (saved registers, a function
pointer, return addresses of calls that finished long ago, whole stale call chains). A word counts as
a frame when the code before it is a call (read from the build's ei_hook.dll or the run's game.exe)
that reaches the function of the frame before it (through jump thunks, tail calls and forwarders to
the original game functions; where that cannot be told, an indirect call or a hooked function with a
differing name, the word waits until a later word links to it). Words before the throw's own function
(CxxThrowException) and after the thread's first function (entry) are dropped. The named stack reads
outermost first and ends at the throw: "... -> ComposeTexture -> ... -> CFile::Write -> CxxThrowException".

usage: python tools/resolve_crash.py [run folder or build/verify]   (defaults to build/verify; last crash only)
       --raw  every logged word, one per line, instead of the filtered frames and the named stack
"""
import bisect, csv, os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NOISE_PREFIXES = ('__ehhandler$', '__catch$', '__unwind$', '__tryend$', '__ehfuncinfo$', '??_C@')


def code_sections(path):
    """Section numbers whose class is CODE in the map's segment table."""
    out = set()
    with open(path, errors='replace') as f:
        for line in f:
            m = re.match(r'\s*([0-9a-fA-F]{4}):[0-9a-fA-F]{8} [0-9a-fA-F]{8}H \S+\s+(\S+)\s*$', line)
            if m and m.group(2) == 'CODE':
                out.add(m.group(1))
            elif 'Publics by Value' in line:
                break
    return out


def load_map(path, code_only=False):
    """[(rva, symbol)] sorted. code_only: symbols of code sections only, no exception handler funclets."""
    syms = []
    base = 0x10000000
    code = code_sections(path) if code_only else None
    with open(path, errors='replace') as f:
        for line in f:
            m = re.match(r'\s*Preferred load address is ([0-9a-fA-F]+)', line)
            if m:
                base = int(m.group(1), 16)
            m = re.match(r'\s*([0-9a-fA-F]{4}):[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s', line)
            if m and int(m.group(3), 16) >= base:
                if code_only and (m.group(1) not in code or m.group(2).startswith(NOISE_PREFIXES)):
                    continue
                syms.append((int(m.group(3), 16) - base, m.group(2)))
    syms.sort()
    return syms


def load_game_functions(root=ROOT):
    """(starts, names) of the known game functions, sorted: config/functions.csv, with the names of the
    rewritten functions from tests/coverage/coverage.csv over its FUN_ placeholders."""
    rows = {}
    with open(os.path.join(root, 'config', 'functions.csv')) as f:
        for r in csv.DictReader(f):
            rows[int(r['address'], 16)] = r.get('name') or ''
    path = os.path.join(root, 'tests', 'coverage', 'coverage.csv')
    if os.path.exists(path):
        with open(path) as f:
            for r in csv.DictReader(l for l in f if not l.startswith('#')):
                a = int(r['address'], 16)
                if r['name'] and (not rows.get(a) or rows[a].startswith(('FUN_', 'sub_'))):
                    rows[a] = r['name']
    keys = sorted(rows)
    return keys, [rows[a] for a in keys]


def undecorate(names, name_only=False):
    """Demangled names (dbghelp's UnDecorateSymbolName, else undname); a name that fails stays as it is."""
    out = []
    try:
        import ctypes
        dbghelp = ctypes.WinDLL('dbghelp')
        buf = ctypes.create_string_buffer(2048)
        for n in names:
            if n.startswith('?') and dbghelp.UnDecorateSymbolName(n.encode(), buf, len(buf), 0x1000 if name_only else 0):
                out.append(buf.value.decode(errors='replace'))
            else:
                out.append(n)
        return out
    except (OSError, AttributeError):
        pass
    try:
        text = subprocess.run(['undname'] + names, capture_output=True, text=True).stdout
        found = re.findall(r'is :- "(.*)"', text)
        if len(found) == len(names):
            return found
    except OSError:
        pass
    return names


def short_name(demangled):
    """Drops what the project's namespaces add: 'mfc::impl::CFile::Write' -> 'CFile::Write'."""
    s = demangled
    for ns in ("`anonymous namespace'::", 'mfc::impl::', 'mfc::', 'libcrt::', 'impl::', 'ei::'):
        s = s.replace(ns, '')
    return s.strip() or demangled


class Image:
    """Bytes of a PE file by virtual address (the instruction before a return address)."""

    def __init__(self, path, base=None):
        with open(path, 'rb') as f:
            self.data = d = f.read()
        pe = struct.unpack_from('<I', d, 0x3c)[0]
        nsec, = struct.unpack_from('<H', d, pe + 6)
        optsz, = struct.unpack_from('<H', d, pe + 20)
        o = pe + 24
        self.base = base if base is not None else struct.unpack_from('<I', d, o + 28)[0]
        self.secs = []
        so = o + optsz
        for _ in range(nsec):
            vsz, va, rsz, rptr = struct.unpack_from('<IIII', d, so + 8)
            self.secs.append((va, min(max(vsz, rsz), rsz), rptr))
            so += 40

    def read(self, rva, n):
        for va, size, rptr in self.secs:
            if va <= rva and rva + n <= va + size:
                return self.data[rptr + rva - va: rptr + rva - va + n]
        return None


def call_of(code):
    """What the 8 bytes before a return address end in: None (no call), 'rel' (E8 rel32: displacement
    in rel), or 'indirect' (FF /2, call r/m32). Returns (kind, rel)."""
    if len(code) != 8:
        return None, 0
    if code[3] == 0xE8:
        return 'rel', struct.unpack_from('<i', code, 4)[0]
    for length in range(2, 8):
        b = code[8 - length:]
        if b[0] != 0xFF or (b[1] >> 3) & 7 != 2:
            continue
        mod, rm = b[1] >> 6, b[1] & 7
        need = 2 + (1 if mod != 3 and rm == 4 else 0) + {0: 4 if rm == 5 else 0, 1: 1, 2: 4, 3: 0}[mod]
        if need == length:
            return 'indirect', 0
    return None, 0


def norm_name(name):
    """A function's name without namespaces, 'Verify' and case, to match a hooked game function
    (coverage.csv name) with the rewrite that replaced it."""
    n = name.split('::')[-2:] if '::' in name else [name]
    return re.sub(r'verify$', '', ''.join(n).lower())


class Stack:
    """Names addresses and judges which logged words form the chain of calls to the crash."""

    def __init__(self, map_path, images, root=ROOT):
        self.syms = load_map(map_path, code_only=True)
        self.offsets = [s[0] for s in self.syms]
        self.gstarts, self.gnames = load_game_functions(root)
        self.images = images
        self.cache = {}

    def function(self, kind, a):
        """(start address, raw name or '?') of the function holding a."""
        if kind == 'ei':
            i = bisect.bisect_right(self.offsets, a) - 1
            return (self.syms[i][0], self.syms[i][1]) if i >= 0 else (a, '?')
        i = bisect.bisect_right(self.gstarts, a) - 1
        return (self.gstarts[i], self.gnames[i] or f'FUN_{self.gstarts[i]:08x}') if i >= 0 else (a, '?')

    def nice(self, kind, raw):
        if kind == 'ei' and raw.startswith('?'):
            if raw not in self.cache:
                self.cache[raw] = short_name(undecorate([raw], True)[0])
            return self.cache[raw]
        return raw

    def targets(self, kind, start):
        """The direct call and jump targets (function starts of this module) found in the bytes of the
        function at start: a plain scan for E8/E9 rel32, a wrong hit only adds an edge nobody takes."""
        key = (kind, start)
        if key not in self.cache:
            img = self.images[kind]
            starts = self.offsets if kind == 'ei' else self.gstarts
            i = bisect.bisect_right(starts, start)
            end = starts[i] if i < len(starts) else start + 0x400
            code = img.read(start - img.base, min(end - start, 0x2000)) or b''
            found = set()
            for j in range(len(code) - 4):
                if code[j] in (0xE8, 0xE9):
                    k, t = self.thunk(kind, start + j + 5 + struct.unpack_from('<i', code, j + 1)[0])
                    if k == kind:
                        found.add(self.function(kind, t)[0])
            self.cache[key] = found
        return self.cache[key]

    def thunk(self, kind, t):
        """(module, address) where code at t really goes: through jump thunks (incremental linking) and
        the forwarders to the original game function (push address; jmp ei_code_dispatch, S18i)."""
        for _ in range(3):
            img = self.images.get(kind)
            head = img.read(t - img.base, 10) if img else None
            if not head:
                break
            if head[0] == 0xE9:
                t += 5 + struct.unpack_from('<i', head, 1)[0]
            elif len(head) == 10 and head[0] == 0x68 and head[5] == 0xE9 and 'game' in self.images:
                kind, t = 'game', struct.unpack_from('<I', head, 1)[0]
            else:
                break
        return kind, t

    def reaches(self, kind, start, goal, depth=2):
        """Does the function at start call or tail-jump to goal, directly or through depth-1 more functions?"""
        if start == goal:
            return True
        return depth > 0 and any(self.reaches(kind, t, goal, depth - 1) for t in self.targets(kind, start))

    def callee(self, kind, a):
        """(call kind, module, start of the function that call reaches) for the call before the return
        address a. Call kind: None (no call there), 'rel', 'indirect', 'unknown' (no image to look at)."""
        img = self.images.get(kind)
        if img is None:
            return 'unknown', kind, None
        code = img.read(a - img.base - 8, 8)
        ck, rel = call_of(code) if code else (None, 0)
        if ck != 'rel':
            return ck, kind, None
        k, t = self.thunk(kind, a + rel)
        if self.images[k].read(t - self.images[k].base, 1) is None:    # outside the image: a trampoline
            return ck, k, None
        return ck, k, self.function(k, t)[0]

    def link(self, cur, prev):
        """Does the call before cur's return address reach the function prev is in? True / False when the
        callee is known; None when it cannot be told (indirect call, or a function of the other module
        whose name differs: the rewrite behind a hooked game address has its own name)."""
        ck, tkind, target = self.callee(*cur)
        if ck == 'unknown':
            return True
        if ck is None:
            return False
        if target is None:
            return None
        if tkind == prev[0]:
            return self.reaches(tkind, target, self.function(*prev)[0])
        if cur[0] == 'ei' and prev[0] == 'game':
            return False                    # the callee is ours, the frame before it is game code: no link
        tname = self.nice(tkind, self.function(tkind, target)[1])
        pname = self.nice(prev[0], self.function(*prev)[1])
        return True if not tname.startswith('FUN_') and norm_name(tname) == norm_name(pname) else None


WEAK_GAP = 1           # an unverifiable word (indirect call, hooked function) is taken only this close to the chain's end
ROOTS = ('entry', 'WinMain', 'mainCRTStartup', 'WinMainCRTStartup')     # a chain ends at its thread's first function
THROW_MARKERS = ('CxxThrowException', 'RaiseException')


def frames(words, stack):
    """Picks the frames of the call chain out of the logged words (innermost first), see the module text.
    Unverifiable words (WEAK_GAP) wait in `pending` and join the chain when a later word links to them.
    'at' words (the faulting address) start the chain."""
    marker = next((i for i, (_, k, a) in enumerate(words)
                   if any(m in stack.function(k, a)[1] for m in THROW_MARKERS)), 0)
    kept, pending, run = [], [], 0
    for line, kind, a in words[marker:]:
        word = (line, kind, a)
        if re.search(r'\]\s+at ', line) or not kept and not pending:
            kept.append(word)
            continue
        weak = False
        for i in range(len(pending), -1, -1):       # the latest pending word first, the confirmed chain last
            prev = pending[i - 1] if i else (kept[-1] if kept else None)
            if prev is None:
                break
            v = stack.link((kind, a), prev[1:])
            if v is True:
                kept += pending[:i] + [word]
                pending, run = [], -1
                if stack.function(kind, a)[1] in ROOTS:         # the thread's first function: the rest is stale
                    return kept
                break
            weak = weak or v is None
        else:
            if weak and run <= WEAK_GAP:
                pending.append(word)
                run = -1
        run += 1
    return kept


def stack_words(lines, start):
    """The code words logged for the crash at lines[start]: [(line, kind, address)]; kind 'ei' (RVA) or 'game'."""
    out = []
    for line in lines[start + 1:]:
        if not re.search(r'\]\s+(in|at|stack) ', line):
            if out or 'harness: ' not in line:      # the free-space line may sit between the crash line and its words
                break
            continue
        m = re.search(r'ei_hook\+([0-9A-F]+)', line)
        g = re.search(r'([0-9A-F]{8}) \(game\.exe\)', line)
        if m:
            out.append((line, 'ei', int(m.group(1), 16)))
        elif g:
            out.append((line, 'game', int(g.group(1), 16)))
    return out


def find_crash(lines):
    return max((i for i, l in enumerate(lines) if 'harness: exception' in l or 'harness: the game crashed' in l
                or 'standalone: no code at' in l), default=None)


def guess_game_exe(logdir, root=ROOT):
    """run/<install>/game.exe for a run folder named <scenario>-<install> (vanilla when unknown)."""
    install = os.path.basename(os.path.normpath(logdir)).rsplit('-', 1)[-1]
    for r in (root, os.path.dirname(os.path.dirname(os.path.dirname(root)))):    # a worktree's run\ may be absent
        for inst in (install, 'vanilla'):
            p = os.path.join(r, 'run', inst, 'game.exe')
            if os.path.exists(p):
                return p
    return None


def resolve(logdir, build=None, game_exe=None, raw=False, root=ROOT):
    """Lines of the named stack of the last crash in logdir/ei_hook.log (see the module text), or
    ['no crash logged']. build: the folder with ei_hook.map and ei_hook.dll (default: logdir when it holds
    the map, else build/verify). game_exe: the run's game.exe (default: run/<install>/game.exe by the
    folder's name). Without the dll or the exe the words of that module are not judged."""
    build = build or (logdir if os.path.exists(os.path.join(logdir, 'ei_hook.map')) else os.path.join(root, 'build', 'verify'))
    with open(os.path.join(logdir, 'ei_hook.log'), errors='replace') as f:
        lines = f.read().splitlines()
    start = find_crash(lines)
    if start is None:
        return ['no crash logged']
    images = {}
    dll = os.path.join(build, 'ei_hook.dll')
    if os.path.exists(dll):
        images['ei'] = Image(dll, 0)
    exe = game_exe or guess_game_exe(logdir, root)
    if exe and os.path.exists(exe):
        images['game'] = Image(exe)
    stack = Stack(os.path.join(build, 'ei_hook.map'), images, root)
    words = stack_words(lines, start)
    kept = words if raw else frames(words, stack)
    out = [lines[start]]
    chain = []
    for line, kind, a in kept:
        entry, name = stack.function(kind, a)
        name = stack.nice(kind, name)
        out.append(f'{line}  {name}+0x{a - entry:X}')
        chain.append(name if kind == 'ei' else f'{name} {a:08X}')
    if not raw and chain:
        out.append('named stack: ' + ' -> '.join(reversed(chain)))
    return out


def named_stack(logdir, max_frames=0, **kw):
    """Just the 'outer -> ... -> throw' text of the crash in logdir ('' when none or no frames); max_frames:
    only that many frames nearest the crash, after '... -> '."""
    for line in resolve(logdir, **kw):
        if line.startswith('named stack: '):
            names = line[len('named stack: '):].split(' -> ')
            if max_frames and len(names) > max_frames:
                return '... -> ' + ' -> '.join(names[-max_frames:])
            return ' -> '.join(names)
    return ''


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    logdir = args[0] if args else os.path.join(ROOT, 'build', 'verify')
    print('\n'.join(resolve(logdir, raw='--raw' in sys.argv)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
