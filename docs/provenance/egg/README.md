# EGG

FultaArc reads ESTsoft's EGG archive format (`.egg`, split volumes `NAME.volN.egg`, and self-extracting `.exe`
archives including `NAME.vol1.exe`) using `src/egg/egg.c`, specified and verified across **two independent, mutually
firewalled black-box clean rooms** (`egg-container` and `egg-verify`) plus the unifying `fultaarc-cleanroom-alzegg` clean
room. Compression method 3 (**AZO**) is documented separately in [`../azo/`](../azo/README.md).

| Page | What it answers |
|---|---|
| [`format.md`](format.md) | EGG signatures, sub-records, solid/split layout, ZipCrypto/WinZip AES/LEA-CTR framing, and SFX support |
| [`derivation.md`](derivation.md) | how two independent firewalled clean rooms derived the format from ALZip outputs alone and reconciled every difference |
| [`provenance.md`](provenance.md) | FultaArc files, honest disclosure of `egg-container`'s neutralized incident, excluded sources, and SHA-256 anchors |
