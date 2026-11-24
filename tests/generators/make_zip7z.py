#!/usr/bin/env python3
"""Make the inputs and the 7z / ZIP test archives, and the manifest of what each must contain.

    python3 tests/generators/make_zip7z.py OUTDIR [MANIFEST]

OUTDIR/archives/ gets the archives (kept private: they are program output), MANIFEST (default
tests/manifests/zip7z.txt) gets one line per expected file: archive|entry|size|crc32.

Needs `7z` (7-Zip 25.01 was used) and `zip` (Info-ZIP 3.0) on PATH. Inputs are generated from fixed seeds, so the
inputs are identical on every run; the archives differ only in times and random salts. Every command run is written
to OUTDIR/commands.txt.
"""

import os
import random
import shutil
import subprocess
import sys
import zlib


def inputs(d):
    os.makedirs(d, exist_ok=True)
    rnd = random.Random(20261002)
    words = [''.join(rnd.choice('abcdefghijklmnop') for _ in range(rnd.randint(2, 9))) for _ in range(400)]
    files = {
        'text.txt': ' '.join(rnd.choice(words) for _ in range(60000)).encode(),
        'random.bin': bytes(rnd.getrandbits(8) for _ in range(70000)),
        'zeros.bin': bytes(300000),
        'empty.txt': b'',
        'one.txt': b'x',
        # long-distance repeats: the same 40 KiB block twice, 50 KiB apart (Deflate64 distances)
        'far.bin': (lambda blk, gap: blk + gap + blk)(bytes(rnd.getrandbits(8) for _ in range(40000)),
                                                      bytes(rnd.getrandbits(8) for _ in range(10000))),
        # bytes chosen so every branch converter finds candidates (E8/E9/0F8x, EB, F0/F8, 48, 40/7F, 94/90, 6F/EF,
        # 17/97, 00/FF, IA64 templates)
        'code.bin': bytes(rnd.choice([0xE8, 0xE9, 0x0F, 0x80, 0x85, 0x00, 0xFF, 0xEB, 0xF0, 0xF8, 0x48, 0x01,
                                      0x40, 0x7F, 0xC0, 0x94, 0x90, 0x6F, 0xEF, 0x17, 0x97, 0x10, 0x16, 0x12,
                                      0x08, 0x13, rnd.getrandbits(8)]) for _ in range(120000)),
        'sub/deep/nested.txt': b'nested\n' * 100,
        '한글 이름.txt': '압축 해제 테스트\n'.encode() * 50,
    }
    for name, data in files.items():
        p = os.path.join(d, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data)
    os.makedirs(os.path.join(d, 'emptydir'), exist_ok=True)
    return files


SEVENZ = [
    ('7z_copy', ['-m0=Copy']),
    ('7z_lzma', ['-m0=LZMA']),
    ('7z_lzma_eos', ['-m0=LZMA:eos']),
    ('7z_lzma2', ['-m0=LZMA2']),
    ('7z_lzma2_lc4', ['-m0=LZMA2:lc0:lp4:pb0']),
    ('7z_ppmd', ['-m0=PPMd']),
    ('7z_ppmd_small', ['-m0=PPMd:mem=64k:o=2']),
    ('7z_ppmd_o32', ['-m0=PPMd:mem=1m:o=32']),
    ('7z_bzip2', ['-m0=BZip2']),
    ('7z_deflate', ['-m0=Deflate']),
    ('7z_deflate64', ['-m0=Deflate64']),
    ('7z_bcj', ['-m0=BCJ', '-m1=LZMA']),
    ('7z_bcj2', ['-m0=BCJ2', '-m1=LZMA', '-m2=LZMA', '-m3=LZMA', '-mb0s0:1', '-mb0s1:2', '-mb0s2:3']),
    ('7z_arm', ['-m0=ARM', '-m1=LZMA2']),
    ('7z_armt', ['-m0=ARMT', '-m1=LZMA2']),
    ('7z_arm64', ['-m0=ARM64', '-m1=LZMA2']),
    ('7z_ppc', ['-m0=PPC', '-m1=LZMA2']),
    ('7z_sparc', ['-m0=SPARC', '-m1=LZMA2']),
    ('7z_ia64', ['-m0=IA64', '-m1=LZMA2']),
    ('7z_riscv', ['-m0=RISCV', '-m1=LZMA2']),
    ('7z_delta', ['-m0=Delta:4', '-m1=LZMA2']),
    ('7z_swap4', ['-m0=Swap4', '-m1=LZMA2']),
    ('7z_nonsolid', ['-ms=off']),
    ('7z_plainheader', ['-mhc=off']),
    ('7z_aes', ['-ppass1234']),
    ('7z_aes_hdr', ['-ppass1234', '-mhe=on']),
    ('7z_default', []),
]

ZIP_7Z = [
    ('zip_store', ['-tzip', '-mm=Copy']),
    ('zip_deflate', ['-tzip', '-mm=Deflate']),
    ('zip_deflate64', ['-tzip', '-mm=Deflate64']),
    ('zip_bzip2', ['-tzip', '-mm=BZip2']),
    ('zip_lzma', ['-tzip', '-mm=LZMA']),
    ('zip_lzma_eos', ['-tzip', '-mm=LZMA:eos']),
    ('zip_aes256', ['-tzip', '-ppass1234', '-mem=AES256']),
    ('zip_aes128', ['-tzip', '-ppass1234', '-mem=AES128']),
    ('zip_zipcrypto7z', ['-tzip', '-ppass1234', '-mem=ZipCrypto']),
    ('zip_utf8', ['-tzip', '-mcu=on']),
    ('zip_ppmd', ['-tzip', '-mm=PPMd']),
    ('zip_ppmd_o16', ['-tzip', '-mm=PPMd:o=16:mem=1m']),
    ('zip_xz', ['-tzip', '-mm=xz']),
]


def run(cmd, log, cwd):
    log.write(' '.join(cmd) + '\n')
    r = subprocess.run(cmd, cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode != 0:
        log.write('  -> exit %d: %s\n' % (r.returncode, r.stderr.decode(errors='replace').strip()))
    return r.returncode


def main():
    out = os.path.abspath(sys.argv[1])
    if os.path.exists(out):
        shutil.rmtree(out)
    src = os.path.join(out, 'in')
    inputs(src)
    arc = os.path.join(out, 'archives')
    os.makedirs(arc)
    with open(os.path.join(out, 'commands.txt'), 'w') as log:
        for name, sw in SEVENZ:
            run(['7z', 'a', '-bd', '-y'] + sw + [os.path.join(arc, name + '.7z'), '.'], log, src)
        for name, sw in ZIP_7Z:
            run(['7z', 'a', '-bd', '-y'] + sw + [os.path.join(arc, name + '.zip'), '.'], log, src)
        # Info-ZIP: deflate, store, bzip2, ZipCrypto, streamed input (data descriptor), zip64 forced by a stream
        run(['zip', '-r', '-q', os.path.join(arc, 'izip_deflate.zip'), '.'], log, src)
        run(['zip', '-r', '-q', '-0', os.path.join(arc, 'izip_store.zip'), '.'], log, src)
        run(['zip', '-r', '-q', '-Z', 'bzip2', os.path.join(arc, 'izip_bzip2.zip'), '.'], log, src)
        run(['zip', '-r', '-q', '-P', 'pass1234', os.path.join(arc, 'izip_zipcrypto.zip'), '.'], log, src)
        # output through a pipe (not seekable): zip must use data descriptors
        for name, sw in (('izip_stream.zip', ['-fz']), ('izip_stream_crypt.zip', ['-P', 'pass1234'])):
            log.write('zip -q %s - - < in/text.txt | cat > archives/%s   (pipe in, pipe out)\n' % (' '.join(sw), name))
            with open(os.path.join(src, 'text.txt'), 'rb') as fi:
                r = subprocess.run(['zip', '-q'] + sw + ['-', '-'], stdin=fi, stdout=subprocess.PIPE, check=True)
            with open(os.path.join(arc, name), 'wb') as fo:
                fo.write(r.stdout)
        # split 7z and split zip
        run(['7z', 'a', '-bd', '-y', '-v100k', os.path.join(arc, 'split.7z'), '.'], log, src)
        run(['zip', '-r', '-q', '-s', '100k', os.path.join(arc, 'izip_split.zip'), '.'], log, src)
    man = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'manifests', 'zip7z.txt')
    files = inputs(os.path.join(out, 'in'))
    names = sorted(os.listdir(arc))
    with open(man, 'w') as m:
        m.write('# archive|entry|size|crc32 - written by tests/generators/make_zip7z.py from its own inputs\n')
        for a in names:
            if a.endswith(('.z01', '.z02', '.z03')) or (a.startswith('split.7z.') and not a.endswith('.001')):
                continue
            if 'stream' in a:
                d = files['text.txt']
                m.write('%s|-|%d|%08x\n' % (a, len(d), zlib.crc32(d)))
                continue
            for n, d in sorted(files.items()):
                m.write('%s|%s|%d|%08x\n' % (a, n, len(d), zlib.crc32(d)))
    print('samples in', out, 'manifest', man)


if __name__ == '__main__':
    main()
