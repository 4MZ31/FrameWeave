#!/usr/bin/env python3
"""After merging upstream: lines a merge added that our side already had.

Upstream has taken PushFPS features over as its own code (2.2.0: Live HUD Lock, the adaptive multiplier,
Frame Pacing Repair, the encoder tuning). Git merges such a copy in beside the original WITHOUT a conflict,
so the tree then holds two declarations, two rows, two rules. This lists, for every file both sides changed
since the merge base, the lines that now occur more often than they did on our side. Most hits are harmless
repeats (a lock, a return); read the ones that name a feature.

    python3 -B scripts/checks/merge_duplicates.py <merge base> <upstream ref> [path filter ...]

Run it with the merge in the working tree (before or after it is committed; "ours" is HEAD while the merge is
uncommitted, so pass HEAD~1 as a third ref once it is committed: --ours HEAD~1).
"""
import re
import subprocess
import sys


def names(a, b):
    out = subprocess.run(['git', 'diff', '--name-only', a, b], capture_output=True, text=True).stdout
    return set(out.split('\n')) - {''}


def main():
    args = sys.argv[1:]
    ours = 'HEAD'
    if '--ours' in args:
        i = args.index('--ours')
        ours = args[i + 1]
        del args[i:i + 2]
    if len(args) < 2:
        sys.exit(__doc__)
    base, theirs, only = args[0], args[1], args[2:]
    both = sorted(f for f in names(base, ours) & names(base, theirs)
                  if re.search(r'\.(cpp|h|hlsl|qml|js|txt|cmake|py|ps1|yml)$', f) or f == 'CMakeLists.txt')
    for f in both:
        if only and not any(o in f for o in only):
            continue
        head = subprocess.run(['git', 'show', ours + ':' + f], capture_output=True, text=True, errors='replace').stdout.split('\n')
        try:
            cur = open(f, encoding='utf-8', errors='replace').read().split('\n')
        except OSError:
            continue
        had, has = {}, {}
        for line in head:
            had[line.strip()] = had.get(line.strip(), 0) + 1
        for line in cur:
            has[line.strip()] = has.get(line.strip(), 0) + 1
        hits, seen = [], set()
        for n, line in enumerate(cur, 1):
            k = line.strip()
            if len(k) < 28 or k.startswith('//') or k.startswith('#include') or k in seen:
                continue
            if 1 <= had.get(k, 0) < has[k]:
                seen.add(k)
                hits.append((n, k))
        if hits:
            print(f'## {f}: {len(hits)}')
            for n, k in hits:
                print(f'   {n}: {k[:170]}')


if __name__ == '__main__':
    main()
