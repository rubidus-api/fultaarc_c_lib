# CAB Quantum — how the decoder was derived

This page explains, rule by rule, how the knowledge in FultaArc's Quantum decoder was obtained. A reader — person or
program — should be able to follow each rule in [`format.md`](format.md) and [`algorithm.md`](algorithm.md) back to
the observation it rests on, and see that the rule follows from that observation and from nothing else. Format §*n*
is derived in §C*n* below; algorithm §*n* in §M*n*. The sources, their terms and what was kept out are in
[`provenance.md`](provenance.md).

## 0. How the work was done

### 0.1 The room

The decoder was written in a separate repository, the clean room `fultaarc-cleanroom-quantum-lzx`, on 2026-10-15/16.
The room kept a numbered **derivation log** (steps Q0001–Q0052; there is no Q0042 — the number was skipped when a step
was renumbered) and five dated **audits**. Every query it sent and every answer it got is saved, so each observation
can be replayed from the record. Those records are kept, with the room's full history, in FultaArc's private record;
the step numbers below (Q00nn) point into them.

### 0.2 What it started from

Microsoft never published Quantum. The only public descriptions are inside reverse-engineered implementations that
are copyleft or of unverified licence, and the room's brief forbade reading any of them (provenance §4). So the room
started from:

1. **The public Cabinet container**, inherited and pinned (FultaArc's `src/cab/cab.c` at `9e2881d`, SHA-256 recorded):
   the folder and data-block framing, `typeCompress` type 2 = Quantum, and nothing about the inside of a Quantum
   body. The meaning of `typeCompress`'s upper bits was explicitly *not* inherited.
2. **Public mathematics**: LZ77/LZSS, prefix codes, arithmetic coding (Witten, Neal & Cleary 1987), adaptive models.
3. **An instrument**, chosen at the owner's decision (Q0001, below).

### 0.3 The instrument

The setup found no Quantum encoder and no Quantum cabinet (Q0001): Windows 11's `makecab` refuses
`CompressionType=QUANTUM` exactly as it refuses a made-up name; 45 cabinets in Windows' own folder hold only MSZIP and
LZX. But Windows' `extrac32` (over `cabinet.dll`) answered a type-2 folder with a garbage body with "Failure
decompressing" — the same answer MSZIP and LZX folders with garbage bodies get — and an invalid type 5 with "Unknown
compression type". So type 2 reaches *something that consumes the body*.

The owner chose **option 4**: use that reader as a **chosen-input oracle**. The room builds a cabinet around a body
of its own choosing (`tools/cabwrap.py`, container framing only), runs `extrac32` on the owner's licensed Windows 11
virtual machine, and records the exit code, the message and the bytes it writes. Nothing inside Windows was read,
disassembled or traced. An oracle answers "does this body decode, and to what?" — never "how would an encoder write
this?" — so every rule had to be found by asking questions and checking the answers.

Later two more Microsoft sources came in, both used as black boxes too (provenance §3): **Microsoft-made Quantum
cabinets** from public floppy images of Windows 95, Plus! and Internet Explorer 2.0 (Q0049), and **Diamond**
1.00.0530, Microsoft's 1995 encoder, which accepts `CompressionType=QUANTUM` (Q0051).

### 0.4 The working rules

Set in the room's brief before the first step:

- **Trace or quarantine.** A step cites only earlier steps, the pinned container facts or named public mathematics. A
  "must be X" without such a trace is quarantined and not built on.
- **Direction check.** Every step ends by saying whether its result confirms or contradicts the running hypothesis.
  A wrong turn is retracted in its own line, never quietly removed.
- **Audits** every ten or so steps look for recalled knowledge, coincidence read as proof, and hidden dependence on an
  outside answer.
- **Finite is not universal.** A rule that passes on the inputs tried is *tested*, never *proven*.
- **No legal conclusions** in the record: licence questions are named and left to the owner.

The techniques, all on the oracle unless said otherwise:

- **Acceptance search.** Which bodies decode at all, under which `typeCompress` values.
- **Census.** Fix everything but the first bytes the decoder will read next, sweep those 16 bits over a range, and
  record what comes out: the boundaries between outputs are the coder's interval boundaries.
- **Block-end search.** Vary `cbUncomp` and see where decoding stops failing: a symbol may not cross the end, so the
  smallest accepted end measures the symbol's length.
- **Chosen plaintext.** Encode a plaintext with the room's own encoder, built from the rules found so far
  (`tools/acenc.py`), and check that the oracle returns exactly that plaintext.
- **Offline fits.** Score candidate rules against everything recorded; a fit is a hypothesis until a fresh query
  confirms it.

---

## Part C — where the data sits (format.md)

### C1. The window parameter X (format §1)

- **Every body failed at first.** With `typeCompress = 0x0002`, every body that had to produce output failed: 691
  queries of every short kind (all 256 one-byte bodies, random, zero and 0xFF bodies, two-block folders); only
  `cbUncomp` 0 succeeded, which needs no decoding (Q0002, Q0003). Two readings were open: a stub
  behind the error, or a precondition in the upper bits.
- **The upper bits gate acceptance.** 180 queries with single upper bits and combinations set over large random
  bodies: 6 accepted, all at `typeCompress` 0x0F02, 0x1002, 0x1502, 0x1572 or 0x15F2, with deterministic, structured
  output (Q0004). The "stub" reading was retracted: a working decoder is behind the error.
- **Bits 8–12 are the parameter; 4–7 and 13–15 do nothing.** All 16 values of bits 4–7 and each of bits 13–15 on top
  of 0x1502 gave output identical to 0x1502 (Q0005). Acceptance by `(typeCompress >> 8) & 0x1F`: 0 of 16 random
  bodies at X = 0, 5, 6, 7, 8, 9, 22, 23, 25 and 31; 7–11 of 16 at every X from 10 to 21 (Q0006, Q0007). So **X is
  valid from 10 to 21**.
- **What X means** was held as a hypothesis ("window") until distance tables were measured against it (M5): their
  slot counts follow 2·min(X, cap) at every X measured (Q0021, Q0032).
- **Diamond's field.** Diamond wrote `0x0072 | X << 8` for every X = 10..21 (Q0051); the shipped cabinets declare
  `0x1572` (Q0049).

### C2. Data blocks (format §2)

- **The model carries over** (Q0009). The same accepted body twice in two blocks fails after the first; an accepted
  body followed by another gives the second block an output different from that body decoded alone. So state crosses
  the block boundary.
- **The coder restarts; model and window persist** (Q0027). Block 2 encoded with the model continued from block 1
  and a fresh coder (a new 16-bit start at the block's first byte): byte-identical output. The same with the model
  reset: fails. One continuous coder stream cut at three byte positions: fails or wrong. Copies in block 2 reached into
  block 1 and decoded. Confirmed on a two-block 32 KiB folder (Q0039) and on every multi-block folder after it.
- **`cbUncomp` is exact; a symbol may not cross it.** The `cbUncomp` values at which a fixed body failed lay exactly
  inside repeated runs of its output (bytes 41–43 repeat 28–30, so 42 and 43 fail and 44 succeeds) (Q0006). After
  a 255-literal prefix, a selector-2 copy succeeds with `cbUncomp` 258 and fails at 256; a selector-1 copy needs 262
  (Q0014). Every accepted query since is consistent with it.

### C3. Where a block ends (format §3)

- **Input exhausted is an error.** A random body cut to half its length fails (Q0005).
- **Surplus is ignored.** 15 accepted bodies with 1–4 bytes cut off their surplus, or 4 bytes of 0x00 or 0xFF
  appended: 90 of 90 identical (Q0047). The cuts did not reach a needed bit, so this showed only that extra bytes do
  not matter.
- **The boundary** (Q0048). The same 15 bodies, cut to the shortest prefix the room's reference decoder can decode
  — every bit its 16-bit register model reads, including the renormalisation shifts after the last symbol: all 15
  decode. One, two or three bytes shorter: 45 of 45 rejected. With the last byte replaced, 25 of 30 identical; the
  other 5 differ only in the last copy's extra bits, as the model predicts. So the decoder needs exactly those bits,
  and bits past the end are not zeros.
- **The cap.** On incompressible input Diamond's blocks reach at most 33,265 bytes for 32,768 (1.0146–1.0152, Q0052).
  FultaArc's limit of 32,768 + 6,144 is a policy with a sixfold margin.

### C4. Folders across cabinets (format §4)

These are container facts (public), checked on Quantum data (Q0049): on the Windows 95 and Plus! sets, 17 folders
continue across a cabinet boundary, each with one split block whose first part has `cbUncomp` 0. Decoding each as one
stream with the joined bodies gives files identical to `extrac32`'s for every one of them.

### C5. Reader policies (format §5)

- **Copies from before the start.** A copy reaching before the start of output read zeros in the early censuses
  (Q0010, Q0014, Q0015). At X = 10 the oracle wrote non-zero, unrepeatable bytes there (Q0036 addendum), judged
  undefined behaviour for input no encoder produces. Of 9,851 recorded accepted bodies with such a copy, 9,840 match
  the oracle under zero fill (audit 04). No such copy occurs in the 75 MB of shipped streams (Q0049), nor in Diamond's
  output even after a leading 64 KiB zero run (Q0051, Q0052). The room's decoder, and FultaArc's, treat it as an error.
- **X out of range** is refused because the oracle accepts nothing there (C1).
- **Sets** are opened from any cabinet: FultaArc's CAB reader, the same for every compression (D-023 in FultaArc's
  decisions).

---

## Part M — the method (algorithm.md)

### M1. The bit reader (algorithm §1)

- **Bytes in order, most significant bit first.** In the single-bit flip table of Q0005, flips in input bytes 0–1
  change output byte 0 first, later bytes later outputs, roughly in step; under MSB-first byte order the table is
  consistent, which the boundary census (M2) then pins at the bit level.
- **One reader for both uses.** The raw extra bits of copies come from the input just after the bits the coder has
  already taken (Q0018, Q0021, M5), so they share the coder's reader.
- Confirmed by every chosen-plaintext decode from Q0010 on, whose encoder writes exactly this order.

### M2. The arithmetic decoder (algorithm §2)

- **Not a prefix code.** In the flip table some *later* input bits change *earlier* output bytes (bit 15 → output 2
  but bits 16 and 17 → output 1; more pairs in Q0006) (Q0005). A prefix code read in order cannot do that; a code
  value compared with interval boundaries can. That made the arithmetic coder the hypothesis to test, from public
  mathematics, not from any knowledge of Quantum.
- **16 bits.** The first output byte depends only on the first two input bytes (Q0005).
- **The first census** (Q0008). Bodies `[a, b, 0, …]` with `cbUncomp` 1: as `code = a·256 + b` rises, the codes up
  to 0x6D80 all fail and from 0x6E00 every byte value appears once, in strictly descending order, about 146.29 codes
  each = 65536 / 448. So a first decision splits the code space 192 : 256.
- **Exact formulas** (Q0009). The exact first code of twelve outputs (for example 0x6DB6, 0x8000, 0xDB6D, 0xFF6E)
  rules out every single-division flat model. Among all structures and rounding variants tried, only the standard
  Witten–Neal–Cleary 16-bit decoder with the two-level literal model of M3 fits all 813 first-decision points and 512
  second-decision points, with 0 mismatches; every alternative misses at least 2.
- **Validated by an encoder** (Q0010). The room wrote the matching encoder; 24 random literal strings it encoded
  decoded on the oracle byte-identically.

### M3. Tables (algorithm §3)

- **Equal start weights, entry 0 lowest.** Every table measured — selector, literal class, length, each distance
  table — was first seen with bands of equal width (Q0009, Q0015, Q0017, Q0024, Q0030).
- **Table sizes.** Selector 7 and literal 64 (Q0009); length 27 (Q0024, Q0026, Q0029); distance 2·min(X, 12),
  2·min(X, 18), 2·X (M5).
- **Entries carry symbols.** After a reorder (M6) a census at the next decision found whole entries moved with their
  own internal pattern: the first region's widths followed the class-4 table's 33/41 pattern, the second was uniform
  like class 3's (Q0031). So entries move, and a symbol's later updates follow it.
- **Carry-over across blocks:** C2.

### M4. Symbols (algorithm §4)

- **The selector and literals** (Q0008, Q0009). The 192 : 256 split of M2 refined into a 7-entry selector with equal
  start weights: entries 0–2 failed at output position 0 (copies with nothing to copy, C2), entries 3–6 each lead to
  a 64-entry table. The byte order — class 3 holds 0xFF…0xC0, descending inside each class — is read straight off the
  census, where the byte falls as the code rises.
- **Selector 2 copies 3 bytes; selector 1 copies 4** (Q0014). After a 255-literal prefix, 20 bodies per class:
  selector 2 succeeds at `cbUncomp` 258 and writes exactly 3 bytes then continues; selector 1 succeeds from 262 (19
  of 20 at 262) and writes exactly 4 bytes. Selector 0 failed at every end tried: a longer, coded length.
- **Selector 0 has a coded length of at least 5** (Q0018). Sweeping its first decision and searching the block end:
  the shortest accepted length rises 5, 6, 7, 8, 9, 10, then in pairs 11–12, 13–14, …
- **The length slots** (Q0024, Q0026, Q0029). Each slot's shortest length from an all-zero continuation (its extra
  bits all 0) and longest from an all-ones continuation: 5/5 … 10/10; 11/12, 13/14, 15/16, 17/18; 19/22, 23/26, 27/30,
  31/34; 35/42 … 59/66; 67/82 … 115/130; 131/162 … 227/258; and a bottom slot whose shortest and longest are both
  259. That is the table of algorithm §4, 27 slots.
- **The bit order of selector 0** (Q0034). One selector-0 copy after the prefix, 63 lengths covering every slot, five
  distances at X = 21 and three at X = 15, under each candidate order of the length's extra bits and the distance
  decision: "length slot, length bits, distance slot, distance bits" is byte-identical 504 of 504 times; the other
  orders 217 and 175.

### M5. Distances (algorithm §5)

- **The slot layout** (Q0015). After a selector-2 symbol, the next 16 bits swept over the whole interval with
  `cbUncomp` 258, so the three copied bytes name the distance (up to 255): bands of equal width, from the top
  1, 2, 3, 4, then 5–6, 7–8, 9–12, 13–16, … 193–256, then a far region of 8 more bands — 24 slots. Inside a band the
  exact distance varied with the bits after it: a slot plus extra bits, the widths given by the band edges.
- **The slot counts** (Q0017, Q0021, Q0030, Q0032). Band widths measured at many X: selector 1 has 20, 22, 24, 28, 32,
  34, 36 slots at X = 10, 11, 12, 14, 16, 17, 18 and 36 above; selector 2 has 20, 22, 24 at X = 10, 11, 12 and 24
  above; selector 0 has 2·X at every X measured (10, 12 and 14 to 21). So 2·min(X, 18), 2·min(X, 12) and 2·X. This tied X to the window
  (C1). Q0030's first reading for selector 0 ("25–26 slots at X = 15") was corrected in Q0032 by re-measuring that
  selector's interval from the record.
- **Extra bits are raw** (Q0018, Q0021). With the slot fixed at 129–192 and the next 16 bits swept, the distance was
  `129 + 2·((w >> 6) & 31)`: the top 5 bits of the sweep did not matter. An arithmetic-coded value would depend on the
  top bits; a raw field read just after the bits already in the register does not. Q0021 located the field to the bit
  against the simulator's position.
- **Chosen copies** (Q0023). 36 three-byte copies at distances 1..4096 and 12 four-byte copies up to 2^18: 48 of 48
  byte-identical.
- **Selector 0's distance order.** An early reading "its top symbol is far" (Q0021) was an artefact of an all-ones
  continuation interacting with the length's extra bits; a direct census showed the same order as the other classes,
  far at the bottom (Q0027), and the earlier reading was retracted.
- **Overlap.** Copies with a distance below their length reproduce runs (Q0014); the decoder copies byte by byte.

### M6. Adaptation (algorithm §6)

- **+8** (Q0009, Q0023). In the second census only the increment pair (8, 8) with a separate table per literal class
  fits all 512 points; the next best misses 49. After a copy, the used distance slot's band was 72 points against 8
  for every other slot (1 → 9), and the selector's copy entry had grown by 8.
- **The trigger: total above 3800** (Q0010, Q0011, Q0022). Long literal runs went wrong at symbol 469 (a class-3
  cycle) and 475 (all classes): exactly where the simulated class-3 table reaches 64 + 8·468 = 3808 and the selector
  7 + 8·475 = 3807. A later residue walk pinned the limit: decoding long streams with 3800 keeps them right far longer
  than with 3801 (Q0022).
- **The ordinary rescale was measured, not guessed.** The obvious candidates — halving each weight, with either
  rounding, halving the cumulative counts, resetting — were tried and all failed at the trigger (Q0011). Censuses
  right after an event then gave exact tables:
  - the class-3 literal table: cumulative counts halved with floor, 0 mismatches in 1,040 points; per-weight halving
    misses at least 631 (Q0013);
  - the selector (Q0016, Q0020, Q0022) and a table of mostly weight-1 entries (Q0019) at first disagreed with that.
    Solving the selector's exact boundaries as integers gave a rule that fits *all* of them: cumulative halving, then
    raising each cumulative count to at least the previous one + 1, so no entry drops to 0 (Q0022). It reproduced the
    other instances without refitting, and absolute solves later confirmed it (totals 1903 and 1911, Q0037, Q0038).
  - the distance tables follow the same rule at their first rescale: 1,028 of 1,028 predicted copies (Q0027).
- **Some rescales reorder the table.** At one sequence's 4th selector rescale, classes 3 and 4 came out exchanged
  (Q0028); designed first and second events never reordered (Q0031, Q0033); four sequences reordered at their 4th
  rescale, and the one 3rd rescale measured did not (Q0035). An absolute solve showed the weights at that event are halved *rounding up*,
  with no fix-up (Q0038: total 1906 from [947, 921, 992, 944]). The 5th to 8th rescales were ordinary (Q0038).
- **Which order** (Q0039–Q0045, then an offline identification). A stable ascending sort was right where weights
  differ but wrong where many are equal. The orders were read directly: five full 64-entry literal tables with 3–10
  groups of equal keys, six near-empty tables with 6–20 heavy entries, and six seven-entry selector cases with ties.
  Selection, insertion, bubble, heap, merge, shell, comb and odd-even sorts had already failed on parts of this data;
  2,400 quicksort settings scored against all 17 reached at most 9. The process of algorithm §6 — passes from the end, each entry
  exchanged with the nearest strictly greater key below it, until a pass makes no exchange — reproduces all 17. The
  number of passes is tested: the cases need 2–10, and stopping one pass early breaks every case needing more than
  one productive pass. Raw or floor-halved keys, a non-strict comparison or a scan from the front fail. Three ways of
  continuing after an exchange gave the same result on all 17 and on 20,000 random key vectors, so the choice among
  them is observationally empty.
- **Confirmed fresh** (Q0046). 27 whole folders encoded with that process — cycles, random data, slot-uniform copies
  of each class, mixes in up to four blocks, at X = 10, 12, 15, 16, 18, 21, every table through 5–11 rescales: 27 of 27
  decoded to the intended plaintext (2.2 MB).
- **Every 50th after the 4th** (Q0049, Q0050). The rule then said "once, at the 4th rescale". The shipped cabinets
  proved it short: four folders with known plaintext went wrong exactly after the selector's **54th** rescale, having
  been byte-identical before. Reordering at rescale counts 4, 54, 104, … decoded them completely; "4 and 54 only"
  failed at the 104th, period 25 and period 10 earlier. With period 50, all 21 cabinets' 2,132 files matched
  `extrac32`. The room's generators had never taken a table past 17 rescales, which is why its own tests missed it.
  The rule was then confirmed by chosen plaintext: seven folders that drive every table type past its 104th rescale
  (up to 545) decoded exactly, and two controls encoded with the old once-only rule were rejected (Q0050). The once-only clause
  was retracted in its own line. "4" and "50" are observed counts; nothing explains them.

### M7. The window (algorithm §7)

- **Zero start.** After one literal 'A', the only accepted selector-0 points wrote `41 00 00 00 …`: a copy from before
  the start reads zeros (Q0010); later censuses agree (Q0014, Q0015).
- **The policy** and its evidence: C5.

### M8. How strongly it is tested (algorithm §8)

The counts are those of the room's validation steps: 49,476 oracle records under sanitizers and fuzzing (C port step);
Q0046 and Q0050 for chosen plaintext; Q0049 and Q0051 for the 22 shipped cabinets (2,160 files with the cabinet stored
inside the Internet Explorer 2.0 cabinet); Q0051 and Q0052 for Diamond at every X.

---

## X. From the specification to FultaArc

### X.1 Specification, then code

- **The specification first.** The room wrote its rules into its own specification (`quantum-observed.md`), each
  citing the steps above.
- **The C decoder from the specification only.** After Q0046 and audit 04, the C port was written from that
  specification, which first gained three clarifications (the cumulative-range convention, the shared bit reader and
  its end rule, the far-copy policy and copy overlap). FultaArc's LZX code was not read; only its function prototypes
  and the CAB glue, to match the seam.
- **Checked against the record, not against the room's Python.** The port was validated against the recorded oracle
  answers (49,476 records), not by comparing it with the reference decoder's code. Port and reference decoder agree on
  all 59,462 accepted bodies (identical output, or both reject the same far-copy bodies).
- **Later rules reached the port the same way**: the end-of-block rule (Q0048) and the 50-rescale period (Q0049).

### X.2 Hypotheses that came from memory, and their fate

The session knew LZ77, Huffman and arithmetic coding as general mathematics. No Quantum-specific rule was written
before the observation that supports it (audits 01–05).

| Hypothesis | Where it came from | Fate |
|---|---|---|
| A 16-bit arithmetic coder | the non-monotone flip pairs (M2), tested against public mathematics | confirmed (813 + 512 points) |
| "X is the window" | a name for a parameter | held as a hypothesis until the distance tables were measured against X (M5) |
| Halving variants, cumulative halving, reset, frequency sort for the rescale | generic adaptive-coder practice | all failed at the trigger; the rule adopted was solved from boundaries (M6) |
| Standard sorting algorithms for the reorder | generic computer science | all scored and rejected; the exchange process was found by scoring (M6) |

### X.3 Audits

Five audits (after Q0011, Q0024, Q0040, Q0046 and Q0050) asked of every claim whether it is traced, whether recalled
knowledge got in, whether a finite test was taken for proof, and whether an outside answer was used in disguise. The
retractions they recorded — the "translated partition" of Q0013, the "zero-weight entries" of Q0016 (sampling
aliasing), the "dead bits" of Q0005/Q0006, "selector 0's top symbol is far", the first selector-0 slot count, "a stable
sort", "once at the 4th rescale" — are all corrected in the log in their own lines. **No use of a forbidden source was
found.**

### X.4 Into FultaArc, and FultaArc's own check

`src/codec/quantum.c` is the room's `port/quantum.c`. From its first `#define` on the code is unchanged (checked by
`diff` at integration and again for this page); only the header comment and the include block differ, where the
port's standalone allocator switch gives way to FultaArc's internal header. `src/cab/cab.c` calls it per block.

FultaArc tests it on fixtures of its own, independently of the room: `tests/manifests/cab.txt` lists
`quantum10.cab`, `quantum12.cab` and `quantum21.cab`, made by Diamond from neutral plaintext and checked with
`extrac32`, and a three-cabinet Quantum set `qset1-3.cab` opened from each of its cabinets; `tests/test_cab.c` checks a
Quantum file spanning blocks, the refusal of X = 0 and the set walk. The Microsoft-shipped cabinets are not FultaArc
test input and are not distributed.
