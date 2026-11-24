# RAR 1.5 Encryption (`fultaarc-cleanroom-rar15-enc3`)

FultaArc decodes password-protected RAR 1.5 entries (`HEAD_FLAGS & 0x0004`) using the standalone MIT stream cipher
[`src/rar/rar15_enc/rar15_cipher.c`](../../../../src/rar/rar15_enc/rar15_cipher.c), derived in the separate clean room
`fultaarc-cleanroom-rar15-enc3` (final commit `2c3c4a4`, byte-identical to `02-mit-library/src/rar15_cipher.c`,
SHA-256 `1fd0002b6d00443552837502962deb18c9119f090b1cd1a279f53372cf36e5cf`).

## Published Reports, Specifications & Interactive Guides

| Document | Language | What it contains |
|---|---|---|
| [`RAR15-DERIVATION-GUIDE.html`](RAR15-DERIVATION-GUIDE.html) | English | Interactive offline step-by-step derivation guide (Stages 0–IV, `Z_2^4` gauge equivalence, worked `pw="a"` trace, JS simulators) |
| [`RAR15-DERIVATION-GUIDE.ko.html`](RAR15-DERIVATION-GUIDE.ko.html) | 한국어 | 단계별 대화형 유도 가이드 (오프라인 HTML) |
| [`RAR15-DECRYPT-UNPACK-SPEC.md`](RAR15-DECRYPT-UNPACK-SPEC.md) · [HTML](RAR15-DECRYPT-UNPACK-SPEC.html) | English | Encrypted archive decryption & decompression (`0x30..0x35`) specification |
| [`RAR15-DECRYPT-UNPACK-SPEC.ko.md`](RAR15-DECRYPT-UNPACK-SPEC.ko.md) · [HTML](RAR15-DECRYPT-UNPACK-SPEC.ko.html) | 한국어 | 암호화 아카이브 복호화 및 압축 해제 규격서 (`0x30..0x35`) |
| [`RAR15-KEYSTREAM-RESEARCH-REPORT.md`](RAR15-KEYSTREAM-RESEARCH-REPORT.md) | English | Formal mathematical research report |
| [`RAR15-KEYSTREAM-RESEARCH-REPORT.ko.md`](RAR15-KEYSTREAM-RESEARCH-REPORT.ko.md) | 한국어 | 공식 수학적 연구 보고서 |
| [`FINAL.md`](FINAL.md) | English | Clean-room mission summary (`6/6` Universal Decryption Gate runs, `600/600` passwords, `1,800/1,800` files) |
| [`FINAL-REVIEW-324.md`](FINAL-REVIEW-324.md) | English | Independent Cycle 324 review and hardened C99 verification record |
| [`PROVENANCE.md`](PROVENANCE.md) | English | Source boundaries, approved priors, incident disclosures, and synthetic gate fixture provenance |
| [`APPENDIX-2026-11-23-c322-c323-session-log.md`](APPENDIX-2026-11-23-c322-c323-session-log.md) | English | Redacted historical transparency appendix for the Cycle 322 -> 323 exploration interval |

## Reproducible Evidence Release Bundles

The full clean-room release & offline evidence bundles (containing these reports, the standalone MIT C99 library, and the reproducible Cycles `001..324` research archive + `verify_bundle.py`) are packaged for GitHub Release distribution:

- `fultaarc-cleanroom-rar15-enc3-release-en.zip` — SHA-256 `a8f5ed1350355852b12b2f9d6c0dcc60ada6743cdc71dcf74101fb8265b4009d`
- `fultaarc-cleanroom-rar15-enc3-release-ko.zip` — SHA-256 `a85cd8e384a814dcb42d1a01a051b86c23837bc5b14e687f9d8e155bac143b2a`

**Public packaging note:** Prior to public distribution, five archived run logs under
`03-reproducible-evidence/work/archive/{323,324}*/runs/` (`323-full-keystream-generator-blind/runs/0046.err` and
`324-c322-to-c323-constructive-bridge/runs/{0003.err,0005.err,0012.err,0014.out}`) had their local workspace path
prefix masked to `<repo>/`, with `SHA256SUMS.txt` updated accordingly (`archive.jsonl` inside the bundle retains the
pre-masking hash values for those five run logs). The unmodified original archives are preserved in FultaArc's private
evidence record.
