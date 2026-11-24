#!/usr/bin/env python3
# zenc.py - a scratch Unix-compress (.Z) encoder used ONLY to make test fixtures for FultaArc's .Z decoder.
#
# This is NOT a trusted component and NOT part of the library. Its sole purpose is to produce .Z files whose
# correctness is then checked by an INDEPENDENT reference decoder (`gzip -d`, which decompresses .Z): a fixture is
# committed only after `gzip -d` decompresses it back to the original bytes. That independent check is what makes
# these fixtures a valid oracle for the decoder (src/codec/compress.c) - the encoder may share the author's
# understanding, but gzip's decoder is the judge, which breaks the circularity of a same-author encoder/decoder pair.
#
# It mirrors compress's output()/free_ent ordering and the region-relative byte-group alignment on a width change /
# CLEAR (see docs/research/formats/compress.md). MIT, same as the project.
import sys

def compress(data, maxbits=16, block_mode=True, force_clear_after=None):
    out = bytearray([0x1f, 0x9d, maxbits | (0x80 if block_mode else 0)])
    acc = 0; accn = 0; total = 0; region_start = 0
    st = {'n_bits': 9, 'maxcode': 511, 'clear': 0}
    maxmax = 1 << maxbits

    def emit(code, nbits):
        nonlocal acc, accn, total
        acc |= (code & ((1 << nbits) - 1)) << accn
        accn += nbits; total += nbits
        while accn >= 8:
            out.append(acc & 0xff); acc >>= 8; accn -= 8

    def pad(old_nbits):               # pad the current width region to a whole number of old_nbits-byte groups
        nonlocal region_start
        g = old_nbits * 8
        p = (-(total - region_start)) % g
        if p: emit(0, p)
        region_start = total

    def output(code, free_ent):       # pack at the current width, then bump/reset like compress's output()
        emit(code, st['n_bits'])
        if st['clear']:
            pad(st['n_bits']); st['n_bits'] = 9; st['maxcode'] = 511; st['clear'] = 0
        elif free_ent > st['maxcode'] and st['n_bits'] < maxbits:
            pad(st['n_bits']); st['n_bits'] += 1
            st['maxcode'] = maxmax if st['n_bits'] == maxbits else (1 << st['n_bits']) - 1

    FIRST = 257 if block_mode else 256
    def fresh(): return {bytes([i]): i for i in range(256)}
    table = fresh(); free_ent = FIRST; ncodes = 0
    w = b''
    for byte in data:
        c = bytes([byte]); wc = w + c
        if wc in table:
            w = wc; continue
        output(table[w], free_ent); ncodes += 1
        if free_ent < maxmax:
            table[wc] = free_ent; free_ent += 1
        w = c
        if force_clear_after is not None and ncodes == force_clear_after:
            st['clear'] = 1; output(256, free_ent); ncodes += 1
            table = fresh(); free_ent = FIRST
    if w:
        output(table[w], free_ent)
    if accn:
        out.append(acc & 0xff)
    return bytes(out)

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description="make a .Z fixture (validate the output with `gzip -d` before trusting it)")
    ap.add_argument('infile'); ap.add_argument('outfile')
    ap.add_argument('--maxbits', type=int, default=16)
    ap.add_argument('--no-block', action='store_true')
    ap.add_argument('--clear-after', type=int, default=None)
    a = ap.parse_args()
    data = open(a.infile, 'rb').read()
    z = compress(data, maxbits=a.maxbits, block_mode=not a.no_block, force_clear_after=a.clear_after)
    open(a.outfile, 'wb').write(z)
