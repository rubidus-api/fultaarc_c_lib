# EGG — derivation & dual-room verification

FultaArc's EGG reader is built on **two independent, mutually firewalled black-box clean rooms** (`egg-container` and
`egg-verify`) followed by a third cross-verification clean room (`fultaarc-cleanroom-alzegg`), observing only archives
created by ALZip 8.6 Korean, 8.12 English, and 12.37.

---

## 1. Two independent clean rooms

1. **`egg-container` (9 commits, 145 ALZip archives):** Mapped all signatures, sub-records, ZipCrypto/AES/LEA headers,
   split volume chaining, solid headers, and initial method mappings from differential archives created by ALZip 12.37,
   8.12, and 8.6.
2. **`egg-verify` (14 commits / `02ef44f`, 21-step log, 351 ALZip archives):** Firewalled completely from `egg-container` —
   its brief prohibited reading *anything* produced by `egg-container` (`egg-format.md`, fixtures, code, notes) and
   explicitly forbade unpacking, running `strings`, or inspecting any ESTsoft binary.

Key derivation milestones from `egg-verify`'s 21-step log:
- **Step 2:** Six archives wrapping a 13-byte `Hello, EGG!\r\n` with `-m0..-m4` isolated `22 82 E2 08` (`END`), `EGGA`,
  `E3 90 85 0A` (file header with `u64` size), `AC 91 85 0A` (name), a 64-bit `FILETIME`, and `13 0C B5 02` (block header
  varying only method/level/`packed_size`/data across flags).
- **Steps 3–5:** Hangul filenames established UTF-8 path encoding; empty directory inputs established attribute `0x80`;
  encrypted probes isolated `0x08D1470F` inside the file header (leaving block length unchanged for encrypted `13`-byte
  store data); `-s` and `-v` isolated solid grouping and `prev_vol_id`/`next_vol_id` volume chaining.
- **Step 10:** Raw Deflate (zlib `-15`), `bz2`, and raw LZMA (`04 41 05 00 5D 00 00 00 01` without EOS) decoded payloads.
- **Steps 13–14:** Tested PBKDF2-HMAC-SHA1 verifiers and MACs against sample ciphertexts, and decided counter endianness
  & initial counter value on a 13,953-byte file across all four combinations — proving **AES uses 16-byte LE starting at 1**
  and **LEA uses 16-byte BE starting at 0** (with LEA itself implemented from KISA's KS X 3246 / ISO 29192-2 datasheet).
- **Step 16 & 20–21:** Identified method 3 framing (`31 00` BE chunked stream), local wall-clock whole-second FILETIME,
  and multi-block `0xC0000000` splitting on a 4.8 GB `-m1` file.

---

## 2. Settling differences between the two specifications

When `egg-container` and `egg-verify` were compared side by side against the sample bytes, every structural field matched
and the minor differences were resolved empirically:

| Point | `egg-container` | `egg-verify` | Settled from bytes |
|---|---|---|---|
| Signatures, headers, sub-records, AES/LEA counters, solid & split layouts | Match | Match | **Agreement** across every field |
| Encryption sub-record on 0-byte files | **Absent** | Claimed present (prose note) | **`egg-container` correct** (`pw_tp0_empty.egg` bytes `@18..69` have no encryption sub-record on `empty.txt`) |
| Modification timestamp | Assumed UTC FILETIME | **Measured local wall-clock seconds** | **`egg-verify` correct** (controlled UTC input `time_*.egg` in KST) |
| Multi-block files (>3 GB) | Unobserved (refused) | **Observed (4.8 GB file -> 2 Deflate blocks)** | **`egg-verify` correct** |
| File attributes | `0x80` directory only | **`0x01`, `0x02`, `0x80`; System files skipped** | **`egg-verify` correct** |
| ALZip 8.6 encryption length | `data_len + 7` | `data_len + 7` | **Agreement** (accept either `len` or `len - 7` when valid) |

`egg-verify`'s independent parser decoded all 133 files in `egg-container`'s sample set with matching CRCs, and
cross-checking across all 430 archives (785 files) passed with 0 failures.

---

## 3. Completion in `fultaarc-cleanroom-alzegg` (`1f844a6`)

Auditing FultaArc's EGG reader against `fultaarc-cleanroom-alzegg`'s 253 ALZip fixtures caught and resolved four real-world
compatibility edge cases:
1. **Archive description records**: accepted and skipped cleanly (6 fixtures).
2. **CP949 passwords**: trying CP949 before UTF-8 fixes Hangul passwords created on Korean Windows (3 fixtures).
3. **System attribute `0x04`** and ZIP-like attribute mapping on directory/file entries.
4. **Self-extracting EGG (`NAME.vol1.exe`)**: locating `EGGA` headers cleanly after an `MZ` stub (1 fixture).

FultaArc's regression manifest currently runs **601 `egg.txt` + 218 `alzegg.txt` archives** under ASan + UBSan with 0 errors.
