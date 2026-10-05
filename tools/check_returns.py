"""Finds hooked or forwarded game functions declared `void` whose game callers use EAX afterwards.

Two crashes came from rewrites declared void whose original leaves a value in EAX that the
callers then use: UnitActionPoint 0052FDF0 returned its out pointer, CUiWindow::AddSurface
005FFA90 returned the surface it was given. Our void rewrite leaves whatever happens to be in EAX.

For every EI_REPLACE / EI_REPLACE_METHOD / EI_ORIGINAL in src/ and tests/verify/ whose declaration
(from the linker map, undecorated with undname, as tools/check_signatures.py does it) returns void,
this finds the direct CALL rel32 sites of that address in game.exe and follows the code after each
call (both ways at conditional jumps, through direct jumps, up to a few dozen instructions) until
EAX is overwritten or the path ends at a call. A site is flagged when an instruction reads
EAX / AX / AL / AH (test, cmp, push, mov from it, an address through it, ...) before it is written.
Writing AL or AX counts as writing EAX (MOV AL,..; PUSH EAX passes a bool), and XOR/SUB EAX,EAX,
OR EAX,-1 and AND EAX,0 are writes.
When a path reaches RET (or the site is a JMP rel32 tail call), the caller passes EAX on to its own
callers, so those are followed the same way (a few levels deep) and a hit is reported "via" it.

Not found this way: calls through vtable slots, function-pointer tables and other indirect calls.
Call sites inside functions we replace are skipped (that game code does not run, except from a
verify wrapper calling the original).

Hits of a replacement fail the check unless listed in tools/check_returns_allow.txt (reviewed
false positives: "<function address> <site address or *>  # reason"). Hits of forwarders are
warnings only, counted but listed only with -v: our code ignores the value, but the declaration is
wrong, and the check fails once the function is replaced with it.

usage: python tools/check_returns.py [-v] [--map build\\<cfg>\\ei_hook.map] [--address 0052FDF0 ...]
       (needs capstone and undname on PATH; build.cmd runs it after check_thunks.)
       --address checks the given addresses whatever their declared return type (to test the tool).
exit code 1 when a replacement has a hit that is not allow-listed
"""
import argparse, bisect, os, re, struct, sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_GRP_CALL, CS_GRP_JUMP, CS_GRP_RET, CS_GRP_INT
from capstone import x86

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from check_signatures import EXE, ROOT, MAP, load_sizes, declarations, registrations, candidates_for

ALLOW = os.path.join(ROOT, 'tools', 'check_returns_allow.txt')
BASE = 0x400000
MAX_STEPS = 48        # instructions followed after one call site, over all paths
MAX_DEPTH = 3         # levels of "the caller returns EAX to its callers"

EAX_REGS = {x86.X86_REG_EAX, x86.X86_REG_AX, x86.X86_REG_AL, x86.X86_REG_AH}


def return_type(und):
    m = re.match(r'^(?:(?:public|private|protected):\s*)?(?:(?:static|virtual)\s+)*(.*?)\s*'
                 r'__(?:thiscall|stdcall|fastcall|cdecl)\b', und)
    return m.group(1).strip() if m else None


class Game:
    def __init__(self):
        self.data = open(EXE, 'rb').read()
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True
        sizes = load_sizes()
        self.starts = sorted(sizes)
        self.sizes = sizes
        self._insn_starts = {}
        self._callers = None

    def function_of(self, address):
        i = bisect.bisect_right(self.starts, address) - 1
        if i >= 0 and address < self.starts[i] + self.sizes[self.starts[i]]:
            return self.starts[i]
        return None

    def insn_starts(self, function):
        """Addresses of the instructions of a function, disassembled linearly from its start."""
        s = self._insn_starts.get(function)
        if s is None:
            off = function - BASE
            md = Cs(CS_ARCH_X86, CS_MODE_32)
            s = {i.address for i in md.disasm(self.data[off:off + self.sizes[function]], function)}
            self._insn_starts[function] = s
        return s

    def callers(self, target):
        """[(site, 'call' | 'jmp')] of direct CALL / JMP rel32 to `target` that are real instructions
        of a known function."""
        if self._callers is None:
            self._callers = {}
            data = self.data
            for opcode, kind in ((0xE8, 'call'), (0xE9, 'jmp')):
                op = bytes([opcode])
                i = data.find(op)
                while i >= 0:
                    if i + 5 <= len(data):
                        site = BASE + i
                        dest = (site + 5 + struct.unpack_from('<i', data, i + 1)[0]) & 0xFFFFFFFF
                        if dest in self.sizes:
                            self._callers.setdefault(dest, []).append((site, kind))
                    i = data.find(op, i + 1)
        out = []
        for site, kind in self._callers.get(target, []):
            f = self.function_of(site)
            if f is not None and site in self.insn_starts(f):
                out.append((site, kind, f))
        return out

    def insn_at(self, address):
        off = address - BASE
        return next(self.md.disasm(self.data[off:off + 16], address, 1), None)

    def follow(self, start):
        """Follows the code from `start` with EAX unwritten. Returns (reads, returns): the first
        instructions reading EAX on some path, and whether some path reaches RET with EAX unwritten.
        A write of AL / AX counts as writing EAX: MSVC passes and returns bools and shorts in the low
        part and ignores the rest (MOV AL,..; PUSH EAX passes a bool; FNSTSW AX; SHL EAX,n)."""
        reads, returns = [], False
        work, seen, steps = [start], set(), 0
        while work and steps < MAX_STEPS:
            address = work.pop()
            while steps < MAX_STEPS and address not in seen:
                seen.add(address)
                steps += 1
                insn = self.insn_at(address)
                if insn is None:
                    break
                ops = insn.operands
                dest = ops[0].reg if ops and ops[0].type == x86.X86_OP_REG and ops[0].reg in EAX_REGS else None
                same_reg = len(ops) == 2 and ops[1].type == x86.X86_OP_REG and ops[1].reg == dest
                minus_one = len(ops) == 2 and ops[1].type == x86.X86_OP_IMM and ops[1].imm in (-1, 0xFFFFFFFF)
                zero = len(ops) == 2 and ops[1].type == x86.X86_OP_IMM and ops[1].imm == 0
                if dest is not None and ((insn.mnemonic in ('xor', 'sub', 'sbb') and same_reg)
                                         or (insn.mnemonic == 'or' and minus_one)
                                         or (insn.mnemonic == 'and' and zero)):
                    break                                     # idioms that set EAX whatever it held
                if insn.mnemonic.startswith('pusha'):
                    pass                                      # saves every register, uses none
                else:
                    read, write = insn.regs_access()
                    if any(r in EAX_REGS for r in read):
                        reads.append(insn)
                        break
                    if any(r in EAX_REGS for r in write):
                        break
                if insn.group(CS_GRP_CALL) or insn.group(CS_GRP_INT) or insn.mnemonic == 'hlt':
                    break                                     # the callee overwrites EAX
                if insn.group(CS_GRP_RET):
                    returns = True
                    break
                if insn.group(CS_GRP_JUMP):
                    target = ops[0].imm if ops and ops[0].type == x86.X86_OP_IMM else None
                    if insn.mnemonic == 'jmp':
                        if target is None:
                            break                             # tail call through a vtable, or a jump
                        address = target                      # table whose index was read above
                        continue
                    if target is not None:
                        work.append(target)
                address += insn.size
        return reads, returns


def scan(game, address, replaced, depth=0, chain=()):
    """[(site, chain, instruction)] of call sites reading the EAX `address` leaves, plus the number
    of sites skipped inside replaced functions."""
    hits, skipped = [], 0
    for site, kind, caller in game.callers(address):
        if caller in replaced:
            skipped += 1
            continue
        if kind == 'call':
            reads, returns = game.follow(site + 5)
        else:
            reads, returns = [], True                         # tail call: the caller returns EAX
        for insn in reads:
            hits.append((site, chain, f'{insn.address:08X} {insn.mnemonic} {insn.op_str}'))
        if returns and depth < MAX_DEPTH and caller not in chain and caller != address:
            sub, sub_skipped = scan(game, caller, replaced, depth + 1, chain + (caller,))
            hits += [(site, c, f'its call at {s:08X} reads {text}') for s, c, text in sub]
            skipped += sub_skipped
    return hits, skipped


def load_allow():
    allow = set()
    if os.path.exists(ALLOW):
        for line in open(ALLOW):
            parts = line.split('#')[0].split()
            if len(parts) >= 2:
                allow.add((int(parts[0], 16), parts[1].upper()))
    return allow


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--map', default=MAP)
    ap.add_argument('--address', nargs='*', default=[])
    ap.add_argument('-v', '--verbose', action='store_true', help='list the forwarder warnings too')
    args = ap.parse_args()
    forced = {int(a, 16) for a in args.address}

    game = Game()
    by_name = declarations(args.map)
    regs = [r for r in registrations() if r[2] in ('replace', 'forwarder')]
    replaced = {r[0] for r in regs if r[2] == 'replace'}
    allow = load_allow()

    checked = unresolved = failures = warnings = allowed = uncalled = 0
    done = set()
    for address, name, kind, rel, line in regs:
        if (address, kind) in done:
            continue
        if forced:
            if address not in forced:
                continue
        else:
            candidates = candidates_for(by_name, name, kind, rel, return_type)
            types = {return_type(u) for u, _ in candidates} - {None}
            if len(types) != 1:
                unresolved += 1
                continue
            if types.pop() != 'void':
                continue
        done.add((address, kind))
        checked += 1
        hits, skipped = scan(game, address, replaced)
        if kind == 'replace' and not game.callers(address):
            uncalled += 1
        new = [h for h in hits if (address, f'{h[0]:08X}') not in allow and (address, '*') not in allow]
        allowed += len(hits) - len(new)
        if not new:
            continue
        if kind == 'replace':
            failures += 1
            label = 'PROBLEM'
        else:
            warnings += 1
            label = 'warning'
            if not args.verbose:
                continue
        print(f'{label}: {rel}:{line} {name} ({kind} {address:08X}) is {'treated as' if forced else 'declared'} void, but {len(new)} game '
              f'call site(s) read EAX after it' + (f' ({skipped} more inside replaced functions skipped)'
                                                   if skipped else ''))
        for site, chain, text in new:
            via = ''.join(f', returned by {c:08X}' for c in chain[:1])
            print(f'    call at {site:08X} (in {game.function_of(site) or 0:08X}){via}: {text}')
    print(f'{checked} void declarations checked against their direct call sites ({unresolved} not resolved; '
          f'virtual-slot and other indirect calls are not seen), {failures} problem(s), {warnings} forwarder '
          f'warning(s){"" if args.verbose or not warnings else " (-v lists them)"}, {allowed} allow-listed hit(s); '
          f'{uncalled} void replacement(s) have no direct call site and are not checked')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
