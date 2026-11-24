# ALZ — provenance

This page records where FultaArc's ALZ decoder came from, why it carries no code or specification from `unalz`, and how
to check its evidence record.

This is an engineering record, not legal advice. Where a step depended on a licence judgement, the record identifies who
made it; it does not state a legal conclusion.

---

## 1. Statement

FultaArc's ALZ container parser (`src/alz/`) is **MIT-licensed C23 code** written from FultaArc's own black-box clean room
(`fultaarc-cleanroom-alzegg`, commit `1f844a6`), which worked out the format solely from ALZip 4.9–12.37 outputs.

- **Independence from `unalz`:** Although earlier exploratory work in a sibling repo (`alz-decoder`) had referenced
  `unalz 0.65` (zlib license), FultaArc replaced that parser on 2026-10-09 with the `fultaarc-cleanroom-alzegg` parser
  derived from scratch with zero access to `unalz` source code. (`unalz 0.65` fails to decode ALZ method-1 bzip2
  fixtures that FultaArc's clean-room parser decodes cleanly.)
- **Permissive codec reuse:** `vendor/bzip2_alz/` contains `bzip2 1.0.8` (bzip2 license) with explicitly documented framing
  hooks for `DLZ\x01`/`DLZ\x02` (`vendor/bzip2_alz/VENDORED.md`).

---

## 2. What FultaArc contains

| Path | Role | Origin | License |
|---|---|---|---|
| `src/alz/alz.c`, `src/alz/alz.h` | ALZ container & multi-volume reader | Transcribed from `fultaarc-cleanroom-alzegg` (`1f844a6`, black-box only) | MIT |
| `src/alz/alz_glue.c`, `src/alz/compat.h` | FultaArc API adapter, ZipCrypto & CP949 handling, resource caps | FultaArc contributors | MIT |
| `vendor/bzip2_alz/*` | Modified bzip2 1.0.8 decompressor for `DLZ\x01`/`DLZ\x02` | Julian Seward's `bzip2 1.0.8` + ALZ framing hooks | bzip2 license (`vendor/bzip2_alz/LICENSE`) |

---

## 3. Sources used vs. sources kept out

| Category | Details | Status |
|---|---|---|
| **Black-box instruments** | ALZip 4.9, 5.03, 6.7, 8.12, 8.6, 12.37 run only to produce archives of chosen inputs | Permitted black-box observation; executables not inspected or redistributed |
| **Bzip2 upstream** | `bzip2 1.0.8` (`vendor/bzip2/`) | Permissive bzip2 license |
| **Code pages** | Microsoft CP949 Unicode mapping table | Unicode License v3 (`THIRD_PARTY_NOTICES.md`) |
| **Excluded code** | `unalz`, ESTsoft internal/proprietary code, disassembly/strings/`objdump` of ALZip | Excluded from `fultaarc-cleanroom-alzegg` and shipped parser |

---

## 4. Verifiable evidence anchors (private archive)

```text
f432402877a9ecbbe99291c70573735bdaa051c4a38e950e37da4d328d9fe952  alzegg.bundle
b948012c31b03a5a5d63ba36e8432d2e06efb3eabca796ede99ba8afd9b81869  alzegg-cleanroom-1f844a6b0c54.tar.gz
41288daabe24458cddb43b50a1947d2edd18fc7bedc8e775e7db9d979841b970  alz-decoder.bundle  (superseded historical lineage)
```
