# RAR 2.0 – 7.0 — derivation

This page explains how FultaArc's RAR 2.0–7.0 specification and MIT C decoder were created and verified. **No black-box
reverse engineering of RAR 2–7 codecs was claimed or needed**: permissive BSD-2 implementations and RARLAB's published
RAR 5.0 technical note existed, so FultaArc used a strict **two-team specification-based clean room**.

---

## 1. Two-team clean-room workflow

The separation between specification author and decoder author was enforced across separate git repositories:

1. **Stage 1 — Specification (`rar-spec`, 10 commits)**:
   - Worked in an isolated workspace with **zero UnRAR code**.
   - Authored a self-contained decompression specification (`rar-decompression.md`) by reading three openly published
     and permissively licensed sources:
     - **libarchive 3.7.7** RAR and RAR5 readers (BSD-2-Clause),
     - **nwaples/rardecode** (BSD-2-Clause),
     - **RARLAB's published "RAR 5.0 archive format" technical note**, plus public-domain LZMA SDK PPMd sources and
       standard cryptographic specifications.
   - Cited the exact source for every section, documented every disagreement between readers, settled discrepancies
     against generated black-box test fixtures, and verified header layouts, key derivations, and BLAKE2sp vectors.
   - Wrote **no production decoder**.

2. **Stage 2 — Decoder (`rar-decoder`, 7 implementation commits)**:
   - Worked in a fresh isolated repository with baseline commit `ef1c399` and **zero access** to UnRAR, libarchive,
     rardecode, 7-Zip, SharpCompress, junrar, unarr, or any RAR implementation.
   - Read only the written specification, public cryptographic standards, public-domain PPMd source (`vendor/lzma/Ppmd7*`),
     and binary test manifests/oracles.
   - Every source file was written from scratch in C and records the exact specification sections and standards implemented.

3. **Stage 3 — FultaArc integration**:
   - The MIT decoder commits were exported as clean patches, audited, integrated, and later ported into FultaArc's core
     archive API and `proven_allocator_t` memory architecture (`src/rar/` and `src/rar/codec/`).

---

## 2. Incremental build & verification sequence

In `rar-decoder`, each layer was implemented and validated against the fixture manifests before starting the next:

| Commit | Scope implemented |
|---|---|
| `4aa1262` | RAR 4 & RAR 5 container headers, multi-volume scanning, password callback integration, cryptographic primitives (SHA-1, SHA-256, HMAC, PBKDF2, AES, RAR 3 KDF, BLAKE2s/BLAKE2sp); stored files verified |
| `e20b74b` | Shared bitstream/unpacking window layer and RAR 5.0–7.0 decompression |
| `cef59d9` | RAR 2.9 / 3.x LZ decoder, PPMd var.H integration (`vendor/lzma/Ppmd7*`), and standard RAR 3 byte filters |
| `3d72df5` | RAR 2.0 LZ and audio decompression |
| `bf7a515` | Decoder registered in top-level format dispatch |
| `463b9d6` | Self-extracting (SFX) RAR detection |

Where the specification noted two plausible interpretations (spec §15), the recommended constant was implemented and
verified directly against the fixtures (`questions-rar-decoder.md`). Arbitrary custom RAR 3 VM bytecode programs remain
explicitly unsupported (`FULTA_ARC_ERR_UNSUPPORTED`); only RAR's standard predefined filters (Delta, Audio/RGB/E8/E8E9/Itanium
in RAR 3; Delta/E8/E8E9/ARM in RAR 5) are executed.

---

## 3. Cryptographic primitives & standards check

Every cryptographic and integrity primitive in `src/rar/codec/crypto.c` was implemented from published standards (never copied
from UnRAR) and checked against standard Known-Answer Tests (KATs):

| Primitive | Standard | Verification vector |
|---|---|---|
| CRC-32 | ISO 3309 / ITU-T V.42 | `"123456789"` -> `0xCBF43926`; every RAR 2–4 fixture CRC |
| AES-128 / AES-256 | FIPS 197 | Appendix C.1 & C.3 test vectors |
| SHA-1, SHA-256 | FIPS 180-4 | `"abc"` digest vectors |
| HMAC-SHA-256 | RFC 2104 | RFC 4231 Test Case 1 |
| PBKDF2-HMAC-SHA-256 | RFC 8018 | RAR 5 key, hash key, and check value vector |
| RAR 3 Key Derivation | Custom SHA-1 loop (spec §6.1.1) | Specification KDF vector & RAR 3 encrypted fixtures |
| BLAKE2s / BLAKE2sp | RFC 7693 & BLAKE2 paper | Two reference vectors + `rar5_blake2.rar` |

---

## 4. Oracle & corpus verification

The clean-room decoder was tested against three independent black-box oracle manifests (created by running reference tools
without reading their source code):

- `expected.txt` (libarchive BSD-2 test archives checked with `bsdtar`): **47 / 47 passed**.
- `expected-unrar.txt` (black-box output of UnRAR 7.3.1 on generated archives): **48 / 48 passed**.
- `expected-oldrar.txt` (RAR 2.50, WinRAR 2.90, RAR 3.93, 6.24, 7.12 archives): **45 / 47 default, 47 / 47 with large tests enabled**.

Within FultaArc, the integrated reader is continuously checked under ASan + UBSan across `tests/manifests/rar.txt`,
`tests/manifests/libarchive.txt`, `tests/test_time.c` (231 timestamp checks to 100 ns / 1 ns precision against `7z l -slt`),
`tests/test_security.c` (`max_dictionary` and `max_memory` caps), and `tests/test_robust.c` fuzz mutations.
