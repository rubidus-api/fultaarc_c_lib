# Provenance and Clean-Room Boundaries (`enc3`)

This document states the provenance, source boundaries, approved priors, incident disclosures, and distribution limits of the delivered clean-room library in `tools/libcleanroom/`. The root `LICENSE` file contains standard MIT License text only.

## 1. Software & Component Provenance

- **Container parser, CRC-32 primitive, cipher keystream, CLI wrappers, and tests** (`tools/libcleanroom/` excluding `src/u15_unpack.{h,c}`): Original C99 implementation (`Copyright (c) 2026 The fultaarc-cleanroom-rar15-enc3 authors`, `SPDX-License-Identifier: MIT`), written from the documented container specification (`docs/FORMAT.md`), mathematical definitions (IEEE 802.3 CRC-32 `0xEDB88320`), and black-box mathematical derivation (`Cycles 001..324`).
- **Decompressor (`tools/libcleanroom/src/u15_unpack.{h,c}`)**: Owner-supplied MIT-licensed clean-room RAR 1.5 LZSS+Huffman decoder (`Copyright (c) 2026 the fultaarc-cleanroom-rar15 authors`, `SPDX-License-Identifier: MIT`). Contains no cipher code; decodes only already-decrypted compressed streams for methods `0x31..0x35`.
- **Gate fixtures (`tests/fixtures/gate/{T.TXT,I.PNG,X.EXE}`)**: AI-generated synthetic example test fixtures created specifically for this project and confirmed by the repository owner (`2026-11-23` / `2026-11-24`), pinned by SHA-256 in `AGENTS.md`. They contain zero third-party copyrighted media or binaries and raise no copyright conflict for MIT redistribution (resolving external review queries regarding `I.PNG` and `X.EXE`).
- **Offline black-box oracle (`tools_private/`)**: Third-party `RAR.EXE` and DOS emulators live exclusively under `tools_private/` (gitignored, excluded from source distribution), were used strictly as black-box input-output generators via `tools/obs.py`/`tools/dosrun.py` without binary inspection (Rule 7), and are never required or referenced by `tools/libcleanroom/`.

## 2. Epistemic Boundary (`APPROVED-PRIOR` vs `DATA-DERIVED`)

Per `docs/provenance/AMENDMENT-2026-11-16-owner-approved-priors.md`:

- **`APPROVED-PRIOR`**:
  - `RECALL-208`: Small 16-bit word-state machine (`~64`-bit hypothesis class, explicitly non-physical).
  - `RECALL-209`: Standard reflected IEEE 802.3 CRC-32 table (`0xEDB88320`) update structure as a hypothesis candidate explaining the data-derived degree-32 primitive polynomial factor `0x104C11DB7` (`Cycle 208`).
- **`DATA-DERIVED` (`Cycles 001..322` & Constructive Bridge `Cycle 324`)**:
  - `77`-byte password truncation, unsigned `uint8_t` byte semantics, unfinalized CRC accumulator `R`, additive/XOR key-schedule accumulators `H` and `S_0(bx) = 0xFFFF ^ T_LO[bx] ^ bx`, 5-word live recurrence (`w, m, k2, hr, s_state`), rotations `(hr_rot=2, m1_rot=1, m_rot=2, t_rot=1)`, `+0x1234` addition, cumulative `K2 ^= M`, output byte `w >> 8`, and exact `Z_2^3`/`Z_2^4` black-box gauge equivalence.

## 3. Disclosed Incidents & Historical Process Caveats

In accordance with `docs/ROLES.md:107-108` and `docs/provenance/LEDGER.md`:

1. **`RECALL-ZIP` (`2026-11-16`)**: A ZIP-like cipher family was logged, tested, and empirically falsified (`LEDGER.md:238-242`) and quarantined; zero `RECALL-ZIP` logic exists in `tools/libcleanroom/`.
2. **`OWNER-SUGGESTION-2026-11-20`**: Recorded owner exploration guidance (`LEDGER.md:360`).
3. **Cycle `322 -> 323` synthesis gap (`2026-11-23`)**: An approximately `71`-minute offline synthesis step between Cycle `322` and Cycle `323` occurred outside archived shell logs before Cycle `323` was pre-registered (`LEAP`, blind-validated across `P1..P5`). Cycle `324` (`FORCED`, `CONFIRMED 3/3`, `SOLID`) retrospectively demonstrated zero-hardcoded-constant algorithmic recoverability strictly from pre-Cycle-`323` archived datasets under `RECALL-208`/`RECALL-209`, without altering or erasing the historical `2026-11-23` chronology. For additional historical transparency, a strictly redacted excerpt of those `82` exploratory commands is preserved as a non-core supporting appendix in [docs/provenance/APPENDIX-2026-11-23-c322-c323-session-log.md](APPENDIX-2026-11-23-c322-c323-session-log.md) (`.jsonl`); the proof chain itself does not depend on it.
4. **Historical `cycle.py verify` debt**: Cycles `001` (`#7`), `112` (`#3`), `274` (`#9`, `INCIDENT-2026-11-19-archive-run-collision.md`), and early `002` review debt carry permanently recorded historical archive-tool collision artifacts prior to the `O_EXCL` fix; none affect Cycles `323..324` or the delivered library.
5. **Pretraining caveat**: Because LLMs participated in derivation, theoretical pretraining exposure to public archival literature cannot be excluded by repository logs alone; mathematical/cleanroom engineering evidence does not substitute for independent legal review.

## 4. Trademark Disclaimer & Legal Notice

`RAR` and `WinRAR` are trademarks of their respective owners (Alexander Roshal / win.rar GmbH). This independent clean-room implementation is solely for format interoperability and academic/engineering study, is not affiliated with or endorsed by `win.rar GmbH`, and provides technical provenance documentation rather than formal legal advice.
