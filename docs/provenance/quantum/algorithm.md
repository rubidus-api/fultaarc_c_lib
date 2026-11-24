# CAB Quantum — the decompression method

This page describes how a Quantum folder's data is decoded. Where the data sits in a cabinet, the window parameter X
and the block rules are in [`format.md`](format.md). How each rule here was found — the observation it rests on — is
in [`derivation.md`](derivation.md): algorithm §*n* is established in derivation §M*n*. The sources and their
licences are in [`provenance.md`](provenance.md).

Quantum is an LZ77-style method — literals and copies from a sliding window — whose symbols are coded with an
adaptive model and a 16-bit arithmetic coder. Every rule below was tested against Microsoft's own reader on chosen
input and on Microsoft-made cabinets (§8). Where no sample shows what should happen, FultaArc's decoder makes a
**reader decision**, marked as such.

## 1. The bit reader

Each data block has one bit reader. It takes the block's bytes in order, **most significant bit first**. The same
reader feeds both the arithmetic decoder (its first 16 bits and every renormalisation shift) and the raw extra bits
of copies (§4, §5), so the two are interleaved in the order the decoder asks for them.

A block that needs a bit beyond its last byte is an error (format §3).

## 2. The arithmetic decoder

The decoder has three 16-bit registers: `low`, `high` and `code`. At the start of every block: `low = 0`,
`high = 0xFFFF`, `code` = the first 16 bits of the block.

**Decoding one symbol** from a table (§3) with total weight `T`:

```text
rng = high - low + 1
cum = ((code - low + 1) * T - 1) / rng                  # integer division
find the entry i whose cumulative range [lo_i, hi_i) contains cum
high = low + rng * hi_i / T - 1
low  = low + rng * lo_i / T
```

Then **renormalise**, repeating while one of the three cases holds:

```text
if high < 0x8000:                         (nothing to subtract)
elif low >= 0x8000:                       low, high, code -= 0x8000
elif low >= 0x4000 and high < 0xC000:     low, high, code -= 0x4000
else: stop
low = 2*low;  high = 2*high + 1;  code = 2*code + next bit
```

This is the integer arithmetic decoder of Witten, Neal and Cleary, "Arithmetic coding for data compression"
(CACM 30(6), 1987), at 16 bits — public mathematics; Quantum's own part is the model it drives.

## 3. Tables

Every decision is made in an adaptive **table** of *n* entries. Each entry has a weight and carries a symbol.

- **Order.** Entry 0 takes the lowest part of the code range: with weights `w[0..n-1]`, entry *i* covers
  `[w[0] + … + w[i-1], w[0] + … + w[i])`.
- **Start.** Every weight is 1, and entry *i* carries symbol *i*.
- **Symbols move.** A reorder (§6) moves entries, each with its symbol; later decisions use the new positions, and
  an update always goes to the entry that was decoded.

The tables of a folder:

| Table | Entries | Used for |
|---|---|---|
| selector | 7 | the kind of each symbol |
| literal classes 3, 4, 5, 6 | 64 each | the literal byte |
| length | 27 | the length of a selector-0 copy |
| distance for selector 2 | 2·min(X, 12) | the distance of a 3-byte copy |
| distance for selector 1 | 2·min(X, 18) | the distance of a 4-byte copy |
| distance for selector 0 | 2·X | the distance of a selector-0 copy |

All tables start afresh at the start of a folder and carry over from block to block (format §2).

## 4. Symbols

Every symbol starts with a decision in the **selector**. Its symbol *k* says what follows:

- **k = 3, 4, 5 or 6: a literal.** A second decision in literal class *k*'s table gives index *i* (0..63). The byte
  is `(6 − k) · 64 + (63 − i)`: class 6 holds bytes 0x00–0x3F, class 3 bytes 0xC0–0xFF, and inside a class the
  higher index is the lower byte.
- **k = 2: a copy of 3 bytes**, then a distance from the selector-2 distance table (§5).
- **k = 1: a copy of 4 bytes**, then a distance from the selector-1 distance table.
- **k = 0: a copy with a coded length.** In this order: a decision in the length table, the length's raw extra
  bits, a decision in the selector-0 distance table, the distance's raw extra bits.

**Length slots** (selector 0). The length table's symbol *j* gives slot *s* = 26 − *j*; the length is the slot's base
plus its extra bits, read raw, most significant bit first:

| Slots *s* | Lengths | Extra bits |
|---|---|---|
| 0–5 | 5, 6, 7, 8, 9, 10 | 0 |
| 6–9 | 11–12, 13–14, 15–16, 17–18 | 1 |
| 10–13 | 19–22, 23–26, 27–30, 31–34 | 2 |
| 14–17 | 35–42, 43–50, 51–58, 59–66 | 3 |
| 18–21 | 67–82, 83–98, 99–114, 115–130 | 4 |
| 22–25 | 131–162, 163–194, 195–226, 227–258 | 5 |
| 26 | 259 | 0 |

## 5. Distances

A distance table of *N* entries (§3) gives symbol *j*; the slot is *s* = *N* − 1 − *j*, so entry 0 at the start is
the farthest slot. Slots 0–3 are distances 1–4 with no extra bits. For *s* ≥ 4, with *e* = (*s* − 4) / 2 + 1
(integer division):

```text
distance = 2^(e+1) + 1 + ((s - 4) mod 2) * 2^e + (e extra bits, raw, most significant bit first)
```

| Slots *s* | Distances | Extra bits |
|---|---|---|
| 0, 1, 2, 3 | 1, 2, 3, 4 | 0 |
| 4, 5 | 5–6, 7–8 | 1 |
| 6, 7 | 9–12, 13–16 | 2 |
| 8, 9 | 17–24, 25–32 | 3 |
| 2*e* + 2, 2*e* + 3 | 2^(*e*+1)+1 … 2^(*e*+1)+2^*e*, … 2^(*e*+2) | *e* |

So a table of 2·*m* slots reaches distance 2^*m*: 4096 for 3-byte copies (*m* ≤ 12), 2^18 for 4-byte copies
(*m* ≤ 18), the whole window 2^X for selector 0.

The extra bits are read from the bit reader at its current position: just after the bits the arithmetic decoder has
already shifted into `code`. They are not arithmetic-coded.

A copy takes its bytes one at a time from `distance` bytes back in the output, so a copy may overlap its own output
(distance smaller than length).

## 6. Adaptation

**Increment.** After every decision the decoded entry's weight grows by **8** — in the selector, in the literal class
table, in the length table and in the distance table, whichever were used.

**Rescale.** When an increment makes a table's total exceed **3800**, the table is rescaled at once. A table counts its
rescales. At its **4th** rescale and every **50th** after it (the 4th, 54th, 104th, …) the table is *reordered*;
every other rescale is *ordinary*.

**Ordinary rescale** — halve the cumulative counts, keeping every weight at least 1:

```text
c[0] = 0;  c[i] = w[0] + … + w[i-1]   (i = 1..n)
c[i] = c[i] / 2                        (floor)
for i = 1..n:  c[i] = max(c[i], c[i-1] + 1)
w[i] = c[i+1] - c[i]
```

The fix-up gives an entry that would fall to 0 a weight of 1, taken from the entries after it.

**Reorder** — sort the entries on the keys `key = (w + 1) / 2` (the weights halved rounding up) by this exchange
process, each entry carrying its weight and symbol; then set every weight to its key:

```text
repeat
    swapped = false
    for i = n-1 down to 1:
        for j = i-1 down to 0:
            if key[j] > key[i]:             # strictly greater; the first such j going down
                exchange entries i and j; swapped = true
                break
until not swapped
```

The result is ascending by key. Among equal keys the order is whatever this process leaves; it is not a stable sort,
and FultaArc's decoder follows it exactly.

## 7. The window

The output of a folder is the window. It starts as zeros.

**Reader decision:** a copy whose source would lie before the first byte of the folder's output is an error.
Microsoft's reader returns zeros for such a copy at most windows, and output that cannot be reproduced at X = 10;
no Microsoft-made stream that was examined contains one.

A copy that would run past the end of the block (`cbUncomp`) is an error (format §2).

## 8. How strongly it is tested

Every rule was tested — not proven — on these inputs:

- **Microsoft's reader as an oracle.** 49,476 recorded queries whose output the room's reference decoder reproduces
  (about 39 MB of output) decode byte-identically with the C decoder, under AddressSanitizer and UBSan, plus a fuzz
  pass of two random bit flips per record.
- **Chosen plaintext.** The room's own encoder, built from these rules, produced whole folders that Microsoft's
  reader decoded to exactly the intended plaintext: literal cycles, random data, copies of every class with every
  slot, mixes in up to 40 blocks, at X = 10, 12, 15, 16, 18 and 21; every table type through hundreds of rescales, past the 4th, 54th
  and 104th.
- **Microsoft-made cabinets.** 22 cabinets shipped with Windows 95, Plus! for Windows 95 and Internet Explorer 2.0
  (2,160 files, about 77 MB, X = 21, many folders continued across cabinets; the selector passes about 13,000
  rescales): every file identical to `extrac32`'s.
- **Microsoft's encoder.** Cabinets made by Diamond 1.00.0530 (1995) from the room's own plaintext at every X = 10
  to 21, including a leading 64 KiB zero run, matches at distances 2^X − 1, 2^X and 2^X + 1, 1 MiB of incompressible
  data and copies of length 259: identical to the plaintext and to `extrac32`.

What is **not** covered: weight patterns that neither the room's generators nor Microsoft's encoders produced, a
behaviour that would change only after many more rescales than were seen, and the reader decisions above, which no
Microsoft-made stream exercises.
