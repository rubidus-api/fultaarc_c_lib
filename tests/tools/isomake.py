#!/usr/bin/env python3
# isomake.py - a minimal ISO 9660 (ECMA-119) fixture-maker for FultaArc's ISO reader. NOT a trusted component.
#
# Plain ISO 9660 (no Joliet / Rock Ridge), level-2 uppercase names, one level of subdirectory. Each fixture is
# validated by an INDEPENDENT reader (7z, which reads ISO) back to the original bytes before commit - that is what
# makes it an oracle for FultaArc's reader, independent of this maker. MIT, same as the project.
import struct, sys

BS = 2048
def both16(v): return struct.pack('<H', v) + struct.pack('>H', v)
def both32(v): return struct.pack('<I', v) + struct.pack('>I', v)
def pad(b, n, c=b' '): return (b + c * n)[:n]

def _dirrec(lba, length, flags, name):
    base = 33 + len(name)
    if base % 2: base += 1
    r = bytearray(base)
    r[0] = base; r[1] = 0
    r[2:10] = both32(lba); r[10:18] = both32(length)
    r[18:25] = bytes([80, 1, 1, 0, 0, 0, 0]); r[25] = flags
    r[28:32] = both16(1); r[32] = len(name); r[33:33 + len(name)] = name
    return bytes(r)

def build(files):
    """files: list of (path, data); path may be 'NAME' or 'SUB/NAME' (one subdir level)."""
    tree = {'': []}; subdirs = []
    for path, data in files:
        if '/' in path:
            d, n = path.split('/', 1)
            if d not in tree: tree[d] = []; subdirs.append(d)
            tree[d].append((n, data))
        else:
            tree[''].append((path, data))
    for d in subdirs: tree[''].append((d, None))

    lba = {'': 20}; cur = 21
    for d in subdirs: lba[d] = cur; cur += 1
    file_lba = {}
    for path, data in files:
        file_lba[path] = cur; cur += max(1, (len(data) + BS - 1) // BS)
    total = cur
    img = bytearray(total * BS)

    def write_dir(dname, self_lba, parent_lba):
        recs = bytearray()
        recs += _dirrec(self_lba, BS, 0x02, b'\x00')
        recs += _dirrec(parent_lba, BS, 0x02, b'\x01')
        for name, data in tree.get(dname, []):
            if data is None:
                recs += _dirrec(lba[name], BS, 0x02, name.upper().encode())
            else:
                full = (dname + '/' + name) if dname else name
                recs += _dirrec(file_lba[full], len(data), 0x00, (name.upper() + ';1').encode())
        assert len(recs) <= BS, "a single directory sector is enough for these fixtures"
        img[self_lba * BS:self_lba * BS + len(recs)] = recs

    write_dir('', lba[''], lba[''])
    for d in subdirs: write_dir(d, lba[d], lba[''])
    for path, data in files:
        o = file_lba[path] * BS; img[o:o + len(data)] = data

    def ptab(be):
        r = bytearray([1, 0]) + struct.pack('>I' if be else '<I', lba['']) + struct.pack('>H' if be else '<H', 1) + b'\x00'
        if len(r) % 2: r += b'\x00'
        return r
    ptl, ptm = ptab(False), ptab(True)
    img[18 * BS:18 * BS + len(ptl)] = ptl
    img[19 * BS:19 * BS + len(ptm)] = ptm

    pvd = bytearray(BS)
    pvd[0] = 1; pvd[1:6] = b'CD001'; pvd[6] = 1
    pvd[8:40] = pad(b'', 32); pvd[40:72] = pad(b'FULTAISO', 32)
    pvd[80:88] = both32(total)
    pvd[120:124] = both16(1); pvd[124:128] = both16(1); pvd[128:132] = both16(BS)
    pvd[132:140] = both32(len(ptl))
    pvd[140:144] = struct.pack('<I', 18); pvd[148:152] = struct.pack('>I', 19)
    pvd[156:156 + 34] = _dirrec(lba[''], BS, 0x02, b'\x00')
    for a, b in [(190, 318), (318, 446), (446, 574), (574, 702)]: pvd[a:b] = pad(b'', 128)
    for a in (813, 830, 847, 864): pvd[a:a + 17] = b'0' * 16 + b'\x00'
    pvd[881] = 1
    img[16 * BS:17 * BS] = pvd
    term = bytearray(BS); term[0] = 255; term[1:6] = b'CD001'; term[6] = 1
    img[17 * BS:18 * BS] = term
    return bytes(img)

# ---- Joliet + Rock Ridge extension -------------------------------------------------------------------------------
# A richer builder for the Joliet (Microsoft Unicode) and Rock Ridge (IEEE P1281/P1282 SUSP/RRIP) fixtures. Kept
# apart from build() so basic.iso stays byte-stable (test_iso hardcodes its layout). Each output is still validated
# by `7z x` before commit. An entry is a dict: {'iso','name','data'|'symlink','dir'(optional, one level)}.
#   'iso'  - the uppercase 8.3 identifier stored in the primary tree (no ';1' here, added below)
#   'name' - the long name carried by Joliet (UCS-2) and Rock Ridge (NM); for a hidden-from-manifest agree test,
#            make 'name' == 'iso'-with-mixed-case so 7z (Joliet) and our reader (RR) list the same string
#   'symlink' - if present, a Rock Ridge SL target (the entry is a symlink; 'data' ignored)

def _nm_entries(name):
    """One or more NM entries (CONTINUE-chained) for `name`; each NM payload capped so an entry fits a byte."""
    b = name.encode()
    out = []
    i = 0
    while i < len(b) or not out:
        chunk = b[i:i + 100]
        i += len(chunk)
        flags = 0x01 if i < len(b) else 0x00            # CONTINUE if more follows
        out.append(b'NM' + bytes([5 + len(chunk), 1, flags]) + chunk)
    return out

def _sl_entry(target):
    """A single SL entry: 1-byte SL flags then component records (flags,len,text). '/'-split; leading '/' = ROOT."""
    comps = bytearray()
    parts = target.split('/')
    if target.startswith('/'):
        comps += bytes([0x08, 0])                       # ROOT component
        parts = [p for p in parts if p]
    for p in parts:
        if p == '.':   comps += bytes([0x02, 0])
        elif p == '..': comps += bytes([0x04, 0])
        else: comps += bytes([0x00, len(p)]) + p.encode()
    return b'SL' + bytes([5 + len(comps), 1, 0]) + comps

def _px_entry(mode):
    """PX: POSIX file mode + nlink/uid/gid, each both-endian 32-bit (we only fill mode meaningfully)."""
    body = both32(mode) + both32(1) + both32(0) + both32(0)
    return b'PX' + bytes([4 + len(body), 1]) + body

def build_ext(entries, joliet=True, rr=True):
    S_IFREG, S_IFDIR, S_IFLNK = 0o100644, 0o040755, 0o120777
    subdirs = []
    for e in entries:
        d = e.get('dir')
        if d and d not in subdirs: subdirs.append(d)

    # sector plan: 16 PVD, 17 SVD(if joliet) / term, 18 term, then dir extents and file data.
    sec = {}
    nxt = 20
    sec['pvd_root'] = nxt; nxt += 1
    for d in subdirs: sec['pvd_' + d] = nxt; nxt += 1
    cont = nxt; nxt += 2                                 # 2 sectors reserved for RR CE continuation areas
    if joliet:
        sec['jol_root'] = nxt; nxt += 1
        for d in subdirs: sec['jol_' + d] = nxt; nxt += 1
    for e in entries:
        if 'symlink' in e: continue
        full = (e['dir'] + '/' + e['iso']) if e.get('dir') else e['iso']
        sec['f_' + full] = nxt; nxt += max(1, (len(e['data']) + BS - 1) // BS)
    total = nxt
    img = bytearray(total * BS)
    ce_used = [0]                                        # bytes consumed in the continuation region

    def su_for(e, is_dir):
        """Rock Ridge System-Use entries for one entry."""
        if not rr: return []
        su = []
        su += _nm_entries(e['name'])
        if 'symlink' in e:
            su.append(_sl_entry(e['symlink'])); su.append(_px_entry(S_IFLNK))
        else:
            su.append(_px_entry(S_IFDIR if is_dir else S_IFREG))
        return su

    def pack_su(su, base_len):
        """Pack SU entries after a dir record of `base_len` bytes; spill overflow to a CE continuation area.
        Returns (in_record_bytes, ce_entry_or_b'')."""
        budget = 254 - base_len                          # leave room; records are <=255 and even
        inrec = bytearray(); i = 0
        while i < len(su) and len(inrec) + len(su[i]) + 28 <= budget:
            inrec += su[i]; i += 1
        if i == len(su):
            return bytes(inrec), b''
        spill = b''.join(su[i:])                          # the rest goes to a continuation area
        off = ce_used[0]; assert off + len(spill) <= 2 * BS, "continuation region too small for fixture"
        img[cont * BS + off:cont * BS + off + len(spill)] = spill
        ce_used[0] += (len(spill) + 1) & ~1
        clba, coff, clen = cont, off, len(spill)
        ce = b'CE' + bytes([28, 1]) + both32(clba) + both32(coff) + both32(clen)
        return bytes(inrec), ce

    def dirrec_su(lba, length, flags, name, su=b'', sp=False):
        base = 33 + len(name)
        if base % 2: base += 1
        head = bytearray(base)
        head[0] = 0; head[2:10] = both32(lba); head[10:18] = both32(length)
        head[18:25] = bytes([80, 1, 1, 0, 0, 0, 0]); head[25] = flags
        head[28:32] = both16(1); head[32] = len(name); head[33:33 + len(name)] = name
        extra = bytearray()
        if sp: extra += b'SP' + bytes([7, 1, 0xBE, 0xEF, 0])
        extra += su
        total_len = base + len(extra)
        if total_len % 2: total_len += 1
        r = bytearray(total_len); r[:base] = head; r[base:base + len(extra)] = extra
        r[0] = total_len
        return bytes(r)

    def write_pvd_dir(dname, self_lba, parent_lba, is_root):
        recs = bytearray()
        recs += dirrec_su(self_lba, BS, 0x02, b'\x00', sp=(rr and is_root))
        recs += dirrec_su(parent_lba, BS, 0x02, b'\x01')
        for e in entries:
            if (e.get('dir') or '') != dname: continue
            su = su_for(e, False)
            name = (e['iso'] + ';1').encode()
            base = 33 + len(name) + (1 if (33 + len(name)) % 2 else 0)
            inrec, ce = pack_su(su, base) if rr else (b'', b'')
            recs += dirrec_su(sec['f_' + ((dname + '/' + e['iso']) if dname else e['iso'])] if 'symlink' not in e else 0,
                              len(e.get('data', b'')) if 'symlink' not in e else 0,
                              0x00, name, inrec + ce)
        if is_root:
            for d in subdirs:
                recs += dirrec_su(sec['pvd_' + d], BS, 0x02, d.upper().encode(),
                                  b''.join(_nm_entries(d)) if rr else b'')
        assert len(recs) <= BS, "pvd dir sector overflow"
        img[self_lba * BS:self_lba * BS + len(recs)] = recs

    def write_jol_dir(dname, self_lba, parent_lba, is_root):
        recs = bytearray()
        recs += dirrec_su(self_lba, BS, 0x02, b'\x00')
        recs += dirrec_su(parent_lba, BS, 0x02, b'\x01')
        for e in entries:
            if (e.get('dir') or '') != dname: continue
            if 'symlink' in e: continue                  # Joliet carries no symlinks
            nm = (e.get('jname', e['name']) + ';1').encode('utf-16-be')   # jname: a Joliet name distinct from RR's
            recs += dirrec_su(sec['f_' + ((dname + '/' + e['iso']) if dname else e['iso'])], len(e['data']), 0x00, nm)
        if is_root:
            for d in subdirs:
                recs += dirrec_su(sec['jol_' + d], BS, 0x02, d.encode('utf-16-be'))
        assert len(recs) <= BS, "joliet dir sector overflow"
        img[self_lba * BS:self_lba * BS + len(recs)] = recs

    write_pvd_dir('', sec['pvd_root'], sec['pvd_root'], True)
    for d in subdirs: write_pvd_dir(d, sec['pvd_' + d], sec['pvd_root'], False)
    if joliet:
        write_jol_dir('', sec['jol_root'], sec['jol_root'], True)
        for d in subdirs: write_jol_dir(d, sec['jol_' + d], sec['jol_root'], False)
    for e in entries:
        if 'symlink' in e: continue
        full = (e['dir'] + '/' + e['iso']) if e.get('dir') else e['iso']
        o = sec['f_' + full] * BS; img[o:o + len(e['data'])] = e['data']

    def mkvd(vtype, root_lba, esc=b''):
        v = bytearray(BS)
        v[0] = vtype; v[1:6] = b'CD001'; v[6] = 1
        v[8:40] = pad(b'', 32); v[40:72] = pad(b'FULTAEXT', 32)
        v[80:88] = both32(total)
        if esc: v[88:88 + len(esc)] = esc
        v[120:124] = both16(1); v[124:128] = both16(1); v[128:132] = both16(BS)
        v[156:156 + 34] = dirrec_su(root_lba, BS, 0x02, b'\x00')
        for a in (813, 830, 847, 864): v[a:a + 17] = b'0' * 16 + b'\x00'
        v[881] = 1
        return v
    img[16 * BS:17 * BS] = mkvd(1, sec['pvd_root'])
    s = 17
    if joliet:
        img[17 * BS:18 * BS] = mkvd(2, sec['jol_root'], b'%/E'); s = 18
    term = bytearray(BS); term[0] = 255; term[1:6] = b'CD001'; term[6] = 1
    img[s * BS:(s + 1) * BS] = term
    return bytes(img)

# ---- zisofs (transparent zlib compression on ISO 9660) -----------------------------------------------------------
# A zisofs file's stored data is: 16-byte header (magic 37E45396C9DBD607 + LE32 uncompressed size + [4, block_log2,
# 0, 0]), then (nblocks+1) LE32 pointers (byte offsets from file start), then per-block zlib streams. A zero-length
# block (ptr[i]==ptr[i+1]) is a run of zeros. The block codec is standard zlib, so an independent zlib (this maker's
# encoder) vs the reader's miniz inflate is the cross-implementation oracle (no mkzftree needed). NOT a trusted part.
ZISOFS_MAGIC = bytes([0x37, 0xE4, 0x53, 0x96, 0xC9, 0xDB, 0xD6, 0x07])

def zisofs_compress(data, block_log2=15):
    import zlib
    bsize = 1 << block_log2
    n = len(data)
    nblocks = (n + bsize - 1) // bsize
    blocks = []
    for i in range(nblocks):
        chunk = data[i * bsize:(i + 1) * bsize]
        blocks.append(b'' if chunk == b'\x00' * len(chunk) else zlib.compress(chunk, 9))  # hole -> zero-length block
    hdr = ZISOFS_MAGIC + struct.pack('<I', n) + bytes([4, block_log2, 0, 0])
    cur = 16 + (nblocks + 1) * 4
    ptrs = []
    for blk in blocks:
        ptrs.append(cur); cur += len(blk)
    ptrs.append(cur)
    table = b''.join(struct.pack('<I', p) for p in ptrs)
    return hdr + table + b''.join(blocks)

# ---- Rock Ridge deep-directory relocation (CL / RE / PL) ---------------------------------------------------------
# A minimal RRIP relocation disc. A directory `deep` that would sit too far down is MOVED under a top-level
# `RR_MOVED`: its real record there is flagged RE, and a zero-length FILE-flagged placeholder with a CL entry is
# left at the original parent, pointing at the moved extent. A reader that follows RRIP lists `deep/<file>` via the
# CL and shows nothing under RR_MOVED (the RE record is skipped). Validated by `7z x` like the rest - IF 7z follows
# CL (checked empirically; recorded in the manifest/test-index either way).

def _rr_cl(lba): return b'CL' + bytes([12, 1]) + both32(lba)
def _rr_re():    return b'RE' + bytes([4, 1])
def _rr_pl(lba): return b'PL' + bytes([12, 1]) + both32(lba)

def build_reloc(filename='relocated file.txt', data=b'relocated deep file\n'):
    # A plain file at root and a second file inside the relocated directory prove the CL walk and the normal walk
    # coexist in one listing (a reader that only followed CL and dropped everything else would mislist).
    def drec(lba, length, flags, name, su=b'', sp=False):
        base = 33 + len(name)
        if base % 2: base += 1
        head = bytearray(base)
        head[2:10] = both32(lba); head[10:18] = both32(length)
        head[18:25] = bytes([80, 1, 1, 0, 0, 0, 0]); head[25] = flags
        head[28:32] = both16(1); head[32] = len(name); head[33:33 + len(name)] = name
        extra = bytearray()
        if sp: extra += b'SP' + bytes([7, 1, 0xBE, 0xEF, 0])
        extra += su
        total = base + len(extra)
        if total % 2: total += 1
        r = bytearray(total); r[:base] = head; r[base:base + len(extra)] = extra
        r[0] = total
        return bytes(r)
    ROOT, MOVED, DEEP, FDATA, FDATA2, RFDATA, total = 20, 21, 22, 23, 24, 25, 26
    rootfile, rootdata = 'top.txt', b'a plain file at the root\n'
    deep2, deep2data = 'second.txt', b'a second deep file\n'
    img = bytearray(total * BS)
    # root: ".", "..", RR_MOVED dir, the "DEEP" CL placeholder (a file record, flag 0, CL -> DEEP extent), and a
    # plain file (so the normal walk and the CL walk must coexist in one listing)
    r = bytearray()
    r += drec(ROOT, BS, 0x02, b'\x00', sp=True)
    r += drec(ROOT, BS, 0x02, b'\x01')
    r += drec(MOVED, BS, 0x02, b'RR_MOVED', b''.join(_nm_entries('rr_moved')))
    r += drec(0, 0, 0x00, b'DEEP', b''.join(_nm_entries('deep')) + _rr_cl(DEEP))
    r += drec(RFDATA, len(rootdata), 0x00, b'TOP.TXT;1', b''.join(_nm_entries(rootfile)))
    img[ROOT * BS:ROOT * BS + len(r)] = r
    # RR_MOVED: ".", "..", and the real "deep" directory record flagged RE (a reader must skip it here)
    m = bytearray()
    m += drec(MOVED, BS, 0x02, b'\x00')
    m += drec(ROOT, BS, 0x02, b'\x01')
    m += drec(DEEP, BS, 0x02, b'DEEP', b''.join(_nm_entries('deep')) + _rr_re())
    img[MOVED * BS:MOVED * BS + len(m)] = m
    # deep: ".." carries PL -> the real parent (root); two files inside
    d = bytearray()
    d += drec(DEEP, BS, 0x02, b'\x00')
    d += drec(ROOT, BS, 0x02, b'\x01', _rr_pl(ROOT))
    iso = filename.upper().replace(' ', '_')[:8].rstrip('_') or 'F'
    d += drec(FDATA, len(data), 0x00, (iso + '.TXT;1').encode(), b''.join(_nm_entries(filename)))
    d += drec(FDATA2, len(deep2data), 0x00, b'SECOND.TXT;1', b''.join(_nm_entries(deep2)))
    img[DEEP * BS:DEEP * BS + len(d)] = d
    img[FDATA * BS:FDATA * BS + len(data)] = data
    img[FDATA2 * BS:FDATA2 * BS + len(deep2data)] = deep2data
    img[RFDATA * BS:RFDATA * BS + len(rootdata)] = rootdata
    # PVD + terminator
    pvd = bytearray(BS)
    pvd[0] = 1; pvd[1:6] = b'CD001'; pvd[6] = 1
    pvd[40:72] = pad(b'FULTARELOC', 32); pvd[80:88] = both32(total)
    pvd[120:124] = both16(1); pvd[124:128] = both16(1); pvd[128:132] = both16(BS)
    pvd[156:156 + 34] = drec(ROOT, BS, 0x02, b'\x00')
    for a in (813, 830, 847, 864): pvd[a:a + 17] = b'0' * 16 + b'\x00'
    pvd[881] = 1
    img[16 * BS:17 * BS] = pvd
    term = bytearray(BS); term[0] = 255; term[1:6] = b'CD001'; term[6] = 1
    img[17 * BS:18 * BS] = term
    return bytes(img)

if __name__ == '__main__':
    files = [('HELLO.TXT', b'hello iso\n'),
             ('DATA.BIN', bytes((i * 37) & 0xff for i in range(3000))),
             ('SUB/INNER.TXT', b'inside a subdir\n')]
    open(sys.argv[1], 'wb').write(build(files))
