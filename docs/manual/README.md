[한국어](README-ko.md) | **English** — **FultaArc Manual**

# FultaArc Developer Manual

Public API reference and usage guide for [`include/fulta/arc.h`](../../include/fulta/arc.h).

---

## 1. Building and Linking

FultaArc compiles with `-std=c23 -Wall -Wextra -pedantic -Werror`.

| Make target | Output | Purpose |
|---|---|---|
| `make` / `make all` | `build/host/libfultaarc.a` | Static library (ASan + UBSan enabled by default via `SAN`) |
| `make warn` | `build/warn/` | `-Og -DNDEBUG -Werror` build catching `-Wmaybe-uninitialized` |
| `make test` | `build/host/tests/*` | Pin checks, `no-libc-alloc.sh`, and full host test suite |
| `make bench` | `build/bench/bench` | Optimized `-O2 -DNDEBUG` build without sanitizers |
| `make win64` | `build/win64/libfultaarc.a` | Windows x64 UCRT cross-build (`mingw-w64`) |
| `make deep` | `build/tsan/`, `build/fuzz/` | ThreadSanitizer (`test_threads`) + 120s libFuzzer smoke run |
| `make prove` | `build/proofs/` | CBMC bounded proofs + ASan/UBSan brute-force checks |

For a non-sanitizer production static library, override `SAN`:

```sh
make SAN="" OPT="-O2 -DNDEBUG"
cc -std=c23 -O2 -Iinclude app.c build/host/libfultaarc.a -lm -lpthread -o app
```

On Windows (`mingw-w64`), link `-static -lbcrypt`.

---

## 2. Reading Archives

### Opening an Archive

- `fulta_arc_open_file(path, options, &arc)`: opens a file by path and automatically resolves split volumes next to it when `options` does not supply a custom `volume` callback.
- `fulta_arc_open_memory(data, size, options, &arc)`: opens a borrowed memory buffer (`data` must stay valid until `fulta_arc_close(arc)`).
- `fulta_arc_open(source, options, &arc)`: opens a custom random-access `fulta_arc_source_t` (`read_at`, `close`, `size`). `arc` takes ownership of `source` and calls `source->close(source->ctx)` on close or open failure. Convenience constructors `fulta_arc_source_file` and `fulta_arc_source_memory` are also provided.

Pass `options = NULL` for defaults. Key `fulta_arc_options_t` fields:
- `name`, `volume`, `volume_ctx`: first volume name and custom multi-volume resolver (`fulta_arc_volume_fn`).
- `password`, `password_ctx`: `fulta_arc_password_fn(ctx, attempt, &utf8_out)` called on encrypted entries until it succeeds or returns `FULTA_ARC_ERR_PASSWORD_NEEDED`.
- `codepage`, `auto_codepage`: `fulta_arc_codepage_t` (`FULTA_ARC_CP_AUTO`, `CP_UTF8`, `CP_437`, `CP_949`, `CP_932`, `CP_936`, `CP_950`, `CP_1252`, `CP_866`, `CP_1251`, `CP_KOI8R`) for legacy non-Unicode entry names.
- `max_dictionary` (`0` = 1 GiB), `max_memory` (`0` = unlimited): cap attacker-controlled codec allocations.
- `allocator`: custom `fulta_arc_allocator_t` (`alloc`, `realloc`, `free`, `ctx` — supply all three functions or leave all `NULL` to use `proven_c_lib`'s heap).
- `cancel`, `cancel_ctx`, `progress`, `progress_ctx`: polling cancellation and cumulative byte progress reporting.
- `keep_damaged`: when `true`, `fulta_arc_extract` writes decoded bytes even if checksum fails or stream truncates, returning `FULTA_ARC_DAMAGED`.

### Inspecting Entries & Extracting

- `fulta_arc_format(arc)` / `fulta_arc_format_name(fmt)`: detected format (`FULTA_ARC_FORMAT_*`).
- `fulta_arc_count(arc)` and `fulta_arc_entry(arc, index)`: metadata (`name`, `name_raw`, `name_raw_size`, `size`, `packed_size`, `mtime`, `crc32`, `attributes`, `unix_mode`, `solid_group`, `flags`, `method`).
- **Timestamp semantics**: `mtime` is nanoseconds since `1970-01-01T00:00:00Z`. When `FULTA_ARC_ENTRY_MTIME_LOCAL` is set (DOS timestamps in ZIP without UTC extra fields, ALZ, RAR 1.4–4, CAB, EGG, LHA level 0/1), `mtime` holds the local wall clock encoded as if UTC; convert with the creator's/host's local timezone as needed.
- **Streaming extraction**: `fulta_arc_extract(arc, index, &sink)` writes chunks to `sink->write(ctx, data, n)` and verifies size and checksum.
- **In-memory extraction**: `fulta_arc_extract_alloc(arc, index, &out_data, &out_size)` allocates with the archive's allocator (`fulta_arc_free(out_data)` when using the default allocator; your own `allocator.free` when `options.allocator` was supplied).
- **Cheap password check**: `fulta_arc_check_password(arc, index)` verifies a password via the format's check value without full decoding when supported (returns `FULTA_ARC_ERR_UNSUPPORTED` if the format has no separate check value).

---

## 3. Writing and Editing ZIP and 7z

### Writing Fresh Archives

- `fulta_arc_write(format, entries, count, options, sink)`
- `fulta_arc_write_file(path, format, entries, count, options)`
- `fulta_arc_write_volumes(format, entries, count, options, volumes)`

Supported `format` values: `FULTA_ARC_FORMAT_ZIP` and `FULTA_ARC_FORMAT_7Z`.

`fulta_arc_write_options_t` controls:
- `level` (`0` store, `1..9` compress; out-of-range = `6`), `method` (`FULTA_ARC_METHOD_*`), `filter` (`FULTA_ARC_FILTER_*`, 7z only), `filter_delta_dist` (`1..256`), `solid` (`FULTA_ARC_SOLID_{DEFAULT,ON,OFF}`, 7z only).
- `password`, `encryption` (`FULTA_ARC_ENCRYPT_ZIP_AES{128,192,256}`, `FULTA_ARC_ENCRYPT_7Z_AES256`), `encrypt_names` (7z `-mhe=on` header encryption).
- `volume_size` (`>= FULTA_ARC_VOLUME_MIN` = 64 KiB) for split `.z01`..`.zip` or `.7z.001`.. output; use `fulta_arc_volume_name` to format volume names.
- `dos_time` (`FULTA_ARC_DOS_TIME_{LOCAL,OFFSET,UTC}`) and `dos_utc_offset` for ZIP DOS timestamps.
- `zip64`: force Zip64 records.
- `allocator`: custom `fulta_arc_allocator_t` for writer and encoder buffers.

### Editing and Converting

1. Check `fulta_arc_editable(arc)` (`FULTA_ARC_EDIT_OK` for in-place ZIP or non-solid 7z copy; `SOLID`/`MULTIVOLUME`/`FORMAT_NOT_WRITABLE` require converting; `FULTA_ARC_EDIT_ENCRYPTED` is refused).
2. Begin with `fulta_arc_edit_begin(arc, &edit)`.
3. Queue changes: `fulta_arc_edit_remove`, `fulta_arc_edit_replace`, `fulta_arc_edit_add` (using a pull-based `fulta_arc_reader_t`).
4. Commit via `fulta_arc_edit_write`, `fulta_arc_edit_write_file`, or `fulta_arc_edit_write_volumes`.
5. Release with `fulta_arc_edit_free(edit)`.

---

## 4. Security, Path Safety, and Threading

- **Path traversal & symlinks**: `entry->name` never begins with `/`, never contains drive letters (`C:`) or `..` segments (including Windows dot/space forms), and replaces ASCII control bytes (`0x01–0x1F`, `0x7F`) with `_`. Callers extracting symlinks (`FULTA_ARC_ENTRY_SYMLINK`) must still verify link targets do not escape the destination directory before creating filesystem links or extracting subsequent entries through them.
- **Callbacks**: callbacks (`sink`, `progress`, `cancel`, `password`) must **not** call `fulta_arc_extract`, `fulta_arc_check_password`, or `fulta_arc_close` on the same `fulta_arc_t` (re-entrant `extract`/`check_password` returns `FULTA_ARC_ERR_INVALID_ARG`).
- **Thread safety**: use one `fulta_arc_t` per thread; independent `fulta_arc_t` instances can run concurrently across threads.

---

## 5. Error Codes

`fulta_arc_strerror(err)` returns a static English string for any `fulta_arc_err_t`.

| Code | Meaning |
|---|---|
| `FULTA_ARC_OK` (`0`) | Success |
| `FULTA_ARC_ERR_NOMEM` (`1`) | Out of memory (`PROVEN_ERR_NOMEM`) |
| `FULTA_ARC_ERR_INVALID_ARG` (`4`) | Invalid argument or re-entrant callback (`PROVEN_ERR_INVALID_ARG`) |
| `FULTA_ARC_ERR_IO` (`5`) | Source or sink I/O error (`PROVEN_ERR_IO`) |
| `FULTA_ARC_ERR_NOT_ARCHIVE` (`0x1000`) | No recognized archive or stream signature |
| `FULTA_ARC_ERR_CORRUPT` (`0x1001`) | Invalid container structure or compressed stream |
| `FULTA_ARC_ERR_TRUNCATED` (`0x1002`) | Unexpected end of input |
| `FULTA_ARC_ERR_UNSUPPORTED` (`0x1003`) | Unsupported method, filter, or feature |
| `FULTA_ARC_ERR_PASSWORD_NEEDED` (`0x1004`) | Encrypted entry with no password supplied |
| `FULTA_ARC_ERR_PASSWORD_WRONG` (`0x1005`) | Supplied password failed verification |
| `FULTA_ARC_ERR_CHECKSUM` (`0x1006`) | CRC-32 / BLAKE2sp / MAC mismatch |
| `FULTA_ARC_ERR_VOLUME_MISSING` (`0x1007`) | Required split volume unavailable |
| `FULTA_ARC_ERR_LIMIT` (`0x1008`) | `max_dictionary` or `max_memory` limit exceeded |
| `FULTA_ARC_ERR_CANCELLED` (`0x1009`) | Cancelled by `options.cancel` |
| `FULTA_ARC_ERR_BAD_NAME` (`0x100A`) | Invalid or unsafe output entry name |
| `FULTA_ARC_DAMAGED` (`0x100B`) | Partial/damaged output delivered (`keep_damaged = true`) |
| `FULTA_ARC_ERR_EXISTS` (`0x100C`) | Trailing split volume already exists on disk |

---

## 6. Clean-Room Provenance

See [`docs/provenance/README.md`](../provenance/README.md) for the format, algorithm, derivation, and provenance records of FultaArc's clean-room decoders (`rar/`, `rar15/`, `rar15/enc3/`, `alz/`, `egg/`, `azo/`, `lha/`, and `quantum/`).
