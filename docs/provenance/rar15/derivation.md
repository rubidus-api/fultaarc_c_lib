# RAR 1.4 / 1.5 — how the decoder was derived

This page explains, rule by rule, how the knowledge in FultaArc's RAR 1.4 / 1.5 decoder was obtained. A reader —
person or program — should be able to follow each rule in [`format.md`](format.md) and [`algorithm.md`](algorithm.md)
back to the observation it rests on, and see that the rule follows from that observation and from nothing else.
Format §*n* is derived in §C*n* below; algorithm §*n* in §M*n*. The sources, their terms and what was kept out are
in [`provenance.md`](provenance.md).

Observations are cited by the room's step ids (`Snnnn`) and hypothesis ids (`Hnn`); the room's log is append-only
(a later line may refute an earlier one, never edit it), so those ids are stable. The raw log is kept in the
private record and is not published; these citations point into it.

## 0. How the work was done

### 0.1 The room

The decoder was written in a separate repository, the clean room `fultaarc-cleanroom-rar15`, by a single Claude
Code session beginning 2026-10-02. The room kept a numbered **observation log** (steps S0001 onward), committed
often so the history shows the order of discovery, and saved every input generator, probe script and the record of
every oracle run (commands and the SHA-256 of all inputs and outputs). The step ids below point into that record.

### 0.2 What it started from

By the owner's decision of 2026-10-02, the format was to be worked out **from the bottom, from observation alone**,
because an earlier attempt had begun from a model recalled from training and only then checked. The room started
from exactly three things (each checked in `provenance.md`):

1. **RAR 1.55 for DOS**, run in a DOS emulator as the only oracle — used both as a **compressor** of inputs the
   room chose and as a **decoder oracle** for archives the room built. Only what it wrote was read (archives,
   extracted files, console text, and `RAR.DOC` for its commands and switches).
2. **General compression knowledge**, marked `src=general` in the log and used only for the *shape* of a hypothesis,
   each then tested against data: LZ77/LZSS matches, canonical prefix codes, the Kraft inequality, frequency-ordered
   lists, running averages, move-to-front lists; and the public standards CRC-32 (as in zlib) and MS-DOS date/time.
3. Nothing else. No text or code describing any RAR format, no other program on RAR files, and no look inside
   `RAR.EXE`.

Later, by the owner's decision of 2026-10-07, RAR 1.40.2 beta (writer and reader of the older format) and
RAR 2.50 / 2.02 for DOS (verdict-only judges) were added, and public test inputs and real 1990s archives were
fetched; of public descriptions only the programs' **manuals** were read. All of this is in `provenance.md`.

### 0.3 The working rules

- **Hypothesis first.** Every hypothesis was written in the log with the observations it rests on (`src=obs:<step
  ids>`) or marked `src=general`, before the probe that tested it ran. Numbers in the log are copied from saved
  output; "identical" means a full byte comparison.
- **Recall of RAR is forbidden — not even to test.** This room's rule was stricter than a name-it-and-test rule: if
  a thought about RAR's internals arose, it was logged as one line "recall noticed, set aside" **without its
  content**, and it chose no probe and wrote no rule. The slower path the data forced was preferred. The complete
  list of these, and the memory handling, is §X.2.
- **Claim strength.** Each rule is stated as certified on chosen input, tested on a finite set, tested against a
  judge, or seen only in RAR 1.55's output, as the case is.

### 0.4 The probe techniques

Differential pairs (two runs differing in one input byte or switch); search for known values (sizes, the input's
CRC-32 in both byte orders, times, names); build-it-and-ask (an archive made from a hypothesis, confirmed or
refuted by RAR's listing and extraction); oracle probes (a bit stream the room built, wrapped in an archive,
extracted by RAR — the output length tells where a step ends, the bytes tell what it was); sweeps (one field
through all its values); bit flips (flip one bit, see which output position moves); prefix trees (after a known
prefix, every continuation of *n* bits, drawing the prefix code expected there); signature probes (a step whose
result reveals one hidden state, counting how many extra steps flip it to measure a hidden counter); the model as
code compared in bulk against RAR both ways, with every first difference located and studied; offline fitting with
a separating experiment designed next; and **freeze-and-holdout** (§X.3).

### 0.5 Kind of room

A specification-based room run by one session: the same session ran the probes, wrote the specification, and wrote
the C code **from that specification**. The safeguards are the source restriction (§0.2), the rules above, and the
holdout and real-archive gates. The decoder was written from the spec, not translated from the Python reference
model used to test hypotheses.

---

## Part C — the container (format.md)

### C1. The two container families (format §1)

- **Probe.** `S0003` built eight tiny archives differing in one property each; the marker and first block fall out
  by comparison. `S0383` repeated this for RAR 1.4.
- **Observation.** RAR 1.5 archives begin with the 7-byte marker `52 61 72 21 1a 07 00` (`S0003`; byte 6 is checked
  by RAR, log L93); RAR 1.4 archives with `52 45 7e 5e` (`S0383`).
- **Rule.** A reader tells the two apart by these bytes.

### C2. Blocks and the block head (format §2)

- **Starting point.** `ar`-style containers are a chain of records; `src=general`.
- **Observation (`S0003`).** Every RAR 1.5 block starts with the same 7 bytes; differential pairs name them
  `u16 check`, `u8 type`, `u16 flags`, `u16 size`, with `size` counting the whole header. The first block is type
  0x73, a file block is 0x74 and its packed data follows the header directly (the next block starts after the
  data). Types a reader does not know are skipped by `size`, and by the `u32` at offset 7 when flag 0x8000 is set
  (tried on types 0x70, 0x72, 0x76, 0x77, 0x7a, 0x7f, 0x00: they are passed over).
- **Rule.** `format.md` §2.

### C3. Header checks (format §3)

- **Hypothesis / falsifier (`S0004`).** The two unexplained head bytes are a checksum over the header; the
  falsifier is that no standard checksum over any candidate range reproduces them.
- **Observation.** Trying the standard checksums over candidate ranges, the low 16 bits of CRC-32 match: for a file
  block from the type byte to the end of the name, for the main block over bytes 2..12. `S0005`: an archive built
  from the rule is accepted; a wrong check is reported ("header broken") and the block processed anyway.
- **Rule.** `format.md` §3; reporting-and-continue is a reader policy (`format.md` §17).

### C4. The main block (format §4)

- **Observation.** `S0003`: the first block is 13 bytes, six of them always zero. `S0200`–`S0201` built archives
  that are multi-volume, locked, solid, commented, to read the main flags; `H57` (solid), `H58` (volume), `H60`
  (comment), `H92` (authenticity). Other flag bits and the trailing bytes change nothing RAR lists or extracts.
- **Rule.** `format.md` §4.

### C5. The file block (format §5)

- **Observation (`S0003`, `H3`; confirmed by archives built from it).** Differential pairs place every field: two
  sizes, the zero byte, the file CRC-32 (`H5`, confirmed on chosen inputs), DOS time/date, the version byte 0x0f,
  the method byte, name length, attributes and the name. `S0006`: `-m1`..`-m5` write method bytes 0x31..0x35, and
  swapping the method byte between archives changes nothing in extraction, so one decoder serves all (`H8`). `H5`,
  `H6`: the CRC is compared and reported but the bytes are written either way. `H59`: a directory is a block with
  attribute 0x10 and zero sizes/CRC.
- **Rule.** `format.md` §5.

### C6. Stored files (format §6)

- **Observation (`S0006`, `H8`).** Method 0x30: the output is the packed bytes unchanged; packed size equals
  unpacked size in every stored file RAR wrote.

### C7. Multi-volume archives (format §7)

- **Observation (`S0200`–`S0201`, `H58`; `S0366`).** Volumes are named `NAME.RAR`, `.R00`, `.R01`, … A split file
  has a block in each volume with the same name and full unpacked size; the parts concatenated in volume order are
  one packed stream, and the split may fall anywhere (stored files split over four volumes, a 100 KiB file over
  many, solid archives over volumes all decode so). The continued parts carry CRC 0xffffffff, the last part the
  whole file's CRC. `S0366` added 8 real volume sets (5084 entries), all right.
- **Rule.** `format.md` §7; taking volumes in given order, not by name, is a reader policy.

### C8. Solid archives (format §8)

- **Hypothesis / probe (`S0200`–`S0201`, `H57`).** In a solid archive the second file continues the first's decoder
  state; two-file solid archives whose second file needs a known piece of state measure exactly what carries. The
  resets at a file start were each measured: pending flag bits dropped (`H67`), the literal run cut to ≤ 16 so
  literal mode ends (`H74`), and — found later by a soak trial — the repeat count reset to 0 (`H87`, `S0343`). A
  hand-built stored entry inside a solid archive showed RAR keeps the state and does not window its bytes.
- **Rule.** `format.md` §8.

### C9. Self-extracting archives (format §9)

- **Probe (`S0369`–`S0374`).** A front of arbitrary bytes is refused; RAR's own module is accepted. Inverting
  windows of the module and halving them left six bytes that matter: `MZ` at offset 0 and `RSFX` at offset 28.
  Random fronts carrying those bytes gave the offset range (39..32787) that RAR searches for the marker's four
  bytes (`H91`). The two committed `.exe` test fixtures are the room's own constructions of this shape (the marker
  bytes planted in an otherwise empty front), not RAR's module (`provenance.md`).
- **Rule.** `format.md` §9.

### C10. Comments (format §10)

- **Observation (`S0367`–`S0368`, `H60`, `H90`).** RAR's `cw` writes the comment text to a file, giving the
  reference text for 27 comments of 1–20000 bytes; the inner block (type 0x75) decodes with a **fresh** decoder
  state. Hand-built blocks showed which method bytes RAR accepts and that a wrong check gives "Comment broken -
  skipped". File comments (file flag 0x0008) sit after the name; RAR writes them stored and shows only stored ones.
- **Rule.** `format.md` §10.

### C11. Authenticity information (format §11)

- **Observation (`S0376`–`S0377`, `H92`, `H99`; `S0403`–`S0404`).** A survey of block types and main flags in ~800
  real archives against what RAR prints found main flag 0x0020 and a type-0x76 block at the end, pointed to by a
  `u32` at main-header offset 9. Bit flips showed it covers every byte before it. What the
  room observed inside the block is not published (FultaArc does not support it). The shareware cannot write such a
  block and RAR answers only Ok/Failed, so it can be recognised, not verified.
- **Rule.** `format.md` §11.

### C12. RAR 1.5 encryption (format §12)

- **Observation (`S0378`–`S0380`, `S0396`–`S0397`, `S0401`–`S0402`).** The `fultaarc-cleanroom-rar15` room studied
  entries made with `-p` and closed the cipher as **not obtainable by observation** under its limits (no look inside
  RAR, manuals only); its partial notes are kept in FultaArc's private record. The RAR 1.5 keystream generator was
  subsequently derived in full by a separate clean room (`fultaarc-cleanroom-rar15-enc3`); see [`enc3/`](enc3/README.md)
  for the derivation guide, research report, and specification.

### C13. Blocks of other writers (format §13)

- **Observation (`S0376` survey).** Real archives with version-15 entries hold type-0x77 blocks (another OS's
  attributes; 188 of 188 decode to their CRC as a section-2 stream from a fresh state) and type-0x78 `Protect!`
  blocks with main flag 0x0040. RAR 1.55 for DOS never writes them and passes them over; FultaArc does the same.

### C14. The RAR 1.4 container (format §14)

- **The decisive first archive (`S0382`, `H94`).** `RAR14 a T A.TXT` on 100 bytes of `A` produced a 9-byte body
  byte-for-byte identical to the stream RAR 1.55 writes for the same input (known from `S0009`). Hypothesis `H94`
  was made at once: **the older format keeps its own container but its compressed data is the stream of
  `algorithm.md`, unchanged.** Everything after was to confirm `H94` and read the container, not to work out a
  second compressor.
- **The container (`S0383`).** The differential-pair method of `S0003` was repeated on tiny RAR 1.4 archives: the
  4-byte marker, a 7-byte main header (`u16 size`, flag byte), a 21-byte file header (two sizes, a 16-bit sum,
  header size, DOS time/date, attributes, flags, the constant 2, name length, method), then the name, then the
  data. The version byte must be 2; RAR 1.40.2's messages for other values ("Old method", "unknown method",
  "Broken archive") fixed the version check.
- **Rule.** `format.md` §14.

### C15. RAR 1.4 comments and self-extractors (format §15)

- **Observation (`S0389`, `H95`; `S0400`, `H98`; `S0391`).** 10 archive comments of 1–30000 bytes against both
  readers' `cw` gave the comment layout; with main flag 0x10 the comment is a section-2 stream enciphered under
  fixed keys (§M16). File comments (`H98`) were read by driving RAR 1.40.2's interactive `cf` on a virtual display
  (general program automation, nothing about any format). `S0391`: RAR 1.40.2's self-extractor front carries `MZ`
  at 0 and `RSFX` at 28, as RAR 1.55 expects.
- **Rule.** `format.md` §15.

### C16. The RAR 1.4 cipher (format §16)

Derived in §M16 (it is a mechanism, not a container field); `format.md` §16 states where it applies.

### C17. Reader policies (format §17)

Each policy in `format.md` §17 is either RAR's observed behaviour taken as the reader's (wrong-check continue;
truncated-archive handling — the complete entries extracted and the cut one failed, which RAR 1.55 does too,
`S0353`; log L2867–L2870) or a decision the format does not fix (output cut at the unpacked size; refusing other
version bytes and encrypted RAR 1.5 entries; the ~1000× packed-size allocation bound; taking volumes in given
order). None adds a format claim.

---

## Part M — the compressed stream (algorithm.md)

### M1. Common rules (algorithm §1)

- **Bit order (`S0009`–`S0011`, `H11`).** RAR's stream for 100×`A`, decoded with the unpacked size swept 1..100,
  gives output-length jumps that locate the steps; bit flips map bits 43..67 to output positions 95..99, five bits
  each — so bits are read from the top of each byte. Confirmed by hand parses.
- **Copy (`S0009`, `H9`; `src=general`).** The byte-by-byte copy that may repeat its own output is the ordinary
  LZ77 rule; confirmed by every stream decoding exactly under it. `d = 0` means 65536 (`S0202`-range; the largest
  distance seen).

### M2. Canonical codes and tables (algorithm §2)

- **Starting point.** Canonical prefix codes; `src=general` (observed to be canonical, `S0025`-range).
- **Observation (`S0014`–`S0016`, `S0069`, `S0149`).** Prefix trees from the start state, and RAR's code for a first
  byte `b` (`b = 0..255` then a run), draw each table directly: T0 as `5×4, 6×40, 7×16, …` (`H14`), then T1, T2, T3,
  TL and the length codes R and C by the same method, each confirmed to Kraft sum 1. Every index of both length
  codes was later decoded by RAR (log L2661–L2663). Index 256 (the all-ones code) is not a list position: RAR's
  output after it fits no position and RAR's own archives never hold it, so it is an error.

### M3. Count lists (algorithm §3)

- **Hypothesis / probe (`S0017`–`S0019`, `S0029`, `S0041`–`S0046`, `H17`, `H25`).** After decoding position `r`,
  probes that read single positions show a list ordered by use counts with one exchange per use (`H17`), and a
  reset when a count passes a limit (`H25`); the exchange is to the first position whose symbol has a **strictly**
  lower count. The three lists' start orders, counts and limits were each measured (literal, flag, block). The Q
  distance list's one-place-move update is `H29`.

### M4. Decoder state and initial values (algorithm §4)

The state table of `algorithm.md` §4 is the union of the quantities established in M5–M15; each initial value is the
one measured at a fresh decoder (for example `S = 53`, `H23`; `kv = 288`, `H80`; `pfar = true`, `H53`/`H62`). What
carries across a solid file and what resets is C8.

### M5. The main loop (algorithm §5)

- **Observation (`S0009`–`S0011`, `H9`).** A step is written whole: the output-length sweep shows the stream for
  100×`A` is a 1-byte step, a 94-byte step and five 1-byte steps. Decoding runs until the unpacked size is reached
  and completes the step that reaches it.

### M6. Flag bits and the switch to literal mode (algorithm §6)

- **Observation (`S0048`, `H26`).** The field before each run of steps is a byte of flag bits, itself coded by a T1
  code over the flag list: `0` literal, `10`/`11` the two non-literal kinds.
- **Literal mode (`S0053`–`S0055`, `H30`).** Random streams of literals and length-2 matches failed only at a
  flag-byte boundary after ≥ 17 literals; the switch to literal mode happens there, on the first flag bit of a step,
  and the kind bit never triggers it.

### M7. The swap and its counter (algorithm §7)

- **The swap itself (`S0101`–`S0125`, `H44`).** RAR's text stream failed at output 737 under an early model;
  thousands of probes there showed RAR reading a P with the old length layout but **other flag bits** — the flag
  codes of literal and P had traded places (`H44`). Two earlier hypotheses (`H42`, `H43`, "a P length layout
  change") were withdrawn: they had been this swap seen through probes that still used the old codes.
- **The counter (`S0126`–`S0167`).** A probe literal of a byte not in the window tells whether `0` still means
  literal; sweeps over the number and length of steps before it built a counter that literals lower and P steps
  raise, halved every seventh step (`H45`), with kind-A/kind-B roles in both states (`H47`).
- **To the unit (`S0295`–`S0311`, `H80`; `S0336`).** Two failures no half-unit counter explained were resolved by
  measuring the "need" (extra steps to a swap) from exact states: the counter keeps two more bits than the early
  unit (`U = 144`, checks at 144 and 153/145). With the phase-0 rule of the counted swap-back (`H79`), all 9671
  flag-level streams and 1008 exact need-values fit. An exhaustive search over step sequences showed which reset
  values matter: some are pinned only within ranges on which the alternatives behave identically. The rounding of
  the halving is floor, and ceiling in the swapped state (`H85`).

### M8. Literal step (algorithm §8)

- **Observation (`S0020`–`S0040`, `H23`).** Fingerprinting the literal table along chosen histories, then "rulers"
  (one value then a constant run, read where the table flips), gave the score update `S += r - 53 - (S >> 8)` and
  the exact switch levels between TL, T0, T1, T2, T3.

### M9. Q step (algorithm §9)

- **Observation (`S0049`–`S0058`, `S0082`–`S0094`, `H27`–`H41`).** Prefix trees of the `11` kind over a known window
  give the Q code lengths 2..10, the distance from a list of 1..256 updated by one-place moves (`H27`–`H29`), a
  repeat code and the family `100` that reuses recent distances (`H32`, `H33`, `H37`). Two layouts A/B are chosen by
  the score `qs` (`H40`, `H41`). After two repeats in a row every further Q-kind step takes one extra bit
  (`S0187`–`S0191`, `H55`).

### M10. P step (algorithm §10)

- **Length (`S0056`, `S0060`, `S0067`; layouts `S0143`–`S0151`, `H48`).** The P length is unary (layout A), then
  length code R (B), then C (C), chosen by the score `ky`.
- **Distance (`S0072`–`S0100`, `S0168`–`S0180`, `H49`, `H52`, `H75`).** The high part of the distance goes through
  the block list (`H49`); the position code is one of TL/T0/T1 chosen by the score `kd` (`H52`), with thresholds
  1799 and 10537 (the latter pinned from both sides, `H75`); the distance is `128*blk + 7 raw bits`. The length
  corrections (`d <= 256` → +8; the far +1) are M15. The log's L2676–L2678 drove the scores to their ends and found no
  further table or layout.

### M11. R step and the far match (algorithm §11)

- **Recent distances (`S0069`–`S0071`, `H37`).** An R step reuses one of the last four distances with a new length
  from code R; a selector of up to three bits picks which. The push rule (the used distance goes to the front, the
  entry it came from stays) is `H69`, which replaced an earlier move-to-front model.
- **The far match (`S0197`–`S0198`, `S0206`–`S0207`, `H61`).** Inputs with repeats beyond 32767 first got a reading
  (`H56`) that holdout 1 refuted; it was replaced by `H61`: one R code (selector 0, length index 255) is an escape
  that outputs nothing and turns on a far-match code for the rest of the file (`1011` becomes a far match: length
  `5 + C`, distance `32768 + 15 bits`). The far distance is not pushed to `recent` (`H71`).

### M12. Literal mode (algorithm §12)

- **Observation (`S0025`, `S0181`–`S0186`, `H22`, `H54`).** After many literals the flag bits stop (`H22`); in
  literal mode index 0 is an escape, then one bit is either the end of literal mode or a small match. That small
  match is invisible to the rest of the state: `H86` (`S0327`–`S0330`) removed the model's earlier treatment of it
  as `last`, and `recent`, the balance and the scores are likewise untouched (streams with hundreds of such copies
  decode).

### M13. Copy (algorithm §13)

The copy rule is M1; `algorithm.md` §13 only gathers it.

### M14. Scores (algorithm §14)

Each score's update and thresholds were measured where it is used: `S` (M8), `qs` (M9), `ky` and `kd` (M10). The
summary table in `algorithm.md` §14 restates those. The thresholds were pinned to the unit by streams built to sit
on each side of each one.

### M15. The +1 flag and the balance (algorithm §15)

This was the longest thread. In order:

- **The +1 (`S0075`–`S0077`, `S0174`–`S0175`, `H53`).** Matches beyond 8192 are one byte longer.
- **Not always (`S0210`–`S0221`, `H62`).** With many low-position literals the +1 stops; a condition on `S`, pinned
  at `-2775` (`S0221`).
- **A lasting state (`S0231`–`S0276`, `H72`).** Forcing the probe both ways along RAR's own streams showed the +1 is
  a lasting on/off state that ends when a **balance** of P length codes falls; the balance's step per coded length
  was measured for every value 0..255 (`H77`).
- **Unconditional far (`S0285`, `S0292`–`S0293`, `H78`).** From distance 32512 the +1 is unconditional.
- **One threshold, bounded balance (`S0312`–`S0326`, `H81`–`H85`).** What looked like two thresholds is one applied
  to the balance **before** the step's own change (`H81`); the balance never rises above 0 (`H82`) nor below -255
  (`H83`); the flag also follows the balance while `S > -2775` if the P was read in layout B or C (`H84`).
- **The one-distance shifts (`H88`, `H89`).** Two real archives (`S0345`-range) each had a wrong +1 thousands of
  steps after the flag was last visible. Replaying the last steps in a matched state and sweeping the distance
  located them: at `d = 8193` the match takes its +1 but the balance still counts -1, at 8194 it counts +1
  (`H88`); the same shift at the other limit, `d = 32512`/32513 (`H89`). These act at a single distance value each;
  generated inputs did not reach them in 1.6 GB of soak data, real archives did within megabytes.

### M16. The RAR 1.4 cipher (algorithm §16)

- **Read bare (`S0387`–`S0389`, `H96`).** Stored files of zeros, 0xff bytes and a counter under a password showed
  the cipher bare: ciphertext minus plaintext (mod 256) was the same additive stream whatever the data, fixed by
  the password. 97 key streams were collected. For a one-letter password the stream was `(i+2)^2 · c mod 256`;
  finite differences of each stream were a constant second difference (a quadratic in position — three bytes fix
  it). Reading those off the one- and two-letter table gave the key set-up (sum, XOR, rotate-accumulate) and the
  two-register step; the model reproduced all 97 streams, and the reader decodes the format's encrypted entries.
  The archive comment proved to be the same cipher under fixed keys (`H95`). This is the older format's own cipher;
  it is a different mechanism from the RAR 1.5 cipher (C12), which is not read.

### M17. How the stream was tested (algorithm §17)

- **Freeze and holdout.** Each freeze recorded the SHA-256 of the model and C files and had a git tag; each of 21
  holdouts used an input-seed prefix never used before, RAR's own extraction as the reference, and no decoder change
  afterwards (its archives then became development data). The holdouts rose 27→56 of 56 and 125→132 of 132 as the
  rules of M7, M10, M11, M15 were found.
- **Soak and real archives.** Thousands of random archive shapes through RAR and the C decoder (5000 generated +
  2500 real-file-slice cases, 69,944 entries, 2.67 GB: 0 wrong); and 815 real archives of 1994–1997 (13,459
  entries, 432.9 MB) judged by the CRC-32 in their headers and by RAR 1.55's own verdict, with RAR 2.50 / 2.02 as
  further judges. 18 entries in 17 archives fail; RAR 1.55 fails the same 18 and no other (damaged/truncated), and
  both later RAR generations agree. Under the RAR 1.4 container the same stream decoded 2000 + 800 generated and 9
  real archives unchanged (`H94`).

---

## X. From the room to FultaArc

### X.1 Specification, then code

The specification was written from observations before the C code; the C decoder (`src/unpack15.c`) was written
from the specification, not translated from the Python reference model used to test hypotheses. The archive reader
(`src/rar15arc.c`) puts the container of `format.md` around it. Each C file was then checked against the same saved
evidence as its specification section (1264 of 1264 saved entries identical between model and C).

### X.2 Memory, and its fate

This room's rule forbade even testing a recalled fact (§0.3). The whole memory input to the final decoder is
therefore:

| Item | Fate |
|---|---|
| The earlier RAR 1.5 work (kept in a private sibling) | **forbidden by the brief and never read**; the format was redone from observation by the owner's 2026-10-02 decision |
| A thought about RAR's internals while reading the brief | logged "recall noticed, set aside" (log line 17, step S0001); no content, no hypothesis, no probe |
| A thought while looking at some rows | logged "recall noticed, set aside" (log line 189, step S0011); likewise |
| A thought before the encryption experiments | logged "recall noticed, set aside" (log line 3136, before S0378); likewise |
| General compression knowledge (LZ77, canonical codes, count lists, running averages, CRC-32, DOS time) | used only for hypothesis shapes, each marked `src=general` and tested against data |

No rule in `format.md` or `algorithm.md` was written, tested or chosen because of a recalled fact; every rule cites
the step ids of its observations. That is the whole list.

### X.3 Dead ends worth knowing

- `H42`/`H43` (a P length layout driven by a counter): an artefact of probing with codes that had swapped — when a
  probe "changes meaning", check the probe's own coding first.
- The "third far P" trigger (`H46`) and a "ky trigger" for the swap: both dissolved into the block list and the
  length layouts.
- A "cap" on the counter at a swap-back: a calibration run showed the tests failed with no swap-back at all; the
  real cause was the counter's resolution (`H80`).
- Two thresholds with hysteresis for the +1 flag (`H77`'s first form): one threshold on the value before the step
  (`H81`).
- Fitting without a separating experiment produces families of constants that all fit (36727 combinations at one
  point); each time, progress came from a measurement designed to split the family, not from more fitting.
- Void or discarded runs were each logged with the reason (early memory-contaminated oracle runs `S0008`–`S0010`,
  and others).

### X.4 Corrections caught in the record

The log is append-only, so corrections are visible: an early oracle that read leftover memory when a probe ended
too soon (`S0008`–`S0010`, rebuilt to pad every stream); a C port whose code tables were shifted by one length,
caught by the differential test before the first freeze; a distance-0 indexing bug in the model, fixed to 65536
with no corpus result changed; several experiment scripts that failed to start, recorded as void. Every
hypothesis that was withdrawn or replaced is listed in the room's provenance with its successor.

### X.5 Into FultaArc

The decoder and archive reader are MIT-licensed and carry the SHA-256 and the record anchors in
[`provenance.md`](provenance.md), so FultaArc's copy can be checked byte-for-byte against the room's final state and
the private record behind these pages.
