[한국어](README-ko.md) | **English** — **FultaArc v0.1.4** — [RAR15-Enc3(en)](https://github.com/rubidus-api/fultaarc_c_lib/releases/download/v0.1.4/fultaarc-cleanroom-rar15-enc3-release-en.zip) · [RAR15-Enc3(ko)](https://github.com/rubidus-api/fultaarc_c_lib/releases/download/v0.1.4/fultaarc-cleanroom-rar15-enc3-release-ko.zip)

# FultaArc

> A cross-platform MIT C23 archive library with complete clean-room decoder provenance.

[Manual (EN)](docs/manual/README.md) · [매뉴얼 (KO)](docs/manual/README-ko.md) · [Clean-Room Provenance](docs/provenance/README.md) · [RAR 1.5 Encryption Study](docs/provenance/rar15/enc3/README.md) · [Changelog](CHANGELOG.md) · [Third-Party Notices](THIRD_PARTY_NOTICES.md) · [License (MIT)](LICENSE)

**FultaArc** is a pure C23 archive extraction, creation, and editing library built on [`proven_c_lib`](vendor/proven/VENDORED.md). It unpacks **18 archive and compressed-stream formats** — including solid, multi-volume split, SFX, and encrypted archives — and creates, edits, splits, and AES-encrypts **ZIP** and **7z** archives.

Every decoder that could not be adopted directly from an open standard or permissive SDK was built through strict **specification-based** or **black-box** clean rooms, and FultaArc publishes the resulting specifications, step-by-step derivation records, verification evidence, and SHA-256 provenance anchors alongside the MIT-licensed source code.

---

## Features & Engineering Characteristics

- **Broad Read Support (18 Formats)**:
  - **Containers**: ZIP, 7z, RAR (1.4 through 7.0), ALZ, EGG, LHA/LZH (including LArc `.lzs` and PMarc `.pma`), CAB, TAR, cpio, Unix `ar`, ISO 9660 (Rock Ridge, Joliet, zisofs), and UDF.
  - **Streams & Wrappers**: `gzip`, `bzip2`, `xz`, `zstd`, `lzip`, Unix `compress` (`.Z`), plus automatic `.tar.{gz,bz2,xz,zst,lz,Z}` unwrapping.
  - **Encrypted, Solid & Multi-Volume Archives**: WinZip AES-128/192/256, PKWARE Strong Encryption (AES), 7zAES-256 (with header encryption `-mhe`), ZipCrypto, RAR 1.4 cipher, RAR 1.5 XOR stream cipher, RAR 3 AES-128, RAR 5 AES-256, and LEA-128/256 CTR (EGG).
- **ZIP & 7z Creation, Split Volumes & In-Place Editing**:
  - **Create** ZIP (`Store`, `Deflate`, `BZip2`, `LZMA`, `PPMd var.I`, `Zstandard`; Zip64) and 7z (`Copy`, `LZMA`, `LZMA2`, `Deflate`, `BZip2`, `PPMd var.H`) archives with optional 7z content filters (`Delta`, `BCJ`, `BCJ2`, `ARM`, `ARM64`, `ARMT`, `PPC`, `SPARC`, `IA64`, `RISC-V`) and solid grouping control (`-ms=on` / `-ms=off`).
  - **Encrypt** new archives with WinZip AES-128/192/256 or 7zAES-256 (`encrypt_names` supported; legacy ZipCrypto intentionally omitted on write).
  - **Split** output into `.z01`..`.zip` (PKWARE split ZIP) or `.7z.001`.. volumes via disk helpers or custom volume sinks.
  - **Edit** (`fulta_arc_edit_*`): copies untouched entries byte-for-byte without re-encoding for editable archives (`FULTA_ARC_EDIT_OK`), or streams a clean format/option conversion otherwise; encrypted source archives are explicitly refused (`FULTA_ARC_EDIT_ENCRYPTED`).
- **Streamed, Zero-Hidden-I/O Architecture**:
  - Core APIs (`fulta_arc_open`, `fulta_arc_extract`, `fulta_arc_write`) operate exclusively over caller-provided random-access `fulta_arc_source_t` and `fulta_arc_sink_t` / `fulta_arc_write_sink_t` interfaces. Only `*_file` helpers open local files.
- **Memory & Hostile-Input Safety**:
  - **Zero direct libc allocations in `src/`**: every internal allocation routes through `proven_allocator_t` (enforced by [`tests/tools/no-libc-alloc.sh`](tests/tools/no-libc-alloc.sh)), with full support for custom caller allocators via `options.allocator` / `write_options.allocator`.
  - **Resource caps**: `max_dictionary` (default 1 GiB) and `max_memory` bound archive-driven codec windows and buffers before allocation (`FULTA_ARC_ERR_LIMIT`).
  - **Path & control-character sanitization**: entry names are normalized to UTF-8 (across CP437, CP949, CP932, CP936, CP950, CP1252, CP866, CP1251, and KOI8-R), stripped of leading slashes, Windows drive prefixes, `..` components (including Windows dot/space forms), and ASCII control bytes (`0x01–0x1F`, `0x7F` -> `_`), while preserving raw stored bytes in `name_raw`.
  - **Continuously verified**: compiled with `-std=c23 -Wall -Wextra -pedantic -Werror` and `-Og -Werror`, verified under ASan + UBSan (`make test`), ThreadSanitizer + libFuzzer (`make deep`), CBMC bounded model checking (`make prove`), clean-room SHA-256 pin enforcement (`tests/tools/rar15-pin.sh`), and cross-compiled to Windows x64 UCRT (`make win64`).

---

## Building & Using in C23 Code

Requires a C23-capable GCC or Clang, GNU Make, and standard POSIX tools (`mingw-w64` optional for `win64`).

```sh
make            # builds build/host/libfultaarc.a (-std=c23 -Wall -Wextra -pedantic -Werror)
make test       # runs -Og -Werror check, clean-room pins, no-libc-alloc check, and all ASan+UBSan tests
make win64      # optional: cross-compiles build/win64/libfultaarc.a and .exe tests
make SAN="" OPT="-O2 -DNDEBUG"   # non-sanitizer static library for production linking
```

Link your application against `build/host/libfultaarc.a` with `-Iinclude -lm -lpthread` on Linux (or `-lbcrypt` on Windows). For the complete API reference, see [docs/manual/README.md](docs/manual/README.md).

### 1. Opening & Stream-Extracting an Encrypted / Resource-Bounded Archive

```c
#include "fulta/arc.h"
#include <stdio.h>

static fulta_arc_err_t provide_password(void *ctx, uint32_t attempt, const char **utf8_out) {
    if (attempt > 0) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *utf8_out = (const char *)ctx;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t write_to_file(void *ctx, const void *data, size_t n) {
    FILE *fp = (FILE *)ctx;
    return (fwrite(data, 1, n, fp) == n) ? FULTA_ARC_OK : FULTA_ARC_ERR_IO;
}

int main(void) {
    fulta_arc_options_t opts = {
        .password       = provide_password,
        .password_ctx   = "secret-password",
        .max_dictionary = 128u * 1024u * 1024u, /* 128 MiB dictionary cap */
        .max_memory     = 256u * 1024u * 1024u, /* 256 MiB total codec cap */
    };

    fulta_arc_t *arc = NULL;
    fulta_arc_err_t err = fulta_arc_open_file("bundle.rar", &opts, &arc);
    if (err != FULTA_ARC_OK) {
        fprintf(stderr, "open failed: %s\n", fulta_arc_strerror(err));
        return 1;
    }

    printf("Format: %s (%zu entries)\n",
           fulta_arc_format_name(fulta_arc_format(arc)), fulta_arc_count(arc));

    for (size_t i = 0; i < fulta_arc_count(arc); i++) {
        const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
        printf("  [%zu] %s (%llu bytes, %s)\n",
               i, en->name, (unsigned long long)en->size, en->method);
        if (en->flags & FULTA_ARC_ENTRY_DIR) continue;

        FILE *out = fopen("extracted.bin", "wb");
        if (!out) { fulta_arc_close(arc); return 1; }
        fulta_arc_sink_t sink = { .ctx = out, .write = write_to_file };
        err = fulta_arc_extract(arc, i, &sink);
        fclose(out);
        if (err != FULTA_ARC_OK) {
            fprintf(stderr, "extract[%zu] failed: %s\n", i, fulta_arc_strerror(err));
            fulta_arc_close(arc);
            return 1;
        }
    }

    fulta_arc_close(arc);
    return 0;
}
```

### 2. Writing an Encrypted, Header-Protected 7z (or ZIP) Archive

```c
#include "fulta/arc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *msg = "FultaArc clean-room C23 archive library\n";
    fulta_arc_write_entry_t entries[] = {
        { .name = "docs/", .is_dir = 1 },
        { .name = "docs/hello.txt", .data = msg, .size = strlen(msg), .mtime = 1764000000 },
    };
    fulta_arc_write_options_t wopts = {
        .level         = 7,
        .method        = FULTA_ARC_METHOD_LZMA2,
        .filter        = FULTA_ARC_FILTER_NONE,
        .solid         = FULTA_ARC_SOLID_ON,
        .password      = "correct-horse-battery",
        .encryption    = FULTA_ARC_ENCRYPT_7Z_AES256,
        .encrypt_names = 1, /* 7z -mhe: encrypt entry names and metadata */
    };

    fulta_arc_err_t err = fulta_arc_write_file("out.7z", FULTA_ARC_FORMAT_7Z, entries, 2, &wopts);
    if (err != FULTA_ARC_OK) {
        fprintf(stderr, "write failed: %s\n", fulta_arc_strerror(err));
        return 1;
    }
    return 0;
}
```

### 3. Editing an Existing ZIP / 7z Archive (Zero-Reencode Copy or Conversion)

```c
#include "fulta/arc.h"
#include <stdio.h>
#include <string.h>

typedef struct { const uint8_t *buf; size_t len; size_t pos; } mem_reader_t;

static fulta_arc_err_t read_chunk(void *ctx, void *out, size_t cap, size_t *got) {
    mem_reader_t *r = (mem_reader_t *)ctx;
    size_t rem = r->len - r->pos;
    size_t n = rem < cap ? rem : cap;
    if (n > 0) { memcpy(out, r->buf + r->pos, n); r->pos += n; }
    *got = n;
    return FULTA_ARC_OK;
}

int main(void) {
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_file("input.zip", NULL, &arc) != FULTA_ARC_OK) return 1;

    if (fulta_arc_editable(arc) == FULTA_ARC_EDIT_ENCRYPTED) {
        fulta_arc_close(arc);
        return 1; /* encrypted archives are intentionally refused for editing */
    }

    fulta_arc_edit_t *edit = NULL;
    if (fulta_arc_edit_begin(arc, &edit) != FULTA_ARC_OK) {
        fulta_arc_close(arc);
        return 1;
    }

    if (fulta_arc_count(arc) > 0) fulta_arc_edit_remove(edit, 0);

    const uint8_t note[] = "updated entry\n";
    mem_reader_t src = { .buf = note, .len = sizeof(note) - 1, .pos = 0 };
    fulta_arc_reader_t reader = { .ctx = &src, .read = read_chunk, .size = src.len };
    fulta_arc_edit_add(edit, "notes/readme.txt", &reader);

    /* Untouched entries are copied without re-encoding when target format matches source */
    fulta_arc_err_t err = fulta_arc_edit_write_file(edit, FULTA_ARC_FORMAT_ZIP, "edited.zip", NULL);
    fulta_arc_edit_free(edit);
    fulta_arc_close(arc);
    return (err == FULTA_ARC_OK) ? 0 : 1;
}
```

---

## Supported Formats & How Support Was Obtained

Every format in FultaArc falls into one of three transparent acquisition categories — **Open Standards & Permissive SDKs**, **Specification-Based Clean Rooms**, or **Black-Box Clean Rooms**:

| Format | Capabilities | Compression / Filters / Encryption | Acquisition Path & Provenance |
|---|---|---|---|
| **ZIP** | Read, Write, Edit, Split | Store, Deflate(64), BZip2, LZMA, xz, PPMd var.I, Zstandard, Shrink, Reduce, Implode; ZipCrypto, WinZip AES-128/192/256, PKWARE Strong AES; Zip64, `.z01`..`.zip` | PKWARE APPNOTE + RFC 1951 + RFC 8878 + black-box PKZIP 0.90–1.10 checks + permissive SDKs (`miniz`, `bzip2`, `LZMA SDK`, `zstd`) |
| **7z** | Read, Write, Edit, Split | Copy, LZMA, LZMA2, Deflate, BZip2, PPMd var.H, Zstandard; BCJ, BCJ2, Delta, ARM/ARM64/ARMT, PPC, SPARC, IA64, RISC-V; 7zAES-256 (`-mhe`), solid, `.7z.001`.. | Public-domain LZMA SDK / 7z format specifications + permissive SDKs (`LZMA SDK`, `miniz`, `bzip2`, `zstd`) |
| **RAR 2.0 – 7.0** | Read | RAR 4 & RAR 5 containers, RAR 2 LZ/Audio, RAR 3 LZ/PPMd var.H/filters, RAR 5/7 LZ; RAR 3 AES-128, RAR 5 AES-256, BLAKE2sp; solid, split, SFX | **Two-team specification-based clean room** (`rar-spec` -> `rar-decoder`; zero UnRAR code) — [`docs/provenance/rar/`](docs/provenance/rar/README.md) |
| **RAR 1.4 / 1.5** | Read | Store (`0x30`) & LZSS/Huffman (`0x31..0x35`); RAR 1.4 cipher, RAR 1.5 XOR stream cipher; solid, split (`.rar`/`.r00`), SFX | **Black-box clean rooms** (`fultaarc-cleanroom-rar15` + `fultaarc-cleanroom-rar15-enc3`) — [`rar15/`](docs/provenance/rar15/README.md) · [`enc3/`](docs/provenance/rar15/enc3/README.md) |
| **ALZ** | Read | Store, raw Deflate, ALZ-bzip2 (`DLZ\x01`); ZipCrypto; `.alz`/`.a00`.. split volumes | **Black-box clean room** (`fultaarc-cleanroom-alzegg` from zero; zero `unalz` code) + `bzip2 1.0.8` — [`docs/provenance/alz/`](docs/provenance/alz/README.md) |
| **EGG** | Read | Store, Deflate, bzip2, AZO, LZMA; ZipCrypto, WinZip AES, LEA-128/256; solid, `.volN.egg` split, SFX | **Two independent firewalled black-box clean rooms** (`egg-container` + `egg-verify` + `alzegg`) — [`docs/provenance/egg/`](docs/provenance/egg/README.md) |
| **AZO (EGG method 3)** | Read (Decode-only) | `31 <flag>` + x86 `E8`/`E9` transform, 32-bit binary arithmetic coder, dual local/group trees, 128-entry MTF history | **3-round black-box clean room** (`azo-blackbox` + `alzegg`; decode-only respecting KR101049699B1) — [`docs/provenance/azo/`](docs/provenance/azo/README.md) |
| **LHA / LZH** | Read | `-lh0..7-`, `-lhx-`, `-lhd-`, `-lk7-`, `-lz4/5-`, `-lzs-`, `-pm0/1/2-`; LArc, PMarc, SFX | **Specification + black-box clean room** (`ar002` public domain + `lhasa`/`UNLHA32`/`PMarc` oracles) — [`docs/provenance/lha/`](docs/provenance/lha/README.md) |
| **CAB** | Read | Store, MSZIP, LZX, **Quantum**; multi-cabinet sets | `[MS-CAB]` / `[MS-PATCH]` open specs + **Black-box clean room** for Quantum — [`docs/provenance/quantum/`](docs/provenance/quantum/README.md) |
| **TAR / cpio / ar** | Read | ustar, POSIX pax, GNU tar; `newc`/`crc`/`odc`/old-binary cpio; Unix `ar` (`.a`/`.deb`) | POSIX.1, BSD `tar(5)`/`cpio(5)`/`ar(5)` manuals |
| **ISO 9660 & UDF** | Read | ISO 9660 (Rock Ridge, Joliet, zisofs), UDF (ECMA-167 / OSTA) | ECMA-119, Microsoft Joliet, IEEE P1282/P1281, ECMA-167 |
| **Stream filters** | Read | `gzip`, `bzip2`, `xz`, `zstd`, `lzip`, `compress` (`.Z`) | RFC 1952, `bzip2` docs, `.xz`/`.lz` public specs, RFC 8878, expired-patent LZW description |

- FultaArc is a library, not a CLI archiver; writing and editing are intentionally scoped to ZIP and 7z (never RAR, ALZ, EGG, or AZO).
- Arbitrary RAR 3 VM bytecode programs are never executed (`FULTA_ARC_ERR_UNSUPPORTED`); standard predefined filters are supported.

---

## How RAR, ALZ, EGG, and AZO Decoding Was Obtained (With Evidence)

All clean-room provenance records, format specifications, derivation logs, and SHA-256 bundle anchors live under [`docs/provenance/`](docs/provenance/README.md):

1. **RAR 2.0 – 7.0 ([`docs/provenance/rar/`](docs/provenance/rar/README.md))** — *Two-team specification-based clean room*:
   - Team 1 (`rar-spec`) authored `rar-decompression.md` exclusively from BSD-2 readers (**`libarchive` 3.7.7** and **`nwaples/rardecode`**), RARLAB's published RAR 5.0 technical note, and public cryptographic specs (never UnRAR).
   - Team 2 (`rar-decoder`) worked in an isolated repository with zero access to UnRAR, `libarchive`, `rardecode`, or 7-Zip, implementing FultaArc's MIT C decoder from the specification, FIPS/RFC/BLAKE2 standards, and public-domain LZMA SDK `Ppmd7*` alone. Verified against three black-box oracle manifests (47/47, 48/48, 47/47) plus continuous FultaArc fuzz/sanitizer regression.
2. **RAR 1.4 / 1.5 & RAR 1.5 Encryption ([`rar15/`](docs/provenance/rar15/README.md) · [`rar15/enc3/`](docs/provenance/rar15/enc3/README.md))** — *Black-box + SMT clean rooms*:
   - Container & stream unpacker (`src/rar/rar15/`): derived from black-box outputs of RAR 1.55 / 1.40.2 beta for DOS, byte-identical pinned (`tests/tools/rar15-pin.sh`).
   - **RAR 1.5 password stream cipher** (`src/rar/rar15_enc/rar15_cipher.c`): independently derived in `fultaarc-cleanroom-rar15-enc3` (`2c3c4a4`), passing 6/6 Universal Decryption Gate runs (600 passwords, 1,800 files) and directly cross-verified against genuine `RAR.EXE` 1.55 (solid, 5-volume split >64 KiB wrap, rewind, subdirectories, mixed plain/encrypted, 80-char password cutoff).
   - Public deliverables in [`docs/provenance/rar15/enc3/`](docs/provenance/rar15/enc3/README.md) include EN/KO formal research reports, specifications, interactive offline HTML step-by-step guides, and full reproducible release zips (`fultaarc-cleanroom-rar15-enc3-release-en.zip` / `-ko.zip`) attached as GitHub Release assets.
3. **ALZ ([`docs/provenance/alz/`](docs/provenance/alz/README.md))** — *Black-box clean room (`fultaarc-cleanroom-alzegg`)*:
   - Earlier exploratory work read zlib-licensed `unalz 0.65`, so FultaArc replaced `src/alz/alz.c` completely with a parser worked out from scratch in `fultaarc-cleanroom-alzegg` (`1f844a6`) from ALZip 4.9–12.37 outputs alone — including the exact 178-bit-reduced `DLZ\x01`/`DLZ\x02` ALZ-bzip2 framing.
   - Independent differential verification against `unalz 0.65` proved exact agreement on all 72 Store/Deflate archives, fixed missing empty folders, and showed FultaArc correctly decodes ALZ method-1 bzip2 archives where `unalz 0.65` itself fails.
4. **EGG ([`docs/provenance/egg/`](docs/provenance/egg/README.md))** — *Two independent, mutually firewalled black-box clean rooms*:
   - `egg-container` (145 ALZip archives) and a strictly firewalled second room `egg-verify` (351 archives, 21-step log) independently derived every signature, sub-record, solid/split layout, ZipCrypto, WinZip AES (16-byte LE counter from 1), LEA-128/256 (16-byte BE counter from 0, KISA KS X 3246), and CP949 password rule from ALZip 8.6/8.12/12.37 outputs.
   - A self-disclosed unused `Egg.dll` strings encounter in `egg-container` is documented transparently in [`docs/provenance/egg/provenance.md`](docs/provenance/egg/provenance.md) and was completely neutralized by `egg-verify`'s exposure-free re-derivation and `alzegg` fixture verification (601 + 218 archives passing).
5. **AZO ([`docs/provenance/azo/`](docs/provenance/azo/README.md))** — *Three-round black-box clean room (decode-only)*:
   - Respects ESTsoft's Korean patent **KR101049699B1** (in force until 2029-07-17): FultaArc implements decoding only and contains no AZO encoder.
   - Derived across `azo-blackbox` (22 commits, E40–E77, 244/249 code-value bisections, frozen holdout E76–E77 6/6, 18,809/18,809 retained pairs) + `alzegg` corrections (`31 <flag>` x86 `E8`/`E9` undo transform, exact `2^20`/`2^22` dual-tree score rescaling), transparently disclosing the two permitted public/public-domain background hypotheses (`namu.wiki/w/AZO` note and LZMA SDK specification) tested strictly against ALZip outputs.

---

## Why FultaArc Can Legitimately Be Published Under the MIT License

> **Note:** The following summarizes FultaArc's verifiable engineering and licensing record (not formal legal advice):

1. **All FultaArc-written source code (`include/`, `src/`, `tests/`) is original MIT code**: zero GPL, LGPL, AGPL, proprietary source, or UnRAR-restricted code enters the repository. Even clean-room test harnesses run DOSBox or shareware/freeware binaries strictly as external observation instruments — nothing from those tools is linked, copied, or redistributed.
2. **Every vendored dependency is permissive and MIT-compatible** (see [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)):
   - `proven_c_lib` v0.119.3 — **MIT**
   - `LZMA SDK` 26.05 — **Public Domain**
   - `miniz` 3.1.2 — **MIT**
   - `bzip2` 1.0.8 (+ `vendor/bzip2_alz/`) — **bzip2 license** (permissive BSD-style)
   - `Zstandard` 1.5.7 decoder (`vendor/zstd/`) — **BSD-3-Clause** (explicitly electing the BSD branch of upstream's dual BSD/GPLv2 license)
   - Unicode code-page mapping data (`src/text/cp_tables.c`) — **Unicode License v3**
3. **Zero forbidden third-party decoder codebases**: FultaArc contains no code from UnRAR (avoiding UnRAR's non-free redistribution/anti-reconstruction terms), `7-Zip` RAR/LZH/Quantum code, `libmspack`/`cabextract`, `unalz`, `UnEGG`, ESTsoft's `EGG_Specification.pdf`, `EggDotNet`, or GNU tar.
4. **Independent clean-room isolation & SHA-256 verification**: every non-standard format reader carries reproducible git-bundle SHA-256 anchors, source-pin checks (`tests/tools/rar15-pin.sh`), and honest disclosures of any caveats.
5. **Decode-only respect for active patents**: AZO support is strictly decode-only (`KR101049699B1`), and Unix `compress` LZW relies on expired patents.

---

## Repository & License

- **GitHub Repository**: <https://github.com/rubidus-api/fultaarc_c_lib>
- **License**: [MIT](LICENSE)
