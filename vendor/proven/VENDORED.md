# Vendored proven_c_lib

- Upstream: `https://github.com/rubidus-api/proven_c_lib` (MIT; see `LICENSE` and `THIRD_PARTY_NOTICES.md`).
- Snapshot: tag `v0.119.3`, commit `47b6ec70ee0933b8b1207f5f18ef4ad528e193cf`, taken with
  `git archive v0.119.3 include src/proven platform crypto LICENSE THIRD_PARTY_NOTICES.md` on 2026-11-24
  (was `v0.114.0`, `5236974e`, since 2026-11-12; `v0.113.0`, `bd93f44`, since 2026-11-11; `v0.112.3`, `8bd8d20`, before;
  refreshed per D-005 #1, D-014, D-038, D-043, D-046, D-049).
- Changes since `v0.114.0` in `proven_base` + `proven_crypto`:
  - `v0.119.2`: fixed-buffer UTF converters reject byte overlap and enforce `out_cap` / overflow bounds (B-046).
  - `v0.119.1`: `proven_chacha_rng_t` AVX2 8-lane reseed carry fix, `proven_float_scaled_round_sig_digits` buffer sizing,
    and barrier wipe in `proven_sys_random.c`.
  - `v0.117.0`: `proven_aes_rng_t` (AES-256-CTR cryptographic RNG).
  - `v0.114.4`: faster `proven_chacha_rng_*` (same output).
- Libraries taken: proven_base (`include/`, `src/proven/`, `platform/`) and proven_crypto (`crypto/`), which since
  v0.113.0 holds SHA-2 (`proven/sha2.h`); FultaArc uses its SHA-256. proven_codec (`codec/`) and proven_net (`net/`)
  are not used and not taken (upstream's migration guide, step 1: no codec or net names in FultaArc's sources).
- Unmodified. Never edit the vendored upstream files in this repository; report defects upstream and resync.
- The whole of both libraries is compiled; a static link takes only what FultaArc calls.
