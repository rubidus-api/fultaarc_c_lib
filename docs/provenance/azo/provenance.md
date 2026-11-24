# AZO — provenance

This page records the origin, patent-respecting scope, permitted hypotheses, excluded materials, and verifiable evidence
anchors for FultaArc's AZO (`EGG method 3`) decoder.

This is an engineering record, not legal advice. Where a step depended on a licence or patent judgement, the record
identifies who made it; it does not state a legal conclusion.

---

## 1. Statement & patent-respecting decode-only scope

FultaArc's AZO support (`src/azo/azo.c`) is **MIT-licensed C23 code** developed by FultaArc contributors from three
rounds of clean-room black-box analysis (`azo-blackbox`, 22 commits) and extended/corrected on binary corpora in
`fultaarc-cleanroom-alzegg`.

- **Decode-only by policy:** ESTsoft holds Korean patent **KR101049699B1** (covering AZO compression; term running to
  **2029-07-17**). Consistent with FultaArc's policy (`src/azo/azo.c` decodes only; FultaArc writes only ZIP and 7z), **no
  AZO compressor/encoder was written, prototyped, or described**.

---

## 2. Honest disclosure of hypotheses

Rather than overclaiming pure zero-prior derivation, FultaArc's clean-room audit (`ops/DECISIONS.md` D-050 and the
predecessor audit record) explicitly notes the two permitted public inputs allowed as **hypotheses to be tested against
ALZip outputs** by the `azo-blackbox` brief:

1. **Public encyclopedia summary (`namu.wiki/w/AZO`):** Read in Round 1 solely for high-level orientation (identifying
   `.com`/`.sys` file types triggering AZO and stating that AZO uses LZ77 with a binary range coder similar to LZMA).
2. **Public-domain LZMA SDK specification (`lzma-specification.txt`):** Consulted as structural background (FultaArc
   already vendors LZMA SDK 26.05 in public domain) while every exact AZO framing byte, slot formula, dual-tree rule,
    MTFT list size, and normalization branch was derived from and proved against ALZip's own bitstreams and decoder-oracle
   code-value bisections.

No proprietary or third-party AZO code was consulted.

---

## 3. What FultaArc contains

| Path | Role | Origin | License |
|---|---|---|---|
| `src/azo/azo.c` | AZO chunk framing, `31 <flag>` / x86 call/jump transform, 32-bit binary arithmetic coder, dual-tree & MTF decoder | Derived from `azo-blackbox` + `fultaarc-cleanroom-alzegg` specs | MIT |

---

## 4. Sources excluded

| Excluded source | Handling |
|---|---|
| ESTsoft `UnEGG` source code & `EGG_Specification.pdf` | Never downloaded, read, or referenced |
| `EggDotNet` (including its AZO implementation), ClamAV EGG/AZO code | Never read or referenced |
| Disassembly, `strings`, debugging, or binary inspection of ALZip | Strictly prohibited and never performed |
| AZO compression / encoding | Intentionally omitted (decode-only) |

---

## 5. Verifiable evidence anchors (private archive)

```text
5e45fb9df33cfb2dab19b8e134189e37ca5c814614a90473267d7b2aa06c4d99  azo-blackbox.bundle
c0132a60239d58814fc7420046382af5ba39085e67a947ae72839059775f8fce  azo-decoder.bundle
f432402877a9ecbbe99291c70573735bdaa051c4a38e950e37da4d328d9fe952  alzegg.bundle
```
