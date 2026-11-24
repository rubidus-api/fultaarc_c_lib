# ALZ — format specification

ALZ is ESTsoft's older archive format (`.alz`, split volumes `.a00`, `.a01`, ...), written by ALZip by default up to 7.x
and on demand in later versions. ESTsoft has published no specification. All multi-byte integers are little-endian unless
explicitly stated otherwise.

---

## 1. Record stream layout

An ALZ archive is a sequence of records prefixed by 4-byte signatures:

| Signature (bytes) | Little-endian `u32` | Meaning |
|---|---|---|
| `41 4C 5A 01` (`"ALZ"\x01`) | `0x015A4C41` | Archive header (start of file) |
| `42 4C 5A 01` (`"BLZ"\x01`) | `0x015A4C42` | Local file/directory header, immediately followed by payload |
| `43 4C 5A 01` (`"CLZ"\x01`) | `0x015A4C43` | Central directory sentinel (carries no file listing in ALZip 4.9–12.37) |
| `43 4C 5A 02` (`"CLZ"\x02`) | `0x025A4C43` | End of archive |
| `43 4C 5A 03` (`"CLZ"\x03`) | `0x035A4C43` | End of split volume tail |

---

## 2. Archive header

After `"ALZ"\x01`, 4 header bytes follow (`0A 00` plus two bytes, 8 bytes total). On split continuation volumes
(`.a00`, `.a01`, ...), the last two bytes encode the 16-bit volume number (`1` for `.a00`, incrementing per volume).

---

## 3. Local file header (`"BLZ"\x01`)

Fixed 9 bytes after `"BLZ"\x01`:

| Offset | Size | Field |
|---|---|---|
| `0` | `2` | `name_len` |
| `2` | `1` | DOS/ALZ attributes (`0x01` read-only, `0x02` hidden, `0x04` system, `0x10` folder, `0x20` file) |
| `3` | `4` | MS-DOS modification date/time (local wall-clock time; high 16 bits date, low 16 bits time) |
| `7` | `1` | Descriptor byte |
| `8` | `1` | Reserved (`0` in all observed archives) |

Descriptor byte:
- `0x01`: encrypted with ZipCrypto
- `0x08`: data descriptor mode (changes the ZipCrypto check byte rule, never observed set in ALZip 4.9–12.37)
- `0xF0`: byte width `w = descriptor >> 4` of the compressed and uncompressed size fields (`0`, `1`, `2`, `4`, or `8`)

When `w > 0`, these fields follow before the filename:

| Size | Field |
|---|---|
| `1` | Compression method: `0` Store, `1` ALZ-bzip2, `2` raw Deflate (`3` observed decoded as Deflate by ALZip) |
| `1` | Reserved (`0` in observed archives) |
| `4` | Uncompressed CRC-32 (ISO 3309 / zlib) |
| `w` | `packed_size` `c` |
| `w` | `unpacked_size` `u` |

Entries with `w = 0` (folders and 0-byte files) have no method, CRC-32, sizes, or encryption header. Next follow:
- `name_len` bytes: path in the creator's ANSI/OEM code page (commonly CP949; no UTF-8 flag exists in ALZ), `/` or `\`.
- `12` bytes (only when encrypted): ZipCrypto encryption header.
- `c` bytes: payload (`c` does **not** include the 12-byte encryption header).

---

## 4. Central end & split volumes

- `"CLZ"\x01` is followed by 8 bytes (ALZip 4.9–12.37; an older fixture tool wrote 12 bytes) and holds no directory list;
  file enumeration comes strictly from walking `"BLZ"\x01` records.
- `"CLZ"\x02` marks end of archive with no trailing payload.
- **Split volumes**: `.alz` (volume 0), `.a00` (volume 1), ..., `.a99` (volume 100), `.b00` (volume 101), etc.
  - Every volume after `.alz` begins with an 8-byte volume header (`"ALZ"\x01`, `0A 00`, u16 volume number).
  - Every volume before the last ends with a 16-byte tail (`"CLZ"\x01`, 8 bytes, `"CLZ"\x03`).
  - Stripping intermediate volume tails and continuation volume headers yields one continuous byte stream. Records and
    compressed payloads split arbitrarily across volume boundaries.

---

## 5. ALZ-bzip2 (`method == 1`, `"DLZ"\x01` framing)

ALZ method 1 uses standard bzip2 block entropy coding (`vendor/bzip2_alz/`) with a compact bitstream framing that saves
178 bits per single-block stream relative to standard `BZh`:

- No `BZh1`..`BZh9` 4-byte prefix; block size is always `9` (`900,000` bytes).
- Block header magic in the MSB-first bitstream is **32 bits**: `0x444C5A01` (`"DLZ"\x01`) instead of 48-bit
  `0x314159265359`.
- End-of-stream magic is **32 bits**: `0x444C5A02` (`"DLZ"\x02`) instead of 48-bit `0x177245385090`.
- Per-block CRC-32, `blockRandomised` 1-bit flag, and combined stream CRC-32 are omitted from the bitstream. Integrity is
  verified solely by the local file header's CRC-32.

---

## 6. Encryption

Standard PKWARE **ZipCrypto**: 3x32-bit key state initialized from password bytes (ALZip writes Hangul passwords as CP949;
a UTF-8 reader tries CP949 and UTF-8), decrypting the 12-byte header and then `c` data bytes. Check byte (`header[11]`)
matches `(mtime >> 8) & 0xFF` if descriptor bit `0x08` is set, else `(crc32 >> 24) & 0xFF`.
