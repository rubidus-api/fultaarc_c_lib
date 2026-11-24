# Historical Transparency Appendix: Redacted `Cycle 322 -> 323` Session Log (`2026-11-23`)

## 1. Purpose, Scope & Artifact Identity

- **Companion log file**: `docs/provenance/APPENDIX-2026-11-23-c322-c323-session-log.jsonl`
- **Format**: JSON Lines (`147` records, pure ASCII, relative repository paths only)
- **Size**: `626,474` bytes uncompressed (`142,585` bytes gzip level `6`)
- **SHA-256**: `b11be611487d49ccf1edb21b6cff6e95f8e6eb8e27abe0df2d0d0cfb90bed41e`
- **Captured window**: `2026-11-23T06:34:29Z` through `2026-11-23T07:45:41Z` (`15:34:29` to `16:45:41 +0900`, `step 993..1271`), immediately spanning the `~71`-minute exploratory interval between the local commit closing Cycle `322` (`dfa8eef`, `15:33:54 +0900`) and the finalization of `docs/cycles/323-full-keystream-generator-blind/1-hypothesis.md` (`16:45:41 +0900`).
- **Record breakdown**: `82` `run_command` shell invocations (with commands and captured outputs), `60` `read_file` metadata entries, `2` `manage_task` status checks, `1` `write_file` metadata entry, and `2` `edit_file` metadata entries.

## 2. Epistemic Status (Supporting Appendix ONLY -- Not Part of the Core Proof)

This appendix is provided **strictly for historical process transparency** and is **NOT a dependency of the mathematical or clean-room proof chain**:

1. **Where the actual proof stands**: Mathematical derivability from pre-Cycle-`323` archived observations (`c248`, `c239`, `c247`, `k109`, `c322`) and owner-approved priors `RECALL-208`/`RECALL-209` is established independently and deterministically by `Cycle 324` (`tools/lab/c324_constructive_bridge.py`, `FORCED`, `CONFIRMED 3/3`), verified alongside blind holdouts (`Cycle 323` `P1..P5`, `Cycle 324` `P6`) and `6 / 6` full runs of `tests/universal_decryption_gate.py`. Any third party can re-run `Cycle 324`'s solver and verify every constant and the exact `Z_2^4` gauge orbit directly without relying on any session transcript.
2. **Retrospective provenance rule (`docs/ROLES.md:107-108`)**: Because the live interactive session log was neither cryptographically hash-chained inside `docs/cycles/.../archive.jsonl` at execution time nor signed by an external trusted timestamping authority, this redacted excerpt does **not** erase the historical `2026-11-23` unarchived synthesis gap or convert it into runtime `archive.jsonl` pre-registration. Instead, it documents transparently what exploratory computations actually took place during those `71` minutes (zero binary-inspection commands and zero external third-party source lookups; all `82` commands operated incrementally on existing archived `work/` corpora).

## 3. Redaction & Sanitization Policy

Applied deterministically when extracting from the raw interactive session log:

- **Preserved**: Exact step indices, UTC timestamps, `82` `run_command` tool summaries, shell/Python commands, stdout/stderr outputs, `2` `manage_task` polls, and relative-path metadata (`path`, line range) for `read_file` / `write_file` / `edit_file`.
- **Stripped**: Unpaired internal assistant reasoning blocks, internal model/token accounting fields, internal prompt/checkpoint control messages, and verbatim file contents from `read_file` (all referenced repository files are already tracked in git or archived under Cycle histories).
- **Masked**: Local user-home and absolute workspace prefixes (converted to `[REDACTED_LOCAL_PATH]`, `[REDACTED_HOME]`, `[SYSTEM_LIB_PATH]`, or repository-relative paths), host OS username (`[REDACTED_USER]`), session/task UUIDs (`[REDACTED_UUID]`), and non-ASCII typographical dashes/quotes (normalized to ASCII). Automated verification after extraction confirmed `0` raw absolute local or workspace paths, `0` unmasked UUIDs, `0` non-ASCII characters, `0` non-monotonic timestamps, and `0` occurrences of `tools_private`.

## 4. Chronological Derivation Milestones Inside the `71`-Minute Window

| Phase (`+0900` KST) | Appendix `seq` (`UTC`) | Technical Summary of Exploratory Progression |
| :--- | :--- | :--- |
| `15:34 - 15:48` | `seq 1..47` (`06:34:29Z - 06:49:13Z`) | Analyzed Cycle `322` `K_r`, zero-parameter `A_C10`, and `Core_0..7` 2nd/3rd-order ANFs; collapsed nonlinear structure into natural coordinate `u_8 = ((x_8 ^ K_8) + 26) & 255`. |
| `15:51 - 16:04` | `seq 49..62` (`06:51:03Z - 07:03:39Z`) | Solved exact `T_ref` addition+XOR structure inside `Core_byte(u_8)` across `108` `bx = 0x4c` CRCs, achieving zero-conflict `byte_1` prediction across all `68` `K_r` VQ rows and `52` `w` values. |
| `16:05 - 16:17` | `seq 63..78` (`07:05:47Z - 07:16:57Z`) | Eliminated Cycle `239` `3573`/`0x89A3` via `H = sum((T_ref[c] >> 16) + c)` and extended `byte_0` across all `128` `bx` bins in `c247 + c248` (`0 / 10,240` errors); constant `0x1234` first emerged here at `seq 78` (`07:16:57Z`, `42.5` minutes after Cycle `322`). |
| `16:21 - 16:33` | `seq 83..102` (`07:21:58Z - 07:33:48Z`) | Reconstructed full 16-bit `W_0`, solved `byte_1` GF(2) residuals across all `128` `bx`, unlocked `byte_2` via `W_1` low-byte scan, and extracted step update rules for bytes `0..16`. |
| `16:34 - 16:39` | `seq 103..115` (`07:34:46Z - 07:39:12Z`) | Collapsed step constants into the 18-line cumulative `K2 ^= M` loop (`K2_0 = 0xFFFF`, `uint8_t` semantics) and checked `0` errors across all existing offline corpora (`c247`, `c248`, `c239`, `enum12`, `c257`, `k109`, `long3_8000`). |
| `16:44 - 16:45` | `seq 141..147` (`07:44:20Z - 07:45:41Z`) | Verified dependency tree (`tools/tree.py`) and wrote/finalized `docs/cycles/323-full-keystream-generator-blind/1-hypothesis.md` to begin formal pre-registered blind validation (`P1..P5`). |
