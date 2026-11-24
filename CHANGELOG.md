# Changelog

All notable changes to this project will be documented in this file.

This project follows Keep a Changelog.

## [Unreleased]

### Added

- RAR 1.5 password decryption (`src/rar/rar15_enc/rar15_cipher.c`, MIT, byte-identical from the
  `fultaarc-cleanroom-rar15-enc3` clean room): password-protected RAR 1.5 entries (Store `0x30` and compressed
  `0x31..0x35`, including solid streams) are now decrypted and verified against CRC-32; the EN/KO derivation guides,
  specifications, research reports, and provenance records are published in `docs/provenance/rar15/enc3/`.
- English and Korean developer manuals (`docs/manual/README.md`, `docs/manual/README-ko.md`), comprehensive EN/KO
  GitHub READMEs (`README.md`, `README-ko.md`), and public release manifest (`public.manifest`, `.gitignore.public`).
- Public clean-room provenance, derivation, and format/algorithm documentation for **RAR 2.0–7.0**
  (`docs/provenance/rar/`), **ALZ** (`docs/provenance/alz/`), **EGG** (`docs/provenance/egg/`), and **AZO**
  (`docs/provenance/azo/`), completing [`docs/provenance/README.md`](docs/provenance/README.md) across all eight
  clean-room lineages.

### Changed

- `proven_c_lib` updated from `v0.114.0` to `v0.119.3` (fixed-buffer UTF overlap rejection and bounds checks,
  ChaCha RNG AVX2 reseed carry fix and barrier wipe, float rounding buffer sizing, and `proven_aes_rng_t`).
- Performance improvements: 8-block AES-NI CTR batching, static `fa_once`-initialized CRC tables for ZipCrypto and
  ALZ, `proven_crc32_update` in RAR, O(1) `clz64` and run-copy fast paths in AZO, chunked C-string reads and hoisted
  folder buffer in CAB, O(1) open-addressing hash sets in ISO/UDF, and 32 KiB UDF extraction buffer.

### Fixed

- Memory safety and bounds hardening across parsers, codecs, and writer: fixed short-input OOB read in `gzip_probe`,
  OOM cleanup OOB in EGG (`nfiles`), duplicate property leaks and `-Wshadow` in 7z, separate capacity tracking and
  64-bit wraparound guards in RAR/ALZ/ZIP/7z/EGG/XZ/CAB/TAR/AR/CPIO/UDF/write, `nametab` NULL guard in `ar`, missing
  `fulta_arc_strerror` cases, and allocator propagation in 7z writing.
- Security hardening: replaced dead-store-prone `memset` on keys, passwords, HMAC pads, and plaintext buffers with
  `fa_wipe` (`proven_mem_wipe`), switched ZIP/EGG/RAR MAC and password checks to constant-time comparison (`fa_ct_eq`),
  capped ZIP password retries, enforced `max_memory` across ZIP CD, 7z headers, ALZ password verification, and
  RAR/ALZ arena growth, fixed 7z encrypted-header retry handling for non-password errors, blocked zero-progress loops
  in bzip2/zstd, and added cancellation checks in solid-skip and CAB/ISO/UDF loops.

## [0.1.3] - 2026-11-13

### Changed

- Faster LZH (about 1.7x on `-lh5-`: a lookup table for the Huffman codes, an eight-byte CRC-16, match copies in
  runs) and AZO, EGG's own method (about 1.1x: the arithmetic decoder shifts several bits at once).

## [0.1.2] - 2026-11-13

### Fixed

- CAB: cabinets made by Windows' own makecab with LZX compression could not be read ("archive is cut short" for
  every file) at most window sizes: the LZX decoder took its look-ahead past the end of a data block for missing
  data. Checked on makecab cabinets at every window from 15 to 21 bits.

### Added

- RAR 2.x-5.x and CAB entries now carry their modification time (`FULTA_ARC_ENTRY_HAS_MTIME`); before they had
  none. RAR 2-4 and CAB times are the local wall clock and come with `FULTA_ARC_ENTRY_MTIME_LOCAL`, RAR 4's extended
  time adds its fraction of a second; RAR 5 times are UTC, to the nanosecond. Converting such an archive now passes
  the times on.

### Changed

- proven_c_lib updated to v0.114.0 (its formatter's `{:g}` became printf's %g; FultaArc formats no floats, so its
  output is unchanged).
- Entry names: a control character (0x01-0x1F, 0x7F: a newline, a terminal escape) becomes `_` in `name`, in every
  format; `name_raw` keeps the stored bytes.
- Faster Deflate: FultaArc's decoder (ZIP, EGG, gzip and the others that use it) now writes literals and copies
  matches straight into the caller's buffer, about 1.6x faster (on par with miniz's); ALZ checks its CRC-32 with
  proven's slicing-by-8, about 5x faster than before.
- A ZIP entry copied unchanged by an edit keeps its NTFS extra field (0x000a: modification, access and creation
  times to 100 ns); it used to keep only the DOS field.

## [0.1.1] - 2026-11-11

### Security

- A ZIP whose Zip64 end record gives the disk number 0xFFFFFFFF no longer crashes the reader (the disk count wrapped
  to zero and an array was read far out of range); it is reported as `FULTA_ARC_ERR_CORRUPT`.

### Changed

- proven_c_lib updated to v0.113.0. Its cryptography moved to a library of its own (proven_crypto, `crypto/`), which
  FultaArc now vendors for SHA-256; a build of FultaArc's sources outside the Makefile adds
  `vendor/proven/crypto/include` to the include path and compiles `vendor/proven/crypto/src/*.c` and
  `vendor/proven/crypto/platform/*.c`. No change to FultaArc's API or output.

## [0.1.0] - 2026-11-10

### Security

- A review of every reader, decoder and API entry point (fuzzed with libFuzzer, ASan and UBSan; threads under
  ThreadSanitizer) fixed: a double free when a gzip, bzip2, zstd, lzip or compress stream's decoder could not start
  (for example under a small `max_memory`); a crash on a read error in a `.Z` header; a heap over-read in 7z headers
  whose file properties come in an unusual order; very large allocations driven by a few bytes of 7z header or ALZ
  entry; RAR windows that ignored `max_dictionary`; ZIP names over 65535 bytes, which were cut in the header and let the
  rest be read as header bytes; names on the edit routes that were not checked (`../x`, absolute, control characters);
  Windows forms of ".." (".. ", ". .") and drive-relative names ("C:x", also after empty parts or another
  drive) that the reader let through; a data race on
  the tables built on first use when archives were opened on several threads at once; and several smaller
  out-of-range and overflow cases. A sink or callback that extracts from its own archive is now refused
  (`FULTA_ARC_ERR_INVALID_ARG`) instead of breaking the running extraction.
- `options.max_memory` now bounds everything an archive sizes, not only the decompressors: ALZ entries, CAB folders
  (cached folders give way when it is reached), EGG's AZO buffers and RAR windows. Temporary files of
  `fulta_arc_edit_write_file` and split writes have random names and are created exclusively. A damaged solid RAR run fails its later entries at
  once instead of decoding the run again for each. CAB entries report their folder as `solid_group`.
- `fulta_arc_edit_add` and renaming through `fulta_arc_edit_replace` refuse unsafe names with
  `FULTA_ARC_ERR_BAD_NAME`, as `fulta_arc_write` does.

### Changed

- **Behaviour change:** a ZIP that FultaArc writes now carries the DOS date and time in local time, as 7-Zip,
  Info-ZIP and Windows write it, so Explorer and other readers that look only at the DOS field show the right time
  (until now they saw UTC, 9 hours early in Korea). The extended timestamp beside it stays UTC. The offset is the
  machine's current one by default; `fulta_arc_write_options_t.dos_time` with `dos_utc_offset` sets one
  explicitly (`FULTA_ARC_DOS_TIME_OFFSET`) or keeps the old UTC field (`FULTA_ARC_DOS_TIME_UTC`). When an edit or
  conversion writes an entry whose time was local (an ALZ, a DOS-only ZIP), the same offset turns it into UTC
  for the UTC fields, and an entry copied unchanged no longer gains a UTC field with its local time in it.

- **API change:** memory from `fulta_arc_extract_alloc` is now released with the new `fulta_arc_free()`, or, when
  the archive was opened with `options.allocator`, with that allocator's free. It is no longer a C-library `malloc`
  block: calling `free()` on it is wrong (on Windows it crashes). `options.allocator` now backs everything the
  archive allocates, not only the decompressor buffers, and `fulta_arc_write_options_t` gains an `allocator` too.
- proven_c_lib is vendored at v0.112.3 (was v0.6.0).
- Every allocation inside FultaArc now goes through proven_c_lib's allocator trait: a caller's
  `options.allocator` receives everything an archive allocates, and the new `fulta_arc_write_options_t.allocator`
  everything a write, edit, conversion or split allocates, including the compressors' own state. Nothing in the
  library calls the C library's `malloc` or `free` any more.

### Performance

- Deflate (ZIP, gzip, CAB MSZIP, zisofs) decodes about twice as fast: the bit reader no longer makes a call per
  input byte, and back-references are copied in blocks.
- Editing: removing or replacing an entry no longer scans and shifts the whole plan, so removing many entries
  from a large archive is linear instead of quadratic.
- Split output keeps its per-header bookkeeping constant-time however many volumes there are.

### Fixed

- CAB: the file table was re-fitted at every power of two of its length (it shrank, then grew again), which is
  wasted work; it now grows by doubling.

- The bundled LZMA SDK is now 26.05 (was 24.08), taken from the 7-Zip author's release and still public domain.
  It brings the upstream fixes to the LZMA match finder and the LZMA2 encoder's error handling. The LZMA and LZMA2
  dictionary per level now matches 7-Zip 24.09 and later: levels 5 to 9 use 32, 64, 128, 256 and 256 MiB on 64-bit
  (never more than the input needs). Large inputs at high levels compress better but need more memory to write:
  about 11 times the dictionary, roughly 3 GiB at level 8 or 9.

### Added

- `FULTA_ARC_ENTRY_MTIME_LOCAL`: an entry's `mtime` is the local wall clock counted as if UTC, because the format
  stored local time: a ZIP DOS time with no UTC extra field, ALZ, RAR 1.5, EGG, LHA levels 0 and 1. Convert such
  times with the local zone, as Windows does.

- ZIP reading takes the NTFS extra field (0x000a) that 7-Zip and Windows tools write: the exact UTC modification
  time, to 100 ns, ahead of the extended timestamp and the DOS time.

- 7z writing can use the ARM-Thumb and RISC-V branch filters, as 7-Zip 24.03 and later do.

- Split archives can be written. Set `volume_size` (at least 64 KiB) and `fulta_arc_write_file` or
  `fulta_arc_edit_write_file` writes 7z as `name.7z.001`, `name.7z.002`, ... and ZIP as `name.z01`, ..., `name.zip`,
  with the disk numbers other tools expect. Every volume is written under a temporary name and all are renamed only
  when the whole set succeeded; a failure or cancel leaves nothing. If an older, longer set of the same name is
  there, the write is refused with the new `FULTA_ARC_ERR_EXISTS` and nothing is deleted. Callers with their own I/O
  use `fulta_arc_write_volumes` / `fulta_arc_edit_write_volumes` with a `fulta_arc_volume_sink_t`, and
  `fulta_arc_volume_name` names the volumes.
- 7z names can be hidden: `encrypt_names` with a password encrypts the whole header (as 7-Zip's `-mhe=on`), so the
  archive cannot be listed without the password.

- Editing and converting into ZIP now honour the chosen compression method: Store, Deflate, BZip2, LZMA, PPMd and
  Zstandard, with or without WinZip AES. The convert route re-encodes every entry with it; the copy route uses it for
  added and replaced entries and still copies untouched entries as they are. Each entry is streamed, so a large file
  is not held in memory (converting a source entry to LZMA passes it through a private temporary file). Before, any
  method other than the default returned `UNSUPPORTED` there. The copy route's added entries are now streamed too;
  they used to be read whole into memory first.

### Fixed

- RAR: RARLAB's own RAR 2.00 and 2.50 DOS installers (self-extractors) were refused as damaged. Their archive ends
  in an authenticity block whose header checksum follows no rule FultaArc knows; FultaArc does not read that block,
  and now skips it when the archive's main header announces authenticity information, instead of failing. All files
  of both installers extract with their stored CRC-32 and match what the installers themselves write.
- Two decoder bugs in paths that the ZIP methods never exercised (ZIP always supplies a known output size, and its
  xz samples carried no integrity Check), found while adding the standalone `.xz`/`.zst` readers: the **xz** block
  Check was read from the wrong offset when the compressed body was not 4-byte aligned (it skipped the Block
  Padding between the data and the Check), so a normal `.xz` with its default CRC64 Check failed; and the **zstd**
  decoder reported a clean end-of-frame as a truncation when the output size was unknown. Both are fixed; reading
  `.xz`/`.zst` and ZIP method 95/93 all verify correctly now.

- Archive editing: replacing an entry with a zero-length reader (one reporting `size == 0`) on the ZIP copy route
  corrupted the heap — the slurp buffer's capacity did not track its allocation, so the first grow freed the
  buffer twice. The buffer capacity is now always at least one byte.
- EGG method 3 (AZO): a stream truncated after its data but before the end record is now rejected. Extraction
  stops at the declared size, so the AZO end record could go unchecked; the decoder now validates that the stream
  ends cleanly there. Valid archives are unaffected.
- 7z extraction could crash in an optimized (non-sanitizer) build when reusing a folder's decode stream for a
  later entry of a solid archive — an error variable was read before being set on that path. Also builds cleanly
  at `-O1`/`-Og` now (`src/zip/zip.c`). A `make warn` step (compiled at `-Og -Werror`) runs as part of `make test`
  to keep this class of bug out.

### Added

- **Encrypted output when editing or converting** (`fulta_arc_edit_write`, `fulta_arc_edit_write_file` with
  `password`): every file entry is re-encoded and encrypted — WinZip AES (AES-128/192/256) for ZIP, 7zAES-256 for 7z
  — also when writing into the source's own format, since copied bytes cannot be encrypted. The encryption is
  streamed, so a multi-GiB entry still converts in bounded memory. The 7z route's temporary spill, which holds
  plaintext, is now created in a fresh owner-only directory (mode 0700, the file 0600) and its names are removed as
  soon as it is open, for every 7z convert. A scheme of the other format is `INVALID_ARG`; BCJ2 with encryption stays
  `UNSUPPORTED`. 7-Zip tests and extracts the results.
- **RAR 1.4 and RAR 1.5 archives**, read-only (`FULTA_ARC_FORMAT_RAR`): the RAR 1.5 compressed stream (methods
  m1-m5), stored files, solid archives, volumes (`.rar`, `.r00`, ...), directories and self-extractor fronts, and
  the older RAR 1.4 container, whose data is the same stream. The reader is the `fultaarc-cleanroom-rar15` clean
  room's (`src/rar/rar15/`, kept byte-identical by `make test`), worked out from RAR 1.55's and RAR 1.40's behaviour
  alone. Checked against what RAR itself extracted from 42 archives (56 files). **Encryption:** RAR 1.4's is
  read (the password callback is asked; RAR 1.4 checks a file with a 16-bit sum only, so a wrong password is caught
  less surely than by a CRC-32). RAR 1.5's is not: such an entry is flagged
  `FULTA_ARC_ENTRY_ENCRYPTED | FULTA_ARC_ENTRY_UNSUPPORTED`, extracting it returns `FULTA_ARC_ERR_UNSUPPORTED`, and
  no password is asked for. An archive that mixes RAR 1.5 entries with later ones
  is still read by the RAR 2-7 reader, which reports the RAR 1.5 entries unsupported.
- **Provenance pages** (`docs/provenance/`): for each decoder made in a clean room, four pages written for readers —
  the format, the decompression method, how every rule was derived from observation, and the provenance (what is
  shipped, every source with its terms, what was kept out, mechanical checks, SHA-256 anchors of the private record).
  LHA/LZH, RAR 1.4/1.5 and CAB Quantum. The Quantum room's raw documents, published byte-identical in an earlier
  edition, gave way to its four pages; the raw records stay private.
- **LHA/LZH archives**, read-only (`.lzh`, `.lha`, LArc `.lzs`, PMarc `.pma`, and self-extractors;
  `FULTA_ARC_FORMAT_LHA`): header levels 0-3 with their extended headers (paths, Unix and Windows times, Unix
  modes, symbolic links, 64-bit sizes) and all 17 methods seen in real archives: `-lh0-`, `-lhd-`, `-lh1-`,
  `-lh2-`, `-lh3-`, `-lh4-`, `-lh5-`, `-lh6-`, `-lh7-`, `-lhx-`, `-lk7-` (LHARK), `-lz4-`, `-lz5-`, `-lzs-`, `-pm0-`,
  `-pm1-` and `-pm2-` (PMarc). An unknown method string is listed and flagged `FULTA_ARC_ENTRY_UNSUPPORTED`. Each
  entry's data is checked against its stored CRC-16 (`FULTA_ARC_ERR_CHECKSUM`); a symbolic link's data is its
  target, as for tar. Names are decoded as UTF-8 when valid, else through the code page (CP932 unless the caller sets one), and a Shift-JIS, GBK or Big5
  character whose second byte is `0x5C` (`\`) stays whole instead of being read as a path separator.
  Self-extractors are searched for, in their first 1 MiB, only behind a recognised stub (DOS/Windows `MZ`, DOS
  `.COM`, Amiga, Atari TOS, Sharp X68000, PMarc). The decoder in `src/lha/lhadec/` is the deliverable of a clean
  room that wrote its own specification from Haruhiko Okumura's public-domain `ar002` and from running lhasa 0.6.0
  (ISC), UNLHA32.DLL 3.00 (`-lh2-`/`-lh3-`) and PMarc 2.00 / PMext 2.22 (`-pm2-`) as black boxes, read no LHA
  reader's source, and released its code under MIT; it is kept byte-identical to that room's final manifest (room
  state `5792dae`; `tests/tools/lhadec-pin.sh`, run by `make test`) and adapted only from outside (`src/lha/lha.c`,
  `src/lha/lhadec_shim.h`). Tested on the lhasa test corpus (`tests/manifests/lha.txt`: 209 archives, 252 files,
  sizes and CRC-32s from the lhasa program's own extraction; `tests/test_lha.c`; the robustness run). The format,
  the methods, how each rule was derived and the provenance are published in `docs/provenance/lha/`. Not supported: multi-volume and encrypted LHA archives (no available tool makes or reads them,
  so they could not be observed).
- Reading more formats, each implemented only from its public standard under MIT (D-020). So far: **gzip streams**
  (`.gz`, `FULTA_ARC_FORMAT_GZIP`) from RFC 1952 — one entry (named by the gzip FNAME field, else the archive name
  with a `.gz`/`.tgz`/`.taz` suffix stripped), the DEFLATE payload through the vendored miniz decoder, and the
  trailer CRC-32 verified; and **bzip2 streams** (`.bz2`, `FULTA_ARC_FORMAT_BZIP2`) — one entry (named from the
  archive name, since bzip2 stores none, `.bz2`/`.tbz2`/`.tbz`/`.tz2` handled), the payload and its per-block and
  combined CRCs through the vendored bzip2 library; **xz streams** (`.xz`, `FULTA_ARC_FORMAT_XZ`) from the public
  `.xz` format spec, through FultaArc's xz container decoder over the vendored LZMA SDK (the same decoder as ZIP
  method 95), the block Check verified; and **Zstandard streams** (`.zst`, `FULTA_ARC_FORMAT_ZSTD`) from RFC 8878,
  through the vendored zstd library. Each reads the first member/stream/frame (which covers tar.gz/tar.bz2/tar.xz/
  tar.zst and essentially every real single-file wrapper); the entry is named from the archive name (or the gzip
  FNAME) with the matching suffix stripped (`.tgz`/`.tbz2`/`.txz`/`.tzst` → `.tar`). More formats — the remaining
  container format CAB — follows the same way.
- **`tar.gz` / `tar.bz2` / `tar.xz` / `tar.zst` now list the tar's files.** A gzip/bzip2/xz/zstd stream that wraps a
  tar is transparently unwrapped: FultaArc decompresses the payload, detects the tar inside and opens the archive as
  TAR, so its members are listed and extracted directly instead of appearing as one `.tar` blob. The payload is
  decompressed into memory (up to 1 GiB) at open; a larger wrapped payload stays a single stored entry of its
  wrapper type (still fully extractable by streaming). A wrapper whose payload is not a known container (e.g. a
  gzip of a plain file) is unchanged — one stored entry.
- **ISO 9660** (`.iso`, `FULTA_ARC_FORMAT_ISO`) — the ECMA-119 CD/DVD filesystem, read-only, written from the
  published standard (no GPL/LGPL source). Reads the Primary Volume Descriptor and walks the directory tree,
  listing each file with its full path (the `;1` version suffix stripped) and streaming its extent. Now also reads
  **Joliet** (Microsoft's UCS-2 long names, from the supplementary volume descriptor) and **Rock Ridge**
  (IEEE P1281/P1282 SUSP/RRIP: the NM long names, SL symbolic links, names continued across a CE area, and
  CL/RE **deep-directory relocation** — a directory staged under `RR_MOVED` is rejoined to the logical tree at its
  real parent, matching the Linux kernel / libarchive behaviour); Rock Ridge is preferred, then Joliet, then the
  plain 8.3 identifier. Directory cycles, oversized extents, out-of-range roots and malformed System-Use entries
  are bounded/rejected. **zisofs** transparently-compressed files are decompressed (detected by the zisofs magic;
  per-block zlib via the block-pointer table, with zero-length blocks as holes), so the listing shows the real
  uncompressed size and extraction returns the original bytes.
- **UDF** (`.udf`/`.iso`, `FULTA_ARC_FORMAT_UDF`) — the ECMA-167 / OSTA UDF optical-disc filesystem, read-only,
  written from the published standard (no GPL/LGPL source). A separate filesystem from ISO 9660: detected by the
  `NSR02`/`NSR03` volume-recognition mark and **preferred over ISO 9660 on a bridge disc** (as 7-Zip does). Follows
  the anchor (sector 256) → volume descriptor sequence → partition + logical volume → file set → root ICB →
  directory tree, listing files by path (8-bit and 16-bit OSTA CS0 names) and streaming each file's extents. Every
  descriptor's tag checksum and CRC-ITU are validated and a mismatch rejected. Scope: 2048-byte blocks, one type-1
  partition map, File Entry and Extended File Entry, short/long allocation descriptors; embedded data, the Virtual
  Allocation Table, metadata/sparable partitions, named streams and extended attributes are `UNSUPPORTED`.
- **Microsoft Cabinet** (`.cab`, `FULTA_ARC_FORMAT_CAB`) — read-only, written from Microsoft's published Cabinet
  format (`[MS-CAB]`), RFC 1951 (MSZIP) and the `[MS-PATCH]` open standard (LZX) (no GPL/LGPL source). Reads
  **Store**, **MSZIP** (a per-block Deflate whose LZ77 window carries across blocks) and **LZX** folders; lists each
  file by its path (backslashes normalised, UTF-8 and code-page names) and slices it out of its folder's
  decompressed stream. Honours the RESERVE_PRESENT header/folder/datablock reserve and verifies each CFDATA
  checksum. LZX decodes the folder's continuous bitstream across its CFDATA blocks (the sliding window, repeated
  offsets, Huffman trees and an in-progress block span CFDATA boundaries), including aligned-offset blocks and the
  x86 E8 call-translation; the match length follows the CAB cap of 257 (no LZXD Extra Length). **Quantum** folders
  (typeCompress 2, window 2^10..2^21) decode too: the decoder was derived black-box, without any third-party Quantum
  code, against Windows' own cabinet reader and checked on Microsoft-shipped cabinets (Windows 95, Plus!, Internet
  Explorer 2.0). **Cabinet sets**: opened by file from any cabinet, a file that continues into the next cabinet is
  read through the set (the next cabinets are found by the names the headers declare, next to the opened file; a
  folder spanning the boundary is one stream, including a CFDATA block split in two), as on Windows 95's
  `win95_02.cab`…`win95_13.cab`. Opened in the middle of a set, the previous cabinets are walked the same way
  (`szCabinetPrev`), so the set reads identically from any of its cabinets; when a predecessor is missing, the
  files of the folder it started stay `UNSUPPORTED`. Oversized blocks, the per-folder
  decode size, the total decode cache and the LZX match offset are bounded.
- **cpio** (`FULTA_ARC_FORMAT_CPIO`) — the Unix cpio archive, read-only, written from the public cpio formats
  (POSIX + the SVR4 and old-binary layouts; no GPL/LGPL source). Reads all four variants — newc (070701), crc
  (070702), odc (070707) and old-binary (both byte orders); a symlink's target is surfaced as the entry data.
  Members extract byte-for-byte (cpio is uncompressed). Hardlinks are not resolved (each member is read on its own).
- **ar** (`.a`, `.deb`, `FULTA_ARC_FORMAT_AR`) — the Unix `ar` archive, read-only, written from the public ar
  format (BSD `ar(5)` + the SysV/GNU and BSD name extensions; no GPL/LGPL source). Resolves GNU short names, the
  GNU `//` long-name table, and BSD `#1/` inline names; symbol tables are skipped. Members extract byte-for-byte
  (ar is uncompressed).
- **lzip** (`.lz`, `FULTA_ARC_FORMAT_LZIP`) — the lzip stream (an LZMA1 payload with a CRC-32 trailer), read-only,
  written from the public lzip specification; the LZMA is decoded by the already-vendored LZMA SDK (no new codec).
  One entry, named from the archive name (`.lz` stripped, `.tlz`→`.tar`). A single-member file's trailer CRC-32 is
  verified. `tar.lz` is unwrapped to its tar members by the wrapper-reprobe layer.
- **Unix compress** (`.Z`, `FULTA_ARC_FORMAT_COMPRESS`) — the classic LZW `compress` stream, read-only, with a new
  LZW decoder written from the public compress format (LZW's patents have expired; no GPL/LGPL source used). One
  entry, named from the archive name (`.Z` stripped, `.taz`→`.tar`). `tar.Z` is unwrapped to its tar members by the
  wrapper-reprobe layer.
- **TAR** (`.tar`, `FULTA_ARC_FORMAT_TAR`) — the Unix tape archive, read-only, written only from the POSIX
  ustar/pax interchange formats and the GNU tar format manual (no GPL reference source). Handles the ustar header
  (with `prefix`+`name` path splitting), POSIX **pax** extended headers (`path`/`linkpath`/`size`/`mtime`), GNU
  long name/long link (`L`/`K`), base-256 numeric fields, and plain **v7** (no magic, detected by the header
  checksum). Regular files and directories extract byte-for-byte; a symbolic/hard link surfaces its target as the
  entry data; GNU sparse members are listed but marked unsupported. Detection validates the first 512-byte header's
  checksum. A bare `.tar` opens directly; `tar.gz`/`tar.bz2`/`tar.xz`/`tar.zst` need the wrapper-reprobe layer,
  which follows. Verified against GNU tar: ustar/pax/GNU/v7 archives (incl. a long path and a multi-block file)
  round-trip their size and CRC-32.
- A memory budget for decompression. `fulta_arc_options_t` gained `max_memory`: a cap on the total live memory the
  vendored decompressors (PPMd, LZMA/LZMA2, bzip2 and zstd) may hold while opening or extracting, so a hostile
  archive cannot drive unbounded allocation from its declared model/dictionary/window sizes. A decode that would
  exceed the cap stops with `FULTA_ARC_ERR_LIMIT`. `0` means unlimited (unlike `max_dictionary`, whose `0` means
  1 GiB); `max_dictionary` still caps each single request and is checked first. The cap bounds the codec buffers
  sized by archive-declared parameters, which are the attacker-driven allocations; it does not account for the
  library's own small bookkeeping or fixed-size scratch (the deflate window and codec input buffers). An embedder
  can own these allocations: `fulta_arc_options_t` also gained `allocator` (`fulta_arc_allocator_t` — an
  `alloc`/`realloc`/`free` triple plus a `ctx`), which backs the capped codec buffers. Supply all three or leave
  all NULL (the default malloc); supplying some is rejected with `INVALID_ARG`. The library never mixes allocators;
  the `fulta_arc_extract_alloc` result stays a plain `malloc` block released with `free()`, independent of this hook.
- The writer can now choose a compression method. `fulta_arc_write_options_t` gained `method`
  (`fulta_arc_method_t`), `filter` (`fulta_arc_filter_t`) and `filter_delta_dist`; the default keeps the previous
  behaviour (ZIP → Deflate, 7z → LZMA). ZIP writing now supports **Store, Deflate, BZip2 (method 12), LZMA
  (14), PPMd var.I (98) and Zstandard (93)**. BZip2/LZMA/PPMd/zstd ZIP output is accepted by 7-Zip and, for the
  Deflate-family methods, by Python's `zipfile`. An impossible method for the format returns `INVALID_ARG`; a
  method not yet implemented for a format returns `UNSUPPORTED`. For **7z**, `fulta_arc_write` now writes **Copy,
  LZMA, LZMA2, BZip2 and Deflate** (chosen by `method`), each accepted by 7-Zip and decoded back by our own reader.
  The **in-place 7z edit route** (editing a non-solid 7z) also honours `method`/`filter` on added or replaced
  entries now — the added entry gets the chosen coder while untouched entries are still copied verbatim (an added
  entry is buffered to re-encode, bounded by the entry). The **7z convert route** (re-encoding any archive into a
  fresh 7z) honours the full selector too — `method`, `filter`, BCJ2 and `solid` (SOLID_OFF yields an editable,
  non-solid result). The default stays the streaming single-folder LZMA/Copy with bounded memory; an explicit
  selector buffers the re-encode (bounded by the whole archive when solid, or by the largest file for SOLID_OFF).
  The **ZIP convert/edit routes** still re-encode with Deflate (ZIP's streaming encoder) and reject an explicit
  non-default method rather than ignoring it. The selector here is `method`/`filter`/`solid`; encrypting the output
  of an edit or convert (a password on those routes) remains a later increment and is still refused.
  7z also writes **PPMd (var.H)**. A 7z **content filter** can be chained before the compression method via
  `filter`: **Delta** (`filter_delta_dist` 1..256) and the branch converters **BCJ (x86), ARM, ARM64, PPC, SPARC,
  IA64**. The filter transforms the solid stream (and composes with 7zAES: decode runs AES → method → filter), and
  7-Zip reads the result. A content filter roughly doubles the writer's in-memory footprint for that archive (it
  buffers the filtered copy; BCJ2 is closer to triple, with its four split streams). **BCJ2** (the 4-stream x86
  filter) is also written: the data is split into its
  main/call/jump/rc streams, the first three compressed with the chosen `method` (rc stored) and chained into the
  BCJ2 coder; 7-Zip reads the result. BCJ2 combined with encryption is not yet written. A **`solid`** option controls
  7z grouping (7-Zip's `-ms`): the default keeps one solid folder for all files (best ratio), while
  `FULTA_ARC_SOLID_OFF` writes **one folder per file** — the result can be edited in place (added/removed/renamed
  without reconverting), which `fulta_arc_editable()` reports. Every method, filter and BCJ2 composes with either
  grouping (SOLID_OFF encryption uses one 7zAES coder per file, which 7-Zip reads). With `SOLID_OFF` the `progress`
  callback is reported per folder (once per file), not as a single ramp over the whole archive. `solid` applies to
  `fulta_arc_write` and the **7z convert route** (SOLID_OFF yields an editable result on both); on the **7z
  in-place-edit route** it is moot — the source non-solid 7z is already one folder per file — and is accepted without
  effect. ZIP has no solid grouping (ZIP is never solid), so `fulta_arc_write` ignores `solid` for a ZIP target and
  the ZIP convert/edit routes reject an explicit non-default `method`/`filter`/`solid` rather than ignoring it.
- ALZ and EGG are now read (`FULTA_ARC_FORMAT_ALZ`, `FULTA_ARC_FORMAT_EGG`), integrated from the
  fultaarc-cleanroom-alzegg clean room. ALZ: store / Deflate / ALZ-framed bzip2, ZipCrypto, split volumes
  (`.a00`…), CP949 or UTF-8 names, empty-folder entries. EGG: store / Deflate / bzip2 / LZMA / AZO (EGG's own
  method), ZipCrypto / AES-128/256 / LEA-128/256, solid and split sets, self-extracting archives, descriptions,
  the system attribute, and CP949 passwords. The ALZ container parser is from the clean room's black-box reader
  (not unalz); verified against 253 ALZip-made fixtures and cross-checked with unalz 0.65.

- API, from Rubraview's integration (all additive): a `progress` callback in `fulta_arc_options_t` (bytes decoded,
  including a solid skip); a per-entry `solid_group` index (entries of one 7z folder / RAR solid run share it);
  `fulta_arc_check_password(arc, index)` to verify a password by the format's check value without decoding;
  a `FULTA_ARC_ENTRY_NAME_NOT_UNICODE` flag and an `auto_codepage` option for legacy (non-Unicode) names; and a
  `keep_damaged` option with a `FULTA_ARC_DAMAGED` result that keeps the bytes an entry did decode past a checksum
  or truncation. `tests/test_api.c` covers them.

- Archive editing (`fulta_arc_editable`, `fulta_arc_edit_begin` / `_remove` / `_replace` / `_add` / `_write` /
  `_write_file` / `_free`): produce an edited copy of an archive — remove, replace, add or rename entries — without
  touching the source. A ZIP edited into a ZIP copies untouched entries' packed bytes verbatim (no re-encode); any
  archive (ZIP, 7z, RAR, ALZ, EGG) can be re-encoded into a fresh ZIP or 7z (the convert route). ZIP output is
  fully streamed, so a multi-GiB entry needs only bounded memory; 7z output keeps memory bounded too but spills
  each entry's plaintext to a temporary file (the LZMA encoder reads its input by pulling, so the pushed decode is
  bridged through disk) — bounded memory, not bounded disk. A non-solid 7z (one file per folder) edited into a 7z
  now also copies untouched entries' folders verbatim — each folder's coders, bind pairs and packed bytes are
  re-emitted byte-for-byte, including multi-coder chains such as BCJ+LZMA2, so an untouched entry is not
  recompressed; only added or replaced entries are encoded. Editing an encrypted archive is refused.
  `tests/test_edit.c` covers it.

- P1 (2026-10-03): the library's skeleton — `include/fulta/arc.h` (`fulta_arc_*`), random-access sources with a
  volume resolver, streaming extraction into sinks; ZIP reader (store, Deflate, Deflate64, bzip2, LZMA, ZipCrypto,
  WinZip AES, Zip64, split archives) and 7z reader (Copy, LZMA, LZMA2, PPMd, BZip2, Deflate, Deflate64, BCJ, BCJ2,
  ARM, ARMT, ARM64, PPC, SPARC, IA64, RISC-V, Delta, Swap, 7zAES with encrypted headers, solid and split archives),
  written from the project's format documents; ZIP and 7z writing (from Rubraview); crypto (AES, SHA-1/256, HMAC,
  PBKDF2, LEA); vendored proven_c_lib v0.6.0, LZMA SDK 24.08, miniz 3.1.2, bzip2 1.0.8 and ALZ's bzip2 apart.
  `make test` (host, sanitizers) and `make win64` (mingw-w64 UCRT build).
- P2 (2026-10-03): RAR 2.0-7.0 reader (RAR 4 and RAR 5 containers, volumes, encryption), ported from the
  rar-decoder clean room; RAR 1.5 data is reported unsupported until its clean room finishes.
- P3 (2026-10-03): ALZ reader (Deflate, ALZ's bzip2, ZipCrypto, volumes) ported from the alz-decoder clean room;
  names and passwords in code pages 437, 1252, 932, 936, 949, 950 (Unicode's Microsoft tables).
- P4/P5 (2026-10-03): EGG reader (blocks, solid, split, ZipCrypto, AES, LEA) and the AZO decoder, written from the
  project's format documents.
- Rarer ZIP methods: xz (95), PPMd var.I (98), Zstandard (93; also the 7z coder `04F71101`, through the vendored
  zstd 1.5.7 decoder); PKWARE strong encryption with AES and a password; ZIP files with bytes appended after the
  end record.
- ZIP methods 1-6 of PKZIP 1.x: Shrink, Reduce (factors 1-4) and Implode (4/8 KiB, two or three trees), including
  PKZIP 1.01's Implode entries with a minimum match of 2; checked on PKWARE's own PKZIP 0.90-2.04g archives and on archives PKZIP 0.90-1.10 wrote in DOSBox.
- Russian names: code pages 866 (DOS), 1251 (Windows) and KOI8-R (`FULTA_ARC_CP_866`, `_1251`, `_KOI8R`), from
  unicode.org's mapping files.
- Tests: libarchive 3.7.7's 7z and ZIP test archives as a manifest set (82 archives, 7-Zip 25.01 as the oracle;
  a per-archive code page field); `test_robust`, every fixture as is and mutated, under the sanitizers
  (`FULTA_ARC_ROBUST=<n>` for a longer run).

### Changed

- ALZ compression method 3 is now read as raw Deflate, matching ALZip's own readers (no tool writes method 3, so
  this only affects hand-made or unusual archives; the CRC-32 still guards the result), rather than reported
  unsupported.
- proven_c_lib adopted further: CRC-32 and SHA-256 now run through proven (`proven_crc32_update`, `proven_sha256_*`)
  instead of FultaArc's own copies; proven vendored at v0.6.0+3 (adds `proven_fs_mkdir_all`).
- AES gains a hardware path: AES-NI (x86, 128/256-bit) for the single-block encrypt and decrypt, chosen at runtime
  only after it reproduces the FIPS-197 known answers, with the table-based software cipher as the fallback
  everywhere else (192-bit, non-x86, no AES-NI). 7z AES decode (AES-256-CBC) went from ~16x 7-Zip's one-thread time to ~2x via 8-wide AES-NI CBC decrypt; WinZip CTR is now bounded by Deflate, not AES.
- Build: header-dependency tracking (`-MMD -MP`) so changing a header rebuilds every object that includes it. This
  fixes a latent hazard where a struct-layout change in a header left stale objects with the old layout.

- Scope narrowed to the completed core for the public MIT release (owner, 2026-10-08): ZIP, 7z and RAR 2.0-7.0 and
  their codecs and crypto. ALZ, EGG and AZO are held out of the build and the format registry until their clean
  rooms deliver and the result is ported; their sources stay in the tree (`src/alz`, `src/egg`, `src/azo`). The
  library depends only on permissive material (MIT, public domain, BSD, zlib, Unicode data licence).

### Deprecated

### Removed

### Fixed

- EGG: FILETIME past year 2262 overflowed (now clamped); two leaks on EGG error paths.
- ZIP: a failing decoder's input was destroyed twice.
- ZIP: general flag bit 13 without strong encryption (PKZIP 1.10's authenticity verification) no longer makes entries
  unsupported.

### Security
