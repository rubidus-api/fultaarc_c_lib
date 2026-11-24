# FINAL: Clean-Room Recovery, Constructive Bridge, and Hardened Delivery of `rar15_keystream` (`enc3`)

- **Mission status**: **`STATE: DONE`** (`RESULT: ACHIEVED` verified across **`6` total full runs** of pinned `tests/universal_decryption_gate.py`: `3` pre-hardening runs incl. external audit `REVIEW.ko.txt` on `bad0d94`, plus **`3` post-hardening runs** = `2` author + `1` fresh independent reviewer in `docs/cleanroom/FINAL-REVIEW-324.md` -> `600 / 600` fresh OS-CSPRNG passwords, `1,800 / 1,800` encrypted files byte-exact).
- **Updated**: `2026-11-24` (updating the initial `2026-11-23` summary with Cycle `324` `FORCED` constructive bridge, `3x` audit + `REVIEW.ko.txt` factual corrections, security hardening, and `FINAL-REVIEW-324.md`).
- **Epistemic classification (Amendment 2026-11-16)**: **Owner-approved-prior + data-derived + constructively bridged + blind-validated** (`docs/provenance/AMENDMENT-2026-11-16-owner-approved-priors.md`). Not claimed as zero-prior derivation without `RECALL-208` / `RECALL-209`, and preserves all historical process disclosures in Section 6.
- **Standalone MIT research reports**:
  - English (LaTeX): `docs/cleanroom/RAR15-KEYSTREAM-RESEARCH-REPORT.md`
  - Korean (LaTeX): `docs/cleanroom/RAR15-KEYSTREAM-RESEARCH-REPORT.ko.md`
  - Provenance boundary & pure MIT notice: `docs/cleanroom/PROVENANCE.md`, `LICENSE`
- **Delivered hardened production implementation (`tools/libcleanroom/`)**:
  - `tools/libcleanroom/include/rar15.h` (`sha256: 193ff31fc66d42b7b5f2f5cba7c7d3a32977161de7da78785300851e2c4a65eb`)
  - `tools/libcleanroom/src/rar15_cipher.c` (`sha256: 1fd0002b6d00443552837502962deb18c9119f090b1cd1a279f53372cf36e5cf`)
  - `tools/libcleanroom/src/rar15_crc32.c` (`sha256: 8573276bfd2299cbd9c4348e23c2cb41a08ce455ecd6fd8ffbaba9e34e4c2851`)
  - `tools/libcleanroom/src/rar15_container.c` (`sha256: 48d6f8486deb36128c913048f468d03125ab9efbfbd02675a6632f72d7c31f00`)
  - `tools/libcleanroom/src/rar15_unpack.c` (`sha256: df0fca779f8d5e0e72da2d1aa5be8be31017e55e370ee2a9fa6a921acaf25b8a`)
  - `tools/libcleanroom/src/u15_unpack.h` (`sha256: 17fba8a5c4bca9a7aa53da5f38861a660821d4ed4a4dc0003f5d4138a97ce868`)
  - `tools/libcleanroom/src/u15_unpack.c` (`sha256: b2e70e20da5bc3ce7cf4f160ff1da71fd7a24391da6c64c84a5ee353507b4ce5`)
  - `tools/libcleanroom/src/rarextract.c` (`sha256: 3b2159f63c1556f16b4a18921a4186ecbe7d801bf18e4870d5b63a6b1777ab6e`)
  - `tools/libcleanroom/src/rarkit.c` (`sha256: 6f0406af209aeb906fcdcb2cd8cdba702305e98443bbad264dc097bd6b6d6f76`)
  - `tools/libcleanroom/tests/test_rar15.c` (`sha256: 30c31f236d757c1e2bc47e096307401b4b948c4a4c089bfba1570d0a841dafc5`)

---

## 1. Explicit Provenance Boundary (`APPROVED-PRIOR` vs `DATA-DERIVED`)

Per `AGENTS.md` and `docs/provenance/AMENDMENT-2026-11-16-owner-approved-priors.md`:

| Component | Classification | Source / Basis | Verification |
|---|---|---|---|
| Small 16-bit iterated word-state class (5 live 16-bit words `w, m, k2, hr, s_state` initialized from `<= 54` effective bits) | `APPROVED-PRIOR` (`RECALL-208`, non-physical) | Hypothesis class only (`Amendment 2026-11-16`); consistent with `cycle 257/262/265` `~53`-bit determining triple and `cycle 250` `>= 2^41.28` state support-size lower bound | Constructive recovery (`Cycle 324` `FORCED`), `P1..P6`, `6x` Gate |
| Reflected IEEE 802.3 CRC-32 `0xEDB88320` table `T_ref` & byte-wise update | `APPROVED-PRIOR` (`RECALL-209` candidate structure on `cycle 208` data poly) | Degree-`32` primitive factor `0x104C11DB7` of degree-`33` `(x + 1) * CRC-32 = 0x30D4326D9` derived in `cycle 208`; `build_crc32_table` generates `T_ref` mathematically on stack | `256/256` `c238`, `68/68` `c322`, `128/128` `c247`, `P1..P6`, `6x` Gate |
| `e = pw[:77]`, unsigned `uint8_t` byte promotion (`0..255`) | `DATA-DERIVED` | `cycles 002, 100, 109`; `k109` (`0/400` vs `240/400` signed) & blind `P3`/`P6_raw` | Blind `P1`/`P3`/`P6` (`0/40` vs `40/40` signed failures) |
| `H = sum((T_ref[c] >> 16) + c) mod 2^16` | `DATA-DERIVED` | `(-3573) * 0x89A3 == 1 (mod 65536)` across `256/256` bytes + `c248` Stage-I fibre scan (`5,888` pairs -> unique `hr_rot = 2`) | Constructive `Cycle 324` Stage I + `P1..P6` + `6x` Gate |
| `S_0(bx) = 0xFFFF ^ T_LO[bx] ^ bx` | `DATA-DERIVED` | `Cycle 324` Stage-IV exhaustive `128`-bin x `65,536` scan (`0/128` errors vs `0xFFFF ^ T_LO[bx] ^ bx`), unifying `c247`, `c248`, `c257` | Constructive `Cycle 324` Stage IV + `P1..P6` + `6x` Gate |
| Initial words `(W = R & 0xFFFF, M = rol16(R >> 16, 1), K2 = 0xFFFF, Hr = H, S = S_0)` & step loop (`W += 0x1234`, `ror16(M,2)^ror16(t_lo,1)`, `K2 ^= M`, `ror16(Hr,2)`, `S += t_hi`, `W ^= S^Hr^K2`, `W >> 8`) | `DATA-DERIVED` | `Cycle 324` Stage II GF(2) left-nullspace bit-peeling (`add_c mod 2^15 = 0x1234`, `[0x7200..0x7220]`, unique `m1_rot=1, t_rot=1`) & Stage III (`('0x8','0xfe/0xff')`, `m_rot=2, t_rot=1, acc=True`), unique up to exact `Z_2^3`/`Z_2^4` black-box gauge | Constructive `Cycle 324` (`FORCED`), `Planted Even` `8` / `Odd` `4`, `P1..P6`, `6x` Gate |

---

## 2. Derivation, Constructive Bridge & Audited Reconciliations (`Cycles 001..324`)

1. **Phase 0..3 Empirical Chain (`Cycles 001..322`)**: Deterministic `f(pw[:77])`, store XOR, degree-`33` polynomial `0x30D4326D9 = (x + 1) * 0x104C11DB7` (with degree-`32` primitive CRC-32 factor `0x104C11DB7`, `Cycle 208`), `~41.28`-bit **state support-size statistical lower bound** (`Cycle 250`, not password security strength), dead CRC bit `0` (`Cycles 259/265`), `(-3573) * 0x89A3 == 1 mod 2^16` (`Cycle 238`), multi-`bx` separability (`Cycles 239/247/248`), and `u`-coordinate carry structure (`Cycles 319..322`).
2. **Closed-Form Synthesis & Initial Blind Validation (`Cycle 323`, `LEAP`, `CONFIRMED 3/3`, `SOLID`)**:
   - **Reconciled calibration breakdown**: `44,545` archived calibration rows consist of `20,245` end-to-end password-to-output rows (`4,559,680` B) plus `24,300` state-seeded `work/c257/corpus.json` rows (`777,600` B); calibration checks establish fit consistency on prior data, not independent null model-selection trials.
   - **Reconciled `P1..P5` strictly unseen counts** (integrating `R2`, `3x` audits, `REVIEW.ko.txt`, and `Cycle 324 #11/#19/#21`): `P1` (`250` rows) has `220` distinct `[:77]` prefixes (`30` shared-`77`-prefix pairs); against ALL exhaustive short corpora (`enum12_n64.bin` `7,310` rows + `enum3_n64.bin` `614,125` rows), `9` `enum12` rows + `3` `enum3` rows (`b'/SM'`, `b'jAa'`, `b'Ui['`) = `12` rows (`768` B) overlapped, leaving **`234,752 / 235,520` strictly unseen `P1..P3` bytes** (`234,944` strictly relative to `c323`'s 7 designated calibration files); `P4` `108/108` `rarkit unpack -K` across methods `0x30..0x35`; `P5` (`cand_check` `200` rows, `7` overlaps `>= 448` B per `REVIEW.ko.txt`).
3. **Constructive Bridge & Gauge Classification (`Cycle 324`, `FORCED`, `CONFIRMED 3/3`, `SOLID`, `archive verify: OK` `#1..#21`)**:
   - Preserves the historical `2026-11-23` `71`-minute unarchived `Cycle 322 -> 323` synthesis gap transparently per `docs/ROLES.md:107-108` while proving zero-hardcoded-constant algorithmic recoverability strictly from pre-`323` data under `RECALL-208`/`209`.
   - Recovers unique `hr_rot = [2]` (`5,888` pairs), `add_c mod 2^15 = 0x1234`, `[0x7200..0x7220]`, `(m1=1, t=1)`, `(0x8, 0xfe/0xff)`, `(m=2, t=1, acc=True)`, `128/128` unique `S_0(bx)` (`0/128` GF(2) errors), `k109` (`0/400` unsigned vs `240/400` signed), `256/256` `c238`, `68/68` `c322 K_r`, and `0xED02`.
   - Proves the exact **`16`-element `Z_2^4` (`8`-element `Z_2^3` solver orbit)** gauge group (`max_diff = 0` across `500` x `256` B) and **Parity-Collapse Lemma** (`Planted Even` `8` reps vs `Planted Odd` `4` MSB-only reps, `0` held-out error; `4/4` unpruned negatives rejected).
   - Validates `P6` (`work/c324/blind_holdout.json.gz`, `min_len = 4`, **`0` overlap** with `enum12`, `enum3`, `c323`, and all prior calibration files): `0/80` (`0/20,480` B) `P6_safe`, `16/16` prefix pairs identical, `0/40` (`0/5,120` B) `P6_raw` vs `40/40` (`5,101` B) signed failures, `0` across all `8` orbit reps, and `8/8` (`+ R1 8/8`) live `RAR.EXE` replays.

---

## 3. Hardened C99 Library Build, Security Verification & 6x Gate Record

### Pinned Checksums (`AGENTS.md`)
All `4` pins match verbatim before and after all verification runs:
- `tests/universal_decryption_gate.py`: `64266a68e9714a677e4228f70f5be35d12036464713de8bca9c12d80ac1b3fd1`
- `tests/fixtures/gate/T.TXT`: `c0e95aceb33a2ebc079bd110e3e1b28e3e0664d8944ec4d9d525703d61562eee`
- `tests/fixtures/gate/I.PNG`: `eb09fa49118607699bb85ad6f24400cdccec0dc019d40604274da2fa13275be7`
- `tests/fixtures/gate/X.EXE`: `e72c206a044108250eb7c236c536ef14214ed66b2352058b8dca0ab8f177adb1`

*(Fixture provenance: `T.TXT`, `I.PNG`, and `X.EXE` are owner-confirmed AI-generated synthetic example files created for this project with zero third-party copyright encumbrance.)*

### Build, Sanitizer, Symbol & Security Hardening Checks
- **Compiler build & unit tests**: `make -C tools/libcleanroom clean all test` (`-Wall -Wextra -Werror -pedantic -std=c99 -O2`, `0` warnings) -> **`21 / 21 ok`, `ALL PASSED`** (incl. KATs for `""`, `"a"`, `"\x80\xaa\xff"`, byte-`76` vs byte-`77` cutoff, OOB `head_size=7/10` & overflow `add_size=0xFFFFFFFF` container rejections, and invalid unpack method `0x2F/0x36` rejections).
- **ASan + UBSan check**: `-fsanitize=address,undefined` build of `test_rar15` + library -> `21 / 21 ok`, `ALL PASSED`, `0` sanitizer diagnostics.
- **Zip-Slip & unsupported-flag probes**: Crafted `../escape.txt`, `sub/../../escape.txt`, `/tmp/escape_rar_test.txt` and `SPLIT_BEFORE | SPLIT_AFTER | SOLID | LARGE | 0x00E0` archives rejected cleanly (`rc != 0`, `0` escaped writes).
- **Symbol audit (`nm -u` & data/BSS)**: `rar15_cipher.o`, `rar15_container.o`, and `rar15_crc32.o` have **zero** undefined external symbols (`nm -u` empty) and zero mutable static globals; `u15_unpack.h`'s MIT copyright notice (`Copyright (c) 2026 the fultaarc-cleanroom-rar15 authors`) intact.

### Universal Decryption Gate History (`6 / 6` Full GREEN Runs)
1. **Pre-Hardening Runs (`commit bad0d94`, `2026-11-23`)**: Author Run 1, Author Run 2, and Fresh Final Reviewer Run (all `100/100` passwords, `300/300` files, `RESULT: ACHIEVED`; additionally independently run `3x` in `work/release-audit-bad0d94-bq8jiano/REVIEW.ko.txt`).
2. **Post-Cycle-324 Hardened Runs (`2026-11-24`, `FINAL-REVIEW-324.md`)**:
   - **Author Run 1**: `100 / 100` passwords, `300 / 300` files byte-exact, `RESULT: ACHIEVED`, `exit 0`.
   - **Author Run 2**: `100 / 100` passwords, `300 / 300` files byte-exact, `RESULT: ACHIEVED`, `exit 0`.
   - **Fresh Independent Reviewer (`Final Gate and Cleanroom Reviewer v2`, `FINAL-REVIEW-324.md`)**: verified `4/4` pins, Rules 5 & 6 + `nm -u` symbol audit (`RULE5_6_CHECK: CLEAN`), `make test` (`21/21 ok`), and Reviewer Run 3 -> `100 / 100` passwords, `300 / 300` files byte-exact, `RESULT: ACHIEVED`, `exit 0`.

---

## 4. Verified Scope, Limits & Historical Disclosures

- **Verified scope**: SAFE passwords `1..85` bytes (`77`-byte cutoff verified across blind prefix pairs and unit tests), raw high-byte `0x80..0xFF` octets (unsigned `uint8_t`), stream offsets up to `4,096` B blind (`16,384` B archived), compressed methods `0x30..0x35`, and full C99 standalone extraction with Zip-Slip & malformed-header hardening.
- **Limits**:
  - Unsupported RAR 1.5 features (`SOLID`, multi-volume `SPLIT_BEFORE`/`SPLIT_AFTER`, `LARGE`, directory `0x00E0`) fail closed cleanly (`RAR.EXE` default non-solid single-volume extraction is supported).
  - The `8` DOS harness-excluded octets `{0x00, 0x09, 0x0a, 0x0d, 0x20, 0x22, 0x27, 0x5c}` are untested via the CLI oracle and handled uniformly via C99 unsigned `uint8_t` up to the first `NUL` (`0x00`).
  - Due to exact `Z_2^4` gauge equivalence (Cycle `324`), bit `0` of `R`/`K2_init` and paired MSB flips cannot be separated by any black-box input-output test; no physical 8086 register allocation inside `RAR.EXE` is claimed.
  - All historical process disclosures (`RECALL-ZIP` falsified & quarantined `2026-11-16`, `OWNER-SUGGESTION-2026-11-20`, `71`-min unarchived `322 -> 323` gap retrospectively bridged in `324`, permanent `001/002/112/274` historical `verify` debt, and LLM pretraining-weight caveat) are documented verbatim in `docs/cleanroom/PROVENANCE.md` and `docs/cleanroom/RAR15-KEYSTREAM-RESEARCH-REPORT.md`. Technical provenance audit does not constitute formal legal advice.
