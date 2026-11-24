# RAR 1.4 / 1.5 — the file format

This page describes the RAR 1.4 and RAR 1.5 archives as FultaArc reads them: how an archive is laid out, what each
header holds, and how the checks are computed. How the compressed data itself is decoded is in
[`algorithm.md`](algorithm.md). How each rule here was found is in [`derivation.md`](derivation.md): format §*n* is
established in derivation §C*n*. The sources and their terms are in [`provenance.md`](provenance.md).

Everything here was observed and tested on archives written by RAR 1.55 for DOS (the RAR 1.5 format) and by
RAR 1.40.2 beta for DOS (the RAR 1.4 format), on archives built for the purpose, and on real archives from the
1990s (RAR 1.5: 815 archives, 13,459 entries, 432.9 MB; RAR 1.4: 9 archives, 395 entries). Nothing is claimed for
inputs unlike those. Where the format leaves a case open, or where a reader must choose a behaviour the format does
not fix, the choice FultaArc makes is marked **policy** — a decision, not a format fact; they are collected in §17.

All integers are little-endian. Offsets are counted from the first byte of the structure named.

## 1. The two container families

RAR 1.5 and RAR 1.4 are two different containers that hold the **same** compressed stream (`algorithm.md`). A
reader tells them apart by the first bytes of the file:

- **RAR 1.5**: a 7-byte marker `52 61 72 21 1a 07 00` (`Rar!` 1a 07 00). All seven bytes are checked.
- **RAR 1.4**: a 4-byte marker `52 45 7e 5e` (`RE~^`).

A self-extracting archive (§9, §15) has a DOS program before the marker; a reader finds the marker behind it under
the rules there. RAR 1.5 is the main subject; §§2–13 describe it. The RAR 1.4 container is §§14–16.

---

## 2. RAR 1.5: blocks and the block head

After the marker an archive is a chain of **blocks**. Every block starts with the same 7-byte head:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | `check`: the low 16 bits of a CRC-32 over the header (§3) |
| 2 | 1 | `type` |
| 3 | 2 | `flags` |
| 5 | 2 | `size`: the length of the whole header, counted from `check` |

Block types seen:

- `0x73` **main block** — the first block (§4).
- `0x74` **file block** — one stored file or directory; its packed data follows the header directly, and the next
  block starts right after that data (§5).
- `0x75` **comment block** — an inner block carried inside another header, never free-standing (§10).
- `0x76` **authenticity block** (§11).

A block whose type a reader does not know is passed over: `size` header bytes, and, when flag `0x8000` is set, the
further data whose length is the `u32` at header offset 7. (`0x77` and `0x78`, written by other RAR 1.5x programs,
are handled this way; §13.)

## 3. RAR 1.5: header checks

The `check` is the low 16 bits of an ordinary CRC-32 (ISO 3309 / ITU-T V.42, as in zlib) over the header bytes
**after** the two check bytes:

- **file block**: from the `type` byte to the end of the name (the whole header when no comment block follows the
  name);
- **main block**: over header bytes 2..12 (the 11 bytes `type`, `flags`, `size` and the six bytes after them), the
  same range even when the header is longer because a comment follows.

A wrong check makes RAR report "file header broken" / "Archive header broken" and then process the block anyway. A
reader may report it and continue (**policy**).

## 4. RAR 1.5: the main block (type 0x73)

Without a comment the main block is 13 bytes: the 7-byte head and six further bytes, all zero in every archive
seen. Its flags:

| Flag | Meaning |
|---|---|
| 0x0001 | multi-volume archive (set in every volume) |
| 0x0002 | an archive comment follows inside this header (§10) |
| 0x0004 | the archive is locked (`k`); nothing else changes |
| 0x0008 | solid archive (§8) |
| 0x0020 | authenticity information is present (§11) |
| 0x0040 | set together with a `Protect!` block by another writer (§13) |

Bits 0x0010 and 0x0080..0x8000, and the six trailing bytes, do not change what RAR lists or extracts; RAR 1.55 was
not seen to write them.

## 5. RAR 1.5: the file block (type 0x74)

Offsets from the block start:

| Offset | Size | Field |
|---|---|---|
| 0x00 | 2 | `check` (§3) |
| 0x02 | 1 | `type` = 0x74 |
| 0x03 | 2 | `flags` (below) |
| 0x05 | 2 | header size (= 32 + name length in every archive seen) |
| 0x07 | 4 | packed size (bytes of data after the header, in this volume) |
| 0x0b | 4 | unpacked size of the whole file |
| 0x0f | 1 | 0 in every archive seen; other values change nothing in list/extract |
| 0x10 | 4 | CRC-32 of the unpacked file (zlib CRC-32) |
| 0x14 | 4 | MS-DOS time (low 16 bits) and date (high 16 bits) |
| 0x18 | 1 | 0x0f — version: written by RAR 1.5 |
| 0x19 | 1 | method: 0x30 stored; 0x31..0x35 compressed (written by `-m1`..`-m5`) |
| 0x1a | 2 | name length |
| 0x1c | 4 | DOS attributes: 0x20 archive (file), 0x10 directory |
| 0x20 | n | name, ASCII, `\` between path parts |

Flags:

| Flag | Meaning |
|---|---|
| 0x8000 | the `u32` at offset 7 is the size of the data after the header (set in every file block; without it RAR does not skip the data) |
| 0x0001 | the file continues from the previous volume |
| 0x0002 | the file continues in the next volume |
| 0x0004 | the packed data is enciphered with a password (§12) |
| 0x0008 | a file comment follows the name inside the header (§10) |

The method byte distinguishes only stored (0x30) from compressed (any of 0x31..0x35); all five efforts share one
decoder (`algorithm.md`). Bits 0x0010..0x4000 change nothing in list/extract and RAR 1.55 was not seen to write
them.

The CRC field is compared with the CRC-32 of the extracted bytes; a mismatch is reported ("CRC failed") and the
bytes are written anyway. For a reader the CRC is the proof of a correct decode.

**Directories** have a block of their own: attribute bit 0x10, sizes 0, CRC 0. A directory that only holds other
entries need not have a block.

## 6. RAR 1.5: stored files

Method 0x30: the data after the header is the file's bytes unchanged; its length is the packed size. In every
stored file RAR wrote, packed size equals unpacked size.

## 7. RAR 1.5: multi-volume archives

Main flag 0x0001. Volumes are named `NAME.RAR`, `NAME.R00`, `NAME.R01`, … A file split across volumes has a file
block in each volume with the same name and the full unpacked size; each block carries its part of the packed data.
The parts, concatenated in volume order, form one packed stream — the split may fall anywhere in the packed bytes,
even inside a code. The CRC field is 0xffffffff in parts with flag 0x0002 (continues) and the CRC of the whole file
in the last part. A reader given the volume files in order does not depend on their names (**policy**).

## 8. RAR 1.5: solid archives

Main flag 0x0008. Each compressed file continues from the decoder state the previous file left — the window and
every list, counter and score of `algorithm.md`. The exceptions, measured at a file start, are:

- pending flag bits are dropped (each file starts by reading a fresh flag byte);
- the literal run is cut to at most 16, so literal mode ends and every file starts with a flag byte;
- the count of repeats in a row starts at 0.

The far-match code and the bytes of a last step beyond the unpacked size carry over. An empty file decodes to
nothing and leaves the state unchanged. Files are decoded in archive order: to extract the fifth file the first
four must be decoded. RAR 1.55 does not itself write a stored entry into a solid archive; given a hand-built one it
extracts it, keeps the decoder state and does not put its bytes into the window. Without flag 0x0008 each file
starts from the initial state.

## 9. RAR 1.5: self-extracting archives

An archive may follow a DOS program (a self-extracting module) in the same file. RAR 1.55 reads a file as a
self-extracting archive only when bytes 0..1 are `4d 5a` (`MZ`) and bytes 28..31 are `52 53 46 58` (`RSFX`); the
archive then starts at the first occurrence of the marker's first four bytes `52 61 72 21` at an offset from 39 to
32787, and the whole marker must be there. Everything from that offset is an archive as in §2. In a volume set made
with `-sfx`, only the first file (`NAME.EXE`) has a module; the rest are `NAME.R00`, `NAME.R01`, …

## 10. RAR 1.5: comments

A comment is a **comment block** (type 0x75) carried inside another header: an archive comment inside the main
header right after its 13 bytes (main flag 0x0002), a file comment inside the file header right after the name
(file flag 0x0008). The inner block:

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | check: low 16 bits of CRC-32 over the inner block's bytes 2..12 |
| 2 | 1 | type 0x75 |
| 3 | 2 | flags, 0 |
| 5 | 2 | size of the inner block (13 + data) |
| 7 | 2 | length of the comment text |
| 9 | 1 | 0x0f |
| 10 | 1 | method: 0x30 stored, otherwise packed |
| 11 | 2 | low 16 bits of CRC-32 of the text |
| 13 | … | the text, stored or packed |

A packed comment is an ordinary compressed stream (`algorithm.md`) decoded from a **fresh** decoder state to the
given length; it does not touch the state of the files, even in a solid archive. RAR shows a comment only when its
check agrees ("Comment broken - skipped" otherwise). RAR writes archive comments packed and file comments stored,
and shows only the kinds it writes. The text is in the writer's DOS code page.

## 11. RAR 1.5: authenticity information

Main flag 0x0020. RAR.DOC says `-av` is a registered-version feature; no such archive could be made in the room, so
authenticity blocks were read only from real archives. The `u32` at offset 9 of the main header (0 otherwise) is
the offset, from the start of the file, of a block of type 0x76: flags 0, no data after its header, 72 to 142 bytes
in the archives seen, its bytes 7..9 `0f 34 00`; it is the last block. RAR verifies it ("Ok" with the creator, the
archive's name and a time; "Failed" after any change to a byte before the block or to the block's contents). Its
contents are not described here: FultaArc does not support authenticity information. **A reader recognises the
block and cannot verify it** (policy).

## 12. RAR 1.5: encryption

File flag 0x0004 marks an entry enciphered with a password (`-p`). The original `fultaarc-cleanroom-rar15` room did not
obtain the RAR 1.5 keystream generator within its observation rules, so this page does not describe it directly;
FultaArc now decodes encrypted RAR 1.5 entries using the stream cipher derived in the separate clean room
`fultaarc-cleanroom-rar15-enc3` (`src/rar/rar15_enc/rar15_cipher.c`). See [`enc3/README.md`](enc3/README.md) and
[`enc3/RAR15-DECRYPT-UNPACK-SPEC.md`](enc3/RAR15-DECRYPT-UNPACK-SPEC.md) for the specification, derivation, and
provenance.

## 13. RAR 1.5: blocks of other writers

Real archives written by other RAR 1.5x programs (never by RAR 1.55 for DOS) carry blocks RAR 1.55 passes over (§2)
and FultaArc does the same:

- **type 0x77** after a file block (flags 0xc000, 24-byte header: `u32` data size at 7, `u16` 1 at 12, `u32`
  unpacked size at 14, 0x0f at 18, method at 19, `u32` CRC-32 at 20): its data is a compressed stream from a fresh
  state holding another operating system's attributes for the file; 188 of 188 such blocks decode to their CRC.
- **type 0x78** (flags 0xc000, the text `Protect!` in its header), with main flag 0x0040: a recovery record.

---

## 14. RAR 1.4: the older container

An archive is:

```
52 45 7e 5e          marker ("RE~^")
u16 size             of the main header, counted from the marker's first byte: 7 without a comment
u8  flags            (§14.1)
…                    size - 7 further bytes (the comment, §15)
file headers, each followed by its packed data, to the end of the file
```

There are no block types and no header checks; a file header starts where the previous file's data ends.

### 14.1 Main flags

| Bit | Meaning |
|---|---|
| 0x01 | volume of a multi-volume archive |
| 0x02 | an archive comment follows the flag byte (§15) |
| 0x04 | locked (`k`) |
| 0x08 | solid |
| 0x10 | the comment is packed (set with every comment RAR 1.40.2 wrote) |
| 0x80 | set in every archive RAR 1.40.2 wrote |

Clearing 0x80 or setting 0x40 / 0x20 changes nothing in either reader's test; real archives of 1993–94 carry 0x80,
0xc0 and 0xba. What 0x20 and 0x40 stand for was not observed (0x20 adds a field after the comment, §15).

### 14.2 File header

Offsets from the header's first byte:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | packed size (bytes of data after this header, in this volume) |
| 4 | 4 | unpacked size |
| 8 | 2 | 16-bit sum of the unpacked file (§14.3); in every part of a split file the sum of the whole file |
| 10 | 2 | header size: 21 + name length (+ 2 + comment length with flag 0x08) |
| 12 | 4 | MS-DOS time (low 16 bits) and date (high 16 bits) |
| 16 | 1 | DOS attributes: 0x20 archive, 0x10 directory |
| 17 | 1 | flags: 0x01 continued from the previous volume, 0x02 continues in the next, 0x04 password, 0x08 a comment follows the name |
| 18 | 1 | 2 — version (both readers print "1.30" / "1.3") |
| 19 | 1 | name length |
| 20 | 1 | method: 0 stored; 1..5 compressed (`-m1`..`-m5`) |
| 21 | n | name, `\` between path parts |

The version byte must be 2; RAR 1.40.2 calls version 1 "Old method" (a still older format, not examined here) and 3
or higher "unknown method". Method bytes 1..6 are read as compressed. **Directories** have their own header:
attribute 0x10, sizes 0, sum 0. A **file comment** (file flag 0x08) follows the name as a `u16` length and the
stored text (lines separated by CR LF); the header size counts them.

### 14.3 The 16-bit sum

RAR 1.4 does not use CRC-32 for the file. The sum: start with `s = 0`; for each byte `b` of the unpacked file,
`s = (s + b) mod 65536`, then rotate `s` left by one bit within 16 bits. Both readers print it as the "CRC"
(RAR 1.55 shows it as `0000xxxx`). A mismatch is reported as "CRC failed"; with 16 bits a wrong password passes it
once in 65536 times by chance.

### 14.4 Stored and compressed files

Method 0: the data is the file. Any other method: the data is a compressed stream exactly as in `algorithm.md`,
decoded from the initial state. **Solid** archives (main flag 0x08) pass the decoder state from file to file as in
§8. **Volumes** (main flag 0x01) are named `NAME.RAR`, `NAME.R00`, … (the same names RAR 1.55 uses, confirmed by
the RAR 1.40 manual); a split file has a header in each volume and the parts joined in order are one stream.

## 15. RAR 1.4: archive comments

With main flag 0x02 the bytes after the flag byte are:

```
u16 n            size of what follows this field (2 + data)
u16 length       of the comment text
data             n - 2 bytes
```

With main flag 0x10 (always, in what RAR 1.40.2 writes) the `data` is a compressed stream (`algorithm.md`, fresh
state, to `length` bytes) enciphered with the RAR 1.4 cipher (§16, `algorithm.md` §16) under the fixed keys
`k1 = 0`, `k2 = 7`, `k3 = 77`. Without flag 0x10 the comment is stored and not enciphered. With main flag 0x20 a
further field follows the comment (a `u16` length and that many bytes); RAR.DOC describes `-av` as adding the
author, the time and the archive's name, but the field's contents were not observed.

A self-extracting RAR 1.4 archive puts a module before the marker with `MZ` at offset 0 and `RSFX` at offset 28, as
RAR 1.55's modules do; RAR 1.55 reads an older-format archive behind such a front under the rule of §9.

## 16. RAR 1.4: encryption — read in full

File flag 0x04 (`-p`). Unlike the RAR 1.5 cipher (§12), the RAR 1.4 cipher **was read completely and is
implemented**: the packed data (the file itself when stored, the compressed stream otherwise) is enciphered byte by
byte with three key bytes derived from the password. The key set-up and the two-register step are in
`algorithm.md` §16. The keys are set anew from the password at the start of each file (also in a solid archive) and
run on across the parts of a file split over volumes. A wrong password shows as a failing sum (§14.3).

---

## 17. Reader policies — choices, not format facts

- **Output length.** RAR writes the whole step that reaches the unpacked size, even when it ends a few bytes
  beyond. FultaArc's decoder cuts the output at the unpacked size (its choice for damaged input); in a solid
  archive those extra bytes stay in the window.
- **A wrong header check** is reported and decoding continues (§3), as RAR does.
- **Code index 256** of a position/flag code, and **reading past the packed size**, are errors; RAR's own archives
  never produce them and RAR's behaviour there is undefined (it reads leftover memory). The decoder has no
  undefined behaviour on hostile input.
- **Other versions.** An entry whose version byte (offset 0x18) is not 0x0f is refused as another format version;
  values 20 and 29 occur in archives of 1996 and later and were not examined.
- **Encrypted entries.** Both RAR 1.4 (`§16`) and RAR 1.5 (`§12`, via [`enc3/`](enc3/README.md)) encrypted entries are
  decoded when the password is given.
- **Truncated archives.** The complete entries are extracted and the cut one is failed, as RAR 1.55 does.
- **Volumes** are taken in the order the reader is given them and do not depend on the volume file names (§7, §14.4).
- **Allocation.** A header's unpacked size is never trusted for an allocation on its own; the stream cannot unpack
  to more than about 1000 times its packed size, and that bounds the buffer.
- **Authenticity** blocks are recognised, not verified (§11).
- FultaArc reads RAR 1.4 / 1.5 only; it never writes them.
