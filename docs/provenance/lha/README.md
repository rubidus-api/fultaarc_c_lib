# LHA / LZH

FultaArc reads LHA/LZH archives (`.lzh`, `.lha`, LArc `.lzs`, PMarc `.pma`, self-extractors) with `lhadec`, a
decoder written in FultaArc's own clean room. Four pages document it:

| Page | What it answers |
|---|---|
| [`format.md`](format.md) | how an LHA archive and its headers are laid out |
| [`algorithm.md`](algorithm.md) | how each compression method is decoded (17 methods) |
| [`derivation.md`](derivation.md) | how every rule in those two pages was found, observation by observation |
| [`provenance.md`](provenance.md) | what FultaArc contains, which sources were used on what terms, which were kept out |
