# RAR 1.5 Encrypted Archive Decryption and Decompression Specification

- **Target Library**: `tools/libcleanroom/` (`02-mit-library/`, `SPDX-License-Identifier: MIT`, C99 `-Wall -Wextra -Werror -pedantic -std=c99`)
- **Scope**: RAR 1.5 non-solid encrypted and plaintext archives, Store (`0x30`) and compressed methods `0x31..0x35`
- **Security Boundary**: Fail-closed rejection on `SPLIT_BEFORE`, `SPLIT_AFTER`, `SOLID`, `LARGE`, directory entries (`0x00E0`), and path traversal (`Zip-Slip`), with `0` external symbol imports (`nm -u`)

---

## 1. End-to-End Pipeline

RAR 1.5 archives store an independent `PACK_SIZE` payload per entry using **Compress-then-Encrypt**. Extraction always executes: **(1) Container Parse & Security Gate -> (2) Keystream XOR Decryption -> (3) Decompression -> (4) Plaintext CRC-32 Check**.

```text
Archive Bytes
  +-> [1] rar15_list()          : "Rar!\x1a\x07\x00" magic + 0x73/0x74 block bounds
        +-> [2] Security Gate   : reject SPLIT/SOLID/LARGE/DIR, method in 0x30..0x35, Zip-Slip guard
              +-> [3] Decryption (if flags & 0x0004):
                    rar15_keystream(pw, ks, PACK_SIZE) -> rar15_decrypt_block(data ^= ks)
                    +-> [4] Decompression:
                          rar15_unpack(0x30 memcpy | 0x31..0x35 u15_init + u15_unpack LZSS/Huffman)
                          +-> [5] rar15_crc32(plain, UNP_SIZE) == FILE_CRC -> write_file()
```

---

## 2. Container Parsing and Security Guards (`rar15_container.c`, `rarextract.c`)

1. **Archive Magic**: First `7` little-endian bytes must equal `52 61 72 21 1a 07 00` (`"Rar!\x1a\x07\x00"`).
2. **Generic Block Header (`7` bytes)**:
   - `0..1`: `HEAD_CRC` (`uint16_t`), `2`: `HEAD_TYPE` (`0x73` main, `0x74` file), `3..4`: `HEAD_FLAGS` (`uint16_t`), `5..6`: `HEAD_SIZE` (`uint16_t`).
   - Returns `RAR15_ERR_FORMAT` whenever `HEAD_SIZE < 7` or `HEAD_SIZE > arc_len - p`.
3. **4-Byte `ADD_SIZE` Bounds & Overflow Guard**:
   - Offset `+7` holds a 32-bit `ADD_SIZE` (= `PACK_SIZE` on `0x74`) iff `type == 0x74` or `(flags & 0x8000) != 0`.
   - Enforces `HEAD_SIZE >= 11` before reading `ADD_SIZE`, and `add_size <= arc_len - p - HEAD_SIZE` to block 32/64-bit pointer wraparound.
4. **File Header (`0x74`, minimum `32` bytes)**:

| Offset | Size | Field | Description |
|---:|---:|---|---|
| `0..6` | `7` | Generic | `HEAD_CRC`, `HEAD_TYPE=0x74`, `HEAD_FLAGS`, `HEAD_SIZE` |
| `7..10` | `4` | `PACK_SIZE` | Encrypted/compressed payload size on disk |
| `11..14` | `4` | `UNP_SIZE` | Expected plaintext output size |
| `15` | `1` | `HOST_OS` | Creating OS byte |
| `16..19` | `4` | `FILE_CRC` | IEEE 802.3 CRC-32 of uncompressed plaintext |
| `20..23` | `4` | `FTIME` | MS-DOS timestamp |
| `24` | `1` | `UNP_VER` | Minimum version required to extract |
| `25` | `1` | `METHOD` | `0x30` (Store), `0x31..0x35` (LZSS + Canonical Huffman) |
| `26..27` | `2` | `NAME_SIZE` | Filename byte length (requires `name_size <= head_size - 32`) |
| `28..31` | `4` | `ATTR` | File attributes |
| `32..` | `NAME_SIZE` | `NAME` | Archive member relative path |

5. **Security Gate & Fail-Closed Flags**:
   - Rejects `0x0001 SPLIT_BEFORE`, `0x0002 SPLIT_AFTER`, `0x0010 SOLID`, `0x0100 LARGE`, and directory entries (`(flags & 0x00E0) == 0x00E0`).
   - `is_safe_rel_path(NAME)` rejects empty names, leading `/`, `\`, `:`, control bytes (`< 0x20`), `.` / `..` segments, and empty segments (`//`).

---

## 3. Stream Cipher Decryption (`rar15_cipher.c`)

When `RAR15_LHD_PASSWORD (0x0004)` is set, `PACK_SIZE` bytes of payload are XORed with a **fresh keystream starting at index `0` for each entry**.

1. **Standard CRC-32 Table ($T_{\text{ref}}$)**: Built locally on the stack from the IEEE 802.3 reflected polynomial `0xEDB88320` (no mutable globals).
2. **Key Schedule (effective prefix $e = \text{pw}[:77]$, unsigned $c \in \{0..255\}$)**:

$$R = \texttt{0xFFFFFFFF}, \qquad H = 0, \qquad S_0 = \texttt{0xFFFF}$$

$$R \leftarrow (R \gg 8) \oplus T_{\text{ref}}[(R \oplus c) \mathbin{\&} \texttt{0xFF}], \quad H \leftarrow (H + (T_{\text{ref}}[c] \gg 16) + c) \bmod 2^{16}, \quad S_0 \leftarrow S_0 \oplus ((T_{\text{ref}}[c] \mathbin{\&} \texttt{0xFFFF}) \oplus c)$$

3. **5-Word State Initialization & Per-Byte Step ($i = 0, \dots, \text{PACK\_SIZE}-1$)**:
   - Init: $W = R \mathbin{\&} \texttt{0xFFFF}$, $M = \text{rol}_{16}((R \gg 16) \mathbin{\&} \texttt{0xFFFF}, 1)$, $K_2 = \texttt{0xFFFF}$, $H_r = H$, $S = S_0$.
   - Step:

$$W' = (W + \texttt{0x1234}) \bmod 2^{16}, \qquad u = (W' \gg 1) \mathbin{\&} \texttt{0xFF}, \qquad t = T_{\text{ref}}[u]$$

$$S \leftarrow (S + (t \gg 16)) \bmod 2^{16}, \qquad M \leftarrow \text{ror}_{16}(M, 2) \oplus \text{ror}_{16}(t \mathbin{\&} \texttt{0xFFFF}, 1)$$

$$K_2 \leftarrow K_2 \oplus M, \qquad H_r \leftarrow \text{ror}_{16}(H_r, 2), \qquad W \leftarrow W' \oplus S \oplus H_r \oplus K_2$$

   - XOR decrypt: $\text{ks}[i] = (W \gg 8) \mathbin{\&} \texttt{0xFF}$, $\text{data}[i] \leftarrow \text{data}[i] \oplus \text{ks}[i]$.
4. **Fail-Closed Wrong Password Rejection**: Any wrong password produces a pseudorandom packed stream that fails either `u15_unpack` (`RAR15_ERR_FORMAT`) or plaintext `FILE_CRC` (`RAR15_ERR_CRC`), never writing unverified output.

---

## 4. Decompression Algorithms (`rar15_unpack.c` & `u15_unpack.{h,c}`)

`rar15_unpack(method, in, in_len, out, out_len)` restores exact `UNP_SIZE` plaintext from decrypted `in`. Methods `0x31..0x35` share a **single unified decoder (`u15_unpack`)**; `-m1..-m5` only control encoder search effort.

### 4.1 Store Mode (`0x30`)
- Returns `RAR15_ERR_FORMAT` if `in_len != out_len`; otherwise performs `memcpy(out, in, out_len)` and returns `RAR15_OK`.

### 4.2 Compressed Modes (`0x31..0x35` -- Adaptive LZSS + Canonical Huffman Decoder)

1. **Initialization (`u15_init`)**: Initialized cleanly per non-solid file entry (solid archives rejected).
   - $64\text{ KiB}$ circular window `win[65536]` (`total = 0`, out-of-history reads `total < d` return `0x00`), MRU cache `recent[4]`, `dlist[i] = i + 1` ($i=0..255$).
   - Three adaptive move-forward rank lists (`u15_list`): `lit` (`lim = 161`), `flag` (`lim = 255`, `sym[0] = 0xFF`, `sym[1..255] = 0..254`), `block` (`lim = 255`, initial `cnt[i] = 7 - (i >> 5)`).
   - Adaptive control state: `S = 53`, `kv = 288` ($2U$ for `U = 144`), `pfar = true`, `swapped = false`, `lmode = false`.

2. **MSB-First Bitstream & 11 Fixed Canonical Huffman Tables (`u15_unpack.c:15-34`)**:
   - `get_bit`: extracts MSB-first (`7 - (pos & 7)`).
   - `get_code`: traverses bit-length count tables returning index `idx` (fails closed with `U15_E_UNOBSERVED` on `idx >= 256` or `U15_E_TRUNCATED` on EOF).

| Table | `maxlen` | Symbol Counts by Bit Length (`count[1..maxlen]`) | Primary Role |
|---|---:|---|---|
| `T0` | 12 | `5:4, 6:40, 7:16, 8:16, 9:4, 11:47, 12:130` | Default literal / mid-range block high byte |
| `T1` | 10 | `5:2, 6:5, 7:46, 8:64, 9:116, 10:24` | Literal, flag byte, `dlist`, short `lmode` position |
| `T2` | 10 | `6:2, 7:14, 8:202, 9:33, 10:6` | Skewed literal (`10590 <= S < 16758`) |
| `T3` | 9 | `8:255, 9:2` | Heavily skewed literal (`S >= 16758`) |
| `TL` | 12 | `4:8, 5:8, 6:8, 7:9, 12:224` | Low-entropy literal (`S <= -9971`) / block (`kd < 1799`) |
| `CR` | 12 | `2:2, 3:1, 4:2, 5:2, 6:4, 7:5, 8:4, 9:4, 10:8, 12:224` | `step_r` / `step_p(layout=1)` length |
| `CC` | 12 | `3:5, 4:2, 5:2, 6:4, 7:5, 8:4, 9:4, 10:8, 11:2, 12:220` | `step_p(layout=2)` / `Q_FAR` length |
| `QA` / `QAF` | 8 | `QA: 1:1, 3:2, 4:3, 5:1, 6:1, 7:1, 8:2` (`QAF: 3:1, 4:5`) | Short-match/repeat/escape dispatch for `qs < 39` |
| `QB` / `QBF` | 6 | `QB: 2:1, 3:4, 4:3, 5:1, 6:2` (`QBF: 3:3, 4:5`) | Short-match/repeat/escape dispatch for `qs >= 39` |

3. **Adaptive Rank Lists (`list_use`)**:
   - Selecting code index `r` emits `s = sym[r]`, increments `cs = ++cnt[s]`, swaps `r` with the first earlier slot `i` where `cnt[sym[i]] < cs`, and renormalizes `cnt[sym[i]] = 7 - (i >> 5)` whenever `cs > lim`.

4. **Decode Loop (`step`, Polarity Flip `kind_step`, and `step_lmode`)**:
   - Refills 8-bit flag byte via `T1 + flag` when `fleft == 0`; if consecutive literals `run >= 17`, enters **literal mode (`lmode = true`)**.
   - **Literal** (`(!swapped && f == 0)` or `(swapped && f == 1 && k == 0)`): selects `{TL, T0, T1, T2, T3}` via `S` thresholds `-9971 / 310 / 10590 / 16758`, updates $S \leftarrow S + r - 53 - \lfloor S / 256 \rfloor$, and calls `kind_step(false)`.
   - **P-Match (`step_p`)**: decodes length `n = 3 + t` via 3 `ky` layouts (`<66 / <125 / >=125`), block upper 8 bits via `kd` (`<1799 / <10537 / >=10537` using `TL/T0/T1`) + 7 low bits `d = (blk << 7) | low`, adds `n += 8` if `d <= 256` and `n += 1` if `d > 8192 && (pfar || d >= 32512)`, and calls `kind_step(true)`.
   - **Q-Match (`step_q`)**: if `rep >= 2`, 1 bit repeats `(last_n, last_d)`; else `QA/QB` (or `QAF/QBF` when `far`) selects length `2..10`, `Q_REP`, `Q_R` (`step_r` MRU `recent[0..3]` + `CR`), or `Q_FAR` (`d = 32768 + low15`, `n = 5 + CC`). General distance codes traverse `dlist[256]` with adjacent swap.
   - `kind_step` tracks literal vs match balance via `kv` (`U = 144`, thresholds `153 / 145`, halving every 7th A-step) to invert flag polarity `swapped` dynamically.
   - **Literal Mode (`step_lmode`)**: code `i >= 1` emits literal `i - 1`; `i == 0` followed by bit `1` exits `lmode`, bit `0` copies short match `(len = 3 + b3, d = (pos << 5) | low5)`.

---

## 5. Integrity Check & Return Codes (`rar15_crc32.c`, `rar15.h`)

- Plaintext output is written to disk **only after** `rar15_unpack(...) == RAR15_OK` AND `rar15_crc32(plain, UNP_SIZE) == FILE_CRC`.
- Return codes: `RAR15_OK (0)`, `RAR15_ERR_PARAM (-1)`, `RAR15_ERR_FORMAT (-2)`, `RAR15_ERR_UNSUPPORTED (-3)`, `RAR15_ERR_CRC (-5)`, `RAR15_ERR_BUFFER (-6)`.

---

## 6. Gate Fixture Provenance & Measured `0x30..0x35` Matrix (`tests/fixtures/gate/`)

All `6/6` Universal Decryption Gate runs (`600` CSPRNG passwords / `1,800` files) and offline `P4` archive tests (`36` archives / `108` entries) use the fixtures below:

| Filename | Kind | Plain Size | Plain CRC-32 | Plain SHA-256 | Provenance & Rights |
|---|---|---:|---|---|---|
| `T.TXT` | Text | `15,360` B | `0xc2d40f28` | `c0e95aceb33a2ebc079bd110e3e1b28e3e0664d8944ec4d9d525703d61562eee` | AI-generated synthetic example (`owner-confirmed`, no copyright conflict) |
| `I.PNG` | PNG Image | `6,447` B | `0x19cd3b82` | `eb09fa49118607699bb85ad6f24400cdccec0dc019d40604274da2fa13275be7` | AI-generated synthetic example (`owner-confirmed`, no copyright conflict) |
| `X.EXE` | DOS Binary Sample | `1,024` B | `0x162a76f8` | `e72c206a044108250eb7c236c536ef14214ed66b2352058b8dca0ab8f177adb1` | AI-generated synthetic example (`owner-confirmed`, no copyright conflict) |

Encrypted `PACK_SIZE` measurements across all `0x30..0x35` methods (`work/archive/323-.../files/{0010..0040}-m*_0.rar`, `HEAD_FLAGS = 0x8004`):

| Method (`METHOD`) | `T.TXT` `PACK_SIZE` | `I.PNG` `PACK_SIZE` | `X.EXE` `PACK_SIZE` | Notes |
|---|---:|---:|---:|---|
| `0x30 (Store, -m0)` | `15,360` B | `6,447` B | `1,024` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x31 (Fastest, -m1)` | `421` B | `6,456` B | `312` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x32 (Fast, -m2)` | `313` B | `6,457` B | `312` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x33 (Normal, -m3)` | `275` B | `6,456` B | `314` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x34 (Good, -m4)` | `273` B | `6,457` B | `314` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x35 (Best, -m5)` | `272` B | `6,457` B | `313` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
