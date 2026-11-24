**한국어** | [English](README.md) — **FultaArc**

# FultaArc

> 클린룸 디코더 출처 기록(provenance)과 검증 증거를 함께 공개하는 크로스 플랫폼 MIT C23 아카이브 라이브러리.

[매뉴얼 (KO)](docs/manual/README-ko.md) · [Manual (EN)](docs/manual/README.md) · [클린룸 Provenance](docs/provenance/README.md) · [RAR 1.5 암호화 분석](docs/provenance/rar15/enc3/README.md) · [변경 이력](CHANGELOG.md) · [서드파티 고지](THIRD_PARTY_NOTICES.md) · [라이선스 (MIT)](LICENSE)

**FultaArc**는 [`proven_c_lib`](vendor/proven/VENDORED.md) 기반의 순수 C23 아카이브 해제·생성·편집 라이브러리입니다. 솔리드·분할 볼륨·자동 압축 풀림(SFX)·암호화 아카이브를 포함해 **18개 아카이브 및 압축 스트림 포맷을 해제**하며, **ZIP과 7z 아카이브는 생성·편집·분할 및 AES 암호화까지 지원**합니다.

공개 표준이나 허용적(permissive) SDK에서 직접 가져올 수 없었던 포맷 디코더는 엄격한 **명세 기반(specification-based)** 또는 **블랙박스(black-box)** 클린룸에서 자체 설계·구현했으며, 각 포맷의 규격·단계별 유도 과정·교차 검증 결과·SHA-256 증거 앵커를 MIT 라이선스 소스 코드와 함께 투명하게 공개합니다.

---

## 주요 기능 및 아키텍처 특징

- **광범위한 읽기 지원 (18개 포맷)**:
  - **컨테이너**: ZIP, 7z, RAR(1.4 ~ 7.0), ALZ, EGG, LHA/LZH(LArc `.lzs`, PMarc `.pma` 포함), CAB, TAR, cpio, Unix `ar`, ISO 9660(Rock Ridge, Joliet, zisofs), UDF.
  - **압축 스트림 및 래퍼**: `gzip`, `bzip2`, `xz`, `zstd`, `lzip`, Unix `compress`(`.Z`) 및 `.tar.{gz,bz2,xz,zst,lz,Z}` 자동 해제.
  - **암호화·솔리드·분할 볼륨**: WinZip AES-128/192/256, PKWARE Strong Encryption(AES), 7zAES-256(헤더 암호화 `-mhe` 포함), ZipCrypto, RAR 1.4 암호, RAR 1.5 XOR 스트림 암호, RAR 3 AES-128, RAR 5 AES-256, LEA-128/256 CTR(EGG) 지원.
- **ZIP 및 7z 생성, 분할 볼륨, 무손실 편집**:
  - ZIP(`Store`, `Deflate`, `BZip2`, `LZMA`, `PPMd var.I`, `Zstandard`; Zip64)과 7z(`Copy`, `LZMA`, `LZMA2`, `Deflate`, `BZip2`, `PPMd var.H`) 생성, 7z 콘텐츠 필터(`Delta`, `BCJ`, `BCJ2`, `ARM`, `ARM64`, `ARMT`, `PPC`, `SPARC`, `IA64`, `RISC-V`) 및 솔리드 그룹 제어(`-ms=on` / `-ms=off`) 지원.
  - WinZip AES-128/192/256 및 7zAES-256(`encrypt_names`) 암호화 출력(쓰기 시 취약한 레거시 ZipCrypto 미사용).
  - `.z01`..`.zip` 및 `.7z.001`.. 분할 볼륨 쓰기 및 사용자 정의 볼륨 싱크 지원.
  - **편집(`fulta_arc_edit_*`)**: 편집 가능한 ZIP 및 비솔리드 7z에서는 변경되지 않은 엔트리의 압축 데이터를 **재압축 없이 바이트 단위로 복사**하고, 솔리드·분할·기타 포맷은 스트리밍 변환 경로를 제공합니다(암호화된 원본 아카이브 편집은 정책상 `FULTA_ARC_EDIT_ENCRYPTED`로 거부).
- **숨겨진 디스크 I/O 없는 추상 입출력 모델**:
  - 코어 API(`fulta_arc_open`, `fulta_arc_extract`, `fulta_arc_write`)는 임의 접근 `fulta_arc_source_t`와 스트리밍 `fulta_arc_sink_t` / `fulta_arc_write_sink_t` 콜백만을 통해 동작하며, `*_file` 편의 함수 외에는 직접 파일 I/O를 수행하지 않습니다.
- **메모리 안전성 및 악의적 입력 방어**:
  - **`src/` 내 직접 libc 할당 금지**: 모든 내부 메모리 할당은 `proven_allocator_t`를 거치며([`tests/tools/no-libc-alloc.sh`](tests/tools/no-libc-alloc.sh)로 강제), 호출자가 `options.allocator` / `write_options.allocator`로 사용자 정의 할당자를 주입할 수 있습니다.
  - **사전·메모리 상한**: `max_dictionary`(기본 1 GiB)와 `max_memory`로 악의적 헤더에 의한 과도한 코덱 버퍼 할당을 사전 차단합니다(`FULTA_ARC_ERR_LIMIT`).
  - **경로·제어 문자 정제(Sanitization)**: 엔트리 이름을 UTF-8로 정규화하고(CP437, CP949, CP932, CP936, CP950, CP1252, CP866, CP1251, KOI8-R 지원), 절대 경로 접두사·Windows 드라이브 문자·`..` 경로(Windows 공백/점 변형 포함)·ASCII 제어 문자(`0x01–0x1F`, `0x7F` -> `_`)를 제거하면서 원본 바이트는 `name_raw`에 보존합니다.
  - **지속적 검증 체계**: `-std=c23 -Wall -Wextra -pedantic -Werror` 및 `-Og -Werror`, ASan + UBSan(`make test`), ThreadSanitizer + libFuzzer(`make deep`), CBMC 유한 모델 검증(`make prove`), 클린룸 SHA-256 핀 검증(`tests/tools/rar15-pin.sh`), Windows x64 UCRT 크로스 빌드(`make win64`)를 통과합니다.

---

## 빌드 및 실제 C23 코드 사용법

`-std=c23`을 지원하는 GCC 또는 Clang, GNU Make, 표준 POSIX 도구가 필요합니다(Windows 크로스 컴파일 시 `mingw-w64` 선택).

```sh
make            # build/host/libfultaarc.a 빌드 (-std=c23 -Wall -Wextra -pedantic -Werror)
make test       # -Og -Werror 검사, 클린룸 핀·할당자 검사 및 전체 ASan+UBSan 테스트 실행
make win64      # 선택: Windows x64 UCRT용 build/win64/libfultaarc.a 및 테스트 .exe 빌드
make SAN="" OPT="-O2 -DNDEBUG"   # 배포용 비-새니타이저 정적 라이브러리 빌드
```

Linux에서는 `build/host/libfultaarc.a`와 함께 `-Iinclude -lm -lpthread`를 링크하고, Windows에서는 `-lbcrypt`를 링크합니다. 전체 API 레퍼런스는 [docs/manual/README-ko.md](docs/manual/README-ko.md)를 참고하십시오.

### 1. 암호화·리소스 제한이 적용된 아카이브 열기 및 스트리밍 해제

```c
#include "fulta/arc.h"
#include <stdio.h>

static fulta_arc_err_t provide_password(void *ctx, uint32_t attempt, const char **utf8_out) {
    if (attempt > 0) return FULTA_ARC_ERR_PASSWORD_NEEDED;
    *utf8_out = (const char *)ctx;
    return FULTA_ARC_OK;
}

static fulta_arc_err_t write_to_file(void *ctx, const void *data, size_t n) {
    FILE *fp = (FILE *)ctx;
    return (fwrite(data, 1, n, fp) == n) ? FULTA_ARC_OK : FULTA_ARC_ERR_IO;
}

int main(void) {
    fulta_arc_options_t opts = {
        .password       = provide_password,
        .password_ctx   = "secret-password",
        .max_dictionary = 128u * 1024u * 1024u, /* 사전 크기 상한 128 MiB */
        .max_memory     = 256u * 1024u * 1024u, /* 코덱 누적 메모리 상한 256 MiB */
    };

    fulta_arc_t *arc = NULL;
    fulta_arc_err_t err = fulta_arc_open_file("bundle.rar", &opts, &arc);
    if (err != FULTA_ARC_OK) {
        fprintf(stderr, "open failed: %s\n", fulta_arc_strerror(err));
        return 1;
    }

    printf("Format: %s (%zu entries)\n",
           fulta_arc_format_name(fulta_arc_format(arc)), fulta_arc_count(arc));

    for (size_t i = 0; i < fulta_arc_count(arc); i++) {
        const fulta_arc_entry_t *en = fulta_arc_entry(arc, i);
        printf("  [%zu] %s (%llu bytes, %s)\n",
               i, en->name, (unsigned long long)en->size, en->method);
        if (en->flags & FULTA_ARC_ENTRY_DIR) continue;

        FILE *out = fopen("extracted.bin", "wb");
        if (!out) { fulta_arc_close(arc); return 1; }
        fulta_arc_sink_t sink = { .ctx = out, .write = write_to_file };
        err = fulta_arc_extract(arc, i, &sink);
        fclose(out);
        if (err != FULTA_ARC_OK) {
            fprintf(stderr, "extract[%zu] failed: %s\n", i, fulta_arc_strerror(err));
            fulta_arc_close(arc);
            return 1;
        }
    }

    fulta_arc_close(arc);
    return 0;
}
```

### 2. 헤더 암호화가 적용된 7z(또는 ZIP) 아카이브 생성

```c
#include "fulta/arc.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *msg = "FultaArc clean-room C23 archive library\n";
    fulta_arc_write_entry_t entries[] = {
        { .name = "docs/", .is_dir = 1 },
        { .name = "docs/hello.txt", .data = msg, .size = strlen(msg), .mtime = 1764000000 },
    };
    fulta_arc_write_options_t wopts = {
        .level         = 7,
        .method        = FULTA_ARC_METHOD_LZMA2,
        .filter        = FULTA_ARC_FILTER_NONE,
        .solid         = FULTA_ARC_SOLID_ON,
        .password      = "correct-horse-battery",
        .encryption    = FULTA_ARC_ENCRYPT_7Z_AES256,
        .encrypt_names = 1, /* 7z -mhe: 파일 이름 및 메타데이터까지 암호화 */
    };

    fulta_arc_err_t err = fulta_arc_write_file("out.7z", FULTA_ARC_FORMAT_7Z, entries, 2, &wopts);
    if (err != FULTA_ARC_OK) {
        fprintf(stderr, "write failed: %s\n", fulta_arc_strerror(err));
        return 1;
    }
    return 0;
}
```

### 3. 기존 ZIP / 7z 아카이브 편집 (재압축 없는 복사 또는 변환)

```c
#include "fulta/arc.h"
#include <stdio.h>
#include <string.h>

typedef struct { const uint8_t *buf; size_t len; size_t pos; } mem_reader_t;

static fulta_arc_err_t read_chunk(void *ctx, void *out, size_t cap, size_t *got) {
    mem_reader_t *r = (mem_reader_t *)ctx;
    size_t rem = r->len - r->pos;
    size_t n = rem < cap ? rem : cap;
    if (n > 0) { memcpy(out, r->buf + r->pos, n); r->pos += n; }
    *got = n;
    return FULTA_ARC_OK;
}

int main(void) {
    fulta_arc_t *arc = NULL;
    if (fulta_arc_open_file("input.zip", NULL, &arc) != FULTA_ARC_OK) return 1;

    if (fulta_arc_editable(arc) == FULTA_ARC_EDIT_ENCRYPTED) {
        fulta_arc_close(arc);
        return 1; /* 암호화된 아카이브는 정책상 편집 거부 */
    }

    fulta_arc_edit_t *edit = NULL;
    if (fulta_arc_edit_begin(arc, &edit) != FULTA_ARC_OK) {
        fulta_arc_close(arc);
        return 1;
    }

    if (fulta_arc_count(arc) > 0) fulta_arc_edit_remove(edit, 0);

    const uint8_t note[] = "updated entry\n";
    mem_reader_t src = { .buf = note, .len = sizeof(note) - 1, .pos = 0 };
    fulta_arc_reader_t reader = { .ctx = &src, .read = read_chunk, .size = src.len };
    fulta_arc_edit_add(edit, "notes/readme.txt", &reader);

    /* 원본과 동일 포맷 출력 시 변경 없는 엔트리는 재압축 없이 그대로 복사 */
    fulta_arc_err_t err = fulta_arc_edit_write_file(edit, FULTA_ARC_FORMAT_ZIP, "edited.zip", NULL);
    fulta_arc_edit_free(edit);
    fulta_arc_close(arc);
    return (err == FULTA_ARC_OK) ? 0 : 1;
}
```

---

## 지원 포맷 목록 및 구현 경로

FultaArc의 모든 포맷 지원은 **공개 표준·허용적 SDK**, **명세 기반 클린룸**, **블랙박스 클린룸**의 세 가지 투명한 경로 중 하나로 확보되었습니다.

| 포맷 | 지원 기능 | 압축 방식 / 필터 / 암호화 | 획득 경로 및 출처 문서 |
|---|---|---|---|
| **ZIP** | 읽기, 쓰기, 편집, 분할 | Store, Deflate(64), BZip2, LZMA, xz, PPMd var.I, Zstandard, Shrink, Reduce, Implode; ZipCrypto, WinZip AES-128/192/256, PKWARE Strong AES; Zip64, `.z01`..`.zip` | PKWARE APPNOTE + RFC 1951 + RFC 8878 + PKZIP 0.90–1.10 블랙박스 검증 + 허용적 SDK(`miniz`, `bzip2`, `LZMA SDK`, `zstd`) |
| **7z** | 읽기, 쓰기, 편집, 분할 | Copy, LZMA, LZMA2, Deflate, BZip2, PPMd var.H, Zstandard; BCJ, BCJ2, Delta, ARM/ARM64/ARMT, PPC, SPARC, IA64, RISC-V; 7zAES-256(`-mhe`), 솔리드, `.7z.001`.. | 퍼블릭 도메인 LZMA SDK / 7z 포맷 규격 + 허용적 SDK(`LZMA SDK`, `miniz`, `bzip2`, `zstd`) |
| **RAR 2.0 – 7.0** | 읽기 | RAR 4 & 5 컨테이너, RAR 2 LZ/Audio, RAR 3 LZ/PPMd var.H/필터, RAR 5/7 LZ; RAR 3 AES-128, RAR 5 AES-256, BLAKE2sp; 솔리드, 분할, SFX | **2팀 명세 기반 클린룸**(`rar-spec` -> `rar-decoder`; UnRAR 코드 0줄) — [`docs/provenance/rar/`](docs/provenance/rar/README.md) |
| **RAR 1.4 / 1.5** | 읽기 | Store(`0x30`) & LZSS/Huffman(`0x31..0x35`); RAR 1.4 암호, RAR 1.5 XOR 스트림 암호; 솔리드, 분할(`.rar`/`.r00`), SFX | **블랙박스 클린룸**(`fultaarc-cleanroom-rar15` + `fultaarc-cleanroom-rar15-enc3`) — [`rar15/`](docs/provenance/rar15/README.md) · [`enc3/`](docs/provenance/rar15/enc3/README.md) |
| **ALZ** | 읽기 | Store, raw Deflate, ALZ-bzip2(`DLZ\x01`); ZipCrypto; `.alz`/`.a00`.. 분할 볼륨 | **제로부터 출발한 블랙박스 클린룸**(`fultaarc-cleanroom-alzegg`; `unalz` 코드 미사용) + `bzip2 1.0.8` — [`docs/provenance/alz/`](docs/provenance/alz/README.md) |
| **EGG** | 읽기 | Store, Deflate, bzip2, AZO, LZMA; ZipCrypto, WinZip AES, LEA-128/256; 솔리드, `.volN.egg` 분할, SFX | **상호 격리된 2개의 독립 블랙박스 클린룸**(`egg-container` + `egg-verify` + `alzegg`) — [`docs/provenance/egg/`](docs/provenance/egg/README.md) |
| **AZO (EGG method 3)** | 읽기 (해제 전용) | `31 <flag>` + x86 `E8`/`E9` 변환, 32비트 이진 산술 부호기, 이중(local/group) 트리, 128항목 MTF 히스토리 | **3라운드 블랙박스 클린룸**(`azo-blackbox` + `alzegg`; 특허 KR101049699B1 존중을 위한 해제 전용) — [`docs/provenance/azo/`](docs/provenance/azo/README.md) |
| **LHA / LZH** | 읽기 | `-lh0..7-`, `-lhx-`, `-lhd-`, `-lk7-`, `-lz4/5-`, `-lzs-`, `-pm0/1/2-`; LArc, PMarc, SFX | **명세 + 블랙박스 클린룸**(퍼블릭 도메인 `ar002` + `lhasa`/`UNLHA32`/`PMarc` 오라클) — [`docs/provenance/lha/`](docs/provenance/lha/README.md) |
| **CAB** | 읽기 | Store, MSZIP, LZX, **Quantum**; 다중 캐비닛 세트 | `[MS-CAB]` / `[MS-PATCH]` 공개 규격 + Quantum **블랙박스 클린룸** — [`docs/provenance/quantum/`](docs/provenance/quantum/README.md) |
| **TAR / cpio / ar** | 읽기 | ustar, POSIX pax, GNU tar; `newc`/`crc`/`odc`/old-binary cpio; Unix `ar`(`.a`/`.deb`) | POSIX.1, BSD `tar(5)`/`cpio(5)`/`ar(5)` 매뉴얼 |
| **ISO 9660 & UDF** | 읽기 | ISO 9660(Rock Ridge, Joliet, zisofs), UDF(ECMA-167 / OSTA) | ECMA-119, Microsoft Joliet, IEEE P1282/P1281, ECMA-167 |
| **스트림 필터** | 읽기 | `gzip`, `bzip2`, `xz`, `zstd`, `lzip`, `compress`(`.Z`) | RFC 1952, `bzip2` 문서, `.xz`/`.lz` 공개 규격, RFC 8878, 특허 만료 LZW 기술 |

- FultaArc는 CLI 압축 도구가 아닌 라이브러리이며, 쓰기·편집은 ZIP과 7z만 지원합니다(RAR, ALZ, EGG, AZO 인코딩 미지원).
- RAR 3의 임의 VM 바이트코드 프로그램은 실행하지 않으며(`FULTA_ARC_ERR_UNSUPPORTED`), 표준 사전 정의 필터만 지원합니다.

---

## RAR, ALZ, EGG, AZO 해제 기능 획득 과정 및 증빙 자료

자체 분석 및 클린룸을 통해 구현한 핵심 포맷들의 전체 규격, 단계별 실험·유도 문서, 오라클 교차 검증 결과, SHA-256 증거 앵커는 [`docs/provenance/`](docs/provenance/README.md)에 공개되어 있습니다.

1. **RAR 2.0 – 7.0 ([`docs/provenance/rar/`](docs/provenance/rar/README.md))** — *2팀 명세 기반 클린룸*:
   - 1팀(`rar-spec`)은 BSD-2 라이선스의 **`libarchive` 3.7.7** 및 **`nwaples/rardecode`**, RARLAB이 공개한 RAR 5.0 기술 노트, 공개 암호 표준만을 참고해 UnRAR 코드 없이 독립 명세서(`rar-decompression.md`)를 작성했습니다.
   - 2팀(`rar-decoder`)은 UnRAR·`libarchive`·`rardecode`·7-Zip 소스 일체에 접근할 수 없는 격리 저장소에서 오직 명세서와 FIPS/RFC/BLAKE2 표준 및 퍼블릭 도메인 LZMA SDK `Ppmd7*`만을 바탕으로 MIT C 디코더를 구현했습니다. 3종의 블랙박스 오라클 매니페스트(47/47, 48/48, 47/47)와 FultaArc 새니타이저/퍼징 테스트로 검증했습니다.
2. **RAR 1.4 / 1.5 및 RAR 1.5 암호화 ([`rar15/`](docs/provenance/rar15/README.md) · [`rar15/enc3/`](docs/provenance/rar15/enc3/README.md))** — *블랙박스 + SMT 클린룸*:
   - 컨테이너 및 스트림 디코더(`src/rar/rar15/`)는 DOSBox에서 실행한 RAR 1.55 / 1.40.2 beta의 출력 관찰만으로 도출했으며 `tests/tools/rar15-pin.sh`로 원본 바이트 일치 검증을 유지합니다.
   - **RAR 1.5 암호 스트림 암호기**(`src/rar/rar15_enc/rar15_cipher.c`)는 별도 클린룸(`fultaarc-cleanroom-rar15-enc3`, `2c3c4a4`)에서 독립 도출되어 6/6 Universal Decryption Gate(비밀번호 600개, 파일 1,800개)와 실제 `RAR.EXE` 1.55 대조 검증(솔리드, 64 KiB 윈도우 랩핑 5분할 볼륨, 되감기, 하위 디렉토리, 일반+암호 혼합, 80자 비밀번호 절단)을 100% 통과했습니다.
   - [`docs/provenance/rar15/enc3/`](docs/provenance/rar15/enc3/README.md)에서 국문·영문 수학적 연구 보고서, 규격서, 오프라인 대화형 HTML 단계별 가이드를 바로 열람할 수 있으며, 전체 재현 증거 번들(`fultaarc-cleanroom-rar15-enc3-release-en.zip` / `-ko.zip`)은 GitHub Release 에셋으로 제공됩니다.
3. **ALZ ([`docs/provenance/alz/`](docs/provenance/alz/README.md))** — *블랙박스 클린룸 (`fultaarc-cleanroom-alzegg`)*:
   - 과거 탐색 단계에서 zlib 라이선스 `unalz 0.65`를 참고했던 초기 파서를 완전히 폐기하고, `fultaarc-cleanroom-alzegg`(`1f844a6`)에서 ALZip 4.9–12.37 출력만을 관찰해 ALZ 컨테이너와 178비트가 절감된 `DLZ\x01`/`DLZ\x02` ALZ-bzip2 프레이밍을 처음부터 독자 도출해 `src/alz/alz.c`로 교체했습니다.
   - `unalz 0.65`와의 대조 검증 결과 Store/Deflate 72개 아카이브에서 완전 일치, 빈 폴더 누락 버그 수정, 그리고 **`unalz 0.65`가 실패하는 ALZ method-1 bzip2 아카이브까지 FultaArc는 정확히 해제**함을 입증했습니다.
4. **EGG ([`docs/provenance/egg/`](docs/provenance/egg/README.md))** — *상호 격리된 2개의 독립 블랙박스 클린룸*:
   - `egg-container`(145개 아카이브)와 그 결과물을 일체 볼 수 없도록 방화벽을 둔 제2의 클린룸 `egg-verify`(351개 아카이브, 21단계 로그)가 ALZip 8.6/8.12/12.37 출력만으로 모든 시그니처·서브레코드·솔리드/분할·ZipCrypto·WinZip AES(16바이트 LE 카운터, 시작값 1)·LEA-128/256(16바이트 BE 카운터, 시작값 0, KISA KS X 3246)·CP949 비밀번호 규칙을 독립 도출했습니다.
   - `egg-container` 작업 중 발생한 미사용 `Egg.dll` 문자열 노출 1건은 자진 보고·즉시 격리 폐기되었으며, 외부 프로그램 개봉/문자열 추출이 원천 금지된 `egg-verify`의 독자 재도출 및 `alzegg` 검증(601 + 218 아카이브 통과)으로 완전히 무력화되었음을 [`docs/provenance/egg/provenance.md`](docs/provenance/egg/provenance.md)에 투명하게 기록했습니다.
5. **AZO ([`docs/provenance/azo/`](docs/provenance/azo/README.md))** — *3라운드 블랙박스 클린룸 (해제 전용)*:
   - 이스트소프트의 국내 특허 **KR101049699B1**(존속기간 2029-07-17까지)을 존중하여 **오직 해제(디코딩)만 구현**하며 압축 인코더나 인코딩 명세를 작성하지 않았습니다.
   - `azo-blackbox`(22커밋, E40–E77, 244/249 코드값 이분 탐색, 동결 홀드아웃 E76–E77 6/6, 총 18,809/18,809쌍 바이트 일치)와 `alzegg` 바이너리 코퍼스 보정(`31 <flag>` x86 `E8`/`E9` 역변환, `2^20`/`2^22` 정밀 점수 스케일링)으로 도출했으며, 관찰 검증용 배경 가설로 허용된 공개 자료(`namu.wiki/w/AZO` 개요 및 퍼블릭 도메인 LZMA SDK 명세)를 기록에 정직하게 명시했습니다.

---

## 왜 FultaArc를 MIT 라이선스로 정당하게 공개할 수 있는가

> **참고:** 아래 내용은 법률 자문이 아닌 검증 가능한 엔지니어링·라이선스 기록 요약입니다.

1. **자체 작성 코드는 100% 오리지널 MIT C23 코드**: `include/`, `src/`, `tests/`의 모든 FultaArc 코드에 GPL·LGPL·AGPL·독점 소스 코드 또는 UnRAR 제한 조항이 적용된 코드가 전혀 포함되어 있지 않습니다. 클린룸 검증 과정에서 실행한 외부 도구(DOSBox, 공유/프리웨어 아카이버 등)는 오직 입출력 관찰 도구로만 사용되었으며 해당 바이너리·코드·테이블은 단 한 바이트도 링크·복사·재배포되지 않습니다.
2. **모든 벤더링 의존성은 MIT와 양립 가능한 허용적 라이선스만 사용** ([`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)):
   - `proven_c_lib` v0.119.3 — **MIT**
   - `LZMA SDK` 26.05 — **Public Domain**
   - `miniz` 3.1.2 — **MIT**
   - `bzip2` 1.0.8 (`vendor/bzip2_alz/` 포함) — **bzip2 라이선스** (허용적 BSD 계열)
   - `Zstandard` 1.5.7 디코더 (`vendor/zstd/`) — **BSD-3-Clause** (상위 프로젝트의 BSD/GPLv2 이중 라이선스 중 BSD 조항만 선택)
   - 유니코드 코드페이지 매핑 데이터 (`src/text/cp_tables.c`) — **Unicode License v3**
3. **제한적·카피레프트 디코더 코드 0%**: UnRAR(재배포 및 압축 알고리즘 재구현 금지 조항 포함), 7-Zip의 RAR/LZH/Quantum 코드, `libmspack`/`cabextract`, `unalz`, `UnEGG`, `EGG_Specification.pdf`, `EggDotNet`, GNU tar 코드를 일체 포함하지 않습니다.
4. **격리된 클린룸 절차 및 SHA-256 검증 앵커**: 비공개·비표준 포맷 디코더마다 독립 저장소 커밋 이력, git 번들 SHA-256 앵커, 바이트 핀 검사(`tests/tools/rar15-pin.sh`) 및 한계·예외 사항의 정직한 공개 기록이 동반됩니다.
5. **특허 존중 (해제 전용 경계)**: 유효 특허가 존재하는 AZO(`KR101049699B1`)는 해제 기능만 제공하며, Unix `compress`(LZW) 등 표준 코덱은 특허가 만료된 공개 알고리즘만 사용합니다.

---

## 저장소 및 라이선스

- **GitHub 저장소**: <https://github.com/rubidus-api/fultaarc_c_lib>
- **라이선스**: [MIT](LICENSE)
