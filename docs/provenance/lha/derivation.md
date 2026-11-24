# LHA / LZH — how the decoder was derived

This page explains, rule by rule, how the knowledge in FultaArc's LHA decoder was obtained. A reader — person or
program — should be able to follow each rule in [`format.md`](format.md) and [`algorithm.md`](algorithm.md) back
to the observation it rests on. They should see that the rule follows from that observation and from nothing
else. Format §*n* is derived in §C*n* below; algorithm §*n* in §M*n*. The sources, their licences and what was kept
out are in [`provenance.md`](provenance.md).

## 0. How the work was done

### 0.1 The room

The decoder (`lhadec`) was written in a separate repository, the clean room `fultaarc-cleanroom-lha`, on
2026-10-15/16. The room kept a numbered **derivation log** (steps L0001–L0033) and eight dated **audits**. It saved
every probe script and every output the log quotes. Those records are kept, with the room's full history, in
FultaArc's private record; the step numbers below (L00nn) point into them.

### 0.2 What it started from

The room started from three things only (the licence of each is checked in `provenance.md`):

1. **Haruhiko Okumura's `ar002`**, a public-domain LZH archiver in C (1990). It was read and run.
2. **lhasa 0.6.0**, an ISC-licensed LHA extractor. It was built and **run as a black box**: the room gave it
   archives and recorded what it listed and extracted. Its source code was never opened. Its test corpus — 233
   real archives made by many programs on many systems — served as data; lhasa's extraction gives each file's
   plaintext.
3. **Public mathematics**: prefix codes and their canonical assignment, the Kraft inequality, LZ77/LZSS copy
   streams, adaptive Huffman coding (Gallager 1978; Knuth 1985), move-to-front lists.

Two more programs were later admitted as black-box instruments, for the methods lhasa cannot read or whose
encoder was missing: **UNLHA32.DLL 3.00** (for `-lh2-` and `-lh3-`, at the owner's instruction) and **PMarc 2.00 /
PMext 2.22** (for `-pm2-`). They too were only run.

### 0.3 The working rules

Every step followed the same rules (adopted before the first derivation step):

- **Hypothesis first.** A hypothesis and its *falsifier* — the result that would refute it — were written in the
  log before the probe ran.
- **Numbers only from saved output.** A probe is a script; its output is saved; every number in the log is copied
  from that output. "Identical" means a full byte comparison.
- **Names only from admitted sources.** A field got a name only from `ar002`'s text or from a label lhasa prints;
  otherwise it was called by its byte offset.
- **Memory is quarantined.** Remembered facts about LHA could suggest which probe to run next, but never write a
  rule. When a hypothesis came from memory, the log says so. Every such case is listed in §X.2, with its fate.
- **Claim strength.** Each result is stated as *certified on chosen input* (the room built the input and the
  oracle accepted exactly what the rule predicts), *tested on a finite set*, *tested against an instrument*, or
  *lhasa only*.

The probes used five techniques:

- **Byte mutation.** Change one byte of a known archive; see which field the oracle reports differently.
- **Value location.** Search the raw bytes for the values the oracle prints.
- **Known plaintext.** Read the compressed bytes side by side with the file they decode to.
- **Chosen input.** Write a stream of the room's own making under a method string, and record what the oracle
  outputs.
- **Encoder output.** Have an admitted encoder compress inputs of the room's own choosing.

### 0.4 Kind of room

This was a **specification-based room** run by one session, not a two-team clean room. The same session read the
admitted sources, ran the probes and wrote the specification, then wrote the C code **from that specification**. A
separate setup session chose the sources and checked their licences, reading licence lines only. The safeguards are
the source restriction, the rules above, and the audits (§X.3).

---

## Part C — the container (format.md)

### C1. Archive structure (format §1)

- **Starting point.** `ar002`'s own archives: a list of entries written one after another.
- **Probe.** A walker built from the header hypotheses of C2–C4 was run over all 233 corpus archives (L0005).
- **Observation.** Every archive that ends with a marker ends with exactly one zero byte, whatever the level of the
  entries before it. Six archives end at the end of the file without one, and eleven have bytes after it.
- **Rule.** The archive ends at a zero byte or at the end of the file, whichever comes first.

### C2. The common front (format §2)

- **Starting point.** `ar002`'s header comment and its header read and write functions, which describe the
  header `ar002` writes (lhasa calls it level 1).
- **Probe (L0003).** Every header byte of an `ar002` archive was changed in turn, with the sum byte recomputed, and
  the fields the oracle printed were recorded.
- **What the mutations showed:**
  - Bytes 2–6 changed the method string.
  - Bytes 7–10 and 11–14 changed `compressed_length` and `length`, as little-endian 32-bit values (each byte
    scales by 256).
  - Bytes 15–18 changed `timestamp`.
  - Byte 20 changed `header_level`.
  - Byte 19 changed nothing printed, so its meaning is not claimed.
- **Time encoding.** The DOS date-time packing was registered as hypothesis H3.4 and tested with five chosen dates
  (time zone UTC). Four matched exactly. The fifth differed by exactly 2³² seconds: lhasa keeps time in 32 bits.
  The packing itself held.
- **Levels 2 and 3.** No layout was assumed for them. The values lhasa reports were searched for in the raw bytes
  (L0005); there the 32-bit Unix time appeared at offset 15.

### C3. Levels 0 and 1 (format §3)

- **Level 1 (L0003).** The same mutation run fixed the rest of the level-1 layout:
  - Byte 0 is the header size, and byte 1 is the sum of the following `S` bytes. Setting byte 0 to 0, or making
    the sum wrong, made lhasa list nothing.
  - Byte 21 is the name length, followed by the name; a zero byte cuts the name short.
  - The two bytes after the name are the file CRC. The CRC's polynomial was taken from `ar002`'s source: reflected
    0xA001, initial value 0. On three chosen inputs Python's CRC equalled lhasa's printed `crc`.
  - The byte after the CRC is the system byte (`os_type`).
  - Two more bytes follow; making them non-zero made lhasa lose the entry. This fits a "first extended-header
    size".
- **Level 0 and the extra area (L0005).** In level-0 entries, the value search found the name and CRC at the same
  places as level 1. Some entries had bytes after the CRC, still inside the `S` header bytes, for example
  `55 00 …` with the Unix time at offset 31. Hypothesis H5.2 was registered: the header is exactly `2 + S` bytes,
  the bytes after the CRC are an extra area, and the data follows at once. The walker held it on every level-0
  entry.
- **The three extra-area kinds (L0006, audit 1).**
  - Each field was compared with what lhasa prints for every entry: Unix time, mode, ids, OS-9 permissions.
  - The user-id/group-id order inside the extra area had first been written into code without a measurement.
    Audit 1 caught this. A mutation probe then measured it: user id first.
  - The first byte of the area equals the printed `os_type` on all 11 non-PMarc entries. On the one PMarc entry
    the area held text, so the rule is limited to what was seen.
- **Level-1 data length (L0005).** In several archives the value at offset 7 exceeded the oracle's
  `compressed_length` (39 against 12 in one Amiga archive). `ar002`'s comment says the field includes the extended
  headers. Hypothesis H5.3 — data length = value at 7 minus the chain's total size — held on every level-1 entry.
- **Names (L0006).** Two rules came from specific corpus archives. A name ends at its first zero byte
  (`metadata.txt\0This is a comment…` in one MorphOS archive). `/` also separates directories (`SUBDIR/SUB…` in a
  regression archive).
- **Double-byte names (L0023).** The 0x5C limit was a hint from the FultaArc session, checked against Python's
  code-page tables. In CP932 52 lead bytes form a character whose second byte is 0x5C; in CP936 118; in CP950 88;
  in CP949 none. The header names no code page, so the decoder cannot tell; the limit is stated. FultaArc's repair
  is in C6.

### C4. Extended headers (format §4)

- **The chain (L0005).** One Amiga archive was read by hand: a size of 0x11 = 17 bytes holds one type byte 0x02,
  the 14 bytes `subdir\xffsubdir2`, and a 2-byte size of the next unit. From that came hypothesis H5.1: each unit
  is type, payload and next size, the size field being 2 bytes wide (4 at level 3), and a zero size ends the chain.
  A walker over every corpus archive kept in step with lhasa's listing on every entry, in count, name, sizes and
  CRC.
- **Unit meanings (L0006).**
  - **Survey.** All type bytes and payload sizes were counted with no meaning assumed. For example: 0x00 in 85
    units of 2–3 bytes, 0x41 in 16 units of 24 bytes, 0x42 in one unit of 16 bytes.
  - **Hypotheses.** Hypotheses H6.3–H6.10 were read off individual samples. Each was then checked against every
    field lhasa prints, for every entry.
  - **Group and user id.** In unit 0x51 both values were equal in every sample, so their order could not be told.
    A mutation probe wrote 1111 and 2222 into it; lhasa printed uid 2222 and gid 1111, so the group id comes
    first.
  - **Header CRC.** The CRC first matched 82 of 85 entries. The three misses were OS-9 level-2 entries. Extending
    the covered range to the data start, which includes OS-9's spare byte, gave 85 of 85.
  - **Windows times.** Unit 0x41's "100 ns since 1601" reading had first been written from memory. Audit 2 caught
    this, and the reading was then checked: on all 16 entries the second value converts to the entry's own Unix
    time.
  - **Unknown units.** Units that lhasa labels with nothing are kept raw, with no meaning claimed.
- **Symbolic links (L0006).** The `link|target` split applies to the joined path and name. This was seen in one
  Unix archive whose path itself held the `|`.
- **Data start at levels 2 and 3 (L0005).** The first hypothesis, H5.4 ("data starts at the stored header
  length"), **failed**: the walker lost step on the OS-9/68k archives. The room then measured
  (chain end − stored length) over all 67 level-2/3 entries:
  - 0 for every other writer;
  - +2 for all 6 OS-9/68k entries;
  - −1 for all 3 OS-9 entries.

  The replacement H5.4′ — the larger of the two — held on all 67 entries.

### C5. Checksums (format §5)

- **The file CRC.** The polynomial and initial value come from `ar002`'s `io.c` (public domain). They were
  confirmed on three chosen inputs (L0003) and on every static-Huffman corpus entry the room decoded (L0007).
- **The header CRC.** This is the C4 measurement: 85 of 85 entries, once the covered range runs to the data start.

### C6. Reader policies (format §6)

- **`-lk7-` (L0006).** lhasa prints `-lk7-` for two LHARK archives whose raw method is `-lh7-` with system byte
  0x20. Hypothesis H6.1 said the system byte alone causes the renaming, and it held:
  - changing only that byte to `M`, `U`, 0, `A` or `K` made lhasa print `-lh7-`, and its CRC test then failed;
  - changing bytes 15–19 did not change the name.

  So the system byte selects a different bit stream (M4).
- **Directories (L0006).** One Amiga archive stores an empty directory as `-lh0-` with an empty name and length 0,
  and lhasa lists it as a directory. Matching that one case is recorded as such.
- **Self-extractors (L0011).**
  - The search rule was tested on the corpus executables.
  - One Amiga self-extractor holds a small archive inside its program, before the real one. Breaking either header
    made lhasa list nothing, so lhasa's own rule could not be found.
  - The 16-byte continuation is the decoder's own choice. It lists both archives, while lhasa lists one.
- **Multi-volume and encryption (L0004, L0018).** No corpus file and no lhasa option shows either. UNLHA32 rejects
  its own `-k` (protect) and `-v` (multi-volume) switches as invalid. Nothing could be observed, so nothing is
  implemented.
- **FultaArc's own policies** (format §6.2) were added in FultaArc's glue code, not in the room:
  - **Detection and stub kinds.** The detection rule and the six self-extractor stub kinds come from the corpus's
    15 self-extracting files. Each stub kind is the one seen in at least one of them.
  - **Name repair.** The 0x5C repair uses FultaArc's own code-page tables, which come from unicode.org's mapping
    files.
  - **Tests.** FultaArc's tests check every rule against the same corpus (§X.4).

---

## Part M — the methods (algorithm.md)

### M1. Common rules (algorithm §1)

- **Bit order.** Most significant bit first comes from `ar002`'s bit reader. It was confirmed for every other
  method by those methods decoding correctly.
- **Copy rule.** The byte-by-byte copy that may overlap its own output is the ordinary LZ77 rule (public
  mathematics). It is confirmed by every method's corpus streams decoding exactly under it.

### M2. Stored methods (algorithm §2)

- **`-lh0-`, `-lhd-` (L0002, L0005).** On every corpus entry the lengths and CRCs agree with lhasa.
- **`-lz4-` (L0010).** Hypothesis H10.0, "data unchanged", was registered first. It held on all 3 entries, byte for
  byte.
- **`-pm0-` (audit 2).** It had first been handled with no log entry. Audit 2 caught this. Both corpus entries were
  then compared with lhasa's extraction and were identical.

### M3. Static Huffman `-lh4-`…`-lhx-` (algorithm §3)

- **Starting point.** `ar002`'s decoder text (public domain) gives the block grammar. That covers the T, C and P
  code-length tables, the zero-run codes and the canonical code assignment. It also gives its constants: 19 T
  symbols, 510 C symbols with a 9-bit count, minimum match 3, and PBIT as "the smallest integer with
  2^PBIT > NP". The grammar was restated in the room's own words.
- **Hypotheses (L0007).**
  - H7.1: `-lh4-`/`-lh5-` are `ar002`'s grammar with PBIT 4.
  - H7.2: `-lh6-`/`-lh7-` use PBIT 5. The window values in H7.2 came **from memory**, were declared as such, and
    were not adopted (see below).
- **Corpus test (L0007).** Every corpus entry was decoded with PBIT 4 and with PBIT 5:
  - all `-lh4-`/`-lh5-` entries decode with a correct CRC only under PBIT 4;
  - all `-lh6-`, `-lh7-` and `-lhx-` entries only under PBIT 5;
  - the one failure is an archive truncated on purpose, which lhasa also rejects.
- **Encoder loop (L0007).** `ar002` was rebuilt with other window parameters.
  - Variants beyond 16-bit distances (window bits 17–20) wrote streams that failed in **both** lhasa and the room's
    own decoder. That pointed to the modified encoder, so those rows were excluded as evidence.
  - Every archive written by the other variants (window bits 12–16) was tested and extracted exactly by lhasa.
  - The data was relabelled under other method strings. PBIT-4 data passed under `-lh4-`/`-lh5-` and failed
    under the others; PBIT-5 data the reverse.
- **Certification (L0008).**
  - An encoder was written **from the room's specification alone**, importing nothing from `ar002` or any decoder.
    Its 50 archives cover five methods, five inputs and header levels 1 and 2. lhasa tested and extracted all 50
    exactly.
  - So the specification by itself is enough to write streams the oracle accepts. That is certification on
    chosen input, for the features those streams use: several blocks, the single-symbol forms, all three zero-run
    kinds.
- **Windows (L0008).** Each method's window was **measured, not assumed**. One match at a chosen distance followed
  random bytes, and the largest distance lhasa accepted was found:
  - 16384 for `-lh4-`/`-lh5-` (16385 rejected);
  - 65536 for `-lh6-`;
  - 131072 for `-lh7-`;
  - 1048576 for `-lhx-`.

  Long distance lists with small distances were all accepted, so the limit is the distance and not the list
  length. The decoder's window rule (algorithm §3.4) is this measurement.

### M4. `-lk7-` (algorithm §4)

- **Starting point.** C6 showed `-lk7-` is a different bit stream. There is no admitted description and no encoder.
  The material is two LHARK archives with known plaintext (18,092 and 399,527 bytes).
- **Step 1: where it fails (L0009).** Read with PBIT 5, the T and C tables came out as complete codes. They were
  also plausible: every byte in the plaintext had a C length, and the lengths tracked byte frequency (correlation
  0.956). But the P list was an incomplete code, and decoding failed at the first symbol. So the change lies in the
  P section.
- **Step 2: finding the end of the P section.** The C decoder was started at each bit position after the C
  lengths. The plaintext's first 56 literals came out from exactly **one** position, 127 bits after them.
- **Step 3: the P list format.** Count widths of 4 to 7 bits were tried, with and without the zero-run field. Only
  one combination uses exactly the located number of bits and gives a complete code: a 6-bit count with no zero-run
  field. It came out the same in both files, independently.
- **Step 4: distances.** Hypothesis H9.2 gave each P symbol a base and a number of extra bits. A depth-first search
  against the plaintext solved them:
  - eight regular points were found, and two outliers just before the search failed;
  - the eight fit one formula, H9.3 (algorithm §4 rule 2).
- **Step 5: lengths.** With H9.3 fixed, both files went wrong at the first match symbol of 267 or 268: the length
  was too short for the repeat the plaintext shows. So long match symbols carry extra length bits.
  - A second search found the bases and bit counts for symbols 264 and up.
  - The first formula fitted to them (H9.5) was **wrong on all 8 points**. The data gave group starts
    11, 19, 35 = 3 + 2^(g+2), and the corrected formula fits every point.
- **Result.** With the three rules, both files decode completely and byte-identically, over 18 blocks.
- **Limits.** P symbols 0–31 and match symbols 256–278 were seen. Beyond 278, up to 287, is a flagged
  extrapolation of the same formula; anything further is rejected.

### M5. LArc `-lz5-`, `-lzs-` (algorithm §5)

- **Starting point.** No admitted reference. Only the generic literal-or-copy idea of LZSS (public mathematics),
  the corpus files, and lhasa as a chosen-input decoder.
- **`-lz5-` by known plaintext (L0010).** The first bytes of one LArc file were read beside their plaintext (20
  spaces, then `GNU GENERAL PUBLIC LICENSE`):
  - **Flag bits.** Reading each flag byte from the low bit, 1 = literal and 0 = two-byte copy, lines the literals
    up with the text.
  - **Copy layout.** The first copy must make 18 spaces. Position = `b0 | (b1 & 0xF0) << 4` and length =
    `(b1 & 0x0F) + 3` fit.
  - **Ring offset.** A later copy must produce `LIC` from the `L` of `PUBLIC`, at output index 35. This places
    output index 35 at ring position 0x011, so output byte `p` sits at `(p + 4078) mod 4096`.
  - **Test.** That hypothesis (H10.1) decoded all 8 corpus entries exactly.
- **The starting ring.** It was read by two independent routes:
  - from one LArc test file that uses all 4096 start bytes;
  - from lhasa, one chosen copy at each of the 4096 positions.

  The routes agree. One disagreement in the first lhasa run came from the probe, not the format: a copy that
  overlapped its own first output. With the probe corrected, the agreement is complete.
- **`-lzs-`.** Its tokens are not byte-aligned. Hypothesis H10.3 named a family of bit-level token shapes: flag
  polarity, 10–13 position bits, 3–5 length bits, either order, length +2 or +3. A scored search over the family
  left exactly one shape consistent for all 400 tokens tried; the runner-up broke after 24 tokens. The ring offset
  was solved from the first copy that lands inside the output. The starting ring was read from lhasa at all 2048
  positions: all spaces.

### M6. `-lh1-` (algorithm §6)

- **Starting point.**
  - The only public-domain program that might have covered `-lh1-` (LZHUF) was **not read**: its licence could not
    be verified.
  - `ar002` turned out to have no adaptive coding at all.
  - What remained was public mathematics (adaptive Huffman coding, Gallager 1978; Knuth 1985), 26 corpus entries,
    and a raw stream.
- **The model: from memory, declared, then tested (L0012).** Hypothesis H12.1 — 314 symbols, counts from 1, the
  update rule, rescale at root count 0x8000 — **came from memory**. It was declared as such and tested before
  anything was built on it.
  - **The falsifier.** A file whose plaintext starts with a long stretch without repeats must decode symbol by
    symbol under the model. The tree changes after every symbol, so a wrong model would fail within a few symbols.
    The first 74 literals decoded exactly.
- **The distance code: derived, not remembered (L0012).**
  - **Shape.** Hypothesis H12.2 took a prefix-coded upper part plus 6 raw bits; the table itself was unknown.
  - **Search.** A depth-first search against the plaintext fixed the codewords for upper values 0–62 over the first
    6000 symbols. They form a canonical code with Kraft sum 255/256, which leaves exactly one 8-bit codeword free.
    Canonical order assigns it to 63, an inference from completeness.
  - **Confirmation.** The value 63 was then seen in use in a corpus file.
- **Result.** All 23 non-empty corpus entries decode with a correct CRC, and so does the raw stream.
  - The rescale ran 4 times in these files.
  - Distances up to 4096 occur, which fixes the window.
  - A window starting as spaces explains files that begin with a copy.
  - UNLHA32's `-lh1-` encoder later gave further archives of the room's own inputs. All decoded exactly (L0021),
    which tests the remembered model on data the room chose.

### M7. `-lh2-` (algorithm §7)

- **Instrument.** lhasa does not read `-lh2-`, so UNLHA32 was the only tool (L0018). Two of its parts were used:
  its encoder, and its documented memory-extraction call as a chosen-input decoder.
- **First guess (L0019).** "`-lh2-` is `-lh1-`" (memory) was **falsified**: the `-lh1-` decoder ran past the data.
- **Probe 1: the initial code.** All 12-bit prefixes were decoded by the instrument, and the first output byte of
  each was recorded:
  - The result is a prefix code with Kraft sum exactly 1.
  - Its structure is 286 symbols: 256 literals and 30 match symbols, with a window of spaces.
  - The room's own `-lh1-` model, set to 286 symbols, computes **exactly** these initial codewords.
- **Literal test.** 300 literals written with that model came back exactly, exercising the update rule 300 times.
- **Distances.** Probes 2–3 set a copy after N known literals, for N from 10 to 400, and read which bytes came out.
  They showed a 64-byte block number from a prefix code, followed by 6 raw bits. They also showed a block code that
  **grows** as the output grows.
  - Hypothesis H19.3 states the growth rule as a second adaptive tree.
  - It was fitted to four observed snapshots.
  - All 24 neighbouring variants were tried; only this rule (and one with every count doubled) fits.
- **Encoder test.** 46 of the room's inputs went through the encoder. All 40 that it wrote as `-lh2-` decode
  exactly, and so does the corpus entry, with a correct CRC.
- **Chosen streams** fixed the remaining rules:
  - lengths 3–287;
  - the 8192 window (after 9000 literals, copies at 8192, 8191, … and 200 random distances all decoded exactly);
  - the literal tree's rescale.
- **The distance-tree rescale.** This needed a search. 32 candidate rules failed on all three saved seeds; that was
  first checked on one seed and re-run on three after audit 6. Forcing the rescale at chosen points then narrowed
  where it must fall. The rule found (H19.4) reproduces all three saved 309,000-byte outputs. Nine fresh streams
  that took no part in the fit — three of 3.1 MB with many rescales — all came out identical.

### M8. `-lh3-` (algorithm §8)

- **First guess (L0020).** "The static-Huffman grammar of §3" (memory) was **falsified** at the first table.
- **Encoder output (12 minimal inputs).**
  - The first 16 bits equalled the number of tokens.
  - For each of 286 symbols there is one bit, plus 4 bits when it is 1. Read as length − 1, those 4 bits gave
    Kraft sum exactly 1 in all 12 streams.
  - `A` × 200 decoded as `A`, a 198-byte match, then `A`.
- **An instrument defect.** In inputs with one copy distance per file, distances of 65 and up decoded wrongly.
  UNLHA32's own CRC check failed on those archives too. Its encoder writes broken archives when every match lies in
  one 64-byte block other than block 0. Those archives were excluded from evidence.
- **The distance region.** Chosen-input probes found two modes:
  - a flag 1 followed by 128 four-bit lengths, one per block — probed with two-block tables {0, k} for k = 1, 2, 3,
    10, 63, 64, 100 and 127, each copying from the predicted block;
  - a flag 0 with a fixed code, read off 676 patterns after a match symbol: canonical, Kraft sum 1.
- **Test.** The candidate decoder and the instrument's CRC verdict agree on all 187 archives: 186 written by the
  instrument plus the corpus entry. That includes the 30 broken ones, where both say "bad".
- **Chosen streams.** Several modes and up to 40 blocks were tested, including a block of 65535 tokens: all
  identical.
- **Edge rules.** One-symbol tables, a single non-zero position length, and incomplete and over-full codes were
  rejected by the instrument. Eleven such predictions were registered in advance in L0029; all eleven held, and
  the C decoder agrees on each.

### M9. `-pm2-` (algorithm §9)

- **lhasa alone (L0014, L0022).** The room first worked with lhasa alone, by chosen input.
  - **The list.** A 5-bit field and repeated values produced runs that only a move-to-front list explains (H14.1).
    One probe then read the whole initial order: all 256 positions, without conflict.
  - **The literal classes.** The eight classes (H14.2) came from where single bits change the output.
  - **Stop.** The header grammar resisted. Two successive hypotheses failed — one a tree-shape match that turned
    out to be coincidence — and the step stopped at its pre-set stop rule.
- **With the encoder (L0026–L0027).** PMarc 2.00 (encoder) and PMext 2.22 (the author's own extractor) were
  admitted under their authors' terms and run under a CP/M emulator. **Every encoder archive used as evidence was
  extracted byte-identically by both lhasa and PMEXT.**
  - **Header.** An input of one repeated byte set one bit at a place that moves with the byte's class. This gave
    the header shape H27.1: N, P, w, then N fields.
  - **Code lengths.** All 48 encoder headers gave Kraft sum 1. Two large real streams did not, until H27.2 made the
    code length `f + P − 1`.
  - **Distances.** Literal-only inputs showed where data starts. Inputs with copies showed a 15-bit region of five
    3-bit fields, one of which moved with the copy distance.
  - **Blocks.** A 9000-byte stream decoded only up to byte 1024 until the right bits were found. That located the
    region re-reads at 1024, 2048 and 4096, and the flag every 4096 bytes (H27.3).
  - **Copy symbols** were mapped by chosen input.
  - **Length-2 copies.** Tracing a real stream showed that they read 6 bits, not a class code (H27.4).
  - **List update.** Copied bytes must move to the front of the list: without that, all but one real stream fail
    within 250 bytes.
- **P = 0.** One encoder archive failed after a header with P = 0. A lhasa bit-flip map around that point gave
  H27.5: a single symbol with no fields. Afterwards everything matched:
  - all 5 real streams;
  - 170 encoder archives;
  - 48 fresh prediction archives, on which lhasa, PMEXT, the room's model and the C decoder all agree.

### M10. `-pm1-` (algorithm §10)

- **No encoder.** No `-pm1-` encoder could be found from an admissible source (L0028, L0032). The derivation is
  chosen-input measurement through lhasa alone, using the `-pm2-` result (list and classes) as the source of
  hypothesis shapes.
- **First attempt (L0028).** It stopped after two falsified hypotheses.
- **L0031, at the owner's request.**
  - **Unit-at-a-time output.** Orientation found that lhasa writes its output a unit at a time. That had
    confounded an earlier probe.
  - **Hypotheses H31.1 and H31.2.** H31.1: a certain symbol is a 2-byte copy — held 96 of 96. H31.2: the code
    changes after exactly 15 symbols — held 24 of 24. (A first run built its streams one symbol late; it is kept
    on record.)
  - **10-bit maps.** Maps of every 10-bit pattern after a fixed point showed the unit structure: run length,
    literals, one copy.
  - **H31.3 failed** (16 of 60). The failures pointed to three components. The run-length codes, copy lengths and
    distance classes were then **measured** with chosen streams: binary searches on marker literals, and the
    output length before marker literals.
  - **Start values.** The first 5 bits select one of 32 literal codes. Each was measured as a complete prefix code.
    The corpus README's phrase "5-bit start values" confirmed the reading; it was used as a hint only.
- **H31.4 failed** (1 of 35 streams). The failures fell into two sharp clusters (before 65 bytes; after 2624
  bytes), each tied to one component:
  - **Before 65 bytes.** A 2-byte copy there reads one 6-bit field. The earlier scan had tested 8 bits and missed
    it.
  - **After 2624 bytes.** A fourth distance class, far-3 from 2625, appears. All 32 first-disagreeing copies fit.
  - **After 216-literal runs.** No copy follows a run of the largest size.
- **Result.** With these, all 36 real and generated streams decode identically to lhasa, with correct CRCs.
  - **One flawed table.** The h = 17 table was re-measured after a flaw in the leaf rule was found.
  - **Boundaries.** Every boundary of the far-2 and far-3 widths was then fine-scanned: 65 of 65 agree.
  - **Coverage.** Every field width occurs in the real data, and no fifth class appears up to 186,224 bytes out.
- **The C table.** The C decoder's 32-entry literal table was **generated from the specification's text** by a
  script, not copied from probe output.

---

## X. From the specification to FultaArc

### X.1 Specification, then code

- **Order.** The container specification and methods §1–2 were written before any C code (L0008). The library was
  written from the two specification files (L0011). Each later method followed the same order: spec section first,
  then the C function written from it.
- **No translated tools.** The Python decoders used for testing hypotheses were instruments; the C was not
  translated from them.
- **Same evidence.** Each C decoder was then checked against the same saved evidence as its spec section.
- **Policies.** Choices that are not format facts are marked "policy" in the code.

### X.2 Hypotheses that came from memory, and their fate

| Hypothesis | Fate |
|---|---|
| The setup brief's list of methods in the corpus | partly wrong; the scope was set from data (L0004) |
| Window bits 15/16 for `-lh6-`/`-lh7-` | not adopted; windows measured (M3) |
| The generic LZSS literal/copy form | only the shape; every bit and offset read from data (M5) |
| The `-lh1-` adaptive model | confirmed by 23 entries and a raw stream; the distance code derived, not recalled (M6) |
| "`-lh2-` is `-lh1-`" | falsified (M7) |
| "`-lh3-` is the static grammar" | falsified (M8) |
| Windows times since 1601 (written into code from memory) | caught by audit 2, then checked on 16 entries (C4) |
| User/group id order in the level-0 extra area (written without data) | caught by audit 1, then measured (C3) |

No hypothesis for `-pm2-` or `-pm1-` came from memory.

### X.3 Audits

Eight audits, about every ten steps and at each close-out, looked for:
- rules resting on memory;
- coincidences taken as proof;
- counts not taken from saved output;
- any use of a forbidden source.

Every defect found was corrected in place, with a note. **No use of disallowed material was found.** The ones that
bear on where knowledge came from are in the table above.

### X.4 Into FultaArc, and FultaArc's own check

The room's five library files were copied into `src/lha/lhadec/` **byte-identical**, from its final state (commit
`5792dae`). `make test` checks them against the room's SHA-256 manifest (`tests/tools/lhadec-pin.sh`), and they are
compiled unmodified: a small header maps their allocator and symbol names.

FultaArc then tests the whole reader against the same black-box oracle, independently of the room:
`tests/manifests/lha.txt` lists 209 corpus archives with 252 files. Every file's size and CRC-32 are those of the
pinned lhasa binary's extraction, and FultaArc must agree on every file of every listed archive. The remaining
corpus archives are checked one by one in `tests/test_lha.c`, each for the reason it is not in the manifest (for
example the two `-lh2-`/`-lh3-` archives lhasa refuses).
