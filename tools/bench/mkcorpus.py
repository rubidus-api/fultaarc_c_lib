#!/usr/bin/env python3
"""Write the benchmark corpus: neutral generated files, the same bytes on every run (seeded). MIT.

    python3 tools/bench/mkcorpus.py DIR [MiB]       (default 96 MiB in all)

text.txt (word text, 1/3), code.sys (x86-like: E8 calls among opcodes, 1/4), table.doc (fixed-width records,
1/4), mixed.bin (random runs and repeats, 1/6). The extensions steer ALZip's -m4, which picks the EGG method by
file type (.sys AZO, .doc LZMA, .txt bzip2, else Deflate).
"""
import os, random, sys

def main():
    d = sys.argv[1]
    mib = int(sys.argv[2]) if len(sys.argv) > 2 else 96
    total = mib << 20
    os.makedirs(d, exist_ok=True)
    r = random.Random(20261112)
    words = ('archive window block tree verbatim aligned offset match length literal chunk folder cabinet frame '
             'decoder stream header entry volume solid method filter password checksum record table').split()
    with open(os.path.join(d, 'text.txt'), 'wb') as f:
        n, i = total // 3, 0
        while n > 0:
            line = (' '.join(r.choice(words) for _ in range(r.randrange(6, 14))) + ' %d\n' % i).encode()
            f.write(line); n -= len(line); i += 1
    with open(os.path.join(d, 'code.sys'), 'wb') as f:
        n = total // 4
        ops = bytes(range(0x40, 0x60)) + b'\x8b\x89\x48\x83\xc3\x90\x0f\x85'
        buf = bytearray()
        while len(buf) < n:
            if r.random() < 0.12:
                buf += b'\xe8' + r.randrange(-1 << 20, 1 << 20).to_bytes(4, 'little', signed=True)
            else:
                buf.append(r.choice(ops))
        f.write(bytes(buf[:n]))
    with open(os.path.join(d, 'table.doc'), 'wb') as f:
        n, i = total // 4, 0
        while n > 0:
            rec = ('%08d|%-12s|%10.2f|%s|%04d-%02d-%02d\r\n' % (i, r.choice(words), r.random() * 1e6,
                   r.choice('ABCDEF'), 1990 + i % 35, 1 + i % 12, 1 + i % 28)).encode()
            f.write(rec); n -= len(rec); i += 1
    with open(os.path.join(d, 'mixed.bin'), 'wb') as f:
        n, hist = total // 6, bytearray()
        while len(hist) < n:
            if hist and r.random() < 0.5:
                at = r.randrange(len(hist)); hist += hist[at:at + r.randrange(16, 4096)]
            else:
                hist += r.randbytes(r.randrange(16, 2048))
        f.write(bytes(hist[:n]))

if __name__ == '__main__':
    main()
