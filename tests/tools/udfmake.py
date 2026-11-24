#!/usr/bin/env python3
# udfmake.py - a minimal UDF (ECMA-167 / OSTA UDF 1.02) filesystem-image maker for FultaArc's UDF reader. NOT a
# trusted component. There is no UDF image tool on this host (no mkudffs/genisoimage), so this maker builds a
# bridge disc from ECMA-167 by hand; each output is validated by an INDEPENDENT reader (7z, which reads UDF) with
# `7z l`/`7z x` before commit, and every descriptor carries the self-verifying ECMA-167 tag checksum + CRC-ITU
# (CRC-16/CCITT; binascii.crc_hqx with seed 0) that the reader re-validates. MIT, same as the project.
#
# Scope: 2048-byte sectors, one type-1 partition map, File Entry (tag 261) with short_ad allocation, a root
# directory of File Identifier Descriptors (8-bit and 16-bit OSTA CS0 names), regular files and one subdirectory.
# Out: VAT, metadata/sparable partitions, extended attributes, named streams, inline/embedded data.
import struct, sys, binascii

SS = 2048                                      # sector / logical block size

def le16(v): return struct.pack('<H', v)
def le32(v): return struct.pack('<I', v)
def le64(v): return struct.pack('<Q', v)

def crc(body): return binascii.crc_hqx(body, 0)   # ECMA-167 7.2.6 CRC-ITU-T = CRC-16/XMODEM, seed 0

def tag(tag_id, body, location):
    """ECMA-167 descriptor tag (16 bytes) + body. `location` is the logical-block/sector the tag sits at."""
    t = bytearray(16)
    t[0:2] = le16(tag_id)
    t[2:4] = le16(2)                           # descriptor version (ECMA-167 2nd ed / UDF 1.02)
    t[4] = 0                                    # checksum, filled below
    t[5] = 0                                    # reserved
    t[6:8] = le16(1)                            # tag serial number
    t[8:10] = le16(crc(body))                   # descriptor CRC (over the body)
    t[10:12] = le16(len(body) & 0xffff)         # descriptor CRC length
    t[12:16] = le32(location)                   # tag location
    t[4] = (sum(t[0:4]) + sum(t[5:16])) & 0xff  # tag checksum: all tag bytes except byte 4
    return bytes(t) + body

def regid(ident, flags=0, suffix=b''):
    r = bytearray(32)
    r[0] = flags
    r[1:1 + len(ident)] = ident[:23]
    r[24:24 + len(suffix)] = suffix[:8]
    return bytes(r)

def charspec():                                # OSTA CS0
    c = bytearray(64)
    c[0] = 0
    info = b'OSTA Compressed Unicode'
    c[1:1 + len(info)] = info
    return bytes(c)

def dstring(s, fieldlen):
    """OSTA CS0 d-string in a fixed field; last byte holds the used length. 8-bit compression for ASCII."""
    if s == '':
        return bytes(fieldlen)
    b = bytes([8]) + s.encode('latin-1')       # compression id 8 (8-bit), then the characters
    out = bytearray(fieldlen)
    out[0:len(b)] = b
    out[fieldlen - 1] = len(b)                 # used length (incl. the compression-id byte)
    return bytes(out)

def timestamp():
    t = bytearray(12)
    t[0:2] = le16(0x1000 | 0)                   # type 1 (local), timezone 0
    t[2:4] = le16(2020); t[4] = 1; t[5] = 1     # year, month, day
    return bytes(t)

def extent_ad(length, loc): return le32(length) + le32(loc)      # 8 bytes
def lb_addr(block, part): return le32(block) + le16(part)        # 6 bytes
def long_ad(length, block, part): return le32(length) + lb_addr(block, part) + bytes(6)   # 16 bytes
def short_ad(length, pos, typ=0): return le32((typ << 30) | (length & 0x3fffffff)) + le32(pos)  # 8 bytes

def icbtag(file_type, parent_block, parent_part, flags):
    t = bytearray(20)
    t[0:4] = le32(0)                            # prior recorded number of direct entries
    t[4:6] = le16(4)                            # strategy type 4
    t[6:8] = bytes(2)
    t[8:10] = le16(1)                           # max number of entries
    t[10] = 0
    t[11] = file_type                           # 4 = directory, 5 = regular file
    t[12:18] = lb_addr(parent_block, parent_part)
    t[18:20] = le16(flags)                      # allocation descriptor type in low 3 bits (0 = short_ad)
    return bytes(t)

def fid(name, child_block, is_dir, location, is_parent=False):
    """File Identifier Descriptor (tag 257) at `location` (its dir-data block), padded to a 4-byte multiple."""
    chars = 0
    if is_parent:
        ident = b''
    else:
        if all(ord(c) < 128 for c in name):
            ident = bytes([8]) + name.encode('latin-1')          # 8-bit CS0
        else:
            ident = bytes([16]) + name.encode('utf-16-be')       # 16-bit CS0
    fc = (0x02 if is_dir else 0) | (0x08 if is_parent else 0)    # bit1 directory, bit3 parent
    body = bytearray()
    body += le16(1)                             # file version number
    body += bytes([fc])                         # file characteristics
    body += bytes([len(ident)])                 # length of file identifier
    body += long_ad(SS, child_block, 0)         # ICB of the child (one block)
    body += le16(0)                             # length of implementation use
    body += ident
    pad = (-(16 + len(body))) % 4               # tag(16) + body padded to 4
    body += bytes(pad)
    return tag(257, bytes(body), location)

def file_entry(location, file_type, parent_block, info_len, ad_bytes, link_count=1):
    """File Entry (tag 261) with short_ad allocation descriptors `ad_bytes`."""
    body = bytearray()
    body += icbtag(file_type, parent_block, 0, 0)       # flags 0 -> short_ad
    body += le32(0)                             # uid
    body += le32(0)                             # gid
    body += le32(0o755 if file_type == 4 else 0o644)    # permissions (UDF bit layout differs, 7z tolerant)
    body += le16(link_count)                    # file link count
    body += bytes([0])                          # record format
    body += bytes([0])                          # record display attributes
    body += le32(0)                             # record length
    body += le64(info_len)                      # information length
    body += le64((info_len + SS - 1) // SS)     # logical blocks recorded
    body += timestamp()                         # access
    body += timestamp()                         # modification
    body += timestamp()                         # attribute
    body += le32(1)                             # checkpoint
    body += long_ad(0, 0, 0)                     # extended attribute ICB
    body += regid(b'*FultaArc UDF')             # implementation id
    body += le64(location)                      # unique id
    body += le32(0)                             # length of extended attributes
    body += le32(len(ad_bytes))                 # length of allocation descriptors
    body += ad_bytes
    return tag(261, bytes(body), location)


def _vrs(which):                               # Volume Structure Descriptor (BEA01 / NSR02 / TEA01)
    d = bytearray(SS)
    d[0] = 0
    d[1:6] = which
    d[6] = 1
    return bytes(d)

def build(root_files, subdir=None):
    """root_files: [(name, data), ...]; subdir: (dirname, [(name, data), ...]) or None. One subdirectory level."""
    PART_START = 257                            # absolute sector of partition logical block 0

    # ---- partition-relative block layout ----
    pb = 0
    fsd_pb = pb; pb += 1
    root_fe_pb = pb; pb += 1
    root_data_pb = pb; pb += 1
    plan = []                                   # (kind, name, data, fe_pb, data_pb, nblocks)
    for name, data in root_files:
        fe = pb; pb += 1
        nb = max(1, (len(data) + SS - 1) // SS)
        dpb = pb; pb += nb
        plan.append(('file', name, data, fe, dpb, nb, 'root'))
    sub_fe_pb = sub_data_pb = None
    sub_plan = []
    if subdir:
        sub_fe_pb = pb; pb += 1
        sub_data_pb = pb; pb += 1
        for name, data in subdir[1]:
            fe = pb; pb += 1
            nb = max(1, (len(data) + SS - 1) // SS)
            dpb = pb; pb += nb
            sub_plan.append(('file', name, data, fe, dpb, nb, 'sub'))
    part_blocks = pb
    total = PART_START + part_blocks + 1        # + a trailing anchor sector
    img = bytearray(total * SS)

    def put(abs_sector, data):
        img[abs_sector * SS:abs_sector * SS + len(data)] = data

    # ---- Volume Recognition Sequence ----
    put(16, _vrs(b'BEA01')); put(17, _vrs(b'NSR02')); put(18, _vrs(b'TEA01'))

    # ---- Main Volume Descriptor Sequence @ 32, reserve copy @ 48 ----
    MVDS, RVDS, LVID_S = 32, 48, 64
    def pvd(loc):
        b = bytearray()
        b += le32(1) + le32(0)                  # vds number, primary vd number
        b += dstring('FULTAUDF', 32)            # volume identifier
        b += le16(1) + le16(1)                  # vol seq number, max vol seq number
        b += le16(2) + le16(2)                  # interchange level, max interchange level
        b += le32(1) + le32(1)                  # charset list, max charset list
        b += dstring('FultaArc UDF Volume Set', 128)   # volume set identifier
        b += charspec()                         # descriptor charset
        b += charspec()                         # explanatory charset
        b += extent_ad(0, 0)                    # volume abstract
        b += extent_ad(0, 0)                    # volume copyright notice
        b += regid(b'*FultaArc')                # application id
        b += timestamp()                        # recording date and time
        b += regid(b'*FultaArc UDF')            # implementation id
        b += bytes(64)                          # implementation use
        b += le32(0)                            # predecessor VDS location
        b += le16(0)                            # flags
        b += bytes(22)                          # reserved
        return tag(1, bytes(b), loc)
    def pd(loc):
        b = bytearray()
        b += le32(2)                            # vds number
        b += le16(0)                            # partition flags (bit0 allocated=0 is fine; set 1)
        b = bytearray(b); b[4:6] = le16(1)      # partition flags = 1 (allocated)
        b += le16(0)                            # partition number
        b += regid(b'+NSR02')                   # partition contents
        b += bytes(128)                         # partition contents use
        b += le32(1)                            # access type (1 = read-only)
        b += le32(PART_START)                   # partition starting location (absolute sector)
        b += le32(part_blocks)                  # partition length (sectors)
        b += regid(b'*FultaArc UDF')            # implementation id
        b += bytes(128)                         # implementation use
        b += bytes(156)                         # reserved
        return tag(5, bytes(b), loc)
    def lvd(loc):
        b = bytearray()
        b += le32(3)                            # vds number
        b += charspec()                         # descriptor charset
        b += dstring('FULTAUDF', 128)           # logical volume identifier
        b += le32(SS)                           # logical block size
        b += regid(b'*OSTA UDF Compliant', suffix=bytes([0x02, 0x01]) + bytes(6))   # domain id (UDF 1.02)
        b += long_ad(SS, fsd_pb, 0)             # logical volume contents use -> FSD (partition-relative)
        pmap = bytes([1, 6]) + le16(1) + le16(0)    # type-1 partition map: type,len,volseq,partnum
        b += le32(len(pmap))                    # map table length
        b += le32(1)                            # number of partition maps
        b += regid(b'*FultaArc UDF')            # implementation id
        b += bytes(128)                         # implementation use
        b += extent_ad(SS, LVID_S)              # integrity sequence extent -> LVID
        b += pmap
        return tag(6, bytes(b), loc)
    def iuvd(loc):
        b = bytearray()
        b += le32(4)                            # vds number
        b += regid(b'*UDF LV Info', flags=0, suffix=bytes([0x02, 0x01]) + bytes(6))
        iu = bytearray()
        iu += charspec()
        iu += dstring('FULTAUDF', 128)          # logical volume identifier
        iu += dstring('FultaArc', 36)           # LVInfo1
        iu += dstring('', 36)                   # LVInfo2
        iu += dstring('', 36)                   # LVInfo3
        iu += regid(b'*FultaArc UDF')
        iu += bytes(128)
        b += bytes(iu)
        return tag(4, bytes(b), loc)
    def usd(loc):
        return tag(7, le32(5) + le32(0), loc)   # vds number, number of allocation descriptors = 0
    def td(loc):
        return tag(8, bytes(496), loc)
    for base in (MVDS, RVDS):
        put(base + 0, pvd(base + 0))
        put(base + 1, pd(base + 1))
        put(base + 2, lvd(base + 2))
        put(base + 3, iuvd(base + 3))
        put(base + 4, usd(base + 4))
        put(base + 5, td(base + 5))

    # ---- Logical Volume Integrity Descriptor ----
    def lvid(loc):
        b = bytearray()
        b += timestamp()
        b += le32(1)                            # integrity type: 1 = close
        b += extent_ad(0, 0)                    # next integrity extent
        b += le64(16) + bytes(24)               # logical volume contents use: next unique id = 16
        b += le32(1)                            # number of partitions
        b += le32(0)                            # length of implementation use
        b += le32(0xFFFFFFFF)                   # free space table (unknown)
        b += le32(part_blocks)                  # size table
        return tag(9, bytes(b), loc)
    put(LVID_S, lvid(LVID_S))

    # ---- Anchor Volume Descriptor Pointers @ 256 and total-1 ----
    def avdp(loc):
        b = extent_ad(6 * SS, MVDS) + extent_ad(6 * SS, RVDS) + bytes(480)
        return tag(2, b, loc)
    put(256, avdp(256))
    put(total - 1, avdp(total - 1))

    # ---- File Set Descriptor (partition block fsd_pb) ----
    def fsd(loc):
        b = bytearray()
        b += timestamp()
        b += le16(3) + le16(3)                  # interchange level, max interchange level
        b += le32(1) + le32(1)                  # charset list, max charset list
        b += le32(0) + le32(0)                  # file set number, file set descriptor number
        b += charspec()                         # logical volume id charset
        b += dstring('FULTAUDF', 128)           # logical volume identifier
        b += charspec()                         # file set charset
        b += dstring('FULTAUDF', 32)            # file set identifier
        b += dstring('', 32)                    # copyright file identifier
        b += dstring('', 32)                    # abstract file identifier
        b += long_ad(SS, root_fe_pb, 0)         # root directory ICB (partition-relative)
        b += regid(b'*OSTA UDF Compliant', suffix=bytes([0x02, 0x01]) + bytes(6))   # domain id
        b += long_ad(0, 0, 0)                   # next extent
        b += long_ad(0, 0, 0)                   # system stream directory ICB
        b += bytes(32)                          # reserved
        return tag(256, bytes(b), loc)
    put(PART_START + fsd_pb, fsd(fsd_pb))

    # ---- directories: build FID lists, then File Entries ----
    def dir_data(entries, self_fe_pb, parent_fe_pb, self_data_pb):
        d = bytearray()
        d += fid('', parent_fe_pb, True, self_data_pb, is_parent=True)
        for kind, name, data, fe, dpb, nb, where in entries:
            d += fid(name, fe, kind == 'dir', self_data_pb)
        if subdir and entries is root_entries:
            d += fid(subdir[0], sub_fe_pb, True, self_data_pb)
        assert len(d) <= SS, "directory does not fit one block in this maker"
        return bytes(d)

    root_entries = plan
    rdata = dir_data(root_entries, root_fe_pb, root_fe_pb, root_data_pb)
    put(PART_START + root_data_pb, rdata)
    put(PART_START + root_fe_pb,
        file_entry(root_fe_pb, 4, root_fe_pb, len(rdata), short_ad(len(rdata), root_data_pb)))

    for kind, name, data, fe, dpb, nb, where in plan:
        put(PART_START + fe, file_entry(fe, 5, root_fe_pb, len(data), short_ad(len(data), dpb)))
        put(PART_START + dpb, data)

    if subdir:
        sdata = dir_data(sub_plan, sub_fe_pb, root_fe_pb, sub_data_pb)
        put(PART_START + sub_data_pb, sdata)
        put(PART_START + sub_fe_pb,
            file_entry(sub_fe_pb, 4, root_fe_pb, len(sdata), short_ad(len(sdata), sub_data_pb)))
        for kind, name, data, fe, dpb, nb, where in sub_plan:
            put(PART_START + fe, file_entry(fe, 5, sub_fe_pb, len(data), short_ad(len(data), dpb)))
            put(PART_START + dpb, data)

    return bytes(img)


if __name__ == '__main__':
    root = [('hello.txt', b'hello udf\n'),
            ('data.bin', bytes((i * 41) & 0xff for i in range(5000)))]
    sub = ('subdir', [('inner.txt', b'inside a udf subdir\n')])
    open(sys.argv[1], 'wb').write(build(root, sub))
