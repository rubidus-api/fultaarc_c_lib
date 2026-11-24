#!/usr/bin/env python3
# lzenc.py - a scratch lzip (.lz) fixture-maker for FultaArc's lzip reader. NOT a trusted component.
#
# The LZMA1 payload is produced by Python's `lzma` module (liblzma, the xz project) - an implementation INDEPENDENT
# of the LZMA SDK that FultaArc vendors for decoding, so a round-trip through it cross-checks the decode across two
# implementations. This script only adds lzip's framing: the "LZIP" header, the coded dictionary byte, and the
# 20-byte trailer (CRC-32 of the uncompressed data, the uncompressed size, the member size). FultaArc's reader then
# decodes the payload and verifies that trailer CRC-32 against the original bytes. MIT, same as the project.
# Single-member only (lzip's common case); uses a power-of-two dictionary so the coded byte is just the exponent.
import lzma, struct, zlib, sys

def make(data, dict_log=23):
    assert 12 <= dict_log <= 29
    D = 1 << dict_log
    raw = lzma.compress(data, format=lzma.FORMAT_RAW,
                        filters=[{"id": lzma.FILTER_LZMA1, "dict_size": D, "lc": 3, "lp": 0, "pb": 2}])
    hdr = b'LZIP' + bytes([1, dict_log])          # version 1; dict byte = exponent (fraction 0)
    member_size = len(hdr) + len(raw) + 20
    trailer = struct.pack('<IQQ', zlib.crc32(data) & 0xffffffff, len(data), member_size)
    return hdr + raw + trailer

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser(description="make a single-member .lz fixture")
    ap.add_argument('infile'); ap.add_argument('outfile'); ap.add_argument('--dict-log', type=int, default=23)
    a = ap.parse_args()
    open(a.outfile, 'wb').write(make(open(a.infile, 'rb').read(), a.dict_log))
