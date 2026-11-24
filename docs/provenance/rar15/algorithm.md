# RAR 1.4 / 1.5 — the compressed stream

This page describes how the compressed data of a RAR 1.4 or RAR 1.5 entry is decoded, and the RAR 1.4 cipher. The
container around it is in [`format.md`](format.md). How each rule here was found is in
[`derivation.md`](derivation.md): algorithm §*n* is established in derivation §M*n*. The sources and their terms are
in [`provenance.md`](provenance.md).

The compressed stream is **the same** in both container families: a RAR 1.4 entry (method 1..5) and a RAR 1.5 entry
(method 0x31..0x35) hold a stream decoded exactly as below. It is an LZ77-style stream — the output is built from
**literals** (one byte given directly) and **matches** (copy *n* bytes from *d* bytes back) — but almost every
choice in it adapts while decoding, from state that the encoder and decoder both keep and update identically.
Nothing but codes is transmitted; there is no table in the stream. If any update differs from RAR's in the least,
the two drift apart and everything after is wrong, which is why the constants below are pinned to the unit.

Every rule was tested; the strength of each is given in §17. Where a reader must choose a behaviour the stream
never exercises, FultaArc makes a **reader decision**, marked as such (collected in `format.md` §17).

## 1. Common rules

- **Bits** are read most significant bit first, from consecutive bytes. A code is a bit string in that order.
- **A copy** of *n* bytes from distance *d* appends, one byte at a time, the byte *d* positions before the current
  end of the output; so a copy with `d < n` repeats bytes it has just written. A distance `d = 0` means 65536.
- **The window** (the output history a copy can reach) is 65536 bytes; the largest distance is 65536. Positions
  before the first output byte read as 0.
- **End.** Decoding runs until the output of this entry reaches its unpacked size. RAR completes the step that
  reaches it; FultaArc cuts the output at the size (`format.md` §17). The CRC-32 (`format.md` §5) is then checked.
- **Reading past the packed size** is an error (RAR reads leftover memory there; there is no defined result).

## 2. Canonical codes and tables

Every prefix code here is **canonical**: codes are assigned in index order, lengths non-decreasing; the first code
is all zeros; each next code is the previous plus one, shifted left by the increase in length. A table is written
`length`×`count` in index order. To decode, read bits until the value so far falls inside the range of its length.

**Position tables** — each has 257 codes (Kraft sum 1):

| Table | Code lengths × counts |
|---|---|
| T0 | 5×4, 6×40, 7×16, 8×16, 9×4, 11×47, 12×130 |
| T1 | 5×2, 6×5, 7×46, 8×64, 9×116, 10×24 |
| T2 | 6×2, 7×14, 8×202, 9×33, 10×6 |
| T3 | 8×255, 9×2 |
| TL | 4×8, 5×8, 6×8, 7×9, 12×224 (the same code as the first P distance code) |

Index 256 of a position code is the all-ones code; it is not a valid list position and is an error (reader
decision). A position table runs from very skewed (TL: 8 four-bit codes … 224 twelve-bit codes) to flat (T3: 255
eight-bit codes).

**Length codes** — 256 codes each (Kraft sum 1):

| Code | Code lengths × counts |
|---|---|
| R (lengths 2..257 as index 0..255) | 2×2, 3×1, 4×2, 5×2, 6×4, 7×5, 8×4, 9×4, 10×8, 12×224 |
| C | 3×5, 4×2, 5×2, 6×4, 7×5, 8×4, 9×4, 10×8, 11×2, 12×220 |

## 3. Count lists and the distance list

A **count list** holds 256 symbols in positions 0..255, each with a use count; the stream names a symbol by its
position. `use(r)`:

1. `s` = the symbol at position `r`; add 1 to `s`'s count.
2. Find the first position `q < r` whose symbol has a count **strictly lower** than `s`'s new count; if there is
   one, exchange positions `q` and `r`.
3. If `s`'s count now exceeds the list's limit, set every symbol's count from its (new) position `i` to
   `7 - (i >> 5)`, then add 1 to `s`'s count.

`use` returns `s`. The three count lists differ only in start order, start counts and limit:

| List | Symbols | Initial order | Initial counts | Limit |
|---|---|---|---|---|
| literal list | byte values | 0, 1, …, 255 | all 0 | 161 |
| flag list | flag byte values | 0xff, 0x00, 0x01, …, 0xfe | all 0 | 255 |
| block list | 128-byte distance blocks 0..255 | 0, 1, …, 255 | `7 - (i >> 5)` | 255 |

The **distance list** `Q` is separate: distances 1..256 in positions 0..255; using position `j > 0` exchanges
positions `j-1` and `j` (one place toward the front); position 0 stays.

## 4. Decoder state and initial values

`>>` on a negative number is an arithmetic shift (floor division). A fresh decoder:

| Name | Initial | Meaning |
|---|---|---|
| window | empty (bytes before the start read as 0) | output history |
| the four lists | §3 | |
| S | 53 | literal score |
| fbits | none | pending flag bits |
| run | 0 | literals in a row (flag mode) |
| lmode | false | literal mode |
| swapped | false | kind assignment (§7) |
| kv | 288 | swap counter |
| nA | 0 | kind-A steps counted |
| kpost | false | a flag used after a second-check swap |
| K | 0 | the `kv` of the last second-check swap into the swapped state |
| ky | 0 | P length score |
| kd | 0 | P distance score |
| pfar | true | the next far match gets its +1 (§15; true before the first P) |
| ph | 0 | the balance of P length codes (§15), held within -255..0 |
| qs | 0 | Q score |
| rep | 0 | repeats in a row among Q-kind steps |
| recent | [0, 0, 0, 0] | the last four match distances, newest first |
| far | false | the far-match code is on (§11) |
| last | none | the last match (length, distance) |

In a solid archive this state carries from one file to the next, with the three resets of `format.md` §8 (pending
flag bits dropped, `run` cut to ≤ 16 so literal mode ends, `rep` set to 0); `ph`, `far` and the window carry over.

## 5. The main loop

Decode steps until the output reaches the unpacked size; the step that reaches it is completed. Outside literal
mode (§12) a step begins with flag bits (§6) that name its kind: literal (§8), P (§10) or Q (§9). A Q step may be a
plain match, a repeat (REP), or a recent-distance step (R, §11).

## 6. Flag bits and the switch to literal mode

A step outside literal mode starts with flag bits:

| Flag bits | Not swapped | Swapped (§7) |
|---|---|---|
| `0` | literal | P |
| `10` | P | literal |
| `11` | Q | Q |

The first bit is the step's first flag bit; the second (the "kind bit") may come from the next flag byte. `run`
counts literal steps in a row in flag mode: a literal adds 1, a P or Q step sets it to 0.

When a flag bit is needed and none is pending:

- if this is the **first** flag bit of a step and `run >= 17`, switch to literal mode (`lmode = true`; no flag byte
  is read);
- otherwise read a T1 code (index `i`, 0..255) and let `v = use(i)` on the flag list; the 8 bits of `v`, MSB
  first, become the pending flag bits.

The kind bit (the second flag bit) never switches to literal mode. Index 256 of the flag's T1 code is an error.

## 7. The swap and its counter

Normally `0` = literal and `10` = P; when P steps dominate the two trade codes (the **swapped** state). The switch
is decided by a counter `kv` both sides keep. Call the step type that currently has the `0` code **A** (literal
when not swapped, P when swapped) and the other **B**; Q steps do not take part. Every literal (in flag or literal
mode) and every P step updates the counter; Q steps do not. With `U = 144` and `half(x) = x >> 1` when not swapped,
`(x + 1) >> 1` (ceiling) when swapped:

```
if t == A:
    nA += 1
    if nA % 7 == 1:
        if kpost: kpost = false; kv = half(kv - U); done     # decrement before the halving; no second decrement
        else:     kv = half(kv)
    kv -= U
else:                                                        # t is kind B
    if kv >= U:                                              # first check
        swapped = not swapped; kpost = false
        if the swap made swapped false (a swap back):
            kv = U
            if kpost was set before this step:
                kv = K; nA += 1; if nA % 7 == 1: kv = half(kv)
                kv -= U                                       # counted as an A step from kv = K
            done
        kv = 2U; nA += 1; if nA % 7 == 1: kv = half(kv)
        kv -= U                                               # counted as an A step of the new assignment
    else:
        kv += U
        if kv >= (145 if swapped else 153):                   # second check
            swapped = not swapped
            if swapped: K = kv
            kv = U; kpost = swapped
```

A swap changes only the kinds of **later** steps. For a P step the counter update happens after the P's length and
distance are read (§10). The comparison values (first check `U`, second checks 153 unswapped / 145 swapped) are
pinned to the unit by the data; some reset values are pinned only within ranges that never change an outcome
(derivation §M7).

## 8. Literal step (flag mode)

Choose the position table from `S`: `S <= -9971` → TL; `S >= 16758` → T3; `S >= 10590` → T2; `S >= 310` → T1; else
T0. Read a code index `i`; `r = i`, except index 256 means `r = 0`. The byte is `use(r)` on the literal list;
output it. Then `S = S + r - 53 - (S >> 8)`. Then update the swap counter (§7) with type literal.

## 9. Q step

1. If `rep >= 2`: read one bit. `1` → a repeat (step 4) with no further code; `0` → continue with step 2.
2. Read the Q code of the layout chosen by `qs`: layout A while `qs < 39`, layout B when `qs >= 39`:

   | Symbol | Layout A | Layout B |
   |---|---|---|
   | length 2 | `0` | `00` |
   | length 3 | `101` | `010` |
   | length 4 | `1101` | `011` |
   | R (§11) | `100` | `100` |
   | length 5 | `1110` | `101` |
   | REP | `1100` | `1100` |
   | length 6 | `11110` | `1101` |
   | length 7 | `111110` | `1110` |
   | length 8 | `1111110` | `11110` |
   | length 9 | `11111110` | `111110` |
   | length 10 | `11111111` | `111111` |

   After the far escape (§11) is on, for the rest of the file the code `101` (length 3 in A, length 5 in B) is
   split: `1010` is that length and `1011` is a far match (§11). The other codes stay.
3. **A length `n`:** `rep = 0`. Read a T1 code index `j` (0..255; 256 is an error); `d` = the distance at position
   `j` of the distance list, then the list update (§3). `qs = qs + (n - 2) - (qs >> 4)`. Push `d` to `recent` (§11),
   set `last = (n, d)`, copy `n` bytes from distance `d`.
4. **REP:** `rep += 1`; if `last` exists, copy `last`'s length from `last`'s distance; else nothing is output.
   `recent`, `last`, `qs` unchanged.
5. **R:** `rep = 0`, then §11.

Literal and P steps neither reset nor count `rep`; Q and R steps reset it.

## 10. P step

1. **Length**, by the layout chosen by `ky` (before this step's update): A while `ky < 66`; B while `ky < 125`; C
   from 125.
   - A: count zero bits `z` until a 1 bit (consumed) or 8 zeros; `z < 8` → `n = 3 + z`; `z = 8` → `n = 3 + (next 8
     bits)`.
   - B: `n = 3 + (R code index)` (§2).
   - C: `n = 3 + (C code index)` (§2).

   Let `z = n - 3` (the coded length value).
2. **Distance position table**, by `kd`: TL while `kd < 1799`; T0 while `kd < 10537`; T1 from 10537. Read a code
   index `p` (0..255; 256 is an error), then 7 raw bits `l`.
3. `blk = use(p)` on the block list; `kd = kd + p - (kd >> 8)`; `d = 128*blk + l`.
4. **Length corrections and the +1 flag.** If `d <= 256`: `n += 8`. If `d > 8192` and (`pfar` or `d >= 32512`):
   `n += 1`. Then recompute the flag for the next P and the balance, in this order, from the balance **before** this
   step changes it (§15):
   - `on = (ph - 1 > -178)`;
   - `ph = max(-255, min(0, ph + c))`, with `c = -1, 0, +1, +1, 0` for `z = 0..4` and `+1` for `z >= 5`, except
     `c = +1` when `z = 0` and `d` lies one beyond the +1 limit (`d > 8193` when `pfar` was set, `d > 32512` when it
     was not);
   - `pfar = on and (S <= -2775 or layout != A)`.
5. If `d = 0`: `d = 65536`.
6. `ky = ky + z - (ky >> 5)`; then update the swap counter (§7) with type P.
7. Push `d` to `recent`, set `last = (n, d)`, copy `n` bytes from distance `d`.

## 11. R step (recent distances) and the far match

`recent` holds four distances, newest first; a push puts `d` at the front and drops the oldest.

1. **Selector**: read up to three bits, counting ones until a 0 (`0` → 0, `10` → 1, `110` → 2, `111` → 3).
2. Read an R code index `x` (§2).
3. If selector = 0 and `x = 255`: **the far escape** — nothing is output and the far-match code is on for the rest
   of the file (§9 step 2). Done. (It carries into the next file of a solid archive.)
4. **A far match** (the Q code `1011` while the far code is on): `n = 5 + (C code index)`; `d = 32768 + (15 bits)`;
   `rep = 0`; `last = (n, d)` (`d` is **not** pushed to `recent`); copy.
5. Otherwise `n = x + 2`; `d = recent[selector]`; if `d > 256`: `n += 1`; if `d > 8192` and (`pfar` or
   `d >= 32512`): `n += 1` (a far match takes no such +1). Push `d` to the front (the oldest drops; the entry it
   came from stays). `last = (n, d)`. Copy `n` bytes from distance `d`.

`qs` is not changed by an R step or a far match.

## 12. Literal mode

Entered from §6. Each step reads a code index `i` of the literal table chosen by `S` (§8):

- `i >= 1`: a literal with `r = i - 1`, handled as in §8 (list, S update with `r`, counter with type literal).
  `run` is not changed.
- `i = 0`: an escape. Read one bit `b`:
  - `b = 0`: a match — `n = 3 + (one bit)`; `d = 32*(T1 code index) + (5 bits)`; copy (`d = 0` means 65536).
    Literal mode continues. The position is used directly, **without** a list. This match is deliberately invisible
    to the rest of the state: it does not become `last`, does not enter `recent`, and moves no score or counter.
  - `b = 1`: leave literal mode; `run = 0`.

## 13. Copy

See §1: *n* bytes from distance *d*, one byte at a time (so `n > d` repeats), `d = 0` meaning 65536, positions
before the start reading as 0.

## 14. Scores — the summary

Each score is a running average in integer arithmetic (`x = x + value - (x >> k)`), compared with fixed thresholds
to choose a code:

| Score | Updated by | Update | Chooses | Thresholds |
|---|---|---|---|---|
| S | every literal, with its list position `r` | `S += r - 53 - (S >> 8)` | literal table (§8) | TL ≤ -9971; T0; T1 from 310; T2 from 10590; T3 from 16758 |
| qs | every Q length `n` | `qs += (n - 2) - (qs >> 4)` | Q layout (§9) | B from 39 |
| ky | every P, with its coded length `z` | `ky += z - (ky >> 5)` | P length layout (§10) | B from 66, C from 125 |
| kd | every P, with its block position `p` | `kd += p - (kd >> 8)` | P distance table (§10) | T0 from 1799, T1 from 10537 |

`S` starts at 53 and is centred there; `>>` on a negative `S` rounds down.

## 15. The +1 flag and the balance

A P or R match at a distance above 8192 is one byte longer than its code says, but only while the flag `pfar` is
on; at `d >= 32512` always. `pfar` is recomputed at every P step, for the **next** match (§10 step 4):

```
on   = (ph - 1 > -178)                 # ph: the balance, before this step changes it
ph   = clamp(ph + c, -255, 0)          # c by the coded length z (§10 step 4)
pfar = on and (S <= -2775 or this P was read with length layout B or C)
```

The balance `ph` starts at 0 and moves with the coded length `z`: `z = 0` lowers it by 1; `z = 1` and `z = 4` leave
it; `z = 2, 3` and `z >= 5` raise it by 1. The one exception: a P with `z = 0` whose distance lies one beyond the
+1 limit (`d >= 8194` when the flag was on, `d >= 32513` when it was off) raises the balance by 1 instead of
lowering it, while still taking the longer length. The order inside a P step matters: read length, read distance,
apply the length corrections with the **old** flag, compute the next flag from the **old** balance, update the
balance, update `ky`, then the counter, then copy.

## 16. The RAR 1.4 cipher

RAR 1.4 encrypted entries (`format.md` §16) and RAR 1.4 packed archive comments (`format.md` §15) use one additive
cipher. From the password three key bytes are set up, then each data byte is enciphered by a two-register step:

```
set-up:   k1 = k2 = k3 = 0
          for each password byte c:
              k1 = (k1 + c) mod 256
              k2 = k2 XOR c
              k3 = (k3 + c) mod 256, then rotated left by one bit within 8 bits

per byte: k2 = (k2 + k3) mod 256
          k1 = (k1 + k2) mod 256
          cipher byte = (plain byte + k1) mod 256        # decoding: plain = cipher - k1
```

The keys are set anew from the password at the start of each file (also in a solid archive) and run on across the
parts of a file split over volumes. For a packed archive comment the fixed keys `k1 = 0`, `k2 = 7`, `k3 = 77` are
used instead of the password set-up. RAR 1.5 encryption is a different mechanism (an XOR stream cipher derived in
`fultaarc-cleanroom-rar15-enc3`), documented in [`enc3/RAR15-DECRYPT-UNPACK-SPEC.md`](enc3/RAR15-DECRYPT-UNPACK-SPEC.md)
and [`enc3/RAR15-KEYSTREAM-RESEARCH-REPORT.md`](enc3/RAR15-KEYSTREAM-RESEARCH-REPORT.md).

## 17. How strongly each part is tested

"Tested" always means on a finite set of inputs; it is never a claim for every possible input.

| Part | Tested against |
|---|---|
| container fields | differential pairs; archives built from the rules and accepted by RAR |
| tables and codes (§2) | every index of every table decoded by RAR in probe streams; every code of the C length code confirmed |
| count lists (§3) | position-by-position traces; random streams |
| score thresholds (§14) | pinned to the unit by RAR on streams built to sit on each side |
| the swap and its counter (§7) | 9671 flag-level streams and 1008 need-values from exact states; comparison values to the unit; some reset values only within ranges that change no outcome |
| the +1 flag and balance (§15) | the balance step for every `z = 0..255`; thresholds to the unit; in all three length layouts |
| the whole stream | 1170 development archives; 21 holdouts on frozen models; soak tests of 5000 generated and 2500 real-file-slice cases (69,944 entries, 2.67 GB), 0 wrong; real archives of 1994–1997 (815 archives, 13,459 entries), all agreeing with RAR 1.55 and with RAR 2.50/2.02 as judges |
| RAR 1.4 cipher (§16) | 97 key streams of 1–146-character passwords reproduced; the format's encrypted entries decoded |
| the stream under the RAR 1.4 container | 2000 + 800 generated archives and 9 real archives decode unchanged |
