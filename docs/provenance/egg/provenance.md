# EGG — provenance

This page records where FultaArc's EGG reader came from, discloses a self-reported boundary incident during the
first exploratory clean room and explains how a second firewalled clean room neutralized it, and lists the SHA-256
evidence anchors.

This is an engineering record, not legal advice. Where a step depended on a licence judgement, the record identifies who
made it; it does not state a legal conclusion.

---

## 1. Statement

FultaArc's EGG reader (`src/egg/egg.c` + cryptographic primitives in `src/crypto/`) is **MIT-licensed C23 code** written
by FultaArc contributors from specifications derived solely by black-box observation of ALZip outputs.

- **Zero third-party EGG code or docs read:** No ESTsoft source code (`UnEGG`), no `EGG_Specification.pdf`, no
  `EggDotNet` code, and no ClamAV EGG code was read or used.

---

## 2. Disclosed exposure in `egg-container` and neutralization by `egg-verify`

In the spirit of complete engineering transparency (and recorded in `ops/DECISIONS.md` D-050):

- **What happened (`egg-container`, 2026-10-01):** While searching for which ALZip installer version still emitted AZO, the
  `egg-container` session unpacked an ALZip installer and searched its binaries for `"AZO"`, encountering UTF-16 method
  and cipher name strings inside ALZip's `Formats/Egg.dll` roughly two hours *after* the room had already committed its
  method/cipher ID table from black-box archive bytes.
- **Immediate containment:** The session self-disclosed the incident in its log, recorded *"Nothing from it is used"*,
  deleted all unpacked installer files, and escalated to the owner.
- **Complete neutralization (`egg-verify`):** Rather than relying on `egg-container` alone, FultaArc launched a completely
  separate, firewalled clean room (**`egg-verify`**) whose brief forbade reading *anything* from `egg-container` and
  explicitly prohibited unpacking, listing strings of, or inspecting any ESTsoft executable (*"Run them; read only the
  archives they write"*). `egg-verify` independently re-derived every signature, structure, method number, and cipher
  rule from ALZip outputs alone, and `fultaarc-cleanroom-alzegg` independently verified the reader against 253 ALZip
  fixtures.
- **Formal disposition:** The predecessor clean-room audit confirmed full containment, and `egg.c` rests on the
  uncontaminated `egg-verify` + `alzegg` lineage.

---

## 3. What FultaArc contains

| Path | Role | Origin | License |
|---|---|---|---|
| `src/egg/egg.c` | EGG container, SFX scan, solid/split framing, cipher dispatch | FultaArc contributors from `egg-verify` + `alzegg` specs | MIT |
| `src/crypto/*` | ZipCrypto, AES-128/256 CTR, LEA-128/256 CTR, PBKDF2-HMAC-SHA1 | FultaArc contributors from PKWARE/FIPS 197/RFC 2104/RFC 8018/KISA KS X 3246 | MIT |
| `vendor/miniz/`, `vendor/bzip2/`, `vendor/lzma/` | Deflate, bzip2, LZMA decoders | Permissive vendored SDKs (`THIRD_PARTY_NOTICES.md`) | MIT / bzip2 / Public Domain |

---

## 4. Sources used vs. sources kept out

| Category | Details | Status |
|---|---|---|
| **Black-box instruments** | ALZip 8.6, 8.12, 12.37 run unmodified to write test archives | Outputs observed only; tools excluded from repo |
| **Public cryptography specs** | FIPS 197 (AES), FIPS 180-4 / RFC 2104 / RFC 8018 (HMAC-SHA1, PBKDF2), KISA KS X 3246 / ISO 29192-2 (LEA) | Public standards |
| **Excluded code & specs** | ESTsoft `UnEGG`, `EGG_Specification.pdf`, `EggDotNet`, ClamAV EGG decoder | Excluded across all clean rooms and FultaArc |

---

## 5. Verifiable evidence anchors (private archive)

```text
844cb71413e5eb7fed777229db9bedeef879428fc3ad31b8ff988a462d968db2  egg-container.bundle
6e9d9fb0a257c652af44157c8e204e7183dbaf15cf63c46abb9a9f8ec9d6fe4f  egg-verify.bundle
c391f57e8c904f0e6121bddbe6abf9e7f1ea7b2820e162148da3b7e923de7c00  egg-verify-02ef44f.bundle
f432402877a9ecbbe99291c70573735bdaa051c4a38e950e37da4d328d9fe952  alzegg.bundle
```
