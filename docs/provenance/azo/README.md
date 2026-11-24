# AZO (EGG compression method 3 — decode-only)

AZO is ESTsoft's proprietary compression algorithm used as method `3` inside `.egg` archives. **FultaArc implements AZO
decoding only** and writes neither AZO encoders nor encoder specifications, respecting ESTsoft's Korean patent
**KR101049699B1** (in force until 2029-07-17).

| Page | What it answers |
|---|---|
| [`algorithm.md`](algorithm.md) | AZO chunk framing, x86 `E8`/`E9` transform, 32-bit binary arithmetic coder, dual-tree model, and 128-entry history matches |
| [`derivation.md`](derivation.md) | how three rounds of black-box experiments, decoder-oracle bisections, and `alzegg` binary-corpus fixes pinned every constant |
| [`provenance.md`](provenance.md) | patent-respecting decode-only boundary, honest disclosure of permitted hypotheses, excluded sources, and SHA-256 anchors |
