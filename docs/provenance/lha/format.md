# LHA / LZH — the file format

This page describes the LHA (LZH) archive as FultaArc reads it: how an archive is laid out, what each entry
header holds, and how the checksums are computed. How the compressed data itself is decoded is in
[`algorithm.md`](algorithm.md). How each rule here was found is in [`derivation.md`](derivation.md): format §*n*
is established in derivation §C*n*. The sources and their licences are in [`provenance.md`](provenance.md).

Everything here was observed and tested on real archives (the lhasa 0.6.0 test corpus, 233 archives from many
tools and systems) and on archives built for the purpose. Nothing is claimed for inputs unlike those. Where the
format leaves a case open, the choice FultaArc makes is marked **policy** — it is a decision, not a format fact.

All integers are little-endian. Offsets are counted from the first byte of an entry.

## 1. The archive

An LHA archive is a sequence of entries, one per stored file or directory, normally ended by a single zero
byte. Some archives end at the end of the file without that byte, and some have bytes after it; a reader stops
at whichever comes first.

A self-extracting program (an `.exe`, `.com` or similar) may come before the first entry. Finding where the
archive starts inside such a file is a reader policy (§6).

Each entry is a header followed by the entry's data. There are four header layouts, called **levels** 0 to 3.
All four begin with the same fields.

## 2. The common front of every header

| Offset | Size | Field |
|---|---|---|
| 2 | 5 | method string, such as `-lh5-`: the compression method (see `algorithm.md`) |
| 7 | 4 | data length (at level 1: data length plus the extended headers, §3.2) |
| 11 | 4 | original length of the file |
| 15 | 4 | levels 0 and 1: DOS date and time; levels 2 and 3: Unix time (seconds since 1970, UTC) |
| 19 | 1 | a byte whose meaning was not established; kept as stored |
| 20 | 1 | header level: 0, 1, 2 or 3 |

The DOS date-time is one 32-bit value, `(date << 16) | time`, where
`time = hour << 11 | minute << 5 | second / 2` and `date = (year − 1980) << 9 | month << 5 | day`. It is local
time; the archive does not say which time zone. A date with an out-of-range field (a month of 0, for example) is
treated as having no valid time.

## 3. Levels 0 and 1

These two levels start with a size byte and a checksum byte:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `S`: the number of header bytes that follow these first two |
| 1 | 1 | the sum of those `S` bytes, modulo 256 |
| 21 | 1 | `n`: the length of the name |
| 22 | n | the name |
| 22 + n | 2 | CRC-16 of the original file (§5.1) |

A header size `S` of 0 is the end-of-archive byte. A wrong sum is an error.

### 3.1 Level 0

The header is exactly `2 + S` bytes long and the data follows at once. Any header bytes after the CRC (from
offset `24 + n` up to `2 + S`) form an **extra area**. Three kinds were seen; in each the first byte names the
system that wrote the archive:

- `U` (Unix): a 32-bit Unix time that replaces the DOS time, then a 16-bit Unix mode, user id and group id.
- `K` (OS-9/68k): a 32-bit Unix time, four bytes of unknown use, 16-bit OS-9 permissions, user id and group id.
- `9` (OS-9): the next byte holds OS-9 permissions; the rest is of unknown use.

Archives written by PMarc put plain text there instead; it is not a system byte.

### 3.2 Level 1

The byte at offset `24 + n` names the system that wrote the archive. The last two bytes of the `2 + S`-byte
header are the size of the first **extended header** (§4), and the chain of extended headers follows the header.
The data starts after the chain, and its length is the value at offset 7 minus the total size of the chain.

### 3.3 Names at levels 0 and 1

A name ends at its first zero byte, if there is one. Both `\` and `/` separate directory names.

The header does not record which character set a name is in. In practice names are in the code page of the
system that made the archive — for Japanese archives, Shift_JIS (CP932). In the double-byte code pages CP932,
CP936 (Simplified Chinese) and CP950 (Traditional Chinese), the byte 0x5C (`\`) is also the second byte of many
characters. A reader that treats every 0x5C as a separator splits those characters in two. FultaArc's decoder
does that, and FultaArc's reader then rejoins them (§6).

## 4. Extended headers (levels 1, 2 and 3)

### 4.1 The chain

Extended headers form a chain. Each one starts with a size field. If the size is 0, the chain ends; otherwise the
header is `size` bytes long: one type byte, the payload, and the size of the next header at the end. The size
field is 2 bytes wide at levels 1 and 2 and 4 bytes wide at level 3.

### 4.2 Types with a known meaning

| Type | Payload | Meaning |
|---|---|---|
| 0x00 | 16-bit value (sometimes one more byte) | CRC-16 of the whole header (§5.2) |
| 0x01 | bytes | the file name (may be empty) |
| 0x02 | bytes | the directory path; its parts are separated by 0xFF bytes, a final 0xFF is optional |
| 0x41 | three 64-bit values | Windows creation, modification and access times, in 100-nanosecond units since 1601-01-01 UTC |
| 0x42 | two 64-bit values | data length and original length; they replace the 32-bit fields, so files of 4 GiB and more can be stored |
| 0x50 | 16-bit value | Unix mode (with system byte `K`: OS-9 permissions) |
| 0x51 | two 16-bit values | group id, then user id |
| 0x54 | 32-bit value | Unix time; replaces the DOS time of levels 0 and 1 |
| 0xCC | bytes | with system byte `9`: payload byte 7 holds OS-9 permissions |

Other types (0x40, 0x46, 0x4F, 0x71, 0x7F and more) occur, but their meaning was not established. They are kept
as stored and nothing is claimed for them.

The full name of an entry is its path followed by its file name. If the full name contains `|`, the part before
it is the name of a **symbolic link** and the part after it is the link's target.

### 4.3 Levels 2 and 3

| | Level 2 | Level 3 |
|---|---|---|
| Offset 0 | 16-bit total header length `T` | 16-bit value (4 in every sample) |
| Offset 21 | CRC-16 of the file | CRC-16 of the file |
| Offset 23 | system byte | system byte |
| Total header length | `T` (offset 0) | 32-bit value at offset 24 |
| First extended-header size | 16 bits at offset 24 | 32 bits at offset 28 |
| Extended headers start at | offset 26 | offset 32 |

These levels have no name in the base header; the name is in extended headers 0x01 and 0x02.

### 4.4 Where the data starts at levels 2 and 3

The data starts at the entry start plus the larger of two values: the stored total header length, and the end of
the extended-header chain. The two agree for most writers; some OS-9 writers leave one spare byte after the chain,
and some OS-9/68k writers store a total that is two bytes short.

## 5. Checksums

### 5.1 The file's CRC-16

The checksum of each file is a CRC-16 over the original (uncompressed) bytes: the reflected polynomial 0xA001,
initial value 0, no final XOR. A reader checks it after decoding. LHA stores no CRC-32.

### 5.2 The header CRC (extended header 0x00)

The same CRC-16, computed over the bytes from the entry start to the data start (§4.4), with the two bytes of
the stored CRC itself counted as zero.

## 6. Reader policies — choices, not format facts

### 6.1 Made by FultaArc's decoder (lhadec)

- **LHARK.** An entry with method `-lh7-` and system byte 0x20 is decoded as the LHARK variant `-lk7-`
  (`algorithm.md` §4); only that reading gives a correct CRC on the archives that have it.
- **Directories.** `-lhd-` is a directory entry. A level-0 `-lh0-` entry with an empty name and length 0 is also
  treated as a directory.
- **Names** are returned as stored; their case is never changed.
- **Unknown methods.** An entry whose method string is not one of those in `algorithm.md` is listed and reported
  as unsupported.
- **Not supported:** multi-volume archives and encryption. No sample or tool available showed either, so neither
  is implemented or claimed.
- **Self-extractors.** In a file that does not start with an entry, the first 1 MiB is searched for the first
  offset where an entry header parses: a method string of the form `-l??-` or `-pm?-`, level 0 to 3, and for
  levels 0 and 1 a correct header sum. After the end-of-archive byte, the search continues if a valid entry
  starts within 16 bytes, because one Amiga self-extractor keeps a small archive in its program before the real
  one.

### 6.2 Made by FultaArc's reader on top of it

- **Detection.** A file is taken as LHA when an entry header is plausible at offset 0 (method string, level 0 to
  3, header sum for levels 0 and 1 when the header fits) and the first entry then parses. A stray `-lh5-` pattern
  is not enough.
- **Self-extractors** are searched only behind a program stub of a kind seen in real self-extracting LHA files:
  DOS/Windows `MZ`; a DOS `.com` starting with a jump (0xE9 or 0xEB); an Amiga hunk file (0x000003F3); an Atari TOS
  program (0x601A); a Sharp X68000 `.x` file (`HU`); and PMarc 2's MSX-DOS self-extractor, which starts with a
  header-shaped `-pms-` record. LHA is the last format FultaArc tries.
- **Names** are decoded as UTF-8 when they are valid UTF-8, otherwise in the caller's code page (CP932 when the
  caller leaves it automatic, because LHA archives are mostly Japanese). Before that, a double-byte character
  that the decoder split at a 0x5C second byte (§3.3) is rejoined, in CP932, CP936 and CP950.
- **Times:** the Windows time (extended header 0x41) when present, else the Unix time, else the DOS time.
- **Symbolic links** are listed as links; their data is the link target.
- **A failed CRC** is reported as a checksum error; with the caller's "keep damaged" option the decoded bytes are
  still delivered and the entry is marked damaged.
- FultaArc reads LHA only; it never writes it.
