# EGG — format specification

EGG is ESTsoft's newer archive format (`.egg`, split volumes `NAME.vol1.egg`, `NAME.vol2.egg`, ...), introduced with
ALZip 7.x. All integers are little-endian unless marked otherwise. Unobserved field values or flags are rejected safely
rather than guessed.

---

## 1. Record layout & signatures

```text
normal = archive_header { file_header { block_header data } } END
solid  = archive_header(SOLID) { file_header } block_header data END
```

Every major structure starts with a 4-byte signature, holds fixed fields plus zero or more optional sub-records, and is
terminated by `END` (`0x08E28222`).

| Signature (`u32` LE) | Bytes | Record |
|---|---|---|
| `0x41474745` | `45 47 47 41` (`"EGGA"`) | Archive header |
| `0x08E28222` | `22 82 E2 08` | `END` terminator |
| `0x0A8590E3` | `E3 90 85 0A` | File header |
| `0x0A8591AC` | `AC 91 85 0A` | UTF-8 path sub-record |
| `0x2C86950B` | `0B 95 86 2C` | Modification time & attributes sub-record |
| `0x08D1470F` | `0F 47 D1 08` | Encryption sub-record |
| `0x02B50C13` | `13 0C B5 02` | Compressed data block header |
| `0x24E5A060` | `60 A0 E5 24` | Solid sub-record (archive header) |
| `0x24F5A262` | `62 A2 F5 24` | Split-volume sub-record (archive header) |

Empty files (`size == 0`) and directories (`attr & 0x80`) have **no blocks** and **no encryption sub-record**.
Multi-block files concatenate each block's decompressed bytes sequentially (observed on a 4.8 GB `-m1` file split into
`0xC0000000` + `1,610,612,736` byte Deflate blocks). Self-extracting archives are located via an `MZ`-gated header scan
finding a valid `EGGA` archive header inside `.exe` (including `NAME.vol1.exe`).

---

## 2. Sub-records & ALZip 8.6 quirk

Each sub-record carries:
- `u32 sig`
- `u8 flag` (`0` in all observed archives; nonzero triggers rejection)
- `u16 length` (data payload length in bytes)
- `length` payload bytes

**ALZip 8.6 encryption length quirk:** ALZip 8.6 includes the 7-byte sub-record header inside `length` on encryption
sub-records only (storing `24`, `28`, `36` for `17`, `21`, `29` bytes of data). Because encryption payload size is fixed by
its method byte, FultaArc accepts either `length` or `length - 7` when `length - 7` matches the expected cipher payload size
and `END` immediately follows.

---

## 3. Archive & file headers

- **Archive header** (`EGGA`): `u16 version` (`0x0100`), `u32 archive_id` (links split volumes), `u32 reserved` (`0`), then
  optional sub-records (`SOLID`, `SPLIT`), archive description records (accepted and skipped), and `END`.
- **File header** (`0x0A8590E3`): `u32 index`, `u64 unpacked_size`, then sub-records (`Name` in UTF-8 without NUL, `Time/Attr`,
  optional `Encryption`) and `END`.
  - **Time/Attributes** (9 bytes): `u64` Windows `FILETIME` representing **local wall-clock time** in whole seconds
    (`FULTA_ARC_ENTRY_MTIME_LOCAL`), followed by `u8 attributes` (`0x01` read-only, `0x02` hidden, `0x04` system, `0x80`
    directory).

---

## 4. Compression methods (`0x02B50C13` block header)

Fixed block fields before `END`: `u8 method`, `u8 level`, `u32 unpacked_size`, `u32 packed_size`, `u32 crc32`.

| `method` | Codec | Notes |
|---|---|---|
| `0` | Store | Raw uncompressed bytes |
| `1` | Deflate | Raw RFC 1951 Deflate (no zlib header) |
| `2` | bzip2 | Standard `BZh<level>` stream |
| `3` | **AZO** | ESTsoft's method ([`../azo/`](../azo/README.md), decode-only) |
| `4` | LZMA | `u8 major`, `u8 minor` (`04 41`), `u16 props_len` (`05 00`), 5-byte LZMA props + raw LZMA1 without end marker (stop at `unpacked_size`) |

---

## 5. Encryption schemes (`0x08D1470F`)

Passwords typed in ALZip use the creator's ANSI code page (CP949 on Korean systems); FultaArc tries CP949 bytes first and
falls back to UTF-8.

| Method byte | Cipher | Payload layout | Length | Key derivation / CTR / Auth |
|---|---|---|---|---|
| `0` | ZipCrypto | 12-byte header + `u32` whole-file CRC-32 | `17` | Check byte (`header[11]`) against whole-file CRC `>> 24` |
| `1` | AES-128 CTR | salt (8), verifier (2), MAC (10) | `21` | `PBKDF2-HMAC-SHA1(pw, salt, 1000)`; **16-byte LE counter from `1`**; 10-byte HMAC-SHA1 |
| `2` | AES-256 CTR | salt (16), verifier (2), MAC (10) | `29` | Same with 256-bit key; **16-byte LE counter from `1`** |
| `5` | LEA-128 CTR | salt (8), verifier (2), MAC (10) | `21` | Same KDF/MAC; LEA (KISA KS X 3246 / ISO 29192-2); **16-byte BE counter from `0`** |
| `6` | LEA-256 CTR | salt (16), verifier (2), MAC (10) | `29` | Same with 256-bit LEA key; **16-byte BE counter from `0`** |

---

## 6. Split & solid archives

- **Split (`NAME.vol1.egg`, `NAME.vol2.egg`, ...):** Every volume carries a 33-byte archive header containing the `SPLIT`
  sub-record (`u32 prev_vol_id`, `u32 next_vol_id`). Stripping the 33-byte headers from volumes `2..N` produces one
  continuous byte stream.
- **Solid (`0x24E5A060`):** All file headers appear sequentially, followed by **one** shared block whose `unpacked_size`
  and `crc32` cover the concatenation of every entry in file-header order.
