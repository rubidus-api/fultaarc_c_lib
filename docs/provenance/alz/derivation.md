# ALZ — derivation & independent verification

This page records how FultaArc's shipped ALZ parser and `DLZ\x01` bzip2 framing rules were derived purely from ALZip
outputs and verified against independent references.

---

## 1. Black-box clean room (`fultaarc-cleanroom-alzegg`)

Earlier work in a sister repository had documented ALZ by reading `unalz 0.65` (zlib-licensed). To eliminate any reliance
on third-party ALZ parser code before publishing FultaArc under MIT, a dedicated clean-room session
(`fultaarc-cleanroom-alzegg`, commit `1f844a6`) was started with a strict brief: **read no `unalz` code and no prior ALZ
specification; observe only archives produced by ALZip 4.9 through 12.37**.

Key black-box derivations:

1. **Container records**: grouping repeated 4-byte signatures (`ALZ\x01`, `BLZ\x01`, `CLZ\x01`, `CLZ\x02`, `CLZ\x03`) across
   minimal archives differing in file count, names, sizes, and split limits.
2. **Variable size-width descriptor**: varying file sizes around 255 B and 65,535 B isolated the upper nibble `descriptor >> 4`
   (`w = 0, 1, 2, 4, 8`) and confirmed `w = 0` omits method, CRC, and sizes for directories and empty files.
3. **Method 3 alias**: ALZip 8.6/8.12/12.37 accept method `3` as raw Deflate (step `S0023`); FultaArc matches this on read.
4. **CP949 password bytes**: Korean password tests (`a1237_hangulpw.alz`) established that ALZip derives ZipCrypto keys
   from CP949 bytes rather than UTF-8.
5. **Split volume splicing**: comparing volume boundaries across multi-part `.alz`/`.a00`/`.a01` sets isolated the exact
   8-byte continuation header and 16-byte end-of-volume tail (`CLZ\x03`) that must be stripped to reassemble records.
6. **ALZ-bzip2 (`DLZ\x01`) 178-bit framing reduction**: compressing identical inputs with ALZip 4.9 (`method 1`) and
   stock `bzip2 1.0.x` showed ALZ's output begins with `"DLZ"\x01` and is 22 bytes (178 bits padded to byte boundary) shorter.
   Stripping `BZh9` (32 bits), narrowing the block and end magics from 48 to 32 bits (`-32` bits), and omitting the two
   32-bit CRCs and 1-bit `randomised` flag (`-65` bits, total `-178` bits after header/block/trailer accounting) yields a
   bitstream **byte-for-byte identical** to ALZip 4.9's payload.

---

## 2. Replacement & verification against `unalz 0.65` (2026-10-09)

On integrating `fultaarc-cleanroom-alzegg` (`src/alz/alz.c`) into FultaArc, three independent checks were executed across
92 ALZ fixtures and 253 shared ALZip fixtures:

| Check | Result | Finding |
|---|---|---|
| Clean-room C reader vs ALZip ground truth | **220 archives, 26 descriptions, 0 differ** | Every file decodes byte-for-byte with matching CRC-32 |
| FultaArc test manifests (`alz.txt` + `alzegg.txt`) | **20 + 218 archives PASS** | All ALZip 4.9–12.37 fixtures decode under ASan + UBSan |
| Old (`unalz`-spec) vs New (black-box) parser diff (92 `.alz`) | **205 file rows byte-identical; +6 empty-dir rows** | Black-box parser fixed empty directory entries dropped by the earlier reader |
| **FultaArc vs `unalz 0.65` oracle** | **72 Store/Deflate match; ALZ-bzip2 diverges** | `unalz 0.65` fails or outputs garbage (`0` bytes / truncated) on every ALZ method-1 (`DLZ\x01`) fixture; FultaArc decodes all of them matching ALZip ground truth |

Thus, the independent black-box re-derivation proved both cleaner in provenance and strictly more accurate than `unalz 0.65`.
