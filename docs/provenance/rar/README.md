# RAR (2.0 – 7.0 and 1.4 / 1.5)

FultaArc unpacks the complete RAR family from **RAR 1.4 through RAR 7.0**, using two distinct clean-room lineages matched
to each generation's availability of permissive documentation:

| RAR Generation | Decoder in FultaArc | Method | Documentation |
|---|---|---|---|
| **RAR 2.0 – 7.0** (RAR 4 / RAR 5 containers, LZ, audio, PPMd var.H, filters, RAR 3/5 AES encryption, BLAKE2sp) | `src/rar/` (excluding `rar15/`, `rar15_enc/`) | Two-team **specification-based clean room** (`rar-spec` -> `rar-decoder`) | [`derivation.md`](derivation.md) · [`provenance.md`](provenance.md) |
| **RAR 1.4 / 1.5** | `src/rar/rar15/` | **Black-box** clean room (`fultaarc-cleanroom-rar15`) | [`../rar15/README.md`](../rar15/README.md) |
| **RAR 1.5 Encryption** | `src/rar/rar15_enc/` | **Black-box + SMT** clean room (`fultaarc-cleanroom-rar15-enc3`) | [`../rar15/enc3/README.md`](../rar15/enc3/README.md) |

Neither lineage uses UnRAR source code or 7-Zip's RAR code.
