#!/usr/bin/env python3
"""lhamanifest.py - write tests/manifests/lha.txt from a black-box oracle's extraction of the LHA fixtures.

The oracle is lhasa 0.6.0's `lha` (ISC), the binary the LHA clean room pinned in its data/L0002/pins.sha256; this
script refuses any other binary (ORACLE_SHA256). Since the room was absorbed, the pinned build is kept in the private
record at cache/cleanrooms/lha/work/lhasa/inst/bin/lha. It is run as a program only: its
source is not read, by this script or by the clean room. For each fixture archive:

  1. the oracle extracts it (`lha xfw=<dir> <archive>`); every regular file it writes, and every symbolic link it
     creates (taking the link target as the data, as FultaArc does), gives a size and a CRC-32;
  2. tests/tools/arcls (FultaArc's public API) lists the archive. FultaArc's entry name is paired with the oracle's
     file case-insensitively, because the oracle lower-cases names for some os bytes (the room's log L0006);
  3. the archive gets rows only when every file FultaArc lists has an oracle file of the same size and CRC-32, and
     the oracle writes no file FultaArc does not list (test_manifests runs this set in exact mode). Otherwise it is
     excluded, and the reasons are written into the manifest header; tests/test_lha.c covers those cases.

The size and crc32 written are the oracle's. The entry column is FultaArc's UTF-8 name (the oracle cannot supply the
stored case). Usage:

  python3 tests/tools/lhamanifest.py --oracle ops/cache/cleanrooms/lha/work/lhasa/inst/bin/lha \
      --arcls <built arcls> \
      --fixtures ops/fixtures/lha > tests/manifests/lha.txt
"""
import argparse, collections, hashlib, os, re, shutil, subprocess, sys, tempfile, zlib

ORACLE_SHA256 = '3845d7178bcf123e35d46bf008529c9605237dd244b10b04cc7a101c5920faf4'   # room data/L0002/pins.sha256
NOT_ARCHIVES = ('README', 'README.md', 'SHA256SUMS', 'PROVENANCE.md')


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 16), b''):
            h.update(chunk)
    return h.hexdigest()


def oracle_files(oracle, archive, tmp):
    """{lower-cased relative path: (size, crc32)} of what the oracle writes, and its exit status."""
    out = os.path.join(tmp, 'o')
    if os.path.exists(out):
        subprocess.run(['chmod', '-R', 'u+rwx', out], capture_output=True)
        shutil.rmtree(out)
    os.makedirs(out)
    r = subprocess.run([oracle, 'xfw=' + out, archive], capture_output=True)
    files = {}
    for dp, dn, fn in os.walk(out):
        for name in dn + fn:
            p = os.path.join(dp, name)
            if os.path.islink(p):
                data = os.fsencode(os.readlink(p))
            elif os.path.isfile(p) and name in fn:
                with open(p, 'rb') as f:
                    data = f.read()
            else:
                continue
            rel = os.path.relpath(p, out).replace(os.sep, '/')
            files.setdefault(rel.lower(), []).append((len(data), zlib.crc32(data) & 0xFFFFFFFF))
    return files, r.returncode


def oracle_one(oracle, archive, name, tmp):
    """(size, crc32) of entry `name` extracted alone by the oracle into a fresh directory, or None. Used when the
    whole-archive extraction does not produce the file (e.g. an earlier entry is a file named like a later entry's
    directory, so the oracle cannot create the path)."""
    for want in dict.fromkeys([name, name.lower()]):
        out = os.path.join(tmp, 'one')
        if os.path.exists(out):
            subprocess.run(['chmod', '-R', 'u+rwx', out], capture_output=True)
            shutil.rmtree(out)
        os.makedirs(out)
        subprocess.run([oracle, 'xfw=' + out, archive, want], capture_output=True)
        for dp, dn, fn in os.walk(out):
            for f in fn:
                p = os.path.join(dp, f)
                if os.path.relpath(p, out).replace(os.sep, '/').lower() == name.lower() and not os.path.islink(p):
                    with open(p, 'rb') as fh:
                        data = fh.read()
                    return len(data), zlib.crc32(data) & 0xFFFFFFFF
    return None


def fulta_listing(arcls, archives):
    by = collections.defaultdict(list)
    r = subprocess.run([arcls] + archives, capture_output=True, check=True)
    for line in r.stdout.decode('utf-8').splitlines():
        a, kind, name, size, crc, flags, status = line.split('\t')
        name = re.sub(r'\\x([0-9a-f]{2})', lambda m: chr(int(m.group(1), 16)), name)   # arcls escapes
        by[a].append({'kind': kind, 'name': name, 'size': int(size), 'crc': int(crc, 16),
                      'flags': int(flags, 16), 'status': status})
    return by


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--oracle', required=True)
    ap.add_argument('--arcls', required=True)
    ap.add_argument('--fixtures', required=True)
    o = ap.parse_args()
    got = sha256(o.oracle)
    if got != ORACLE_SHA256:
        sys.exit(f'lhamanifest: oracle {o.oracle} has SHA-256 {got}, not the pinned {ORACLE_SHA256}; refusing')
    # The pinned build links liblhasa from its own install tree, through a RUNPATH that named the room's folder at
    # build time. Point the loader at the lib/ beside the binary instead (LD_LIBRARY_PATH outranks RUNPATH), so the
    # unmodified binary runs wherever the tree is kept.
    lib = os.path.join(os.path.dirname(os.path.abspath(o.oracle)), '..', 'lib')
    if os.path.isdir(lib):
        os.environ['LD_LIBRARY_PATH'] = os.pathsep.join(filter(None, [lib, os.environ.get('LD_LIBRARY_PATH')]))

    rels = []
    for dp, dn, fn in os.walk(o.fixtures):
        for f in fn:
            if f in NOT_ARCHIVES:
                continue
            rels.append(os.path.relpath(os.path.join(dp, f), o.fixtures).replace(os.sep, '/'))
    rels.sort()
    paths = [os.path.join(o.fixtures, r) for r in rels]
    fa = fulta_listing(o.arcls, paths)

    rows, excluded, alone, no_files = [], {}, [], []
    tmp = tempfile.mkdtemp(prefix='lhamanifest-')
    try:
        for rel, path in zip(rels, paths):
            ents = fa.get(path, [])
            ofiles, orc = oracle_files(o.oracle, path, tmp)
            reasons, mine = [], []
            if len(ents) == 1 and ents[0]['kind'] == '-':
                reasons.append('FultaArc does not open it: ' + ents[0]['status'])
                ents = []
            files = [e for e in ents if e['kind'] == 'F']
            seen = collections.Counter(e['name'] for e in files)
            for n, c in seen.items():
                if c > 1:
                    reasons.append(f'{n}: listed {c} times')
            used = set()
            for e in files:
                if any(ord(ch) < 0x20 or ch in '|\x7f' for ch in e['name']):
                    reasons.append(f"{e['name']!r}: name holds a control character or '|' (not representable in a"
                                   ' manifest line)')
                    continue
                if e['status'] != 'ok':
                    reasons.append(f"{e['name']}: FultaArc: {e['status']}")
                    continue
                key = e['name'].lower()
                cand = ofiles.get(key)
                if not cand:
                    one = oracle_one(o.oracle, path, e['name'], tmp)
                    if one:
                        cand = [one]
                        alone.append(f"{rel}|{e['name']}")
                if not cand:
                    reasons.append(f"{e['name']}: listed by FultaArc, not written by the oracle")
                    continue
                if len(cand) != 1:
                    reasons.append(f"{e['name']}: the oracle writes {len(cand)} files under this name")
                    continue
                osz, ocrc = cand[0]
                if (osz, ocrc) != (e['size'], e['crc']):
                    reasons.append(f"{e['name']}: FultaArc {e['size']} bytes crc {e['crc']:08x}, "
                                   f"oracle {osz} bytes crc {ocrc:08x}")
                    continue
                used.add(key)
                mine.append((rel, e['name'], osz, ocrc))
            for k in sorted(set(ofiles) - used - {e['name'].lower() for e in files}):
                reasons.append(f'the oracle writes {k}, FultaArc lists no such file')
            # a failing whole-archive extraction is accepted only when per-entry extraction was what it took
            recovered = any(a.startswith(rel + '|') for a in alone)
            if orc != 0 and not reasons and not recovered:
                reasons.append(f'the oracle exits with status {orc}')
            if reasons:
                excluded[rel] = reasons
            elif mine:
                rows.extend(mine)
            else:
                no_files.append(rel)
    finally:
        subprocess.run(['chmod', '-R', 'u+rwx', tmp], capture_output=True)
        shutil.rmtree(tmp, ignore_errors=True)

    n_arch = len({r[0] for r in rows})
    print('# lha.txt - LHA/LZH fixtures: the lhasa 0.6.0 test corpus (test/archives, ISC; private fixtures/lha, see its')
    print('# PROVENANCE.md). GENERATED by tests/tools/lhamanifest.py: size and crc32 are those of the files the pinned')
    print(f'# lhasa `lha` binary (SHA-256 {ORACLE_SHA256[:16]}..., run black-box) extracts; the entry')
    print("# column is FultaArc's name, paired with the oracle's file case-insensitively (the oracle lower-cases names")
    print('# for some os bytes). Exact mode: an archive is listed only when FultaArc and the oracle agree on every file.')
    print(f'# Included: {n_arch} archives, {len(rows)} files. Extracted by the oracle one entry at a time, because its')
    print(f'# whole-archive extraction could not create them: {", ".join(alone) or "none"}.')
    print(f'# Holding no file (directories only), so no row: {", ".join(no_files) or "none"}.')
    print(f'# Excluded: {len(excluded)} archives (corpus files), covered by tests/test_lha.c:')
    for rel in sorted(excluded):
        for i, why in enumerate(excluded[rel]):
            if i == 3:
                print(f'#     ... {len(excluded[rel]) - 3} more of the same kind')
                break
            why = ''.join(ch if ' ' <= ch != '\x7f' else f'\\x{ord(ch):02x}' for ch in why)
            print(f'#   {rel}: {why}')
    for rel, name, size, crc in rows:
        print(f'{rel}|{name}|{size}|{crc:08x}')


if __name__ == '__main__':
    main()
