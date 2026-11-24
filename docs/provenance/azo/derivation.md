# AZO — derivation & experimental verification

This page records how AZO's framing, arithmetic coder, and probability models were derived across three rounds of black-box
experiments (`azo-blackbox`) and refined on large binary corpora in `fultaarc-cleanroom-alzegg`.

---

## 1. Corpus generation & framing (`azo-blackbox` Round 1)

- Using ALZip with `-m4` on files named `.com` and `.sys` triggers EGG method `3` (`AZO`). Later cross-version testing
  (2026-11-13) confirmed ALZip `8.01` through `12.37` all write AZO for `.com`/`.sys` under `-m4`.
- Varying payload sizes (`2` random bytes -> `28` bytes; `1 MiB` random -> `1,048,614 = 2 + 2*(12 + 524,288) + 12` bytes)
  isolated the 2-byte `31 <flag>` header, big-endian `U | C | U^C` 12-byte chunk headers, stored vs. compressed chunks,
  `524,288`-byte (`512 KiB`) chunk cap, and 12-byte zero trailer.

---

## 2. Arithmetic coder boundary bisection (Rounds 1–2)

Testing two explicit public hypotheses — Witten–Neal–Cleary (1987) 32-bit integer arithmetic coding and the structure
style of the public-domain LZMA SDK specification — analysts swept all `256` single-byte probes after fixed prefixes and
used ALZip 8.6 as a **decoder oracle** on crafted AZO chunks:

- By bisecting the initial 32-bit big-endian code value at which ALZip's decompressed first output byte flipped, the room
  measured exact sub-interval boundaries for `244` of `249` reachable probability cells, confirming `B = 12, s = 6` flag
  adaptation, `B = 10, s = 4` tree adaptation, and 3-branch `0x40000000`/`0x80000000`/`0xC0000000` normalization.

---

## 3. Adaptive model isolation (Round 3, experiments E40–E77)

Thirty-eight registered black-box experiments resolved every model detail:

| Experiments | Question resolved |
|---|---|
| **E43–E50** | Separated apparent double-updating of length trees into **two parallel trees** (`local` vs `group`) selected per context |
| **E52** | Proved distance/length extra bits use single-division `uniform(n)` rather than `n` equiprobable binary decisions |
| **E58** | Isolated the **128-entry MTF history list** `(pos, L)` and separate 2-slot recent index cache `idx[2]` |
| **E66–E69** | Established that dual-tree counters compare quantized integer likelihood products rather than floating-point logs |
| **E59–E72** | Falsified counter saturation guesses at `+-9` and `+-31` against corpus behavior |
| **E76–E77** | Froze the complete reference model by SHA-256 (`E76`), then compressed **6 brand-new unseen inputs** (`E77`) — **6 / 6 decoded bit-for-bit cleanly** |

In total, the Round-3 reference decoder reproduced **18,809 of 18,809** retained input/output pairs byte-for-byte with
matching EGG CRC-32 (`18,764` compressed chunks). An independent re-implementation written solely from the written AZO
specification decoded all `81` AZO blocks in `egg-verify`'s corpus.

---

## 4. Corrections from binary/program testing (`fultaarc-cleanroom-alzegg`)

When testing large Windows binaries and diverse real files in `fultaarc-cleanroom-alzegg` (`1f844a6`), three edge cases
missed on smaller text/synthetic corpora were uncovered and fixed:

1. **x86 `E8`/`E9` call/jump transform (S0085–S0090):** Non-zero flag byte `31 01` on `MZ`-prefixed inputs activates the
   x86 relative-address undo filter across stored and compressed chunks alike.
2. **Score rescaling precision (S0085–S0087):** Shifting `scoreL`/`scoreG` every tree level caused a rare boundary tie
   error on 1 chunk in 115 of a 60 MB binary; starting at `2^20` and shifting both scores by 10 bits **only when either
   reaches `>= 2^22`** keeps products exact within 32 bits and fixes the binary corpus completely.
3. **Unbounded counter falsification (S0027):** Testing across 187 new archives falsified an earlier `+-127` counter clamp
   (failing 11 archives); signed 32-bit unbounded counters decode all fixtures cleanly.
