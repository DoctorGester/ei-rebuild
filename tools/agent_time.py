"""Where worker subagents spend their wall time: model turns, tool calls by kind, waiting on
background work, and time stopped (reboots, usage limits, waiting for the coordinator).

Reads the subagent transcripts Claude Code keeps for this repository
(~/.claude/projects/<repo>/<session>/subagents/*.jsonl).

usage: python tools/agent_time.py [--since 2026-10-13] [--top 20] [--polls]
  --polls  also list the slowest foreground shell commands that are not builds or scenario runs
           (polling loops and waits show up there)
"""
import argparse, collections, datetime, glob, json, os, re

PROJECT = os.path.expanduser(os.path.join('~', '.claude', 'projects', 'E--workspace-ei'))
STOPPED = 'stopped (reboot / usage limit / crash) or waiting for the coordinator'


def ts(s):
    return datetime.datetime.fromisoformat(s.replace('Z', '+00:00')).timestamp()


def kind(name, inp):
    if name in ('Bash', 'PowerShell'):
        c = inp.get('command', '')
        if 'run_all' in c:
            rest = re.split(r'[|>]', c.split('run_all.py', 1)[-1])[0]
            names = [w for w in rest.split() if not w.startswith('-') and not w.isdigit()]
            k = 'scenarios (subset)' if names else 'scenarios (full suite)'
        elif 'build.cmd' in c or 'cmake' in c or 'msbuild' in c.lower():
            k = 'build'
        elif re.search(r'check_\w+\.py|check_signatures|gen_originals', c):
            k = 'checks'
        elif re.search(r'x87sim|objdiff|dumpbin|compare_frames|compare\.py', c):
            k = 'binary / frame compare'
        elif re.match(r'\s*(cd [^;&]+(&&|;)\s*)?git\b', c):
            k = 'git'
        elif re.search(r'\b(grep|rg|sed -n|cat|head|tail|ls|find|wc|Get-Content|Select-String)\b', c):
            k = 'read / search (shell)'
        elif 'python' in c:
            k = 'python other'
        else:
            k = 'shell other'
        return k + (' [background]' if inp.get('run_in_background') else '')
    if name in ('Read', 'Grep', 'Glob'):
        return 'read / search (tools)'
    if name in ('Edit', 'Write', 'MultiEdit', 'NotebookEdit'):
        return 'edit'
    if name.startswith('mcp__ghidra'):
        return 'ghidra'
    return name


def is_tool_result(content):
    return isinstance(content, list) and content and isinstance(content[0], dict) and content[0].get('type') == 'tool_result'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--since', default='2000-01-01')
    ap.add_argument('--top', type=int, default=20)
    ap.add_argument('--polls', action='store_true')
    args = ap.parse_args()

    time = collections.Counter(); calls = collections.Counter(); slow = collections.Counter()
    total = 0.0; agents = 0; turns = []
    for path in glob.glob(os.path.join(PROJECT, '*', 'subagents', '*.jsonl')):
        entries = []
        for line in open(path, encoding='utf-8', errors='replace'):
            try:
                e = json.loads(line)
            except ValueError:
                continue
            if 'timestamp' in e and e.get('type') in ('user', 'assistant'):
                entries.append(e)
        if not entries or entries[0]['timestamp'] < args.since:
            continue
        agents += 1
        total += ts(entries[-1]['timestamp']) - ts(entries[0]['timestamp'])
        uses = {}; last_bg = None; prev = entries[0]
        for e in entries[1:]:
            t = ts(e['timestamp']); gap = t - ts(prev['timestamp'])
            content = e.get('message', {}).get('content')
            if e['type'] == 'assistant':
                if prev['type'] == 'user' and is_tool_result(prev.get('message', {}).get('content')):
                    time['model turns'] += gap
                    turns.append(gap)
                elif prev['type'] == 'assistant':
                    time['model turns'] += gap
                for c in content if isinstance(content, list) else []:
                    if c.get('type') == 'tool_use':
                        k = kind(c['name'], c.get('input', {}))
                        uses[c['id']] = (t, k, c.get('input', {}).get('command', ''))
                        if k.endswith('[background]'):
                            last_bg = k[:-len(' [background]')]
            elif is_tool_result(content):
                for c in content:
                    if isinstance(c, dict) and c.get('tool_use_id') in uses:
                        t0, k, cmd = uses.pop(c['tool_use_id'])
                        time[k] += t - t0; calls[k] += 1
                        if args.polls and t - t0 > 15 and k in ('shell other', 'python other', 'read / search (shell)'):
                            slow[(cmd.split() or [''])[0][:40]] += t - t0
            else:   # a message from outside: a background job's notice, or the coordinator resuming it
                text = content if isinstance(content, str) else json.dumps(content)
                if 'sent a message while you were working' in text or 'coordinator sent a message' in text:
                    time[STOPPED] += gap
                elif 'task-notification' in text or 'Background command' in text:
                    time['waiting on background ' + (last_bg or 'work')] += gap
                else:
                    time['waiting (other)'] += gap
            prev = e

    active = total - time[STOPPED]
    print(f'{agents} agents since {args.since}: {total / 3600:.1f} h of wall time, {active / 3600:.1f} h active '
          f'({time[STOPPED] / 3600:.1f} h stopped)')
    if turns:
        turns.sort()
        print(f'{len(turns)} model turns after a tool result: median {turns[len(turns) // 2]:.1f} s, '
              f'mean {sum(turns) / len(turns):.1f} s, {len(turns) / max(agents, 1):.0f} per agent')
    print(f'\n{"hours":>7} {"of active":>9}')
    for k, v in sorted(((k, v) for k, v in time.items() if k != STOPPED), key=lambda kv: -kv[1])[:args.top]:
        n = calls.get(k)
        print(f'{v / 3600:7.1f} {100 * v / max(active, 1):8.1f}%  {k}' + (f'  ({n} calls, {v / n:.0f} s each)' if n else ''))
    if args.polls:
        print('\nslowest foreground commands that are not builds or scenario runs (by first word):')
        for k, v in slow.most_common(10):
            print(f'{v / 3600:7.2f} h  {k}')


if __name__ == '__main__':
    main()
