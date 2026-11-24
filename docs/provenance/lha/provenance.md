# LHA / LZH — provenance

This page records where FultaArc's LHA decoder came from: what code it contains, which sources were used and under
what licence or terms, and which sources were deliberately kept out. How each rule was derived from those sources
is in [`derivation.md`](derivation.md); the format and the methods themselves are in [`format.md`](format.md) and
[`algorithm.md`](algorithm.md).

This is an engineering record, not legal advice. Where a step depended on a licence judgement, the record says who
made it; it does not state a legal conclusion.

## 1. Statement

FultaArc's LHA support is MIT-licensed code written by FultaArc contributors.

- **The library** was written in FultaArc's own clean room from that room's own specification.
- **Where the specification came from:**
  - a public-domain program (`ar002`);
  - observation of permitted programs run as black boxes (lhasa, UNLHA32, PMarc);
  - public mathematics.
- **What it does not contain:** any third party's code, tables or text.
- **What was never read:** no other LHA implementation — free or not.

## 2. What FultaArc contains

| Path | What | Origin | Licence |
|---|---|---|---|
| `src/lha/lhadec/include/lhadec.h`, `src/lha/lhadec/src/lha_internal.h`, `lha_methods.c`, `lha_reader.c` | the decoder library (`lhadec`) | written in the clean room `fultaarc-cleanroom-lha` from its specification; copied **byte-identical** from the room's final state (commit `5792dae`); since 2026-11-03 FultaArc's edition: its allocations go through the proven allocator `lha_open` / `lha_decode_buffer` are given (D-038, owner's instruction), nothing else changed | MIT (`src/lha/lhadec/LICENSE`, "Copyright (c) 2026 FultaArc contributors") |
| `src/lha/lhadec/LICENSE` | the room's MIT licence text | the room | MIT |
| `src/lha/lhadec.sha256` | SHA-256 of the five files as the room delivered them (its close-out manifest); a record since D-038, no longer checked | FultaArc | MIT |
| `src/lha/lhadec_shim.h`, `src/lha/lhadec_rename.h` | renames the library's symbols `fa_lha_*` (until D-038 it also mapped the allocator calls) | FultaArc | MIT |
| `src/lha/lha.c` | the glue: detection, self-extractor gate, names and code pages, times, links, errors | FultaArc | MIT |
| `tests/test_lha.c`, `tests/manifests/lha.txt`, `tests/tools/lhamanifest.py`, `tests/tools/arcls.c` | tests and the manifest generator | FultaArc | MIT |

**Files as the room delivered them** (SHA-256; `make test` checked them through `tests/tools/lhadec-pin.sh` until D-038 changed the allocation calls):

```
af2505b6de2857019f213960f6d023a596117243b0ea23ca2c50697927197ce2  LICENSE
65d8c61c55f6c5b7061974c937eb4a6e9e9819e7f39e56458316b02b8361c147  include/lhadec.h
7699636691108e77d842f36e0871e09de8053d0ca97d72b7b90ec89a57acb08d  src/lha_internal.h
b51b301b8ee2b4997b159b2aed27d62cea29a3e8e3ef34519c19e8cf797bac72  src/lha_methods.c
94d25cb8182b45af94771ecf9650d66ab866dee36554e16ba18dbe0d3b0b4470  src/lha_reader.c
```

**Not distributed.** The lhasa test corpus that the tests read is kept outside the public repository. Without it
those tests skip. It is ISC-licensed: publishing it would need lhasa's notice (see `THIRD_PARTY_NOTICES.md`).

## 3. Sources used, and on what terms

Each source was admitted only after its licence or terms were confirmed from its own origin. A bare licence label
on an unknown copy was not accepted.

### 3.1 Okumura `ar002` — read, built, run (public domain)

- **What:** Haruhiko Okumura's `ar` archiver, version 002 (1990): an LZSS and static-Huffman compressor and
  decompressor in C.
- **Where from:** 12 files fetched from the author's own directory (`okumuralab.org/~okumura/compression/ar002/`),
  each pinned by SHA-256.
- **The grant, from the fetched files:**
  - `00README.txt`, line 1: *"ar002 -- written by Haruhiko Okumura and released to the public domain"*;
  - `ar.c`, line 18: *"You may copy, distribute, and rewrite this program freely."*
- **What it contributed:**
  - the static-Huffman block grammar of `-lh4-`…`-lhx-`;
  - the CRC-16 definition;
  - the level-1 header that `ar002` writes;
  - three constant names (`NT`, `NC`, `CBIT`).

  All of it is restated in the room's own words. The room also rebuilt `ar002` with other window sizes and ran it as
  a controlled encoder.
- **Nothing of it is copied.** A mechanical comparison of each library file with the pinned `ar002` sources (§6)
  finds no shared code beyond generic C idioms.

### 3.2 lhasa 0.6.0 — run only, never read (ISC)

- **What:** Simon Howard's LHA extractor, and its test corpus of 233 archives made by many tools on many systems.
- **Where from:** the 0.6.0 release tarball, SHA-256
  `9840154367f73e9d9c3196f944a121ab4d398d84e921c8fe8fca8a931274aed7`.
- **The licence, from its `COPYING.md`:** *"ISC License / Copyright (c) 2011-2025, Simon Howard / Permission to
  use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted,
  provided that the above copyright notice and this permission notice appear in all copies."*
- **How it was used:**
  - **Built and run as an oracle**, the binary pinned at SHA-256
    `3845d7178bcf123e35d46bf008529c9605237dd244b10b04cc7a101c5920faf4`. The room recorded whether it accepted an
    archive, what it extracted, the header fields it printed, and what it produced for streams of the room's own
    making.
  - **Its own test suite passes** on the machine used (17 of 17).
  - **The corpus was used as data.** Its README files were read as hints only (for example "5-bit start values"),
    never as specification text.
- **Never opened by the room or by FultaArc:** its source code (`lib/`, `src/`, `test/*.c`). The setup session read
  only the licence header of one file.

Although lhasa's licence would permit reading and reusing its code, the room did neither, so that FultaArc's code
owes nothing to it. FultaArc's manifest generator likewise runs only this pinned binary, and refuses any other.

### 3.3 UNLHA32.DLL 3.00 — run only, at the owner's instruction (freeware)

- **What:** a freeware Win32 LZH archiver library. lhasa cannot read `-lh2-` and `-lh3-`, so UNLHA32 was the only
  way to observe them.
- **Where from:** the installer `ulh3300.exe` (SHA-256 `183c0eb1…675645f`), byte-identical from the archiver project's
  page and from the Vector mirror. The room's own extractor unpacked it; the installer program itself was never run.
- **Terms**, from the package readme `UNLHA32.TXT`, section 4 (Japanese; summarised in translation):
  - UNLHA32.DLL is free software.
  - Personal, non-commercial use, and redistribution of the original archive, are unrestricted.
  - Fees beyond media cost, use restrictions and modified redistribution are forbidden.
  - Bundling it with software, or putting it in books, needs prior contact with the author.

  The readme also states that its compression routines are based on LHx 2.02a, copyright Haruyasu Yoshizaki and
  Haruhiko Okumura.
- **Status: owner-directed black-box instrument.** The owner instructed that it be obtained and used (step L0018).
- **How it was used:** run on a Windows machine through the room's own small caller program, on inputs of the
  room's own making, in three roles:
  - an encoder;
  - a decoder of chosen streams;
  - a CRC judge.
- **What was not done:**
  - it was not redistributed, bundled or copied into anything, and stays out of every repository;
  - its program was never disassembled, dumped or traced;
  - its source code, which the archiver project offers, was **not read**: its licence is unverified, and it
    derives from LHx;
  - of its documents, only the usage parts were read (command list, switch summary, one API entry, error codes),
    by recorded line ranges, as hints for which probes to run.

### 3.4 PMarc 2.00 and PMext 2.22 — run only (freeware, non-commercial)

- **What:** Yoshihiko Mino's CP/M archiver (`PMARC2.COM`, the encoder) and extractor (`PMEXT.COM`).
- **Where from:** the author's own page, with his documents `pmarc2.doc` and `pmext2.doc`, all pinned by SHA-256.
- **Terms**, from those documents (Japanese; summarised in translation):
  - The programs are freeware, and copyright is not waived.
  - Redistribution is free under conditions: original unmodified files with the document, no charge.
  - "Use for profit is refused." The author's page adds that modified redistribution is not allowed.
- **Status: black-box instrument, admitted under those terms** in the room's plan RFC 0001, which the owner asked
  for.
- **How it was used:** run privately under a CP/M emulator, on the room's own inputs:
  - PMARC2 as the encoder;
  - PMEXT as a second, independent decoder.

  Only the usage and terms sections of the documents were read.
- **What was not done:** they were not redistributed and not disassembled. No PMarc source code or format
  description was read.

### 3.5 RunCPM — a tool (MIT)

A CP/M emulator, built from its source (git `79396be`) to run the PMarc programs. Its own `LICENSE` file reads "MIT
License / Copyright (c) 2017 Mockba the Borg / Permission is hereby granted, free of charge, …". It is not an LHA
reader; only its README was read, and nothing of it is in FultaArc.

### 3.6 Public mathematics

The following are general knowledge and were used as such. They were used for the *shapes* of hypotheses and for
search spaces, each then tested against data:

- prefix codes and their canonical assignment;
- the Kraft inequality;
- LZ77/LZSS copy streams;
- adaptive Huffman coding (R. G. Gallager, "Variations on a theme by Huffman", 1978; D. E. Knuth, "Dynamic
  Huffman coding", 1985);
- move-to-front lists.

## 4. Sources kept out

| Source | Why | What happened |
|---|---|---|
| The Unix `lha` / LHa for UNIX | non-free licence | not read, not run |
| 7-Zip's LZH code, libarchive's LHA reader, p7zip, The Unarchiver / XADMaster | the room's brief forbade reading any other LHA reader | not read, not run (7-Zip was not used in the room at all) |
| LZHUF | its licence could not be verified | never read; `-lh1-` was derived without it (derivation M6) |
| UNLHA32's source and its `HEADER.TXT`, `NOTE.TXT`, `NOTE2.TXT` | licence unverified; derived from LHx | not opened |
| UnLha64x | its engine is LHa for UNIX | rejected after its README summary |
| PMarc source, any PMA format description, the `lha-pma` and `pma4PC` packages | not admitted | not read |
| Copies of PMarc programs without terms (an MSX-archive `pmarc.com`, archive.org `pmarcmsx.com`) and third-party PC ports | no terms, or not the author's program | not run; the ports not downloaded |

No program was disassembled, dumped, examined with `strings` or traced.

## 5. Knowledge from memory

The room's rule was that remembered facts about LHA may suggest a hypothesis but never write a rule.

- **Declared as memory.** Every hypothesis that came from memory was declared as such and then confirmed or refuted
  by data. Two facts had been written into code from memory without a declaration; audits caught both, and both
  were then measured. The full list, with each one's fate, is in derivation §X.2.
- **What memory contributed to the final decoder:**
  - **The `-lh1-` adaptive model.** It was confirmed on every corpus entry, and its distance code was derived from
    data.
  - **The general LZSS shape.** Every bit and offset was read from data.

## 6. Mechanical checks

- **Overlap with `ar002`.** A tokenising comparison, run in the room at its last step, compared each library file
  with the pinned `ar002` C sources:
  - the longest identical run of code tokens is 23 tokens, a generic loop idiom such as
    `for ( i = 0 ; i < n ; i ++ )`;
  - the longest identical run of comment words is 2;
  - the shared identifiers are generic names (`i`, `n`, `buf`, `crc`, …) plus `ar002`'s three constant names
    (§3.1).

  The lhasa source was **not** compared, because a script opening it would be reading it. A reviewer free to read
  other LHA implementations can run the same comparison against them.
- **Byte identity.** FultaArc's `make test` checks that its copy of the library equals the room's manifest (§2).
- **Behaviour.** FultaArc's tests compare its output with the pinned lhasa binary on 209 corpus archives, 252
  files. For `-lh2-`/`-lh3-`, which lhasa cannot read, the stored CRC-16 is checked (derivation §X.4).

## 7. Decisions recorded, not legal conclusions

- **UNLHA32.** It was used at the owner's instruction (2026-10-16), as a black box on a Windows virtual machine, under
  its package's terms as summarised in §3.3. The room records the decision; it does not conclude that the use is
  lawful.
- **PMarc.** Its programs were run privately and not for profit, under the author's terms (§3.4). FultaArc
  contains none of PMarc's code or text and does not ship it; what it took from PMarc's behaviour is the format
  facts in `algorithm.md` §9, stated in FultaArc's own words. The terms speak about using the programs; how they
  bear on a format description derived from watching them is a question the record names and leaves to the owner,
  as it does for every instrument.
- **lhasa.** It was used only as an oracle, although its licence would allow more; this was the project's choice,
  so that FultaArc owes no notice for its code.

## 8. Where the full record is

Everything below is kept in FultaArc's private record and is not published:

- the clean room's full repository and history;
- its derivation log (steps L0001–L0033) and eight audits;
- its own specification, licence register and provenance files;
- every probe script and saved output;
- the pinned external programs;
- the session records.

This page and the three beside it summarise that record. Their statements cite the record's step numbers, so they
can be checked against it.

**Record anchors.** The room's final state is commit `5792dae` of `fultaarc-cleanroom-lha` (44 commits,
2026-10-15/16); the private record keeps it with its history. The SHA-256 of its record files at that state are
below. Anyone given the record can check that it is the one these pages summarise:

```
256b569d3c581dad1586cf3fa656e2e10a03aeda5588490533f0c8d3e5381f63  CLEANROOM.md
50fdbbaca2fa1e2a67d027c555ced4a00096a0b67b563a521eda057f74b4fe14  docs/spec/lzh-container.md
2d6d996dde119026a8bec852fee9aa26852af316aa85f1053014c6c30050c73d  docs/spec/lzh-methods.md
85b75b5b1f2216e428f3b6e21e4ecc2e9ac4598a3cb5f3ca176c3ef8b30eb693  docs/cleanroom/log.md
5dc079940f54784beedf47e67166a1d49b0188a94a11f2de42f2bd2f5aa4e302  docs/cleanroom/2026-10-16-audit-1.md
29254b3c92e01b1151c056c2015423457c71b3fb6544562046f5517b7fe535f0  docs/cleanroom/2026-10-16-audit-2.md
4d26f39581dda91aa271d249ec3eb7bed1cce4781eb153400a70b2a321b53292  docs/cleanroom/2026-10-16-audit-3.md
cb2c9fddedfeb436d62bacd4c961850e77a34d641c4056178820ed83024e5db0  docs/cleanroom/2026-10-16-audit-4.md
0ebab23fba4368e2cbcb36610d4b11d3c53f8862fe36b887bc847c1881e70244  docs/cleanroom/2026-10-16-audit-5.md
747d35b0033a160d38dad17dee7dd13b6fd03f97069eccff94281931d7aaa703  docs/cleanroom/2026-10-16-audit-6.md
8b2ea196f58aa826c6e5e8ebadc9cc4123f693116652ccad76411744e1ace2fb  docs/cleanroom/2026-10-16-audit-7.md
e278ba30c2f7e70d10720f5f94352110c6863f7c175e888703859bd147e8688b  docs/cleanroom/2026-10-16-audit-8.md
9d5bbd75ae8c7f366efa65fff3b5c595851a172e44f6dfcbfcc25051f5144fd1  docs/provenance/DERIVATION.md
abb533357d9c2ff7bcd724b8234cb65b7bb87c876dfc41591a77b8946f7bb910  docs/provenance/MANIFEST.md
3baa7d55fdb1114d104980495bd66471181fcd6399557c708569a4f289bd8c93  docs/provenance/PROVENANCE.md
b4ded93affe5840fa32a4b4553b827030b5035b55d91da6e3c37717a349b536f  docs/provenance/inherited.md
4c82addee39eeba71566506fd67c885a4637d655aef84f8e0297c0ce1c56c254  docs/provenance/sources.md
f4d5b8c7c676e91971ba884b887acbe936812c396e6a8015383ec00ec1ab96df  docs/rfc/0001-pmarc-instrument-and-lh3-edges.md
```

The room's `MANIFEST.md` in turn pins its tools (125 files) and saved evidence (146 files) by digest.
