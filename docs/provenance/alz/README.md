# ALZ

FultaArc reads ESTsoft's ALZ archive format (`.alz`, split volumes `.a00`, `.a01`, ..., `.b00`, ...) using a parser
(`src/alz/alz.c`) transcribed from an independent black-box clean room (`fultaarc-cleanroom-alzegg`), paired with
a modified bzip2 framing handler (`vendor/bzip2_alz/`) for ALZ's cut-down `DLZ\x01` stream format. Four pages document it:

| Page | What it answers |
|---|---|
| [`format.md`](format.md) | how ALZ headers, variable-width sizes, split volumes, ZipCrypto, and `DLZ\x01` bzip2 framing work |
| [`derivation.md`](derivation.md) | how each rule was derived from ALZip 4.9–12.37 outputs alone and verified against `unalz 0.65` |
| [`provenance.md`](provenance.md) | what FultaArc ships, why the shipped parser is independent of `unalz`, and SHA-256 evidence anchors |
