"""Fails the build when our code calls a rewrite of a Rebirth-patched game function directly (rule 9).

Rewrite-guide rule 9: a mod (Rebirth's addon.dll) patches some game functions in memory. When rewritten
code calls such a function it must go through the function's original address (an EI_ORIGINAL
forwarder), so the mod's patch still runs; a direct call to our rewrite runs the vanilla behaviour in a
Rebirth install. SV3c found one (GameRender -> CWorldClient::Render) and three more with a one-off scan;
this is that scan as a check.

What it does, on the built ei_hook.dll and its linker map:
  1. docs/rebirth-patched-functions.txt lists the patched addresses; the EI_REPLACE / EI_REPLACE_METHOD
     registrations in src/ name our replacement of each (tools/check_signatures.py registrations()).
  2. The replacements' symbols are found in the map by their decorated-name prefix and only those are
     undecorated (the whole map would take tens of seconds).
  3. The incremental-link thunk table (.text$di, one `jmp rel32` per function) is read to add each
     replacement's thunk to the targets.
  4. The rest of .text is searched for E8 (call) and E9 (jmp) whose target is one of them; a hit counts
     only if it lies on an instruction boundary of the function the map puts it in (capstone, from the
     function's start). The calling function is named from the map.
  5. Hits fail the build unless allowed: callers in the verify-only sources (tests/verify, tests/vectors:
     wrappers call both implementations on purpose), a function calling itself, and the reviewed entries
     of tools/check_patched_calls_allow.txt.

Not seen: calls the compiler inlined (a replacement small enough to inline into its caller leaves no
call; grep the sources for small patched functions called by name), calls through vtables or function
pointers, and replacements whose name the check cannot match to a symbol (counted in the summary).

usage: python tools/check_patched_calls.py [--dll build\\<cfg>\\ei_hook.dll] [--map build\\<cfg>\\ei_hook.map]
       (needs capstone; build.cmd runs it for each build.)
exit code 1 when a call is neither allowed nor fixed
"""
import argparse, bisect, fnmatch, glob, os, re, struct, sys, time

from capstone import Cs, CS_ARCH_X86, CS_MODE_32

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_signatures import ROOT, candidates_for, undecorate
from check_registrations import Image

PATCHED = os.path.join(ROOT, 'docs', 'rebirth-patched-functions.txt')
ALLOW = os.path.join(ROOT, 'tools', 'check_patched_calls_allow.txt')
MAP_LINE = re.compile(r'^\s*0001:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+(f\s+)?(?:i\s+)?(\S+)\s*$', re.M)
QUALIFIED = re.compile(r'([\w:~<>]+)\(')


def patched_addresses():
    out = set()
    for line in open(PATCHED):
        m = re.match(r'^([0-9A-Fa-f]{8})\s', line)
        if m:
            out.add(int(m.group(1), 16))
    return out


def load_allow():
    """[(patched address or '*', caller pattern)]: "<patched address|*> <undecorated caller, * allowed>  # reason"."""
    allow = []
    if os.path.exists(ALLOW):
        for line in open(ALLOW):
            parts = line.split('#')[0].split()
            if len(parts) == 2:
                allow.append((None if parts[0] == '*' else int(parts[0], 16), parts[1]))
    return allow


def replacements(patched):
    """[(address, name, kind, file, line)] of the EI_REPLACE / EI_REPLACE_METHOD registrations of the patched
    addresses (check_signatures.registrations() reads every registration of the sources, 6 s)."""
    rx = re.compile(r'EI_REPLACE(?:_METHOD)?\(\s*0x(%s)\s*,\s*([\w:]+)' % '|'.join('%08X' % a for a in patched), re.I)
    out = []
    for path in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
        text = open(path, encoding='utf-8', errors='replace').read()
        for m in rx.finditer(text):
            line_start = text.rfind('\n', 0, m.start()) + 1
            if '//' in text[line_start:m.start()]:
                continue                                       # commented out
            out.append((int(m.group(1), 16), m.group(2), 'replace', os.path.relpath(path, ROOT),
                        text.count('\n', 0, m.start()) + 1))
    return out


def verify_objects():
    """Object names of the verify-only sources."""
    names = set()
    for pattern in ('tests/verify/*.cpp', 'tests/vectors/*.cpp'):
        for path in glob.glob(os.path.join(ROOT, pattern)):
            names.add(os.path.basename(path).lower() + '.obj')
    return names


def prefix_regex(name):
    """The decorated-name prefix of a C++ function `A::B::name` (or a plain C name)."""
    parts = name.split('::')
    ns = ''.join(re.escape(p) + '@' for p in reversed(parts[:-1]))
    return re.compile(r'^\?' + re.escape(parts[-1]) + '@' + ns + r'(?:[^@]+@)*@')


def short_name(und):
    m = QUALIFIED.search(und)
    return m.group(1) if m else und


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--dll', default=os.path.join(ROOT, 'build', 'release', 'ei_hook.dll'))
    ap.add_argument('--map')
    ap.add_argument('-v', '--verbose', action='store_true', help='list the allowed hits too')
    args = ap.parse_args(argv)
    map_path = args.map or os.path.splitext(args.dll)[0] + '.map'
    started = time.time()

    img = Image(args.dll)
    text = next((s for s in img.sections if s[0] == '.text'), None)
    if text is None:
        print(f'{args.dll}: no .text section')
        return 1
    _, text_rva, text_vsz, text_ptr, text_rsz = text
    code = img.data[text_ptr:text_ptr + min(text_vsz, text_rsz)]
    text_va = img.base + text_rva

    # Functions of the map (decorated name, VA, object), sorted by address.
    funcs = []
    for dec, va, flag, obj in MAP_LINE.findall(open(map_path, errors='replace').read()):
        if flag:
            funcs.append((int(va, 16), dec, obj.lower()))
    funcs.sort()
    starts = [f[0] for f in funcs]
    by_dec = {dec: (va, obj) for va, dec, obj in funcs}

    # Our replacement of each patched address.
    patched = patched_addresses()
    regs = replacements(patched)
    patched_with_reg = {r[0] for r in regs}
    by_first = {}                    # decorated name up to its first '@' -> decorated names
    for d in by_dec:
        by_first.setdefault(d.split('@', 1)[0], []).append(d)
    shortlist = {}
    for address, name, kind, rel, line in regs:
        if name not in shortlist:
            rx = prefix_regex(name)
            shortlist[name] = [d for d in by_first.get('?' + name.split('::')[-1], ()) if rx.match(d)]
    und = undecorate({d for names in shortlist.values() for d in names})
    by_name = {}                     # qualified name -> [(signature, object, decorated)], as candidates_for wants
    for names in shortlist.values():
        for d in names:
            u = und.get(d)
            if u and not any(e[2] == d for e in by_name.get(short_name(u), ())):
                by_name.setdefault(short_name(u), []).append((u, by_dec[d][1], d))
    declared = {k: [(u, o) for u, o, _ in v] for k, v in by_name.items()}
    decorated = {(u, o): d for v in by_name.values() for u, o, d in v}
    targets = {}                     # VA -> (patched address, our name)
    unresolved = []
    for address, name, kind, rel, line in regs:
        cands = candidates_for(declared, name, kind, rel, lambda u: u)
        if not cands:
            unresolved.append((address, name, rel, line))
        for c in cands:
            targets[by_dec[decorated[c]][0]] = (address, name)

    # Incremental-link thunks: the table at the start of .text is one `jmp rel32` per function.
    ilt_end = text_va
    thunks = {}
    pos = 0
    while code[pos:pos + 1] == b'\xcc':
        pos += 1
    while pos + 5 <= len(code) and code[pos] == 0xE9:      # 5-byte `jmp rel32` entries up to the first function
        dest = (text_va + pos + 5 + struct.unpack_from('<i', code, pos + 1)[0]) & 0xFFFFFFFF
        if dest in targets:
            thunks[text_va + pos] = dest
        pos += 5
    ilt_end = text_va + pos
    watch = dict(targets)
    for thunk, dest in thunks.items():
        watch[thunk] = targets[dest]

    # Call / jmp sites of the targets outside the thunk table, on instruction boundaries.
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    hits = []
    boundary = {}
    for m in re.finditer(rb'[\xe8\xe9]', code):
        i = m.start()
        site = text_va + i
        if site < ilt_end or i + 5 > len(code):
            continue
        dest = (site + 5 + struct.unpack_from('<i', code, i + 1)[0]) & 0xFFFFFFFF
        target = watch.get(dest)
        if target is None:
            continue
        k = bisect.bisect_right(starts, site) - 1
        if k < 0:
            continue
        start = starts[k]
        ok = boundary.get(start)
        if ok is None:
            lim = starts[k + 1] if k + 1 < len(starts) else text_va + len(code)
            ok = boundary[start] = {x.address for x in md.disasm(code[start - text_va:lim - text_va], start)}
        if site in ok:
            hits.append((site, code[i], target, funcs[k]))

    allow = load_allow()
    verify_objs = verify_objects()
    problems, allowed = [], []
    undec_callers = undecorate({h[3][1] for h in hits})
    for site, op, (address, name), (start, dec, obj) in hits:
        caller = short_name(undec_callers.get(dec, dec))
        reason = None
        if obj in verify_objs:
            reason = 'verify-only source'
        elif start in targets and targets[start][0] == address:
            reason = 'calls itself'
        else:
            for a, pattern in allow:
                if (a is None or a == address) and fnmatch.fnmatchcase(caller, pattern):
                    reason = 'allow-list'
                    break
        (allowed if reason else problems).append((site, op, address, name, caller, obj, start, reason))

    for site, op, address, name, caller, obj, start, _ in problems:
        print(f'PROBLEM: {caller} ({obj}, +{site - start:#x}) {"calls" if op == 0xE8 else "jumps to"} our {name}, '
              f'which replaces the Rebirth-patched {address:08X}: go through the address (an EI_ORIGINAL '
              f'forwarder, rewrite-guide rule 9) or allow it in tools/check_patched_calls_allow.txt with the reason')
    if args.verbose:
        for site, op, address, name, caller, obj, start, reason in allowed:
            print(f'allowed ({reason}): {caller} ({obj}) -> {name} [{address:08X}]')
    for address, name, rel, line in unresolved:
        print(f'note: {rel}:{line} {name} ({address:08X}) matches no symbol in the map (inlined or removed); not checked')
    print(f'{len(patched)} Rebirth-patched functions, {len(patched_with_reg)} with a replacement, {len(targets)} replacement '
          f'symbols ({len(thunks)} thunks), {len(unresolved)} unresolved; {len(hits)} direct call/jmp site(s): '
          f'{len(allowed)} allowed, {len(problems)} problem(s); {time.time() - started:.1f} s')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
