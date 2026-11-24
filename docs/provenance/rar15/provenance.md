# RAR 1.4 / 1.5 — provenance

This page records where FultaArc's RAR 1.4 / 1.5 decoder came from: what code it contains, which sources were used
and on what terms, and which were kept out. How each rule was derived is in [`derivation.md`](derivation.md); the
format and the stream themselves are in [`format.md`](format.md) and [`algorithm.md`](algorithm.md).

This is an engineering record, not legal advice. Where a step depended on a judgement about terms, the record says
who made it; it does not state a legal conclusion.

## 1. Statement

FultaArc's RAR 1.4 / 1.5 support is MIT-licensed code written by FultaArc contributors.

- **The decoder** was written in FultaArc's own clean room (`fultaarc-cleanroom-rar15`) from that room's own
  specification.
- **Where the specification came from:** observation of RAR 1.55 for DOS (and RAR 1.40.2 beta for the older
  format) run as black boxes, plus public compression mathematics.
- **What it does not contain:** any third party's code, tables or text — no RAR code, no emulator code, no SFX
  module.
- **What was never read:** any description of a RAR format, any other program's RAR code, and the inside of any RAR
  executable. The earlier RAR 1.5 work kept in a private sibling repository was forbidden by the room's brief and
  never read.

## 2. What FultaArc contains

| File | What | Origin | Licence |
|---|---|---|---|
| `src/unpack15.c`, `src/unpack15.h` | the decompressor for the compressed stream (`algorithm.md`); no dependency beyond the C library | written in the clean room from `docs/spec/rar15.md` | MIT (SPDX header in `unpack15.h`) |
| `src/rar15arc.c`, `src/rar15arc.h` | the archive reader (`format.md`): volumes, solid archives, comments, the RAR 1.4 cipher, CRC checks; on the vendored `proven_c_lib` | written in the clean room | MIT (SPDX header) |
| `cli/rar15x.c` | a list / test / extract command-line tool on the reader | the clean room | MIT (SPDX header) |

**SHA-256 of the delivered source** (clean-room final state, commit `0f24be6`):

```
2da03f35d470514ef67e964b05e671319480368b3241e99f72ba8c3f898150db  src/unpack15.c
1e10f97ec3f47215c46debbb30c4a91b1d442f771253e62d0f356fd04d472e32  src/unpack15.h
a979cd22f7621062bcf309809615cd157fa5988f4b9b9bf2f06d68d0e690e0a7  src/rar15arc.c
e442a30c2a24e6452acf37714f239cda78662f2c704f4258e5983a9f3f790d2d  src/rar15arc.h
1679b73988b7618a1becfd133298253aa94b8248e211fb5ec49e79b1c7b29b89  cli/rar15x.c
```

The MIT grant, from the SPDX header in `unpack15.h` ("Copyright (c) 2026 the fultaarc-cleanroom-rar15 authors"):

> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
> documentation files (the "Software"), to deal in the Software without restriction, including without limitation
> the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software …

(FultaArc's edition: FultaArc ships the first two rows, byte-identical, as `src/rar/rar15/`; `cli/rar15x.c` stays in
the room's record. FultaArc also ships `src/rar/rar15_enc/rar15_cipher.c` byte-identical from the separate clean
room `fultaarc-cleanroom-rar15-enc3` at commit `2c3c4a4`, documented in [`enc3/PROVENANCE.md`](enc3/PROVENANCE.md).)

**No RAR module is committed.** The two `.exe` test fixtures (`tests/fixtures2/s0372_front64.exe`,
`s0391_14_front64.exe`, 1126 and 1488 bytes) are the room's own constructions: an `MZ` at offset 0, zeros, `RSFX`
at offset 28, then an archive — the self-extractor shape of `format.md` §9, with none of RAR's program in them.

## 3. Sources used, and on what terms

Each source was admitted only after its terms were confirmed from its own origin where origin terms exist.

### 3.1 RAR 1.55 for DOS — run only, never read

- **What:** `RAR.EXE` and `RAR.DOC` of RAR 1.55 for DOS (released 20 Aug 1995), the only oracle for the RAR 1.5
  format.
- **Terms.** RAR 1.55 is shareware distributed for time-limited evaluation; `RAR.DOC` itself carries no licence
  text but points to a licence screen in the installer: *"Choose \"Read license\" to view the RAR license for usage
  and distribution."* The installer's other files (including that screen) were removed before the room received the
  folder, so the licence text was not retained; the recorded status (shareware, evaluation, used here as a **test
  instrument only**) is the setup session's note, not a quote from origin. `RAR.DOC` does state that the
  authenticity feature *"MUST be registered"*, which is why no authenticity block could be produced (`format.md`
  §11).
- **How it was used:** run in a DOS emulator as a compressor of chosen inputs and as a decoder oracle for
  archives the room built; only its output was read (archives, extracted files, console text). `RAR.DOC` was read
  for its commands and switches only — no format statement rests on it.
- **What was not done:** it was never disassembled, dumped, traced, or viewed as hex or `strings`; it was not
  redistributed and stays out of the public repository.
- **Hashes:** `RAR.EXE` `8199a543b1dfbb8229b5079f2d88e1d679731138ce540054f27da74c89e128e6`; `RAR.DOC`
  `190674f2f4e9774d13417e5cd63e9c4cf47475a904d318ad3b4448998b868c3f`.

### 3.2 RAR 1.40.2 beta for DOS — run only (older format)

- **What:** `RAR.EXE` and `RAR.DOC` of RAR 1.40.2 beta (20 March 1994), the writer and reader of the RAR 1.4
  format. Taken from an "unpack-only" distribution bundle holding the executable and `RAR.DOC`; no separate licence
  text file was retained in the bundle at hand.
- **How it was used:** as RAR 1.55 was — writer and reader of chosen inputs, output only; `RAR.DOC` read for the
  manual (volume names, `cf`, `-av`, `-vs`) under the owner's "manuals only" decision; not disassembled, not
  redistributed.
- **Hash:** `51604c0f0f45d34e3eebbab4d08cf3b4a162ee12f8832e82f4d0d06c05e2d37b`.

### 3.3 RAR 2.50 / 2.02 / 2.00 for DOS — judges only

- **What:** `RAR.EXE` of RAR 2.50, 2.02 and 2.00 for DOS.
- **How it was used:** as **verdict-only judges** (`t` — test) of entries that do not decode, to confirm that a
  failing entry is damaged rather than mis-decoded. Only their Ok/Failed lines were read; nothing of their own
  format was examined; not disassembled, not redistributed.
- **Hashes:** 2.50 `426b45982a6a675d149f7ef3cbd3ad3171234c187cb633891c066aa819259651`; 2.02
  `9c083ac85289c1828ab14ca2153728f8afb5d1e5f1f5cd85988282c82e79f9e3`; 2.00
  `f924470acb12fed615862e23999d1fad084127370bfa3aa25049ba7b9e700a01`.

### 3.4 A DOS emulator (DOSBox) and its libraries — a tool, run only

- **What:** DOSBox 0.74-3 and the SDL libraries it needs, used to run the RAR executables.
- **Terms**, from the packages' own copyright files: DOSBox is *"License: GPL-2"*; the SDL libraries are
  *"License: LGPL-2+"*.
- **Status.** A runtime instrument only. **Nothing of DOSBox or SDL is in the deliverable** — it is not linked,
  copied or distributed with FultaArc's MIT code; it only executed RAR. The workspace rule forbids *including*
  GPL/LGPL code, and none is included.

### 3.5 `proven_c_lib` — vendored (MIT)

- **What:** the workspace's base C23 library (`proven_c_lib` v0.6.0), vendored under `vendor/proven/` and used by
  the archive reader and the tool for memory and file I/O; it holds no RAR content.
- **Terms**, from its own `LICENSE` (an MIT licence): *"Permission is hereby granted, free of charge, to any person
  obtaining a copy of this software … to deal in the Software without restriction …"*.
- The copy is never edited in this repository; the tree hash and version are recorded in `DEPENDENCIES.md`
  (SHA-256 over its file list `4e1ec8f89e207d3d4196a249b948a930c7bce2d8ef9a9adfe6be6b451cbb3ad5`).

### 3.6 Build and scripting tools

Python 3 (standard library only) and gcc 14, for the reference model, the probe and fitting scripts, and the C
decoder. Nothing of them is in the deliverable beyond the C runtime that any C program links.

### 3.7 Public mathematics

General compression knowledge, used only for the *shape* of a hypothesis and then tested against data: LZ77/LZSS
copy streams, canonical prefix codes and the Kraft inequality, frequency-ordered and move-to-front lists, running
averages; and the public standards CRC-32 (as in zlib) and the MS-DOS date/time encoding.

### 3.8 Test inputs and real archives (not distributed)

Admitted by the owner's decision of 2026-10-07, as data only — none is about the RAR format, and all stay under the
repository's ignored working directory, not distributed:

- 26 public files of many formats, as inputs for RAR in the soak test (recorded with URL, size and SHA-256);
- about 3005 archives with a `.rar` extension dated 1994–1997, and 65 further volumes of 8 volume sets, from a
  public file-search index of old CD-ROM contents, and 8 older-format archives of 1993–1995 — used as real test
  archives, decoded by this room's code and by RAR 1.55 only (recorded with name, size and SHA-256);
- `IDOS.SFX` of the RAR 1.55 distribution — passed to RAR as a self-extractor module, never run or looked into.

## 4. Sources kept out

| Source | Why | What happened |
|---|---|---|
| Any description of a RAR format (UnRAR, RARLAB technical notes, WinRAR help, any web page about RAR) | the room's brief forbade reading any RAR format description | not read; no web search about RAR |
| Any other program that reads RAR (`unrar`, `7z`, libarchive/`bsdtar`, `rardecode`, junrar, The Unarchiver/unarr, SharpCompress, …) | each contains a RAR decoder | not read, not run on RAR files; FultaArc's own code is the only other reader |
| The inside of any RAR executable | the brief forbade disassembly, debugging, memory dumps, `strings` or hex views of the program | never opened as code; the executables were only run |
| The earlier RAR 1.5 work (a private sibling repository) | the brief required the format to be redone from observation | never read |
| Other RAR / WinRAR versions and their formats | out of the room's scope | not examined; RAR 2.x used only as a verdict judge (§3.3) |

No program was disassembled, dumped, examined with `strings`, or traced. Entries of real archives whose version
byte is not 0x0f (other format versions) were counted and not examined.

## 5. Knowledge from memory

The room's rule was stricter than "declare, then test": a recalled fact about RAR could not even be tested. The
complete memory input to the decoder — the three "recall noticed, set aside" lines (logged without content), the
forbidden-and-unread earlier work, and the `src=general` list — is set out with its fate in derivation §X.2. No
rule was written, tested or chosen because of a recalled fact; every rule cites the step ids of its observations.

## 6. Mechanical checks

- **Specification, then code.** The specification was written from observations before the C code, and the C
  decoder was written from the specification, not translated from the Python reference model used to test
  hypotheses (derivation §X.1). Model and C agree on all 1264 saved entries.
- **Behaviour.** The whole decoder was checked against RAR 1.55 on 1170 development archives, 21 holdouts on frozen
  models (each on inputs never used before), soak tests of 5000 generated and 2500 real-file-slice cases (69,944
  entries, 2.67 GB; 0 wrong), and 815 real archives of 1994–1997 (13,459 entries, 432.9 MB); the 18 failing entries
  fail in RAR 1.55 too (damaged/truncated), and RAR 2.50/2.02 agree. The C decoder builds and tests clean under
  `gcc -std=c23 -Wall -Wextra -Werror -fsanitize=address,undefined`.
- **No RAR code in the tree.** The only `.exe` files committed are the room's own self-extractor fixtures (§2); no
  RAR program, module, or `IDOS.SFX` is in the repository.

## 7. Decisions recorded, not legal conclusions

These are recorded as the owner's decisions, dated; the record does not conclude that any use is lawful.

- **2026-10-02.** Work out RAR 1.5 from the bottom, from RAR 1.55's behaviour alone, in a fresh room; do not read
  the earlier work.
- **2026-10-07.** Admit, as instruments or data only: public test inputs and real 1990s archives (§3.8); RAR 2.50 /
  2.02 as verdict-only judges (§3.3); `proven_c_lib` vendored (§3.5); RAR 1.40.2 beta and RAR 2.00 as writer/reader
  and judge for the older format, from unpack-only bundles, with **manuals only** of their documentation (§3.2);
  `IDOS.SFX` passed to RAR unread; and a general program-automation tip (for driving an interactive DOS prompt on a
  virtual display, about automation and not about any format) used to read RAR 1.40.2's file comments.

RAR and the emulator were used as black-box instruments under those decisions; what FultaArc ships is its own MIT
code, derived from watching them, containing none of their code or text.

## 8. Where the full record is

The following are kept in FultaArc's private record and are not published: the clean room's full repository and
history; its append-only observation log (steps S0001 onward) with every probe script and saved oracle run; its
specification, reader's guide and internal provenance; and the instruments above. This page and the three beside it
summarise that record; their citations give the step ids so they can be checked against it.

**Record anchors.** The room's final state is commit `0f24be6` of `fultaarc-cleanroom-rar15` (155 commits). Anyone
given the record can confirm it is the one these pages summarise:

```
d3da2df7c31dc793848c98dd1daf1fd101d98e04ebe211dcd2acb5f5d9463289  docs/cleanroom/log.md
e38e4c5d1a03b8737b14ec1303c555003923d8e146a89c0dd463efc2a1136a2e  docs/spec/rar15.md
e0fe8a564d77e0e8be61967ce2af15835a02b04a424d555d517137c3e8d9d658  docs/spec/rar14.md
449dba8de8b4dcfb7331b18c1262f50065e9185d586fdc8813aa8df03bb85ec2  docs/cleanroom/provenance.md
a3dadb9a5f3a1b9119a46763ded0e4dc9a5659e9ef24847b8d53d5e2d108a49d  docs/cleanroom/discovery.md
5fcd0f992b7cfe0460a0f6b4bdda91f716c2bad9d4f65ec9f946bbc3292a97d9  CLEANROOM.md
```
