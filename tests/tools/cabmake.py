#!/usr/bin/env python3
# cabmake.py - a minimal Microsoft Cabinet (.cab) fixture-maker for FultaArc's CAB reader. NOT a trusted component.
#
# Builds CFHEADER + CFFOLDER + CFFILE + CFDATA from [MS-CAB], with Store (type 0) and MSZIP (type 1) folders. MSZIP
# compresses each <=32768-byte block as a RAW Deflate stream (zlib wbits=-15) seeded with the previous cumulative
# output's last 32 KiB as the preset dictionary (zdict) and prefixed with the "CK" signature - that cross-block
# window is the whole point of the reader's dict path, so _selfcheck() asserts zdict actually took effect before any
# bytes are written. Each fixture is then validated by an INDEPENDENT reader (7z, which reads CAB) before commit.
# MIT, same as the project. See docs/research/formats/cab.md.
import struct, zlib, sys

MAXBLK = 32768

def _mszip_blocks(stream):
    """Split `stream` into MSZIP CFDATA payloads; return list of (payload, uncomp_len)."""
    out = []
    pos = 0
    while pos < len(stream) or not out:
        chunk = stream[pos:pos + MAXBLK]
        dict_tail = stream[max(0, pos - MAXBLK):pos]          # previous cumulative output, last <=32 KiB
        co = zlib.compressobj(6, zlib.DEFLATED, -15, zdict=dict_tail) if dict_tail else \
             zlib.compressobj(6, zlib.DEFLATED, -15)
        body = co.compress(chunk) + co.flush(zlib.Z_FINISH)
        out.append((b'CK' + body, len(chunk)))
        pos += len(chunk)
        if pos >= len(stream):
            break
    return out

def _store_blocks(stream):
    out = []
    pos = 0
    while pos < len(stream) or not out:
        chunk = stream[pos:pos + MAXBLK]
        out.append((chunk, len(chunk)))
        pos += len(chunk)
        if pos >= len(stream):
            break
    return out

def _fold(buf, seed=0):
    """XOR the buffer as little-endian ULONGs, then a big-endian-positioned 1..3-byte tail, onto `seed`."""
    c = seed
    i = 0
    while i + 4 <= len(buf):
        c ^= struct.unpack('<I', buf[i:i + 4])[0]
        i += 4
    tail = 0
    rest = buf[i:]
    for j, b in enumerate(rest):
        tail |= b << (8 * (len(rest) - 1 - j))
    return c ^ tail

def _csum(resv, data, cb, cu):
    """[MS-CAB] CFDATA checksum: the abReserve and ab byte arrays are each folded INDEPENDENTLY (so a reserve length
    that is not a multiple of 4 does not shift ab's alignment), seeded with (cbData | cbUncomp<<16). Verified against
    7-Zip: concatenating the two before folding does NOT match (see cabmake history / cab.md)."""
    return _fold(data, _fold(resv, cb | (cu << 16)))

def _selfcheck(folders):
    """Prove zdict took effect: a second MSZIP block must NOT decode correctly without its dict."""
    for folder in folders:
        comp, files = folder[0], folder[1]
        if comp != 1:
            continue
        stream = b''.join(d for _, d in files)
        if len(stream) <= MAXBLK:
            continue
        blocks = _mszip_blocks(stream)
        b1, u1 = blocks[0]
        b2, _ = blocks[1]
        tail = stream[:u1][-MAXBLK:]
        ok = zlib.decompressobj(-15, zdict=tail).decompress(b2[2:])   # with dict: must round-trip
        assert ok == stream[u1:u1 + len(ok)], "zdict decode mismatch"
        try:
            nodict = zlib.decompressobj(-15).decompress(b2[2:])
            assert nodict != stream[u1:u1 + len(nodict)], "block 2 decoded WITHOUT a dict - zdict had no effect"
        except zlib.error:
            pass                                              # failing without the dict is the expected outcome too
    return True

# ---- LZX (CFFOLDER typeCompress 3) encoder -----------------------------------------------------------------------
# Written from [MS-PATCH] "LZX DELTA Compression and Decompression" (the non-delta core is the CAB LZX bitstream),
# and validated phase-by-phase with `7z x` (which reads CAB-LZX): uncompressed / verbatim-literals / verbatim+matches
# / aligned-offset / multi-chunk (>32768) / both window extremes (15 and 21) / E8 call-translation. CAB LZX caps the
# match length at 257 (no LZXD "Extra Length" field - that is an Exchange/WIM delta extension 7z does NOT read for
# CAB; pinned empirically). A CAB folder carries no 16-bit chunk-size prefix: each CFDATA is one 32768-byte LZX chunk,
# the bitstream realigns to a 16-bit boundary per CFDATA, and the E8 header is the first bit of the first CFDATA. The
# bitstream is MSB-first within 16-bit little-endian words. NOT a trusted component. See docs/research/formats/cab.md.
LZX_FRAME = 32768
LZX_POSSLOTS = {15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50}

def _lzx_footer(slot):
    return 0 if slot < 4 else min((slot >> 1) - 1, 17)

def _lzx_base_table(nslots):
    base, b = [], 0
    for s in range(nslots):
        base.append(b); b += 1 << _lzx_footer(s)
    return base

def _lzx_pkg_merge(freqs, limit=16):
    """Length-limited canonical Huffman (package-merge). freqs: dict sym->count>0. Returns dict sym->len in [1,limit],
    Kraft-complete."""
    syms = sorted(freqs)
    n = len(syms)
    if n == 0: return {}
    if n == 1: return {syms[0]: 1}                      # caller pads a second length-1 symbol
    base = sorted((freqs[s], (s,)) for s in syms)
    lengths = {s: 0 for s in syms}
    cur = list(base)
    for _ in range(limit - 1):
        packages = [(cur[i][0] + cur[i+1][0], cur[i][1] + cur[i+1][1]) for i in range(0, len(cur) - 1, 2)]
        cur = sorted(base + packages)
    for _w, members in cur[:2 * n - 2]:
        for s in members: lengths[s] += 1
    assert all(1 <= lengths[s] <= limit for s in syms) and sum(2**(limit - lengths[s]) for s in syms) == 2**limit
    return lengths

def _lzx_canonical(lengths, nsyms):
    """lengths dict sym->len (0/absent = not present). Returns sym->(code,len), MSB-first canonical codes."""
    present = sorted((lengths.get(s, 0), s) for s in range(nsyms) if lengths.get(s, 0) > 0)
    codes, code, prev = {}, 0, 0
    for ln, s in present:
        code <<= (ln - prev); codes[s] = (code, ln); code += 1; prev = ln
    return codes

def _lzx_tree_lengths(freq, nsyms, min_two=False):
    nz = {s: c for s, c in freq.items() if c > 0}
    if min_two and len(nz) < 2:
        return {s: 3 for s in range(nsyms)}             # aligned tree is always present; emit a complete 8x3 tree
    if len(nz) == 0: return {}
    if len(nz) == 1:
        s = next(iter(nz)); other = (s + 1) % nsyms
        if other == s: other = (s + 2) % nsyms
        return {s: 1, other: 1}
    return _lzx_pkg_merge(nz, 16)

class _LzxBitW:
    """MSB-first bit packer into 16-bit little-endian words."""
    def __init__(self): self.acc = 0; self.nbits = 0; self.out = bytearray()
    def put(self, value, n):
        if n == 0: return
        assert 0 <= value < (1 << n), (value, n)
        self.acc = (self.acc << n) | value; self.nbits += n
        while self.nbits >= 16:
            self.out += struct.pack('<H', (self.acc >> (self.nbits - 16)) & 0xFFFF)
            self.nbits -= 16; self.acc &= (1 << self.nbits) - 1 if self.nbits else 0
    def align16(self):
        if self.nbits % 16: self.put(0, 16 - (self.nbits % 16))
        assert self.nbits == 0
    def raw(self, data):
        assert self.nbits == 0; self.out += data

def _lzx_encode_lengths(bw, new_lens, prev_lens, use19=False):
    """Emit a 20-element pretree (4 bits each) + the delta codes (0..16, zero-runs 17/18, and same-delta run 19)."""
    out = []; i = 0; N = len(new_lens)
    while i < N:
        if new_lens[i] == 0:
            run = 1
            while i + run < N and new_lens[i+run] == 0: run += 1
            j = 0
            while run - j >= 20 and not use19:
                z = min(run - j - 20, 31); out.append((18, z, 5, None)); j += 20 + z
            while run - j >= 4:
                z = min(run - j - 4, 15); out.append((17, z, 4, None)); j += 4 + z
            while j < run:
                out.append(((prev_lens[i+j]) % 17, None, 0, None)); j += 1
            i += run
        else:
            d = (prev_lens[i] - new_lens[i]) % 17
            run = 1
            if use19:
                while i + run < N and new_lens[i+run] == new_lens[i] and prev_lens[i+run] == prev_lens[i] and run < 5:
                    run += 1
            if use19 and run >= 4:
                out.append((19, run - 4, 1, d)); i += run
            else:
                out.append((d, None, 0, None)); i += 1
    freq = {}
    for t in out:
        freq[t[0]] = freq.get(t[0], 0) + 1
        if t[0] == 19: freq[t[3]] = freq.get(t[3], 0) + 1
    if len(freq) == 1:
        s = next(iter(freq)); plens = {s: 1, (s + 1) % 20: 1}
    else:
        plens = _lzx_pkg_merge(freq, 16) if freq else {}
    for s in range(20): bw.put(plens.get(s, 0), 4)
    pc = _lzx_canonical(plens, 20)
    for sym, ev, eb, d19 in out:
        c, ln = pc[sym]; bw.put(c, ln)
        if eb: bw.put(ev, eb)
        if sym == 19: c2, l2 = pc[d19]; bw.put(c2, l2)

def _lzx_tokens(data, window, max_len):
    """Greedy LZ77 -> list of ('lit', byte) / ('match', offset, length). Overlap uses direct original-data compare."""
    tokens, n, table, i = [], len(data), {}, 0
    while i < n:
        best_len = best_off = 0
        if i + 2 <= n:
            key = bytes(data[i:i+3])
            for p in reversed(table.get(key, [])[-64:]):
                off = i - p
                if off < 1 or off > window - 3: continue
                l, maxl = 0, min(max_len, n - i)
                while l < maxl and data[p + l] == data[i + l]: l += 1
                if l >= 2 and l > best_len: best_len, best_off = l, off
        if best_len >= 2:
            tokens.append(('match', best_off, best_len)); end = i + best_len
            while i < end:
                if i + 3 <= n: table.setdefault(bytes(data[i:i+3]), []).append(i)
                i += 1
        else:
            tokens.append(('lit', data[i]))
            if i + 3 <= n: table.setdefault(bytes(data[i:i+3]), []).append(i)
            i += 1
    return tokens

def _lzx_slot_of(formatted, base):
    s = 0
    for k in range(len(base)):
        if base[k] <= formatted: s = k
        else: break
    return s

def lzx_encode(stream, window=15, e8=False, e8_size=0, block_mode='verbatim', enable_matches=True, max_len=257,
               use19=False):
    """Encode `stream` as a CAB-LZX folder: returns a list of (payload, uncomp_len) per CFDATA (32768-byte chunks).
    block_mode in {'verbatim','aligned','uncompressed'} (one block per chunk)."""
    max_len = min(max_len, 257)
    nslots = LZX_POSSLOTS[window]; base = _lzx_base_table(nslots)
    win_size = 1 << window; main_syms = 256 + nslots * 8
    data = bytearray(stream)
    if e8 and e8_size:                                   # forward E8 x86 pre-translation per 32768 chunk
        for frame in range(0, len(data), LZX_FRAME):
            cs = min(LZX_FRAME, len(data) - frame)
            if frame >= 0x40000000 or cs <= 10: continue
            i = 0
            while i < cs - 10:
                if data[frame + i] == 0xE8:
                    cur = frame + i
                    disp = struct.unpack('<i', bytes(data[frame+i+1:frame+i+5]))[0]
                    target = cur + disp
                    if 0 <= target < e8_size + cur:
                        if target >= e8_size: target = disp - e8_size
                        data[frame+i+1:frame+i+5] = struct.pack('<I', target & 0xFFFFFFFF)
                    i += 4
                i += 1
    bw = _LzxBitW()
    prev_main = [0] * main_syms; prev_len = [0] * 249
    R0 = R1 = R2 = 1; cfdata = []
    total = len(data); nchunks = max(1, (total + LZX_FRAME - 1) // LZX_FRAME)
    for ci in range(nchunks):
        chunk = data[ci*LZX_FRAME: min(ci*LZX_FRAME + LZX_FRAME, total)]
        csize = len(chunk); start = len(bw.out)
        if ci == 0:                                      # E8 header: the first bit(s) of the first CFDATA payload
            bw.put(1 if e8 else 0, 1)
            if e8: bw.put((e8_size >> 16) & 0xFFFF, 16); bw.put(e8_size & 0xFFFF, 16)
        if block_mode == 'uncompressed':
            bw.put(3, 3)
            bw.put((csize >> 16) & 0xFF, 8); bw.put((csize >> 8) & 0xFF, 8); bw.put(csize & 0xFF, 8)
            bw.align16(); bw.raw(struct.pack('<III', R0, R1, R2)); bw.raw(bytes(chunk))
            if csize & 1: bw.raw(b'\x00')
        else:
            btype = 2 if block_mode == 'aligned' else 1
            tokens = _lzx_tokens(chunk, win_size, max_len) if enable_matches else [('lit', b) for b in chunk]
            seq = []; mfreq = {}; lfreq = {}; afreq = {}
            lR0, lR1, lR2 = R0, R1, R2
            for t in tokens:
                if t[0] == 'lit':
                    mfreq[t[1]] = mfreq.get(t[1], 0) + 1; seq.append((t[1], None, 0, 0, None)); continue
                off, length = t[1], t[2]
                formatted = 0 if off == lR0 else 1 if off == lR1 else 2 if off == lR2 else off + 2
                slot = _lzx_slot_of(formatted, base); fb = _lzx_footer(slot)
                if length <= 8: lh, lfoot = length - 2, None
                else: lh, lfoot = 7, length - 9          # footer 0..248 for length 9..257 (CAB caps at 257)
                msym = 256 + (slot << 3) + lh; mfreq[msym] = mfreq.get(msym, 0) + 1
                if lfoot is not None: lfreq[lfoot] = lfreq.get(lfoot, 0) + 1
                if formatted <= 2: vb, vbn, asym = 0, 0, None
                else:
                    foot = formatted - base[slot]
                    if btype == 2 and fb >= 3:
                        asym, vb, vbn = foot & 7, foot >> 3, fb - 3; afreq[asym] = afreq.get(asym, 0) + 1
                    else: asym, vb, vbn = None, foot, fb
                seq.append((msym, lfoot, vb, vbn, asym))
                if formatted == 1: lR0, lR1 = lR1, lR0
                elif formatted == 2: lR0, lR2 = lR2, lR0
                elif formatted >= 3: lR2, lR1, lR0 = lR1, lR0, off
            R0, R1, R2 = lR0, lR1, lR2
            main_lengths = _lzx_tree_lengths(mfreq, main_syms); len_lengths = _lzx_tree_lengths(lfreq, 249)
            main_arr = [main_lengths.get(s, 0) for s in range(main_syms)]
            len_arr = [len_lengths.get(s, 0) for s in range(249)]
            mcodes = _lzx_canonical(main_lengths, main_syms); lcodes = _lzx_canonical(len_lengths, 249)
            bw.put(btype, 3)
            bw.put((csize >> 16) & 0xFF, 8); bw.put((csize >> 8) & 0xFF, 8); bw.put(csize & 0xFF, 8)
            if btype == 2:
                al_lengths = _lzx_tree_lengths(afreq, 8, min_two=True)
                for s in range(8): bw.put(al_lengths.get(s, 0), 3)
                acodes = _lzx_canonical(al_lengths, 8)
            _lzx_encode_lengths(bw, main_arr[:256], prev_main[:256], use19)
            _lzx_encode_lengths(bw, main_arr[256:], prev_main[256:], use19)
            _lzx_encode_lengths(bw, len_arr, prev_len, use19)
            prev_main, prev_len = main_arr, len_arr
            for msym, lfoot, vb, vbn, asym in seq:
                c, ln = mcodes[msym]; bw.put(c, ln)
                if lfoot is not None: c2, l2 = lcodes[lfoot]; bw.put(c2, l2)
                if vbn: bw.put(vb, vbn)
                if asym is not None: ca, la = acodes[asym]; bw.put(ca, la)
        bw.align16()
        cfdata.append((bytes(bw.out[start:]), csize))
    return cfdata


def build(folders, reserve=None, nextcab=None, continued=None):
    """folders: list of (comptype, [(name, data), ...]) or (comptype, files, opts); comptype 0=Store, 1=MSZIP,
    3=LZX (opts: {window,block_mode,e8,e8_size,...} passed to lzx_encode). Files concatenate per folder.
    reserve: None, or (cbCFHeader, cbCFFolder, cbCFData) to emit RESERVE_PRESENT with that per-structure padding.
    nextcab: None, or (szCabinetNext, szDiskNext) to set cfhdrNEXT_CABINET and emit the two names (step-over test).
    continued: None, or a list of file names to emit as CFFILEs with iFolder 0xFFFE (continued into the next cab)."""
    _selfcheck(folders)
    rh, rf, rd = reserve if reserve else (0, 0, 0)
    flags = (0x0004 if reserve else 0) | (0x0002 if nextcab else 0)
    next_bytes = (nextcab[0].encode() + b'\x00' + nextcab[1].encode() + b'\x00') if nextcab else b''
    cfiles = []          # (cbFile, uoff, ifolder, attribs, name_bytes)
    folder_blocks = []   # per folder: list of (payload, uncomp)
    comp_words = []      # per folder: the raw typeCompress word (LZX packs its window in the high byte)
    for fi, folder in enumerate(folders):
        comp, files = folder[0], folder[1]
        opts = folder[2] if len(folder) > 2 else {}
        stream = b''.join(d for _, d in files)
        off = 0
        for name, data in files:
            utf = any(b & 0x80 for b in name.encode())
            attr = 0x20 | (0x80 if utf else 0)               # _A_ARCH | optional _A_NAME_IS_UTF
            cfiles.append((len(data), off, fi, attr, name.encode('utf-8') + b'\x00'))
            off += len(data)
        if comp == 1:
            folder_blocks.append(_mszip_blocks(stream)); comp_words.append(1)
        elif comp == 3:
            win = opts.get('window', 15)
            folder_blocks.append(lzx_encode(stream, **opts)); comp_words.append(3 | (win << 8))
        else:
            folder_blocks.append(_store_blocks(stream)); comp_words.append(0)
    for name in (continued or []):                           # files continued into the next cabinet: iFolder 0xFFFE
        cfiles.append((0, 0, 0xFFFE, 0x20, name.encode('utf-8') + b'\x00'))

    nfolders, nfiles = len(folders), len(cfiles)
    cfhdr = 36 + (4 + rh if reserve else 0) + len(next_bytes)
    cffolder = 8 + rf
    data_start = cfhdr + nfolders * cffolder + sum(16 + len(f[4]) for f in cfiles)
    # lay out CFDATA blocks; record each folder's first-block offset
    coff = []
    cur = data_start
    datasec = bytearray()
    for blocks in folder_blocks:
        coff.append(cur)
        for payload, uncomp in blocks:
            resv = b'\x5a' * rd
            cs = _csum(resv, payload, len(payload), uncomp)     # abReserve and ab folded independently
            datasec += struct.pack('<IHH', cs, len(payload), uncomp) + resv + payload
            cur += 8 + rd + len(payload)
    coff_files = cfhdr + nfolders * cffolder

    out = bytearray()
    out += b'MSCF' + struct.pack('<IIIII', 0, cur, 0, coff_files, 0)
    out += struct.pack('<BBHHHHH', 3, 1, nfolders, nfiles, flags, 0x1234, 0)   # ver 1.3
    if reserve:
        out += struct.pack('<HBB', rh, rf, rd) + b'\xa5' * rh
    out += next_bytes                                        # szCabinetNext + szDiskNext (if cfhdrNEXT_CABINET)
    for fi in range(nfolders):
        out += struct.pack('<IHH', coff[fi], len(folder_blocks[fi]), comp_words[fi]) + b'\x5b' * rf
    for cbFile, uoff, ifolder, attr, name in cfiles:
        out += struct.pack('<IIHHHH', cbFile, uoff, ifolder, 0x4d21, 0x6c3e, attr) + name
    assert len(out) == data_start, (len(out), data_start)
    out += datasec
    struct.pack_into('<I', out, 8, len(out))                 # cbCabinet
    return bytes(out)

if __name__ == '__main__':
    folders = [(1, [('hello.txt', b'hello cab\n'), ('big.bin', bytes((i * 29) & 0xff for i in range(40000)))])]
    open(sys.argv[1], 'wb').write(build(folders))
