# CAB Quantum — where the data sits

This page describes how Quantum-compressed data sits inside a Microsoft Cabinet as FultaArc reads it: which folders
hold it, the window parameter, what a data block holds and where it ends, and how a folder continues across the
cabinets of a set. How the compressed bits themselves are decoded is in [`algorithm.md`](algorithm.md). How each rule
here was found is in [`derivation.md`](derivation.md): format §*n* is established in derivation §C*n*. The sources
and their licences are in [`provenance.md`](provenance.md).

The **container** — the cabinet header, folder and file records, data blocks and their checksum — is Microsoft's
published Cabinet format and is not described here; FultaArc's `src/cab/cab.c` reads it for every compression. This
page covers only what is particular to Quantum. Every rule was observed and tested against Microsoft's own cabinet
reader (Windows 11 `cabinet.dll` through `extrac32.exe`) and on Microsoft-made cabinets; nothing is claimed for
inputs unlike those. Where the format leaves a case open, FultaArc's choice is marked **policy**.

## 1. A Quantum folder and its window

A folder record (CFFOLDER) whose `typeCompress` has **2** in its low four bits holds Quantum data in its data blocks.

The other bits of the 16-bit `typeCompress` field:

| Bits | Meaning |
|---|---|
| 0–3 | 2 = Quantum |
| 4–7 | no effect on decoding |
| 8–12 | **X**, the window parameter: valid **10 to 21** |
| 13–15 | no effect on decoding |

X sets the window, 2^X bytes, and with it the size of the copy-distance tables (algorithm §5). A folder with X
outside 10..21 is not decoded by Microsoft's reader; FultaArc refuses it as **unsupported**.

Microsoft's 1995 encoder writes `typeCompress = 0x0072 | X << 8` (bits 4–6 set, which do not matter); the Windows 95,
Plus! and Internet Explorer 2.0 cabinets all carry `0x1572` (X = 21).

## 2. Data blocks

A Quantum folder's data blocks (CFDATA) form one stream, decoded in order:

- **The arithmetic coder restarts at every block.** Each block's body starts with a fresh coder state, read from the
  block's own first bytes (algorithm §2).
- **The model and the window carry over.** The adaptive tables (algorithm §3–§6) and the output window keep their
  state from one block to the next for the whole folder. A copy in one block may read bytes produced by an earlier
  block.
- **`cbUncomp` is exact.** A block produces exactly `cbUncomp` bytes. A symbol is never split across blocks: a copy
  that would run past the block's end is an error.

## 3. Where a block ends

- **The body must hold every bit the decoder reads**, including the bits the coder shifts in after the block's last
  symbol (algorithm §2). A body one byte shorter than that is an error; bits past the end are not read as zeros.
- **Bytes after those bits are ignored.** Decoding stops once the block has produced `cbUncomp` bytes; anything left
  in the body changes nothing.

Microsoft's encoders were measured to expand incompressible data by at most 1.52 % (`cbData` 33,265 for a 32,768-byte
block). **Policy:** FultaArc accepts a Quantum block body of up to 32,768 + 6,144 bytes.

## 4. Folders across cabinets

A set of cabinets may continue one folder from a cabinet into the next. These rules are part of the public container
format; they were verified on Quantum sets:

- a folder continued into the next cabinet is that cabinet's folder 0; its files are marked with `iFolder` 0xFFFD,
  0xFFFE or 0xFFFF;
- the continued folder is **one Quantum stream**: the model and window go on across the cabinet boundary, and the
  coder restarts at each block as usual;
- a data block split at the boundary has `cbUncomp` 0 in its first part; the two bodies are joined and decoded as one
  block.

## 5. Reader policies — choices, not format facts

- **A copy whose source lies before the start of the folder's output is an error.** Microsoft's reader reads zeros
  there at most windows and gives unrepeatable output at X = 10; no Microsoft-made stream seen contains such a copy
  (algorithm §7).
- **Out-of-range X** is refused as unsupported, not decoded (§1).
- **Opened from any cabinet of a set**, FultaArc follows the previous- and next-cabinet names in the same directory,
  so the set lists and extracts the same files whichever cabinet is opened. A folder whose first cabinet cannot be
  reached is unsupported as a whole. (This is FultaArc's CAB reader, the same for every compression.)
- **Writing** Quantum is not supported.
