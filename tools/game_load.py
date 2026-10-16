"""What harness games cost the machine at a given number at once (WB13; docs/notes/work-board.md, "Games at once").

usage: python tools/game_load.py run --games N --out FILE.jsonl --ticket ID [--scenarios K] [-- <work.py test args>]
       python tools/game_load.py report FILE.jsonl [FILE.jsonl...]
       python tools/game_load.py tdr [--since ISO-TIME]

run     samples the machine (tools/game_load_sample.ps1: nvidia-smi, GPU / CPU / memory counters, every game
        process) while `python tools/work.py test ID ...` plays a batch. With --games N under the machine's
        EI_HARNESS_SLOTS it holds the other slots itself (the same byte locks run_all and the scheduler take, from
        the top), so at most N games run machine-wide, other workers' included; it lets them go while another
        batch gathers every slot for a display-mode game. It can only lower the number of games, never raise
        it. --scenarios K: the first K names of a fixed mix (WORKLOAD; each runs on both installs, 2K runs)
        instead of passing scenario names yourself. --probe PREFIX: the scenarios PREFIX01 .. PREFIX<N> (copies of
        one long probe made for the measurement and not kept; add --vanilla-only after --). Afterwards it prints the report and the TDR check.
report  medians by the number of game processes running: GPU memory (nvidia-smi), GPU utilisation, power,
        total CPU, free RAM, and per game its private bytes, dedicated GPU memory, CPU and 3D engine share.
tdr     the System log's display-driver timeout events (4101 Display, 153 nvlddmkm) since a time (default: a day).
"""
import argparse
import datetime
import json
import os
import statistics
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run_all  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# A fixed mix of ordinary scenarios (no watchdog crash tests, none that sets the display mode or runs alone),
# interleaved so any first K covers boot, world, UI, combat, rendering and scripts.
WORKLOAD = ['world_loot', 'ui_hud', 'zone_gz15h_tornado', 'combat', 'render_scene', 'script_exec', 'ui_camp',
            'unit_ai', 'spells_cast', 'ui_trade_hold', 'effects', 'zone_base_bz1g', 'ui_saveload', 'particles',
            'models', 'levers', 'ui_quick_items', 'unit_ai_cast', 'trader_stock', 'world_load', 'ui_describe',
            'magic_traps', 'render_cpuwindows', 'sandbox_map', 'ui_base_dialogue', 'units', 'terrain_water_load',
            'zone_gz3g', 'ui_hero_screen', 'combat_more', 'script_quest', 'map_objects', 'ui_main_menu',
            'unit_ai_heal', 'effects_bow', 'ground_prints', 'ui_options_bind', 'camera', 'spells_hero',
            'zone_gz2g_traps', 'ui_camp_deals', 'movement', 'render_present', 'world_client', 'ui_objectives',
            'unit_ai_corpse', 'items', 'ui_help', 'hero_death', 'script_fx']


# ---- holding slots

class SlotHolder(threading.Thread):
    """Holds the machine slots above the first `keep` (byte locks in run_all.SLOT_DIR) until stopped."""

    def __init__(self, keep):
        super().__init__(daemon=True)
        self.keep = keep
        self.held = {}
        self.stopping = threading.Event()

    def run(self):
        os.makedirs(run_all.SLOT_DIR, exist_ok=True)
        while not self.stopping.is_set():
            gate = run_all._lock(os.path.join(run_all.SLOT_DIR, 'alone'))
            if gate is None:            # someone gathers every slot for a display-mode game: let ours go
                self.release()
            else:
                run_all._unlock(gate)
                for i in range(run_all.MACHINE_SLOTS - 1, self.keep - 1, -1):
                    if i not in self.held and (f := run_all._lock(os.path.join(run_all.SLOT_DIR, f'slot{i:02}'))):
                        self.held[i] = f
            self.stopping.wait(0.3)
        self.release()

    def release(self):
        for f in self.held.values():
            run_all._unlock(f)
        self.held = {}


def tdr_events(since):
    """[(time, id, provider)] of System log events 4101 (Display) and 153 (nvlddmkm) since a datetime."""
    ps = ("Get-WinEvent -FilterHashtable @{LogName='System'; Id=4101,153; StartTime=[datetime]'%s'} "
          "-ErrorAction SilentlyContinue | Where-Object { $_.ProviderName -in 'Display','nvlddmkm' } | "
          "ForEach-Object { '{0:o}|{1}|{2}' -f $_.TimeCreated, $_.Id, $_.ProviderName }") % since.strftime('%Y-%m-%dT%H:%M:%S')
    out = subprocess.run(['powershell', '-NoProfile', '-Command', ps], capture_output=True, text=True).stdout
    return [tuple(l.split('|')) for l in out.splitlines() if l.count('|') == 2]


def run(args, rest):
    out = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out), exist_ok=True)
    stop = out + '.stop'
    for f in (out, stop):
        if os.path.exists(f):
            os.remove(f)
    started = datetime.datetime.now()
    sampler = subprocess.Popen(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
                                os.path.join(ROOT, 'tools', 'game_load_sample.ps1'), '-Out', out, '-Stop', stop,
                                '-Since', started.strftime('%Y-%m-%dT%H:%M:%S')])
    holder = None
    if args.games < run_all.MACHINE_SLOTS:
        holder = SlotHolder(args.games)
        holder.start()
        deadline = time.time() + 600
        while len(holder.held) < run_all.MACHINE_SLOTS - args.games and time.time() < deadline:
            time.sleep(0.5)
        print(f'game_load: holding {len(holder.held)} of {run_all.MACHINE_SLOTS} slots, {args.games} left for games')
    time.sleep(6)                      # a few samples of the idle machine first
    names = WORKLOAD[:args.scenarios] if args.scenarios else []
    if args.probe:                     # N copies of one probe scenario: <prefix>01 .. <prefix>N, each once
        names += [f'{args.probe}{i:02}' for i in range(1, args.games + 1)]
    cmd = [sys.executable, os.path.join(ROOT, 'tools', 'work.py'), 'test', args.ticket] + names + rest
    code = subprocess.run(cmd, cwd=ROOT).returncode
    time.sleep(6)
    open(stop, 'w').close()
    sampler.wait(timeout=60)
    os.remove(stop)
    if holder:
        holder.stopping.set()
        holder.join(timeout=10)
    report([out])
    events = tdr_events(started)
    print(f'TDR events (4101 / 153) since {started:%H:%M:%S}: {len(events)}')
    for e in events:
        print('  ', *e)
    return 2 if events else code


# ---- report

def load(files):
    samples = []
    for f in files:
        with open(f, encoding='utf-8-sig') as fh:
            samples += [dict(json.loads(l), file=f) for l in fh if l.strip()]
    for s in samples:
        s['games'] = s['games'] if isinstance(s['games'], list) else [s['games']] if s['games'] else []
    return samples


def med(xs):
    return statistics.median(xs) if xs else float('nan')


def report(files, out=sys.stdout):
    samples = load(files)
    # per game CPU share: CPU seconds between consecutive samples where the process is in both
    last = {}
    for s in samples:
        for g in s['games']:
            key = (s['file'], g['pid'])
            prev = last.get(key)
            g['cpu_pct'] = (100 * (g['cpu_s'] - prev[1]) / (s['t'] - prev[0])) if prev and s['t'] > prev[0] else None
            last[key] = (s['t'], g['cpu_s'])
    idle = [s['nv_mem'] for s in samples if not s['games']]
    base = med(idle)
    print(f'{len(samples)} samples; idle GPU memory {base:.0f} MiB ({len(idle)} idle samples)', file=out)
    print(' n samp | GPU MiB med/max  per game | GPU% med/max | W med | CPU% med/max | free RAM min | '
          'per game: priv MB, GPU MB, CPU%, 3D%', file=out)
    bins = {}
    for s in samples:
        bins.setdefault(len(s['games']), []).append(s)
    for n in sorted(bins):
        b = bins[n]
        games = [g for s in b for g in s['games']]
        mem = [s['nv_mem'] for s in b]
        per = (med(mem) - base) / n if n else 0
        print(f'{n:2} {len(b):4} | {med(mem):6.0f} {max(mem):6.0f} {per:7.0f} | {med([s["nv_util"] for s in b]):4.0f} '
              f'{max(s["nv_util"] for s in b):4.0f} | {med([s["nv_w"] for s in b]):5.0f} | '
              f'{med([s["cpu"] for s in b]):5.1f} {max(s["cpu"] for s in b):5.1f} | {min(s["avail_mb"] for s in b):8.0f} | '
              f'{med([g["priv_mb"] for g in games]):6.0f} {med([g["gpu_mb"] for g in games]):6.0f} '
              f'{med([g["cpu_pct"] for g in games if g["cpu_pct"] is not None]):5.1f} '
              f'{med([g["gpu_3d"] for g in games]):5.1f}', file=out)
    exes = sorted({g['exe'] for s in samples for g in s['games']})
    peak = max(samples, key=lambda s: s['nv_mem'])
    print(f'executables: {", ".join(exes)}; peak GPU memory {peak["nv_mem"]:.0f} MiB with {len(peak["games"])} games; '
          f'peak temperature {max(s["nv_temp"] for s in samples):.0f} C; peak power {max(s["nv_w"] for s in samples):.0f} W',
          file=out)


def main():
    argv = sys.argv[1:]
    rest = []
    if '--' in argv:
        rest = argv[argv.index('--') + 1:]
        argv = argv[:argv.index('--')]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    r = sub.add_parser('run')
    r.add_argument('--games', type=int, required=True)
    r.add_argument('--out', required=True)
    r.add_argument('--ticket', required=True)
    r.add_argument('--scenarios', type=int, default=0)
    r.add_argument('--probe')
    p = sub.add_parser('report')
    p.add_argument('files', nargs='+')
    t = sub.add_parser('tdr')
    t.add_argument('--since')
    args = ap.parse_args(argv)
    if args.cmd == 'run':
        if args.games > run_all.MACHINE_SLOTS:
            sys.exit(f'game_load: {args.games} games is above the machine\'s {run_all.MACHINE_SLOTS} slots')
        sys.exit(run(args, rest))
    if args.cmd == 'report':
        report(args.files)
    else:
        since = datetime.datetime.fromisoformat(args.since) if args.since else datetime.datetime.now() - datetime.timedelta(days=1)
        events = tdr_events(since)
        print(f'{len(events)} TDR events (4101 / 153) since {since:%Y-%m-%d %H:%M}')
        for e in events:
            print('  ', *e)


if __name__ == '__main__':
    main()
