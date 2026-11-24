# RAR 2.0 – 7.0 — provenance

This page records where FultaArc's RAR 2.0–7.0 decoder came from, which sources were used and under what licenses,
which sources were kept out, and how to verify the evidence record. For RAR 1.4 and 1.5 (including RAR 1.5 encryption),
see [`../rar15/provenance.md`](../rar15/provenance.md) and [`../rar15/enc3/PROVENANCE.md`](../rar15/enc3/PROVENANCE.md).

This is an engineering record, not legal advice. Where a step depended on a licence judgement, the record identifies who
made it; it does not state a legal conclusion.

---

## 1. Statement

FultaArc's RAR 2.0–7.0 support is **MIT-licensed C23 code** written by FultaArc contributors via a two-team
specification-based clean room.

- **Specification (`rar-spec`):** written from permissively licensed readers (`libarchive` BSD-2-Clause,
  `nwaples/rardecode` BSD-2-Clause), RARLAB's published RAR 5.0 technical note, public-domain LZMA SDK PPMd sources, and
  public cryptography standards.
- **Decoder (`rar-decoder`):** written strictly from that specification and public standards in an isolated repository
  that never contained UnRAR, `libarchive`, `rardecode`, 7-Zip, or any other RAR reader.
- **What FultaArc does not contain:** zero UnRAR source code, zero UnRAR license restrictions ("no re-creating the RAR
  compression algorithm" restriction does not apply — FultaArc only decompresses, and does not incorporate UnRAR),
  zero copyleft code, and zero 7-Zip RAR code.

---

## 2. What FultaArc contains

| Path | Role | Origin | License |
|---|---|---|---|
| `src/rar/rar.c`, `src/rar/rar.h` | RAR 4 & RAR 5 container reader, volumes, timestamps, filters | `rar-decoder` clean room -> FultaArc `proven_allocator_t` port | MIT |
| `src/rar/rar_glue.c`, `src/rar/rar_codec.h`, `src/rar/rarcodec.h`, `src/rar/compat.h` | FultaArc format glue, solid-group error caching, bounds enforcement | FultaArc contributors | MIT |
| `src/rar/codec/codec.c`, `unpack.c`, `unpack20.c`, `unpack29.c`, `unpack50.c`, `rc_internal.h`, `rc_unpack.h` | RAR 2.0, 2.9/3.x, and 5.0–7.0 decompressors | `rar-decoder` clean room (written from `rar-spec` doc alone) | MIT |
| `src/rar/codec/filters.c` | Standard RAR 3 & RAR 5 byte filters (no arbitrary VM execution) | `rar-decoder` clean room | MIT |
| `src/rar/codec/crypto.c` | CRC-32, AES-128/256, SHA-1, HMAC-SHA-256, PBKDF2, RAR 3 KDF, BLAKE2s/BLAKE2sp | `rar-decoder` clean room (from FIPS/RFC/BLAKE2 + spec) | MIT |
| `vendor/lzma/Ppmd7.c`, `Ppmd7aDec.c` | PPMd var.H entropy model used by RAR 3 | LZMA SDK 26.05 (Igor Pavlov / Dmitry Shkarin) | Public Domain |

---

## 3. Permitted sources and terms

| Source | Role | Terms & Compatibility |
|---|---|---|
| **libarchive 3.7.7** RAR / RAR5 modules | Consulted by `rar-spec` only | **BSD-2-Clause**: allows commercial and MIT re-implementation/derivatives; test archives also BSD-2 |
| **nwaples/rardecode** | Consulted by `rar-spec` only | **BSD-2-Clause**: permissive license |
| **RARLAB RAR 5.0 Archive Format Technote** | Consulted by `rar-spec` for RAR 5 container format | Published technical format specification |
| **LZMA SDK `Ppmd7*`** (`vendor/lzma/`) | Compiled into FultaArc for RAR 3 PPMd var.H | **Public Domain** |
| **FIPS 197, FIPS 180-4, RFC 2104/4231/7693/8018, ISO 3309** | Used directly by `rar-decoder` | Public standards |
| RAR 2.50, WinRAR 2.90, RAR 3.93/6.24/7.12, UnRAR 7.3.1 | Run as **black-box fixtures/oracles only** (never read by `rar-spec` or `rar-decoder`) | Not distributed; outputs kept as private test fixtures |

---

## 4. Sources excluded from both clean-room stages

| Excluded material | Handling |
|---|---|
| UnRAR source code (any version) | Excluded from `rar-spec`, `rar-decoder`, and FultaArc; zero UnRAR code exists in FultaArc |
| 7-Zip RAR decoder | Excluded (LGPL + UnRAR-derived terms) |
| SharpCompress, junrar, unarr, The Unarchiver | Excluded from `rar-decoder` (never opened) |
| `libarchive` & `nwaples/rardecode` source code | Excluded from `rar-decoder` (read only by `rar-spec`) |
| Arbitrary custom RAR 3 VM bytecode programs | Intentionally unimplemented (`FULTA_ARC_ERR_UNSUPPORTED`) |

---

## 5. Verifiable evidence anchors (private archive)

The immutable git bundles of both clean-room stages are preserved in FultaArc's private evidence store and anchored by
these SHA-256 digests:

```text
47e2048be855e1228114253dd03921a18c54256c119515653b79c22e3e8c7602  rar-spec.bundle
3cfb86706c3cb69c450e5c32211d36c55c1cd933e5dcfd698e970c785af0ec0e  rar-spec-8cf3ffa.bundle
41806eba9e94125fa08910cec025842a74fe91b494af2abc3f280c8d3e0225  rar-decoder.bundle
```

FultaArc's public tree includes zero proprietary binaries or restricted test archives; all C tests pass cleanly with
missing private fixtures skipped gracefully, and pass full oracle/sanitization/fuzz verification when pointed at the
private fixture corpus.
