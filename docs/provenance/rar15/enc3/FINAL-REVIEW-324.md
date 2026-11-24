# Final Gate and Cleanroom Review (v2, Post-Cycle-324 Hardened Library)

- **Reviewer Role**: Fresh Independent Final Gate and Cleanroom Reviewer (v2)
- **Date**: 2026-11-23 UTC (2026-11-24 +09:00)
- **Toolchain**: `cc (Debian 14.2.0-19) 14.2.0`, `Python 3.13.5`, `-Wall -Wextra -Werror -pedantic -std=c99 -O2`
- **Scope**: Post-Cycle-324 hardened `tools/libcleanroom/` C99 library, CLI tools, unit tests, pinned SHA-256 verification, Rules 5 & 6 symbol/source audit, thread-safety/security hardening audit, and Reviewer Gate Run 3 (`tests/universal_decryption_gate.py`).

---

## 1. Pinned Gate & Fixture SHA-256 Verification (`AGENTS.md`)

Verified via `sha256sum tests/universal_decryption_gate.py tests/fixtures/gate/*` both before and after Reviewer Gate Run 3 (4/4 exact matches):

| File | Expected Pin (`AGENTS.md`) | Observed SHA-256 | Status |
|---|---|---|---|
| `tests/universal_decryption_gate.py` | `64266a68e9714a677e4228f70f5be35d12036464713de8bca9c12d80ac1b3fd1` | `64266a68e9714a677e4228f70f5be35d12036464713de8bca9c12d80ac1b3fd1` | EXACT MATCH |
| `tests/fixtures/gate/T.TXT` | `c0e95aceb33a2ebc079bd110e3e1b28e3e0664d8944ec4d9d525703d61562eee` | `c0e95aceb33a2ebc079bd110e3e1b28e3e0664d8944ec4d9d525703d61562eee` | EXACT MATCH |
| `tests/fixtures/gate/I.PNG` | `eb09fa49118607699bb85ad6f24400cdccec0dc019d40604274da2fa13275be7` | `eb09fa49118607699bb85ad6f24400cdccec0dc019d40604274da2fa13275be7` | EXACT MATCH |
| `tests/fixtures/gate/X.EXE` | `e72c206a044108250eb7c236c536ef14214ed66b2352058b8dca0ab8f177adb1` | `e72c206a044108250eb7c236c536ef14214ed66b2352058b8dca0ab8f177adb1` | EXACT MATCH |

---

## 2. Hardened Library & Test Source SHA-256 Inventory

Exact SHA-256 digests recorded across `tools/libcleanroom/include/`, `tools/libcleanroom/src/*.{c,h}`, and `tools/libcleanroom/tests/test_rar15.c` (10 files, 1,395 total lines, 100% pure ASCII):

| Relative Path | Lines | Observed SHA-256 |
|---|---|---|
| `tools/libcleanroom/include/rar15.h` | 85 | `193ff31fc66d42b7b5f2f5cba7c7d3a32977161de7da78785300851e2c4a65eb` |
| `tools/libcleanroom/src/rar15_cipher.c` | 88 | `1fd0002b6d00443552837502962deb18c9119f090b1cd1a279f53372cf36e5cf` |
| `tools/libcleanroom/src/rar15_container.c` | 69 | `48d6f8486deb36128c913048f468d03125ab9efbfbd02675a6632f72d7c31f00` |
| `tools/libcleanroom/src/rar15_crc32.c` | 36 | `8573276bfd2299cbd9c4348e23c2cb41a08ce455ecd6fd8ffbaba9e34e4c2851` |
| `tools/libcleanroom/src/rar15_unpack.c` | 37 | `df0fca779f8d5e0e72da2d1aa5be8be31017e55e370ee2a9fa6a921acaf25b8a` |
| `tools/libcleanroom/src/rarextract.c` | 190 | `3b2159f63c1556f16b4a18921a4186ecbe7d801bf18e4870d5b63a6b1777ab6e` |
| `tools/libcleanroom/src/rarkit.c` | 199 | `6f0406af209aeb906fcdcb2cd8cdba702305e98443bbad264dc097bd6b6d6f76` |
| `tools/libcleanroom/src/u15_unpack.c` | 504 | `b2e70e20da5bc3ce7cf4f160ff1da71fd7a24391da6c64c84a5ee353507b4ce5` |
| `tools/libcleanroom/src/u15_unpack.h` | 73 | `17fba8a5c4bca9a7aa53da5f38861a660821d4ed4a4dc0003f5d4138a97ce868` |
| `tools/libcleanroom/tests/test_rar15.c` | 114 | `30c31f236d757c1e2bc47e096307401b4b948c4a4c089bfba1570d0a841dafc5` |

---

## 3. Rules 5 & 6 Cleanroom Compliance and Symbol Audit

### Source Inspection Summary (All 10 C/H Files)
- **`rar15_cipher.c`**: Includes only `"rar15.h"` (no libc headers). Computes the IEEE 802.3 reflected CRC-32 table (`0xEDB88320u`) mathematically bit-by-bit into a stack-local `uint32_t crc_tbl[256]`. Truncates at 77 bytes via an explicit non-NUL scan loop (no `strlen`), casts `password` to `const uint8_t *`, and executes the Cycle 323/324 verified 16-bit recurrence (`w`, `m`, `k2`, `hr`, `s_state`) with `+0x1234u`, `ror16(_, 2u)`, `ror16(_, 1u)`, and `(uint8_t)(w >> 8)`. Zero oracle/emulator/subprocess/file/network calls, zero stored keystreams, zero observed lookup tables, zero planted passwords.
- **`rar15_crc32.c`**: Standard public IEEE 802.3 CRC-32 (`0xEDB88320u`) implemented bitwise for `len < 16u` and with a stack-local `uint32_t tbl[256]` for `len >= 16u`. Zero cipher knowledge, zero static mutable globals.
- **`rar15_container.c`, `rar15_unpack.c`, `u15_unpack.h`, `u15_unpack.c`, `rarextract.c`, `rarkit.c`**: Container parser, stateless unpack adapter, clean-room LZSS/Huffman decompressor (methods `0x31..0x35`), and CLI tools contain zero cipher logic, zero oracle/emulator references, and zero external network/subprocess dependencies.
- Pattern scan across `tools/libcleanroom/` for forbidden/external primitives (`dosjail`, `RAR.EXE`, `obs.py`, `popen`, `system(`, `exec`, `socket`, `http`, local path prefixes, `keystream_store`, `cleanroom_keystore`): zero matches (`CLEAN_GREP`).
- Independent pure-Python verification of the `test_rar15.c` KAT vectors (`""`, `"a"`, `"\x80\xaa\xff"`) matched bit-for-bit (`3/3 True`).

### Object Symbol Audit (`nm -u` and Mutable Data/BSS Check)
After `make -C tools/libcleanroom clean && make -C tools/libcleanroom all`:

| Object File | Undefined Symbols (`nm -u`) | Mutable Data/BSS (`nm \| grep ' [bBdDsSgGcC] '`) | Assessment |
|---|---|---|---|
| `tools/libcleanroom/src/rar15_cipher.o` | `(empty)` | `(none)` | CLEAN (zero externals, zero globals) |
| `tools/libcleanroom/src/rar15_container.o` | `(empty)` | `(none)` | CLEAN |
| `tools/libcleanroom/src/rar15_crc32.o` | `(empty)` | `(none)` | CLEAN |
| `tools/libcleanroom/src/rar15_unpack.o` | `free`, `malloc`, `memcpy`, `u15_init`, `u15_unpack` | `(none)` | CLEAN |
| `tools/libcleanroom/src/u15_unpack.o` | `memset` | `(none)` | CLEAN |
| `tools/libcleanroom/src/rarextract.o` | `fclose`, `fopen`, `fprintf`, `fread`, `free`, `fseek`, `ftell`, `fwrite`, `malloc`, `memcpy`, `mkdir`, `printf`, `rar15_crc32`, `rar15_decrypt_block`, `rar15_keystream`, `rar15_list`, `rar15_unpack`, `rewind`, `stderr`, `strlen` | `(none)` | CLEAN (CLI file I/O + libcleanroom API only) |
| `tools/libcleanroom/src/rarkit.o` | `fclose`, `fopen`, `fprintf`, `fread`, `free`, `fseek`, `ftell`, `fwrite`, `malloc`, `memcmp`, `memcpy`, `printf`, `putc`, `putchar`, `puts`, `rar15_crc32`, `rar15_decrypt_block`, `rar15_list`, `rar15_unpack`, `rewind`, `stderr`, `stdout`, `strcmp`, `strtol` | `all.0` (`static rar15_entry all[4096]` in CLI `get_entry`) | CLEAN (CLI file I/O + libcleanroom API; note single-threaded CLI static buffer `all.0`) |

`RULE5_6_CHECK: CLEAN`

---

## 4. Thread-Safety and Security Hardening Checklist

| Check | Location | Verification Evidence | Status |
|---|---|---|---|
| Stack-local CRC-32 tables & zero static mutable globals | `rar15_cipher.c`, `rar15_crc32.c` | `uint32_t crc_tbl[256]` / `uint32_t tbl[256]` on stack; `nm` shows zero `[bBdDsSgGcC]` symbols in all library `.o`s; `rol16`/`ror16` cast via `uint32_t` before shift | PASS |
| No `strlen` dependency in cipher | `rar15_cipher.c` | Bounded loop `while (plen < 77u && password[plen] != '\0') plen++;`; `nm -u rar15_cipher.o` is empty | PASS |
| Container OOB & integer-overflow bounds | `rar15_container.c`, `rarkit.c` | `arc_len - p >= 7u`, `head_size < 7u \|\| (size_t)head_size > arc_len - p`, `rem_after_hdr = arc_len - p - (size_t)head_size`, `head_size < 11u` before `rd32(arc + p + 7)`, `(size_t)add_size > rem_after_hdr`, `head_size < 32u` and `name_size <= head_size - 32u`; confirmed by 3 regression tests (`bad_hs7`, `bad_hs10`, `bad_add_ovf`) and CLI probes (`rc=2`) | PASS |
| Unpack compression method guard (`0x30..0x35`) | `rar15_unpack.c` | `if (method < RAR15_M_STORE \|\| method > RAR15_M_BEST) return RAR15_ERR_UNSUPPORTED;`; verified in `test_rar15.c` (`0x2F`, `0x36`) and `bin/rarextract` probes (`err -3`) | PASS |
| Zip-Slip path traversal protection | `rarextract.c` | `is_safe_rel_path` rejects empty/leading `/` or `\`, `< 0x20`, `\`, `:`, empty segments `//`, `.`, `..`; enforced before write in both `main` and `write_file`. Runtime probes (`../evil.txt`, `a/../evil.txt`, `/tmp/evil.txt`, `..\evil.txt`, `C:evil.txt`, `a//evil.txt`) all exit `2` with zero traversal writes | PASS |
| Unsupported header flag & directory entry guards | `rarextract.c` | Rejects `SPLIT_BEFORE \| SPLIT_AFTER \| SOLID \| LARGE` (`0x0001\|0x0002\|0x0010\|0x0100`) and directory entries `(flags & 0x00E0u) == 0x00E0u` with `rc = 1`. Runtime probes for all 5 flag cases exit `1` | PASS |
| Heap-allocated entry array (avoiding large stack frame) | `rarextract.c` | `rar15_entry *entries = (rar15_entry *)malloc(MAX_ENTRIES * sizeof *entries);` freed on all exit paths | PASS |
| Portable directory creation (`#ifdef _WIN32`) | `rarextract.c` | `#ifdef _WIN32` selects `<direct.h>` + `_mkdir(p)`; `#else` selects `<sys/stat.h>` + `mkdir((p), 0755)` | PASS |
| `u15_unpack.h` MIT copyright notice intact | `u15_unpack.h` | Line 7 verbatim: `* Copyright (c) 2026 the fultaarc-cleanroom-rar15 authors`; full MIT permission text preserved | PASS |
| ASan + UBSan runtime verification (temp build) | `test_rar15.c`, `rarextract.c` + library | Compiled with `-Wall -Wextra -Werror -pedantic -std=c99 -O1 -g -fsanitize=address,undefined`; `selftest` exited `0` (`ALL PASSED`, 0 sanitizer diagnostics); all 14 `rarextract` Zip-Slip/flag/method/valid probes + 3 malformed block probes passed with 0 ASan/UBSan findings | PASS |

---

## 5. Verbatim Unit Test Output (`make -C tools/libcleanroom test`)

Clean build (`make -C tools/libcleanroom clean && make -C tools/libcleanroom all && make -C tools/libcleanroom test`) compiled with `-Wall -Wextra -Werror -pedantic -std=c99 -O2` produced zero warnings and exited `0` (`21/21 ok`):

```text
cc -Wall -Wextra -Werror -pedantic -std=c99 -O2 -Iinclude -o ../../bin/rar15_selftest tests/test_rar15.c src/rar15_crc32.o src/rar15_container.o src/rar15_cipher.o src/rar15_unpack.o src/u15_unpack.o
../../bin/rar15_selftest
ok  : crc32 known vector
ok  : crc32 of empty is 0
ok  : store unpack is a passthrough
ok  : garbage is not accepted as a valid compressed stream
ok  : method 0x2F below 0x30 rejected with RAR15_ERR_UNSUPPORTED
ok  : method 0x36 above 0x35 rejected with RAR15_ERR_UNSUPPORTED
ok  : xor decrypt is self-inverse
ok  : null password returns 0
ok  : null out returns 0
ok  : zero max_out returns 0
ok  : keystream writes requested length
ok  : keystream is deterministic
ok  : distinct passwords give distinct streams
ok  : keystream KAT empty password matches
ok  : keystream KAT 'a' matches
ok  : keystream KAT raw high-byte unsigned '\x80\xaa\xff' matches
ok  : bytes past index 76 (indices 77..79) ignored
ok  : 77th byte (index 76) still affects keystream
ok  : reject flags=0x8000 block with head_size=7 (prevents OOB read)
ok  : reject flags=0x8000 block with head_size=10 (prevents OOB read)
ok  : reject add_size=0xFFFFFFFF overflow block

ALL PASSED
```

---

## 6. Verbatim Acceptance Gate Output (`Reviewer Gate Run 3`)

Command: `python3 tests/universal_decryption_gate.py` -> exit code `0`:

```text
==============================================================================
CLEAN-ROOM RAR 1.5 (enc2) - FIXED UNIVERSAL-DECRYPTION ACCEPTANCE GATE
==============================================================================
gate script sha256 : 64266a68e9714a677e4228f70f5be35d12036464713de8bca9c12d80ac1b3fd1
  (valid only if this matches the value pinned in AGENTS.md)
fixtures verified  : T.TXT(15360B), I.PNG(6447B), X.EXE(1024B)
passwords drawn    : 50 varied-length (1..64) + 50 max-length (64)
injection check    : none of 100 passwords present in any store
building 100 ground-truth archives with real RAR (offline emulator)...
ground truth OK    : real RAR created and self-verified 100 archives

extracting every challenge with OUR engine (sandboxed, no answer files)...

------------------------------------------------------------------------------
passwords fully recovered : 100 / 100
files byte-exact          : 300 / 300
------------------------------------------------------------------------------

RESULT: ACHIEVED - 100% universal decryption verified against real RAR 1.5.
```

---

## Verdict: CLEAN / ACHIEVED
