# CAB Quantum — provenance

This page records where FultaArc's Quantum decoder came from: what code it contains, which sources were used and on
what terms, and which were deliberately kept out. How each rule was derived is in [`derivation.md`](derivation.md);
the format and the method are in [`format.md`](format.md) and [`algorithm.md`](algorithm.md).

This is an engineering record, not legal advice. Where a step depended on a licence judgement, the record says who
made it; it does not state a legal conclusion.

## 1. Statement

FultaArc's Quantum decoder is MIT-licensed code written by FultaArc contributors.

- **It was written** in FultaArc's own clean room, from that room's own specification.
- **The specification came from** observing Microsoft's own cabinet reader and encoder, run unmodified as programs
  on licensed systems, and from public mathematics.
- **No Quantum implementation or description by anyone else** was read, run on Quantum data, ported or paraphrased.
- **It contains** no third party's code, tables or text.

## 2. What FultaArc contains

| Path | What | Origin | Licence |
|---|---|---|---|
| `src/codec/quantum.c` | the Quantum decoder | the clean room `fultaarc-cleanroom-quantum-lzx`'s `port/quantum.c`, unchanged from its first `#define` on; the header comment and include block are FultaArc's | MIT |
| `src/cab/cab.c` | the Cabinet reader: folder dispatch, the block cap, cabinet sets | FultaArc | MIT |
| `tests/test_cab.c`, `tests/manifests/cab.txt` | tests | FultaArc | MIT |

**Not distributed.** The test cabinets (`quantum10/12/21.cab`, `qset1-3.cab`) are made by Microsoft's Diamond from
neutral plaintext and are kept outside the public repository; without them those tests skip. The Microsoft-shipped
cabinets the room decoded are not FultaArc test input and are not distributed.

## 3. Sources used, and on what terms

### 3.1 Windows 11 `cabinet.dll` and `extrac32.exe` — run only, at the owner's decision

- **What:** Windows' own cabinet reader, `5.00 (WinBuild.160101.0800)` on Windows NT 10.0.26200.0 (SHA-256 of
  `extrac32.exe` `7F966ADE…D03B577`, of `cabinet.dll` `9E5E9428…C5C4BA7FF`, full values in the record).
- **Status: the instrument, by the owner's decision** (2026-10-16, "option 4", step Q0001). The room's licence
  register posed the question that decision answers — whether deriving a data format by observing a licensed Windows
  component's input and output is within the Windows licence and the owner's jurisdiction's interoperability rules —
  and deliberately did not answer it.
- **How it was used:** on the owner's licensed Windows 11 virtual machine, given cabinets of the room's own making
  around chosen bodies; its exit code, message and written files were recorded.
- **What was not done:** it was not disassembled, inspected, traced or copied; its bytes were never read as logic.

### 3.2 Windows 11 `makecab.exe` and `expand.exe` — probed only

Run once each to see whether they could serve: `makecab` refuses Quantum; `expand` does not tell an invalid type from
a failed decode (Q0001). Neither was used further.

### 3.3 Microsoft-made cabinets — data only

- **What:** 22 Quantum cabinets from floppy images of Windows 95 (retail), Microsoft Plus! for Windows 95 and Internet
  Explorer 2.0, held by the Internet Archive (items `95_20210828`, `ms-plus95-b310-fd`, `iexplore2_kryoflux`), one of
  them stored inside another.
- **How they were used:** carved from the images by the room's own tools at recorded offsets, each pinned by SHA-256,
  and decoded by the room's decoders; `extrac32`'s extraction of the same cabinets gave the expected output (Q0049,
  Q0051).
- **What was not done:** only these binaries were taken; no format description was read with them, and they are not
  kept in any repository or distributed.

### 3.4 Diamond 1.00.0530 (1995) — run only (Microsoft EULA)

- **What:** Microsoft's 1995 cabinet maker `DIAMOND.EXE`, from the `MCABIN10.ZIP` kit (Internet Archive item
  `cab-sdk_202411`).
- **Terms**, from the kit's licence text: a Microsoft EULA that permits use and redistribution and forbids reverse
  engineering of the tool.
- **How it was used:** run on the licensed virtual machine with a directive file setting `CompressionType=QUANTUM`,
  on the room's own plaintext at every window X = 10..21 (Q0051, Q0052). FultaArc's test cabinets were made the same
  way.
- **What was not done:** not reverse engineered; only its licence text and its sample directive file were read.

### 3.5 The 1997 Cabinet SDK — probed only

`MAKECAB.EXE` 1.00.0601 and `CABARC.EXE` from the Internet Archive item `Cabsdk-03-20-1997` were run once: neither
offers Quantum (Q0049). Only the tool binaries were downloaded; the SDK's documents were not.

### 3.6 The public Cabinet container

The container format — header, folder, file and data-block records, their checksum, continued folders and split
blocks — is Microsoft's published Cabinet format, already read by FultaArc. The room inherited it from FultaArc's
`src/cab/cab.c` at commit `9e2881d` (SHA-256 pinned in the record) and re-derived none of it. Nothing about the inside
of a Quantum body, and not the meaning of `typeCompress`'s upper bits, was inherited.

### 3.7 7-Zip — container checks only

The installed `7z` was used to check the room's own Store and MSZIP control cabinets. It was **never run on Quantum
data**: its Quantum support descends from reverse engineering.

### 3.8 Public mathematics

Used for the shapes of hypotheses, each then tested against the oracle:

- I. H. Witten, R. M. Neal, J. G. Cleary, "Arithmetic coding for data compression", *Communications of the ACM*
  30(6), 1987 — the integer arithmetic decoder of algorithm §2;
- LZ77/LZSS copy streams, prefix codes, adaptive frequency models;
- generic sorting algorithms, scored as candidates for the reorder and rejected (derivation M6).

## 4. Sources kept out

| Source | Why | What happened |
|---|---|---|
| libmspack, cabextract, cablib, 7-Zip's CAB code, wimlib, The Unarchiver | the only public Quantum decoders: copyleft or of unverified licence, and the brief forbade reading any Quantum implementation | not read, not run on Quantum data |
| `github.com/dcarrero/unquantum` (a Rust decoder labelled MIT) | its own credits say it draws on LGPL-2.1 libmspack, so its MIT label could not be shown clean without reading it; it is also a third-party implementation | rejected; only its README and credits were read, for this decision |
| `QUANTUM.DOC` (named in third-party credits as the original authors' format note) | authenticity and terms unverified | never fetched or read; not needed |
| Pages describing the Quantum format (encyclopaedia and file-format sites) | descriptions of the format | not opened; the forbidden projects' hosts were blocked in every search |
| The 1997 Cabinet SDK's documents | not needed | not downloaded |
| `[MS-PATCH]` (LZX DELTA) | another method, implemented elsewhere in FultaArc | not read in the room |

No program was disassembled, dumped, examined with `strings` or traced.

## 5. Knowledge from memory

The room's rule was that remembered facts may suggest a probe but never write a rule. The session held general
knowledge of LZ77, Huffman and arithmetic coding, used as mathematics. The audits found no Quantum-specific statement
written before the observation that supports it. The hypotheses that general knowledge suggested, and their fate, are
in derivation §X.2: the arithmetic coder was confirmed by measurement; the generic rescale and sorting candidates all
failed, and the rules adopted were measured.

## 6. Mechanical checks

- **Code identity.** `src/codec/quantum.c` from its first `#define` equals the room's `port/quantum.c` (`diff`, at
  integration and for this edition).
- **Behaviour.** FultaArc's `make test` decodes the Diamond-made cabinets of §2 against their known plaintext
  (`tests/manifests/cab.txt`, exact) and checks block spanning, the window refusal and the set walk
  (`tests/test_cab.c`).
- **No third-party code to compare.** A textual comparison with the forbidden decoders was not made, because a script
  opening them would be reading them. A reviewer free to read them can make it.

## 7. Decisions recorded, not legal conclusions

- **Windows' reader as the instrument.** The owner's decision of 2026-10-16 (§3.1). The record names the licence
  question and leaves it to the owner.
- **Diamond.** Run as a program under its EULA, which permits use and forbids reverse engineering; no reverse
  engineering of it was done. How its terms bear on a format description derived from watching its output is a
  question the record names and leaves to the owner, as for every instrument.
- **Shipped cabinets.** Public archive copies of Microsoft products, used as test data only, never redistributed.

## 8. Where the full record is

Everything below is kept in FultaArc's private record and is not published:

- the clean room's full repository and history;
- its brief, licence register and inherited-facts file;
- its derivation log (Q0001–Q0052) and five audits;
- its own specification;
- every query batch with its answers (`data/`), the probe scripts, the reference decoder and the encoder (`tools/`);
- the session records.

This page and the three beside it summarise that record. Their statements cite the record's step numbers, so they
can be checked against it.

**Record anchors.** The room's final state is commit `c197dab` of `fultaarc-cleanroom-quantum-lzx` (53 commits,
2026-10-15/16). The SHA-256 of its record files at that state:

```
5d56fb959bd9c62630f6f4f5382c36b66b1ec5c106537cfe187030b4a037297b  CLEANROOM.md
5f4961862e122130d11c9131dc131a869c0b702a854fd1b0e62a348dff3185f5  docs/provenance/sources.md
a037653d9c2d46db746bb0e9d201100778e4b97bc87e080545ea02a17b400575  docs/provenance/inherited.md
40742abbe2bc55a8211ea3be1c234dcaa5d44761a1c538197f30437cb0174553  docs/cleanroom/log.md
3cf699f1914cf1e69c922d85423645cd78478f7668261163e0dc18d92f078e6b  docs/cleanroom/2026-10-16-audit-01.md
0ede2f219aabbedf9673b842c25b044f72dcac87612543adddc50bec435d4cf9  docs/cleanroom/2026-10-16-audit-02.md
52ebccf05d709daabf941150eb08cb0525cffd50c9aa7cd3daa04979d2d2efba  docs/cleanroom/2026-10-16-audit-03.md
7777dd1e19aef0f526a5466deb014b05174a862449f1958abc73f1031e26936e  docs/cleanroom/2026-10-16-audit-04.md
6dbe80434a5b8c4404c332cbe79210e6758a5cf1ab3e3928300112d1458345ff  docs/cleanroom/2026-10-16-audit-05.md
25aa714bf392b29159e6e23950e28413973959d0bab2a46b4c8e2cd59409aaef  docs/spec/quantum-observed.md
1491fc225f38ad1af7558d5dbaba537ee8f0dcadce39f0f5caf96bd0a4d43d5f  port/quantum.c
a84f515ab2857bfce257494bd64add14e14829291e36cfe19ada0643b13a127a  port/quantum.h
```

An earlier edition of this folder published the brief, the licence register, the log, the audits and the
specification byte-identical; they are no longer in the public tree, and the hashes above identify them.
