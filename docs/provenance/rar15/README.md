# RAR 1.4 / 1.5

FultaArc reads the archives written by RAR 1.4 and RAR 1.5 for DOS with a decoder (`unpack15` and its archive
reader `rar15arc`) written in a clean room from the observed behaviour of RAR 1.55 for DOS, with RAR 1.40.2 beta
for the older format. Four pages document it, written for a reader who has not seen the room:

| Page | What it answers |
|---|---|
| [`format.md`](format.md) | how a RAR 1.4 / 1.5 archive and its headers are laid out |
| [`algorithm.md`](algorithm.md) | how the compressed stream is decoded, and the RAR 1.4 cipher |
| [`derivation.md`](derivation.md) | how every rule in those two pages was found, observation by observation |
| [`provenance.md`](provenance.md) | what FultaArc contains, which sources were used on what terms, which were kept out |

Format §*n* is established in derivation §C*n*; algorithm §*n* in derivation §M*n*. Encryption: both the RAR 1.4 cipher
(`format.md` §16, `algorithm.md` §16, `derivation.md` §M16) and the RAR 1.5 stream cipher are implemented. RAR 1.5's
cipher (`src/rar/rar15_enc/rar15_cipher.c`) was derived in a separate clean room (`fultaarc-cleanroom-rar15-enc3`),
whose reports, interactive derivation guides, specification, and provenance are published in
[`enc3/`](enc3/README.md). RAR 1.5 authenticity information is recognised and not verified (`format.md` §11).

**FultaArc's edition.** These pages are the `fultaarc-cleanroom-rar15` room's own, from its final state (`b9fd1c0`),
with two passages shortened by FultaArc: `format.md` §11 and §12 and `derivation.md` §C11 and §C12 no longer describe
what that room observed inside RAR 1.5's authenticity block and password cipher (the full text of those pages stays
in FultaArc's private record). FultaArc ships the `rar15` room's `src/unpack15.*` and `src/rar15arc.*`, byte-identical,
as `src/rar/rar15/`, and `fultaarc-cleanroom-rar15-enc3`'s `rar15_cipher.c`, byte-identical, as `src/rar/rar15_enc/`;
the rooms' standalone command-line tools are not part of FultaArc.
