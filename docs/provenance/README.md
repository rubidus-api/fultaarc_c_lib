# Provenance of FultaArc's decoders

Some of FultaArc's decoders read formats for which no single permissive specification and reference library existed, or for
which FultaArc chose not to reuse third-party implementations directly. Those decoders were developed in **clean rooms**
— isolated repositories governed by a written brief specifying what may and may not be read, a step-by-step log tying
every rule to a cited source or observation, and periodic audits for ungrounded rules or forbidden material.

This directory publishes standalone engineering documentation for every clean-room decoder shipped in FultaArc:

- [`format.md`]` / `algorithm.md`: how the container layout or compression/encryption algorithm works.
- `derivation.md`: how each rule was established — cited permissive sources or logged black-box experiments, refuted
  hypotheses, and oracle cross-checks.
- `provenance.md`: exact files in FultaArc, permitted sources and terms, excluded sources, mechanical checks, and
  SHA-256 anchors in FultaArc's private evidence store.

> **Engineering record, not legal advice.** These documents record facts, dates, sources, and owner decisions at the time
> the work was performed. Where a step depended on a licence judgment, the record identifies who made it; it does not
> constitute a legal opinion.

## Clean-room decoders in FultaArc

| Format | Decoder in `src/` | Documentation | Method & Scope |
|---|---|---|---|
| **RAR 2.0 – 7.0** | `src/rar/` (excl. `rar15/`, `rar15_enc/`) | [`rar/`](rar/README.md) | **Two-team specification-based clean room**: specification written from BSD-2 readers (`libarchive` 3.7.7, `nwaples/rardecode`), RARLAB's published RAR 5.0 technote, and public-domain LZMA SDK PPMd; decoder implemented from specification alone (zero UnRAR code) |
| **RAR 1.4 / 1.5** | `src/rar/rar15/` | [`rar15/`](rar15/README.md) | **Black box from zero**: observed output of RAR 1.55 and RAR 1.40.2 beta for DOS run under DOSBox; byte-identical pinned sources (`rar15-pin.sh`) |
| **RAR 1.5 Encryption** | `src/rar/rar15_enc/` | [`rar15/enc3/`](rar15/enc3/README.md) | **Black box + symbolic SMT**: RAR 1.5 XOR stream cipher derived from RAR 1.55 ciphertexts; byte-identical pinned cipher, EN/KO reports & offline HTML guides |
| **ALZ** | `src/alz/`, `vendor/bzip2_alz/` | [`alz/`](alz/README.md) | **Black box from zero**: ALZ container and `DLZ\x01` 178-bit-trimmed bzip2 framing re-derived from ALZip 4.9–12.37 output alone in `fultaarc-cleanroom-alzegg` (zero `unalz` code; outperforms `unalz 0.65` on ALZ method 1) |
| **EGG** | `src/egg/` | [`egg/`](egg/README.md) | **Two independent firewalled black-box clean rooms**: `egg-container` and `egg-verify` from ALZip 8.6–12.37 outputs, verified and extended in `fultaarc-cleanroom-alzegg` |
| **AZO (EGG method 3)** | `src/azo/` | [`azo/`](azo/README.md) | **Three-round black-box clean room (decode-only)**: respects ESTsoft patent KR101049699B1; derived from ALZip `.com`/`.sys` output with disclosed public wiki + public-domain LZMA spec hypotheses |
| **LHA / LZH** | `src/lha/lhadec/` | [`lha/`](lha/README.md) | **Specification-based + black box**: public-domain `ar002` + black-box runs of `lhasa` (ISC), `UNLHA32.DLL`, and `PMarc` |
| **CAB Quantum** | `src/codec/quantum.c` | [`quantum/`](quantum/README.md) | **Black box from zero**: observed Microsoft `cabinet.dll`/`extrac32.exe` and `DIAMOND.EXE`, cross-checked against Windows 95 retail cabinets |

Formats outside this table (ZIP, 7z, CAB Store/MSZIP/LZX, TAR, cpio, ar, ISO 9660, UDF, gzip, bzip2, xz, zstd, lzip,
Unix compress) are implemented directly from published open standards (RFC, IEEE/POSIX, ECMA, ISO, PKWARE APPNOTE, `[MS-CAB]`,
`[MS-PATCH]`) or use permissively licensed upstream libraries listed in [`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md).

## Methodologies & rules followed across rooms

FultaArc distinguishes two clean-room structures, and states explicitly which one applies to each decoder:

1. **Specification-based two-team clean room** (`rar/`, `lha/`): Team A reads only verified permissive source code or
   published vendor technical notes and writes a standalone prose specification; Team B sees neither the restricted codebases
   nor Team A's upstream sources, and writes FultaArc's MIT decoder from the specification plus published standards
   (FIPS/RFC/public-domain algorithms) alone.
2. **Black-box behavioral clean room** (`rar15/`, `rar15/enc3/`, `alz/`, `egg/`, `azo/`, `quantum/`): analysts observe
   only compressed archives, decompressed bytes, and exit status from existing tools executed as black boxes — never
   reading third-party format source code, and never disassembling, dumping, tracing, or inspecting binaries.

Every room operated under six standing rules:

1. **Trace or quarantine.** A rule is adopted only when it cites a logged observation, a verified permissive source,
   or named public mathematics. Unsupported guesses are quarantined.
2. **Hypothesis before probe.** Every black-box hypothesis and falsification condition is logged before running the
   experiment; measurements are quoted directly from saved logs.
3. **Memory is quarantined.** Recalled facts may suggest what to test, never dictate a specification rule; recalled
   hypotheses are logged and must be confirmed or refuted by data (for example, `rar15/` step L0012 and `rar15/enc3/`).
4. **Periodic audits.** Rooms are audited for unlogged leaps, coincidence treated as proof, and boundary violations;
   false starts and errors are corrected in place and disclosed in the record.
5. **Finite is tested, never proven.** Passing all collected corpora is reported as tested scope, with unobserved cases
   marked and refused safely rather than guessed.
6. **No legal conclusions from engineering records.** Licenses and terms are checked from authoritative upstream files
   and quoted verbatim; whether a workflow is lawful rests with the project owner and legal counsel, not the log.
