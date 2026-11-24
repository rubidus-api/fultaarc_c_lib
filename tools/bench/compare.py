#!/usr/bin/env python3
"""Time FultaArc's bench against other extractors on every archive in a directory.

    python3 tools/bench/compare.py DIR [--password PW] [--repeat N] [--only SUBSTR]

Run from the repository root after `make bench`. Columns: seconds, best of N (default 2): FultaArc
(build/bench/bench), 7-Zip testing with one thread (`7z t -mmt1`) and with its default threads (`7z t`), and for ZIP
files Info-ZIP `unzip -tq`. Every tool decodes all entries and checks them, writing nothing. A tool that fails or is
missing shows `-`.
"""
import os, subprocess, sys, time, shutil

def best(cmd, n):
    b = None
    for _ in range(n):
        t = time.perf_counter()
        r = subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        d = time.perf_counter() - t
        if r.returncode != 0:
            return None
        b = d if b is None or d < b else b
    return b

def main():
    a = sys.argv[1:]
    d = a[0]
    pw = a[a.index('--password') + 1] if '--password' in a else 'pass1234'
    n = int(a[a.index('--repeat') + 1]) if '--repeat' in a else 2
    only = a[a.index('--only') + 1] if '--only' in a else ''
    print('%-22s %9s %9s %9s %9s %8s' % ('archive', 'fultaarc', '7z-mmt1', '7z-mt', 'unzip', 'vs 7z-1'))
    for name in sorted(os.listdir(d)):
        if only not in name:
            continue
        p = os.path.join(d, name)
        f = best(['build/bench/bench', '-r', '1', '-p', pw, p], n)
        z1 = best(['7z', 't', '-mmt1', '-p' + pw, p], n) if shutil.which('7z') else None
        zm = best(['7z', 't', '-p' + pw, p], n) if shutil.which('7z') else None
        u = best(['unzip', '-tq', '-P', pw, p], n) if name.endswith('.zip') and shutil.which('unzip') else None
        s = lambda x: '-' if x is None else '%.2f' % x
        ratio = '%.2fx' % (f / z1) if f and z1 else '-'
        print('%-22s %9s %9s %9s %9s %8s' % (name, s(f), s(z1), s(zm), s(u), ratio), flush=True)

if __name__ == '__main__':
    main()
