# LHA / LZH — the decompression methods

This page describes how the data of each LHA entry is decompressed, for every method FultaArc decodes. The
archive and its headers are described in [`format.md`](format.md). How each rule here was found — the
observation it rests on — is in [`derivation.md`](derivation.md), section by section: algorithm §*n* is
established in derivation §M*n*. The sources and their licences are in [`provenance.md`](provenance.md).

Every rule was tested; the strength of each test is given per method (§12). Where no sample shows what should
happen, FultaArc's decoder makes a **reader decision**, marked as such: a choice, not a format fact.

## 1. Common to all methods

- **Bits** are read most significant bit first from consecutive bytes.
- **Output.** A method produces the original bytes in order. A **copy** (a "match") repeats earlier output:
  `length` bytes starting `distance` bytes back, copied one byte at a time, so a copy whose distance is smaller
  than its length repeats bytes it has just written.
- **Window.** The largest distance a method allows. Some methods start with a window already filled (with spaces,
  0x20, or a fixed pattern), so an early copy may reach "before" the first byte and copy that fill.
- **End.** Decoding stops when the original length from the header has been produced. The CRC-16 is then
  checked (`format.md` §5.1).

| Method | Section | Kind | Window |
|---|---|---|---|
| `-lh0-`, `-lhd-`, `-lz4-`, `-pm0-` | §2 | stored, or a directory | — |
| `-lh4-`, `-lh5-`, `-lh6-`, `-lh7-`, `-lhx-` | §3 | static Huffman blocks | 16384 / 16384 / 65536 / 131072 / 1048576 |
| `-lk7-` (`-lh7-` with system byte 0x20) | §4 | static Huffman, other position and length codes | 65536 |
| `-lz5-`, `-lzs-` | §5 | LArc: literals and copies over a ring | 4096 / 2048 |
| `-lh1-` | §6 | adaptive Huffman | 4096 |
| `-lh2-` | §7 | adaptive Huffman, growing distance tree | 8192 |
| `-lh3-` | §8 | static Huffman blocks with presence-coded tables | 8192 |
| `-pm2-` | §9 | PMarc 2: move-to-front literals, copies, block headers | 8192 |
| `-pm1-` | §10 | PMarc 1: literal runs and copies | 10816 |

## 2. Stored methods

`-lh0-`, `-lz4-` and `-pm0-`: the data is the original bytes, unchanged. `-lhd-`: a directory; it has no data.

## 3. Static Huffman: `-lh4-`, `-lh5-`, `-lh6-`, `-lh7-`, `-lhx-`

### 3.1 Parameters

| Method | PBIT | Window |
|---|---|---|
| `-lh4-`, `-lh5-` | 4 | 16384 |
| `-lh6-` | 5 | 65536 |
| `-lh7-` | 5 | 131072 |
| `-lhx-` | 5 | 1048576 |

All five use three prefix codes:

- **T**, for code lengths: 19 symbols;
- **C**, for literals and match lengths: 510 symbols, its count field 9 bits wide;
- **P**, for distances: as many symbols as a PBIT-bit count can give.

The shortest match is 3 bytes.

### 3.2 Prefix codes

A code is given by a length (0 to 16) for each symbol; 0 means the symbol does not occur. Codes are assigned
**canonically**: shorter lengths first, and within one length in increasing symbol order; each code is the
previous one plus one, shifted left when the length grows. A set of lengths that over-fills the code space is an
error.

### 3.3 The stream

The stream is a sequence of blocks. Each block is:

1. 16 bits: `count`, the number of C symbols in the block. A count of 0 is an error (reader decision; never seen).
2. **The T lengths**, read as a *length list* (below) with a 5-bit count and the zero-run field.
3. **The C lengths.** A 9-bit count `n`. If `n` is 0, 9 more bits give the one symbol that every C read returns
   (it takes no bits). Otherwise T symbols are read until `n` lengths are set: T symbol 0 is one zero length; 1
   is followed by 4 bits `k` and gives `k + 3` zero lengths; 2 is followed by 9 bits `k` and gives `k + 20` zero
   lengths; a symbol `t` of 3 or more is the length `t − 2`. Lengths after the `n`-th are 0.
4. **The P lengths**, read as a length list with a PBIT-bit count and no zero-run field.
5. `count` times: a C symbol `c`. If `c` is below 256, it is a literal byte. Otherwise it is a match of length
   `c − 253`; then a P symbol `j` gives the distance: 1 if `j` is 0, otherwise `2^(j−1) + 1` plus the value of
   `j − 1` further bits.

**A length list** has a count field `n` (5 bits for T, PBIT bits for P). If `n` is 0, a field of the same width
gives the one symbol every read returns. Otherwise `n` lengths follow, each as 3 bits `v`; if `v` is 7, one is
added for every following 1 bit, up to a 0 bit (a length above 16 is an error). For T only, right after the
third length (also when `n` is 3), 2 bits `z` give `z` more zero lengths, which count towards `n`.

### 3.4 Window

A distance greater than the bytes already produced, or greater than the window in §3.1, is an error.

## 4. `-lk7-` (LHARK)

LHARK writes the method string `-lh7-` with the system byte 0x20 (`format.md` §6.1). Its blocks are those of §3.3
with three changes:

1. The P length list has a **6-bit** count (and no zero-run field).
2. A P symbol `s` gives the distance `s + 1` if `s` is below 4. Otherwise, with `k = (s − 2) div 2`, `k` bits `v`
   follow and the distance is `1 + (2 + (s mod 2)) · 2^k + v`.
3. A match symbol `c` (256 or more), with `i = c − 256`: if `i` is below 8 the length is `i + 3`. Otherwise, with
   `g = (i − 8) div 4 + 1` and `r = (i − 8) mod 4`, `g` bits `v` follow **straight after the C symbol**, before
   the P symbol, and the length is `3 + 2^(g+2) + r · 2^g + v`.

Seen in real archives: P symbols 0 to 31 (distances up to 65536) and match symbols 256 to 278. Reader decision:
rule 3 is accepted up to match symbol 287 (an extrapolation of the same formula); larger match symbols and P
symbols above 31 are errors.

## 5. LArc: `-lz5-` and `-lzs-`

Both keep a **ring buffer**. Each output byte is also written into the ring at a position that runs ahead of the
output by a fixed amount, and a copy reads from an absolute ring position. A copy reads each byte before writing
the next, so it may read what it has just written.

### 5.1 `-lz5-`

- Ring: 4096 bytes. Output byte number `p` (counting from 0) is written at ring position `(p + 4078) mod 4096`.
- Data: groups of one **flag byte** and up to eight items. The flag bits are used from the least significant up.
  Flag bit 1: one literal byte. Flag bit 0: two bytes `b0 b1`, a copy from ring position
  `b0 | ((b1 & 0xF0) << 4)` of `(b1 & 0x0F) + 3` bytes.
- **The ring at the start** is filled as follows:

  | Ring positions | Content |
  |---|---|
  | 0x000–0xCFF | each value 0 to 255, thirteen times in a row, in increasing order |
  | 0xD00–0xDFF | 0x00 up to 0xFF |
  | 0xE00–0xEFF | 0xFF down to 0x00 |
  | 0xF00–0xF7F | 0x00 |
  | 0xF80–0xFED | 0x20 |
  | 0xFEE–0xFFF | 0x00 |

### 5.2 `-lzs-`

- Ring: 2048 bytes, all 0x20 at the start. Output byte `p` is written at `(p + 2031) mod 2048`.
- Data, read bit by bit: a 1 bit and then 8 bits is a literal; a 0 bit, then 11 bits of ring position and 4 bits
  `k`, is a copy of `k + 2` bytes.

## 6. `-lh1-` (adaptive Huffman, window 4096)

In an adaptive code the tree is not stored: encoder and decoder both start from the same tree and change it the
same way after every symbol, so they stay in step.

### 6.1 Symbols

314 symbols: 0 to 255 are literal bytes; 256 to 313 are matches of length `symbol − 253` (3 to 60).

### 6.2 The tree

The tree is a list of 627 nodes (0 to 626), each with a count; counts never decrease along the list. An internal
node's two children sit at consecutive positions: bit 0 leads to the first, bit 1 to the second. Node 626 is the
root. At the start, nodes 0 to 313 are the leaves of symbols 0 to 313, each with count 1, and internal node
`313 + k` (for `k` from 1 to 313) has the children `2(k − 1)` and `2(k − 1) + 1`, with the sum of their counts.

### 6.3 Decoding and updating

To decode a symbol, start at the root and follow bits to a leaf. Then update the tree for that symbol:

1. **Rescale first if needed.** If the root count is 0x8000: give each leaf, taken in list order, the count
   `(count + 1) div 2` and place the leaves at positions 0, 1, 2, …; then rebuild the internal nodes, one for each
   consecutive pair of nodes (0 and 1, 2 and 3, …) in turn, each inserted straight after the last node whose count
   is not greater than its own (the nodes after that point move up by one).
2. **Increment.** Starting at the symbol's leaf: add 1 to the node's count. If the count now exceeds that of the
   next node, exchange this node (with its subtree) with the highest-placed node whose count is still lower than
   the new count, so that the order holds. Continue from the parent of the node's new position, and stop after the
   root has been incremented. (The root always has the largest count, so it is never exchanged.)

### 6.4 Distances

After a match symbol, a codeword of the fixed code below gives `u` (0 to 63), then 6 bits give `v`;
the distance is `64u + v + 1`.

| `u` | 0 | 1–3 | 4–11 | 12–23 | 24–47 | 48–63 |
|---|---|---|---|---|---|---|
| code length | 3 | 4 | 5 | 6 | 7 | 8 |

The codes are canonical in increasing `u`.

### 6.5 Window

4096 bytes, initially all 0x20, so a match may reach back before the first output byte.

## 7. `-lh2-` (adaptive Huffman, window 8192, growing distance tree)

### 7.1 Symbols

286 symbols: 0 to 255 literal bytes; 256 to 284 matches of length `symbol − 253` (3 to 31); 285 a match whose
length is 32 plus the next 8 bits (32 to 287).

### 7.2 Literal and length tree

That of §6.2–6.3 with 286 symbols instead of 314 (571 nodes, root 570), started, decoded, updated and rescaled in
exactly the same way.

### 7.3 The distance tree

A second adaptive tree, in the same node-list form, chooses a 64-byte **block** of distance.

- **Start:** one leaf, for block 0, with count 1. While it is the only leaf, a match reads no codeword (block 0)
  and changes no count.
- **Growth.** Before the block codeword of a match: while (number of leaves) × 64 is less than the smaller of
  (bytes output so far) and 8192, add the next block `k`. Every node moves up two positions (child links follow).
  Position 0 becomes a new leaf for block `k` with count 0. Position 1 holds the node that was lowest before (which
  is a leaf: an internal node's count is at least that of its children; a reader decision rejects the other case,
  which never occurred). Position 2 becomes an internal node with children 0 and 1 and the old node's count. Then
  the new leaf is incremented by the rule below. The tree therefore has at most 128 leaves.
- **Decoding:** from the root follow bits to a leaf; its block is `u`; then update that leaf.
- **Update:** as §6.3 step 2, but the rescale test at its start is different: rescale when the root count — minus
  one if no rescale has happened yet — is at least 0x8000. (Until the first rescale the root count is one less
  than the sum of the leaf counts; the rescale recomputes it.) The rescale itself is §6.3 step 1 applied to this
  tree's leaves.

### 7.4 Matches and window

A match: its length (§7.1), then the block `u` (§7.3), then 6 bits `v`; distance `64u + v + 1`. The window is 8192
bytes, initially all 0x20.

## 8. `-lh3-` (static Huffman blocks, window 8192)

### 8.1 Blocks

The data is a sequence of blocks; the window carries over from one block to the next. Each block is: a 16-bit
token count `n` (1 to 65535; reader decision: 0 is an error), the character table, the distance region, then `n`
tokens.

### 8.2 The character table

For each of the 286 symbols of §7.1, in order, one bit: 1 is followed by 4 bits `f`, and the symbol's code length
is `f + 1`; 0 means the symbol is not used. Codes are canonical. The lengths must form a **complete** code (every
code space used) with at least two symbols; anything else is an error.

### 8.3 The distance region

One flag bit.

- **Flag 1:** 128 four-bit code lengths, one per 64-byte block 0 to 127 (0 = unused); canonical codes; they must
  form a complete code with at least two blocks — except that all 128 lengths zero means block 0 is used and no
  bits are read. Exactly one non-zero length, or an incomplete code, is an error.
- **Flag 0:** nothing more; a fixed canonical code is used, with lengths 2 (block 0), 4 (blocks 1–2), 5 (3–5),
  6 (6–12), 7 (13–30), 8 (31–77) and 9 (78–127).

### 8.4 Tokens and window

A character code: below 256 a literal; 256 to 284 a match of length `symbol − 253`; 285 a match of length 32 plus
the next 8 bits. After a match symbol (and its 8 bits, if any): the block code `u`, then 6 bits `v`; distance
`64u + v + 1`. The window is 8192 bytes, initially all 0x20.

## 9. `-pm2-` (PMarc 2, window 8192)

### 9.1 The move-to-front list

PMarc does not code a literal byte directly but its position in a list of the 256 byte values, kept in
most-recently-used order. The list starts as 0x20–0x7F, 0x00–0x1F, 0xA0–0xDF, 0x80–0x9F, 0xE0–0xFF. **Every**
output byte — literal or copied — is moved to the front of the list.

### 9.2 Symbols

| Symbol | Meaning |
|---|---|
| 0–7 | a literal from list class *c*: then `e` bits `v`; the list position is `base + v`, with (base, e) = (0, 3), (8, 3), (16, 4), (32, 5), (64, 5), (96, 5), (128, 6), (192, 6) for classes 0 to 7 |
| 8–22 | a copy of `symbol − 6` bytes (2 to 16) |
| 23 | a copy of 17 plus 3 bits bytes |
| 24 | a copy of 25 plus 3 bits bytes |
| 25 | a copy of 33 plus 5 bits bytes |
| 26 | a copy of 65 plus 6 bits bytes |
| 27 | a copy of 129 plus 7 bits bytes |
| 28 | a copy of 256 bytes from distance 1 (no distance is read) |

### 9.3 The code header

5 bits `N` (the number of symbols used, 1 to 29; reader decision: other values are errors), then 3 bits `P`.

- If `P` is 0, nothing more follows: the only symbol is `N − 1`, with a code of length 0.
- Otherwise 3 bits `w`, then `N` fields of `w` bits. A field `f` other than 0 gives symbol `i` the code length
  `f + P − 1`; 0 means unused. Codes are canonical; a single used symbol has a code of length 0.

If the header includes copy symbols (`N` above 8), the distance region (§9.5) follows immediately.

### 9.4 Blocks

Before the first symbol, and before the first symbol at or after every multiple of 4096 output bytes (also when a
copy ends exactly on one), one flag bit is read: 1 means a new header (§9.3) follows and replaces the code; 0 keeps
the code. Reader decision: a flag 0 at the very start is an error.

### 9.5 Distances

| Class | 1–64 | 65–128 | 129–256 | 257–512 | 513–1024 | 1025–2048 | 2049–4096 | 4097–8192 |
|---|---|---|---|---|---|---|---|---|
| extra bits | 6 | 6 | 7 | 8 | 9 | 10 | 11 | 12 |

The distance region has one 3-bit field per class that can be reached so far: 5 fields while fewer than 1024 bytes
are out, 6 from 1024, 7 from 2048 and 8 from 4096. It is read after a header with copy symbols, and read again,
with the larger count, when the count grows while the current code has copy symbols (at 4096 the flag of §9.4
comes first). The fields are code lengths (0 = unused), canonical; a single used class has a code of length 0.

After a copy symbol other than 8 and 28: the class code, then the class's extra bits `v`; distance = class start +
`v`. A copy of length 2 (symbol 8) reads 6 bits instead: distance = value + 1.

### 9.6 Reader decisions

No encoder output shows these, so the decoder rejects them: a copy that reaches before the first byte (lhasa
writes spaces there; PMarc's own extractor does not finish such an archive); a copy when no distance class is
used; an over-full code.

## 10. `-pm1-` (PMarc 1, window 10816)

### 10.1 List and start value

The move-to-front list of §9.1, updated the same way. The first 5 bits, `h`, choose one of 32 fixed literal codes
(§10.5) for the whole stream.

### 10.2 Units

After the start value come **units** until the output is complete: a run length `R` (§10.3), then `R` literals,
then one copy (§10.4). After a run of 216 literals — the largest run — no copy follows and the next unit starts.
Decoding stops as soon as the output size is reached, even inside a run or a copy.

### 10.3 Run length

`0` is 0. Otherwise 2 bits `v`: below 3 it gives `1 + v`. If `v` is 3: 3 bits `v`, below 7 giving `4 + v`; if
that `v` is 7: 4 bits `v`, below 14 giving `11 + v`; 14 gives 25 plus 6 bits; 15 gives 89 plus 7 bits.

### 10.4 Copies

Let `Q` be the number of bytes out plus one. The distance classes that can be reached are **near** (1–64),
**far-1** (from 65) once `Q` ≥ 65, **far-2** (from 577) once `Q` ≥ 577, and **far-3** (from 2625) once
`Q` ≥ 2625. A copy starts with a type prefix that depends on how many classes can be reached:

| Classes | 2-byte copy | long far-3 | long far-2 | long far-1 | long near |
|---|---|---|---|---|---|
| 1 | `0` + 6 bits (1–64) | | | | `1` |
| 2 | `00` + 6 bits, `01` + 8 bits (65–320) | | | `10` | `11` |
| 3 | `000` + 6 bits, `001` + 8 bits | | `01` | `10` | `11` |
| 4 | `000` + 6 bits, `001` + 8 bits | `110` | `01` | `10` | `111` |

A 2-byte copy has the distance value + 1 (6 bits), or 65 + value (8 bits).

A long copy reads its length and then its distance:
- near: 6 bits, distance value + 1;
- far-*k*: `w` bits, distance = the class start + value. `w` starts at 8 and is the smallest width for which
  start + 2^w − 1 ≥ `Q`, at most 9 (far-1), 11 (far-2) or 13 (far-3). The largest distance is therefore
  2625 + 8191 = 10816.

**The length of a long copy:** 2 bits `v`, below 3 giving `3 + v`. If `v` is 3, 3 bits `v` follow:
- below 5: `6 + v`;
- 5: 11 plus 2 bits;
- 6: 15 plus 3 bits;
- 7: 6 bits `u` follow; below 62 giving `23 + u`, 62 giving 85 plus 5 bits, 63 giving 117 plus 7 bits.

Long copies are therefore 3 to 244 bytes.

### 10.5 The 32 literal codes

For each start value `h`, a prefix code over list classes: an entry `code` base+e means the code followed by `e`
bits `v` gives list position `base + v`. Every table is a complete prefix code.

| h | codes |
|---|---|
| 0 | `(none)` 0+8 |
| 1 | `1` 64+7, `00` 0+5, `010` 32+5, `011` 192+6 |
| 2 | `0` 0+6, `10` 64+6, `11` 128+7 |
| 3 | `00` 0+4, `10` 64+6, `11` 128+7, `010` 16+4, `011` 32+5 |
| 4 | `00` 0+4, `10` 32+5, `11` 128+7, `010` 16+4, `011` 64+6 |
| 5 | `00` 0+4, `10` 32+5, `11` 64+6, `010` 16+4, `011` 128+7 |
| 6 | `0` 0+5, `11` 128+7, `100` 32+5, `101` 64+6 |
| 7 | `0` 0+5, `11` 64+6, `100` 32+5, `101` 128+7 |
| 8 | `0` 0+5, `10` 32+5, `110` 64+6, `111` 128+7 |
| 9 | `0` 0+4, `11` 64+7, `101` 32+5, `1000` 16+4, `1001` 192+6 |
| 10 | `0` 0+4, `11` 64+6, `101` 32+5, `1000` 16+4, `1001` 128+7 |
| 11 | `0` 0+4, `11` 128+7, `101` 64+6, `1000` 16+4, `1001` 32+5 |
| 12 | `0` 0+4, `11` 64+7, `100` 16+4, `1010` 32+5, `1011` 192+6 |
| 13 | `0` 0+4, `100` 16+4, `101` 32+5, `110` 64+6, `111` 128+7 |
| 14 | `0` 0+4, `11` 32+5, `100` 16+4, `1010` 64+6, `1011` 128+7 |
| 15 | `0` 0+4, `10` 16+4, `111` 128+7, `1100` 32+5, `1101` 64+6 |
| 16 | `0` 0+4, `10` 16+4, `110` 32+5, `1110` 64+6, `1111` 128+7 |
| 17 | `1` 64+7, `00` 64+7, `01` 32+5 |
| 18 | `00` 0+4, `10` 32+5, `11` 64+6, `010` 16+4, `011` 128+6 |
| 19 | `0` 0+5, `10` 32+5, `11` 64+7 |
| 20 | `0` 0+4, `11` 64+6, `101` 32+5, `1000` 16+4, `1001` 128+6 |
| 21 | `0` 0+4, `11` 64+7, `100` 16+4, `101` 32+5 |
| 22 | `0` 0+4, `11` 32+5, `100` 16+4, `101` 64+7 |
| 23 | `0` 0+4, `10` 16+4, `110` 32+5, `111` 64+7 |
| 24 | `(none)` 0+7 |
| 25 | `1` 32+5, `00` 0+4, `010` 16+4, `011` 64+6 |
| 26 | `0` 0+5, `10` 32+5, `11` 64+6 |
| 27 | `0` 0+4, `11` 32+5, `100` 16+4, `101` 64+6 |
| 28 | `0` 0+4, `10` 16+4, `110` 32+5, `111` 64+6 |
| 29 | `0` 0+4, `10` 16+4, `11` 32+5 |
| 30 | `(none)` 0+5 |
| 31 | `(none)` 0+4 |

For `h` = 17 two codes give positions 64–191 and positions 0–31 have none; the table is stated as lhasa reads it.

### 10.6 Reader decisions

A copy that reaches before the first byte (the codes can express such distances) and a stream that ends before the
output is complete are rejected.

## 11. Not decoded

Every method string seen in real archives is decoded. Multi-volume archives and encryption are not supported
(`format.md` §6.1).

## 12. How strongly each method is tested

"Tested" always means on a finite set of inputs; it is never a claim for every possible input.

| Method | Tested against |
|---|---|
| `-lh0-`, `-lhd-`, `-lz4-`, `-pm0-` | every corpus entry, byte for byte |
| `-lh4-`…`-lhx-` | every corpus entry; and **certified on chosen input**: an encoder written from the clean room's specification alone (§3 here restates it) produced 50 archives that lhasa accepted and extracted exactly; `-lh5-`, `-lh6-`, `-lh7-` also on a second encoder (UNLHA32) |
| `-lk7-` | 2 real archives, 18 blocks, every symbol checked against the known plaintext |
| `-lz5-`, `-lzs-` | every corpus entry; the starting rings read back from lhasa position by position; a second encoder (UNLHA32), 98 archives |
| `-lh1-` | 23 corpus entries and a raw stream; a second encoder (UNLHA32) |
| `-lh2-`, `-lh3-` | UNLHA32 only (lhasa does not read them): its encoder's output and its decoder on chosen input |
| `-pm2-` | two independent decoders (lhasa and PMarc's own PMEXT) on 149 PMarc-made archives; lhasa and the CRC on 5 real streams; lhasa alone for rules the encoder does not reach |
| `-pm1-` | lhasa only: 36 real and generated streams identical, 150 random streams written from the specification |
