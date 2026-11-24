**한국어** | [English](README.md) — **FultaArc 매뉴얼**

# FultaArc 개발자 매뉴얼

[`include/fulta/arc.h`](../../include/fulta/arc.h) 공개 API 사용 안내서입니다.

---

## 1. 빌드 및 링크

FultaArc는 `-std=c23 -Wall -Wextra -pedantic -Werror` 옵션으로 빌드됩니다.

| Make 타깃 | 산출물 | 용도 |
|---|---|---|
| `make` / `make all` | `build/host/libfultaarc.a` | 정적 라이브러리(기본 `SAN`에 ASan + UBSan 활성) |
| `make warn` | `build/warn/` | `-Og -DNDEBUG -Werror` 빌드(`-Wmaybe-uninitialized` 점검) |
| `make test` | `build/host/tests/*` | 클린룸 해시 핀 검사, `no-libc-alloc.sh` 및 전체 호스트 테스트 |
| `make bench` | `build/bench/bench` | 새니타이저 없는 `-O2 -DNDEBUG` 최적화 빌드 |
| `make win64` | `build/win64/libfultaarc.a` | Windows x64 UCRT 크로스 빌드(`mingw-w64`) |
| `make deep` | `build/tsan/`, `build/fuzz/` | ThreadSanitizer(`test_threads`) + 120초 libFuzzer 스모크 실행 |
| `make prove` | `build/proofs/` | CBMC 유한 모델 검증 + ASan/UBSan 전수 입력 테스트 |

새니타이저 없는 배포용 정적 라이브러리는 `SAN=""`으로 빌드합니다:

```sh
make SAN="" OPT="-O2 -DNDEBUG"
cc -std=c23 -O2 -Iinclude app.c build/host/libfultaarc.a -lm -lpthread -o app
```

Windows(`mingw-w64`)에서는 `-static -lbcrypt`를 링크합니다.

---

## 2. 아카이브 읽기

### 아카이브 열기

- `fulta_arc_open_file(path, options, &arc)`: 파일 경로로 아카이브를 열며, `options->volume` 콜백이 없으면 같은 디렉터리에서 분할 볼륨을 자동으로 찾습니다.
- `fulta_arc_open_memory(data, size, options, &arc)`: 메모리 버퍼에서 엽니다(`data`는 `fulta_arc_close(arc)`까지 유효해야 합니다).
- `fulta_arc_open(source, options, &arc)`: 사용자 정의 랜덤 액세스 `fulta_arc_source_t`(`read_at`, `close`, `size`)로 엽니다. `arc`가 `source` 소유권을 가져가며 닫을 때나 실패 시 `source->close(source->ctx)`를 호출합니다. `fulta_arc_source_file`, `fulta_arc_source_memory` 생성자도 제공됩니다.

기본값은 `options = NULL`입니다. `fulta_arc_options_t` 주요 필드:
- `name`, `volume`, `volume_ctx`: 첫 볼륨 이름과 다중 볼륨 해석 콜백(`fulta_arc_volume_fn`).
- `password`, `password_ctx`: 암호화 엔트리에 대해 호출되는 `fulta_arc_password_fn(ctx, attempt, &utf8_out)`(포기 시 `FULTA_ARC_ERR_PASSWORD_NEEDED` 반환).
- `codepage`, `auto_codepage`: 비유니코드 레거시 파일명 해석용 코드페이지(`FULTA_ARC_CP_AUTO`, `CP_UTF8`, `CP_437`, `CP_949`, `CP_932`, `CP_936`, `CP_950`, `CP_1252`, `CP_866`, `CP_1251`, `CP_KOI8R`).
- `max_dictionary`(기본 `0` = 1 GiB), `max_memory`(`0` = 무제한): 아카이브 헤더가 요구하는 디코더 버퍼 상한.
- `allocator`: 사용자 정의 `fulta_arc_allocator_t`(`alloc`, `realloc`, `free`, `ctx` — 세 함수 모두 지정하거나 모두 `NULL`로 두어 `proven_c_lib` 힙 사용).
- `cancel`, `cancel_ctx`, `progress`, `progress_ctx`: 폴링 기반 취소 및 누적 디코딩 바이트 진행률 콜백.
- `keep_damaged`: `true`이면 체크섬 불일치나 조기 종료 시에도 디코딩된 바이트를 싱크로 전달하고 `FULTA_ARC_DAMAGED`를 반환합니다.

### 엔트리 조회 및 추출

- `fulta_arc_format(arc)` / `fulta_arc_format_name(fmt)`: 감지된 포맷(`FULTA_ARC_FORMAT_*`).
- `fulta_arc_count(arc)`, `fulta_arc_entry(arc, index)`: 메타데이터(`name`, `name_raw`, `name_raw_size`, `size`, `packed_size`, `mtime`, `crc32`, `attributes`, `unix_mode`, `solid_group`, `flags`, `method`).
- **타임스탬프 규칙**: `mtime`은 `1970-01-01T00:00:00Z` 기준 나노초입니다. `FULTA_ARC_ENTRY_MTIME_LOCAL` 플래그가 켜진 경우(UTC 추가 필드가 없는 ZIP DOS 시간, ALZ, RAR 1.4–4, CAB, EGG, LHA level 0/1) 로컬 시각을 UTC처럼 인코딩한 값이므로 필요 시 생성 측/로컬 시간대로 보정하십시오.
- **스트리밍 추출**: `fulta_arc_extract(arc, index, &sink)`는 `sink->write(ctx, data, n)`로 조각을 출력하고 크기·체크섬을 검증합니다.
- **메모리 추출**: `fulta_arc_extract_alloc(arc, index, &out_data, &out_size)`는 아카이브 할당자로 버퍼를 할당합니다(기본 할당자 사용 시 `fulta_arc_free(out_data)`로 해제, 사용자 할당자 사용 시 해당 `allocator.free`로 해제).
- **빠른 비밀번호 검사**: `fulta_arc_check_password(arc, index)`는 별도 검증 값이 있는 포맷에서 전체 압축 해제 없이 비밀번호를 검증합니다(미지원 시 `FULTA_ARC_ERR_UNSUPPORTED`).

---

## 3. ZIP 및 7z 쓰기와 편집

### 새 아카이브 작성

- `fulta_arc_write(format, entries, count, options, sink)`
- `fulta_arc_write_file(path, format, entries, count, options)`
- `fulta_arc_write_volumes(format, entries, count, options, volumes)`

지원 출력 포맷: `FULTA_ARC_FORMAT_ZIP`, `FULTA_ARC_FORMAT_7Z`.

`fulta_arc_write_options_t` 주요 옵션:
- `level`(`0` 무압축, `1..9` 압축, 범위 외 `6`), `method`(`FULTA_ARC_METHOD_*`), `filter`(`FULTA_ARC_FILTER_*`, 7z 전용), `filter_delta_dist`(`1..256`), `solid`(`FULTA_ARC_SOLID_{DEFAULT,ON,OFF}`, 7z 전용).
- `password`, `encryption`(`FULTA_ARC_ENCRYPT_ZIP_AES{128,192,256}`, `FULTA_ARC_ENCRYPT_7Z_AES256`), `encrypt_names`(7z `-mhe=on` 파일명·헤더 암호화).
- `volume_size`(`>= FULTA_ARC_VOLUME_MIN` = 64 KiB): 분할 `.z01`..`.zip` 또는 `.7z.001`.. 생성(`fulta_arc_volume_name`으로 볼륨명 생성).
- `dos_time`(`FULTA_ARC_DOS_TIME_{LOCAL,OFFSET,UTC}`) 및 `dos_utc_offset`(ZIP DOS 시간 기준).
- `zip64`: 크기와 무관하게 Zip64 레코드 강제 기록.
- `allocator`: 라이터 및 인코더 내부 버퍼용 `fulta_arc_allocator_t`.

### 편집 및 변환

1. `fulta_arc_editable(arc)`로 편집 가능 여부를 확인합니다(`FULTA_ARC_EDIT_OK`면 ZIP 또는 비솔리드 7z 제자리 복사 편집 가능, `SOLID`/`MULTIVOLUME`/`FORMAT_NOT_WRITABLE`은 변환 필요, `FULTA_ARC_EDIT_ENCRYPTED`는 거부).
2. `fulta_arc_edit_begin(arc, &edit)`로 편집 세션을 시작합니다.
3. `fulta_arc_edit_remove`, `fulta_arc_edit_replace`, `fulta_arc_edit_add`(`fulta_arc_reader_t` 풀 입력)를 호출합니다.
4. `fulta_arc_edit_write`, `fulta_arc_edit_write_file`, `fulta_arc_edit_write_volumes`로 기록합니다.
5. `fulta_arc_edit_free(edit)`로 해제합니다.

---

## 4. 보안, 경로 안전성 및 스레딩

- **경로 이탈 및 심볼릭 링크**: `entry->name`은 `/`로 시작하지 않으며 드라이브 문자(`C:`)나 `..` 세그먼트(Windows 점/공백 변형 포함)를 포함하지 않고 제어 바이트(`0x01–0x1F`, `0x7F`)를 `_`로 치환합니다. 심볼릭 링크(`FULTA_ARC_ENTRY_SYMLINK`)를 파일시스템에 생성할 때는 링크 대상이 추출 대상 디렉터리 밖을 가리키지 않는지 호출자 측에서 반드시 검증해야 합니다.
- **콜백 재진입 금지**: `sink`, `progress`, `cancel`, `password` 콜백 내부에서 같은 `fulta_arc_t`에 대해 `fulta_arc_extract`, `fulta_arc_check_password`, `fulta_arc_close`를 호출할 수 없습니다(재진입 시 `FULTA_ARC_ERR_INVALID_ARG` 반환).
- **스레드 안전성**: 하나의 `fulta_arc_t`는 한 번에 한 스레드에서만 사용하며, 서로 다른 `fulta_arc_t` 인스턴스는 스레드 간 독립적으로 병렬 실행할 수 있습니다.

---

## 5. 오류 코드

`fulta_arc_strerror(err)`는 `fulta_arc_err_t`에 대한 정적 영문 설명 문자열을 반환합니다.

| 코드 | 의미 |
|---|---|
| `FULTA_ARC_OK` (`0`) | 성공 |
| `FULTA_ARC_ERR_NOMEM` (`1`) | 메모리 부족 (`PROVEN_ERR_NOMEM`) |
| `FULTA_ARC_ERR_INVALID_ARG` (`4`) | 잘못된 인자 또는 콜백 재진입 (`PROVEN_ERR_INVALID_ARG`) |
| `FULTA_ARC_ERR_IO` (`5`) | 소스 또는 싱크 I/O 오류 (`PROVEN_ERR_IO`) |
| `FULTA_ARC_ERR_NOT_ARCHIVE` (`0x1000`) | 지원하는 아카이브·스트림 시그니처 없음 |
| `FULTA_ARC_ERR_CORRUPT` (`0x1001`) | 컨테이너 구조 또는 압축 데이터 손상 |
| `FULTA_ARC_ERR_TRUNCATED` (`0x1002`) | 입력 조기 종료 |
| `FULTA_ARC_ERR_UNSUPPORTED` (`0x1003`) | 미지원 압축 방식·필터·기능 |
| `FULTA_ARC_ERR_PASSWORD_NEEDED` (`0x1004`) | 암호화 엔트리이나 비밀번호 미제공 |
| `FULTA_ARC_ERR_PASSWORD_WRONG` (`0x1005`) | 비밀번호 검증 실패 |
| `FULTA_ARC_ERR_CHECKSUM` (`0x1006`) | CRC-32 / BLAKE2sp / MAC 불일치 |
| `FULTA_ARC_ERR_VOLUME_MISSING` (`0x1007`) | 필요한 분할 볼륨 누락 |
| `FULTA_ARC_ERR_LIMIT` (`0x1008`) | `max_dictionary` 또는 `max_memory` 한도 초과 |
| `FULTA_ARC_ERR_CANCELLED` (`0x1009`) | `options.cancel`에 의한 취소 |
| `FULTA_ARC_ERR_BAD_NAME` (`0x100A`) | 허용되지 않는 출력 엔트리 이름 |
| `FULTA_ARC_DAMAGED` (`0x100B`) | `keep_damaged = true` 상태에서 손상 데이터 전달됨 |
| `FULTA_ARC_ERR_EXISTS` (`0x100C`) | 분할 쓰기 시 뒤이을 기존 볼륨 파일이 이미 존재함 |

---

## 6. 클린룸 Provenance

클린룸 디코더(`rar/`, `rar15/`, `rar15/enc3/`, `alz/`, `egg/`, `azo/`, `lha/`, `quantum/`)의 포맷·알고리즘·유도 과정·출처 문서는
[`docs/provenance/README.md`](../provenance/README.md)를 참고하십시오.
