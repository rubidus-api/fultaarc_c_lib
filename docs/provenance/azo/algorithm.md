# AZO — decoding algorithm

This page describes FultaArc's **decode-only** specification for EGG method `3` (`AZO`). Nothing in FultaArc describes
or implements AZO compression/encoding.

---

## 1. Stream framing & x86 call/jump undo

After any EGG decryption, an AZO block payload starts with a 2-byte header followed by big-endian chunks of up to
`524,288` (`0x80000`) decoded bytes, ending with 12 zero bytes:

```text
header : 0x31 <flag>          (profile 0x31 only; flag bit 0 enables x86 E8/E9 undo)
chunk  : u32_be U | u32_be C | u32_be (U ^ C) | C bytes
end    : 00 00 00 00  00 00 00 00  00 00 00 00
```

- `U == C`: stored chunk (`C` raw bytes, still passed through the `E8`/`E9` undo transform if `flag & 1`).
- `U != C`: compressed chunk decoding to `U` bytes with **all coder probabilities, selector counters, MTF history, and
  distance caches reset completely** (no state carries across chunks).
- `U == 0`, `C == 0`, `(U ^ C)` mismatch, or trailing bytes after the 12-zero terminator are rejected as corrupt.
- **Call/jump filter (`flag & 1`):** observed whenever input begins with `"MZ"` (`4D 5A`). When enabled, after decoding each
  chunk of length `n`, undo `0xE8`/`0xE9` operands while `i < n - 4`: whenever `d[i]` is `0xE8` or `0xE9` and `d[i+4]` is
  `0x00` or `0xFF`, subtract `i` modulo `2^32` from the LE32 operand and advance by `5`; otherwise advance by `1`.

---

## 2. 32-bit binary arithmetic coder

AZO uses a Witten–Neal–Cleary 32-bit integer interval coder (`low = 0`, `high = 0xFFFFFFFF`, initial 32-bit MSB-first
`value`):

- **Normalization**: while `high < 0x80000000` (`sub = 0`), or `low >= 0x80000000` (`sub = 0x80000000`), or
  `low >= 0x40000000 && high < 0xC0000000` (`sub = 0x40000000`), shift `(low - sub) << 1`, `((high - sub) << 1) | 1`,
  and `((value - sub) << 1) | next_bit()`. Past the chunk end, `next_bit()` returns `0` up to a safety bound of `64`
  terminal zero bits (corpus max observed: `43`).
- **Probability decision `bit(p, B)`**: 64-bit interval split `zero = ((high - low + 1ULL) >> B) * p`.
- **Single-step `uniform(n)`**: one division `w = (high - low + 1ULL) >> n`, `e = (value - low) / w` (not `n`
  consecutive `1/2` coin flips).
- **Adaptive models** (all initialized to `2^(B - 1)`):
  - Metadata flags: `B = 12`, shift `s = 6` (initial `2048`).
  - Prefix/tree bits: `B = 10`, shift `s = 4` (initial `512`).

---

## 3. Token grammar, dual trees & MTF history

Chunk initial state: `tokenHist = 0`, `histHist = 0`, `repHist = 0`, `shortHist = 0`, repeat distances `dist = [1, 2]`,
recent history-index cache `idx = [0, 1]`, and a 128-entry MTF history list `entries = [(0, L) for L in 2..129]`.

While `pos = len(out) < U`:
1. `isMatch = (pos == 0) ? 0 : flag('flag', tokenHist)`, shift into `tokenHist & 0xFF`.
2. **Literal (`isMatch == 0`)**: decode 8 bits via dual tree `('literal', ctx=prev, group=prev>>5, depth=8)`.
3. **Match (`isMatch == 1`)**: read `A = flag('history', histHist)`:
   - If `A == 1` (**history match**): read `A1 = flag('short', shortHist)`. If `A1 == 1`, pick index `idx[tree('short_index', 1)]`
     and move that entry in `idx` to front; else decode `tree('long_index', 7)` and insert at front of `idx`. Pop
     `entries[i]` (`(src, L)`), move to front of `entries`, and copy `L` bytes at distance `pos - src` (no length symbol coded).
   - If `A == 0`: read `S = flag('repeat', repHist)`:
     - If `S == 1` (**repeat distance**): pick `d = dist[tree('repeat_index', 1)]`, move to front of `dist`, derive `slot = slot_of(d)`.
     - If `S == 0` (**new distance**): `slot = tree('distance', 7)`, `d = distance_from(slot)`, push `d` onto `dist`.
     - Decode `lslot = dual('length', ctx=slot, group=slot>>4, depth=7)`, compute `L = length_from(lslot)` (max `32,793`),
       and push `(pos, L)` to front of `entries`.

Slot formulas:
- `distance_from(g)`: for `g < 20`, `g + 1`; else `n = (g - 16) >> 2`, `j = (g - 16) & 3`, returning
  `17 + 4*((1<<n) - 1) + (j<<n) + uniform(n)`.
- `length_from(g)`: for `g < 40`, `g + 2`; else `n = (g - 32) >> 3`, `j = (g - 32) & 7`, returning
  `34 + 8*((1<<n) - 1) + (j<<n) + uniform(n)`.

**Dual-tree selector arithmetic (`dual`)**: Each `(category, ctx)` holds an unbounded signed 32-bit counter `c` (initially
`0`) choosing between a `local` tree (`c <= 0`) and a `group` tree (`c > 0`). Both trees are updated on every decoded bit `b`.
Cumulative likelihood scores `scoreL` and `scoreG` start at `2^20`, multiply by each post-update probability `qL`/`qG`
(out of `1024`), and right-shift both together by `10` **only when either reaches `>= 2^22`** (preventing precision loss
from shifting every bit level). At symbol end, `c += (scoreG > scoreL) - (scoreL > scoreG)`.
