# RAR 1.5 암호화 아카이브 복호화 및 압축 해제 전체 알고리즘 규격서

- **구현 대상**: `tools/libcleanroom/` (독립 패키지 `02-mit-library/`, `SPDX-License-Identifier: MIT`, C99 `-Wall -Wextra -Werror -pedantic -std=c99`)
- **적용 범위**: RAR 1.5 비솔리드(non-solid) 암호화·비암호화 아카이브, 저장(`0x30`) 및 압축 방식 `0x31..0x35`
- **보안 경계**: `SPLIT_BEFORE`, `SPLIT_AFTER`, `SOLID`, `LARGE`, 디렉터리(`0x00E0`)는 Fail-Closed 거부, 경로 탈출(Zip-Slip) 차단, 외부 심볼 임포트 `0`개

---

## 1. 종단간(End-to-End) 처리 파이프라인

RAR 1.5 아카이브는 파일 엔트리별로 독립적인 `PACK_SIZE` 바이트 영역을 가지며, **항상 (1) 컨테이너 파싱 및 보안 검사 -> (2) 스트림 복호화 -> (3) 압축 해제 -> (4) 평문 CRC-32 무결성 검증** 순서로 처리한다. (즉, *compress-then-encrypt* 구조이므로 복호화는 반드시 압축 해제 전에 수행된다.)

```text
Archive Bytes
  └─> [1] rar15_list()          : "Rar!\x1a\x07\x00" 시그니처 + 0x73/0x74 블록 경계 검증
        └─> [2] Security Gate   : flags(SPLIT/SOLID/LARGE/DIR) 차단, method(0x30..0x35), Zip-Slip 검사
              └─> [3] Decryption (if flags & 0x0004):
                    rar15_keystream(pw, ks, PACK_SIZE) -> rar15_decrypt_block(data ^= ks)
                    └─> [4] Decompression:
                          rar15_unpack(0x30 memcpy | 0x31..0x35 u15_init + u15_unpack LZSS/Huffman)
                          └─> [5] rar15_crc32(plain, UNP_SIZE) == FILE_CRC -> write_file()
```

---

## 2. 컨테이너 파싱 및 보안 검증 (`rar15_container.c`, `rarextract.c`)

1. **아카이브 시그니처**: 첫 `7`바이트는 반드시 리틀엔디언 매직 바이트 `52 61 72 21 1a 07 00` (`"Rar!\x1a\x07\x00"`)여야 한다.
2. **제네릭 블록 헤더 (`7`바이트)**:
   - `0..1`: `HEAD_CRC` (`uint16_t`), `2`: `HEAD_TYPE` (`0x73` 메인 헤더, `0x74` 파일 헤더), `3..4`: `HEAD_FLAGS` (`uint16_t`), `5..6`: `HEAD_SIZE` (`uint16_t`).
   - `HEAD_SIZE < 7`이거나 `HEAD_SIZE > arc_len - p`이면 즉시 `RAR15_ERR_FORMAT` 반환.
3. **4바이트 `ADD_SIZE` 안전 읽기 및 오버플로 차단**:
   - `type == 0x74` 또는 `(flags & 0x8000) != 0`인 블록에만 offset `+7`에 32비트 `ADD_SIZE`(= 파일 헤더의 `PACK_SIZE`)가 존재한다.
   - 반드시 `HEAD_SIZE >= 11`을 먼저 검증하여 `7..10` 바이트 기형 헤더에서의 4바이트 OOB 읽기를 원천 차단하며, `rem_after_hdr = arc_len - p - HEAD_SIZE` 대비 `add_size <= rem_after_hdr`를 비교해 32/64비트 wraparound 오버플로를 방지한다.
4. **파일 헤더 (`0x74`, 최소 `32`바이트)**:

| Offset | Size | Field | 의미 |
|---:|---:|---|---|
| `0..6` | `7` | Generic | `HEAD_CRC`, `HEAD_TYPE=0x74`, `HEAD_FLAGS`, `HEAD_SIZE` |
| `7..10` | `4` | `PACK_SIZE` | 디스크상 페이로드 크기 (암호화·압축 후 바이트 수) |
| `11..14` | `4` | `UNP_SIZE` | 복원 후 평문 크기 |
| `15` | `1` | `HOST_OS` | 생성 OS 식별자 |
| `16..19` | `4` | `FILE_CRC` | 완전 복원 평문의 IEEE 802.3 CRC-32 |
| `20..23` | `4` | `FTIME` | DOS 타임스탬프 |
| `24` | `1` | `UNP_VER` | 최소 해제 버전 |
| `25` | `1` | `METHOD` | `0x30` (Store), `0x31..0x35` (LZSS+Huffman) |
| `26..27` | `2` | `NAME_SIZE` | 파일명 길이 (`name_size <= head_size - 32` 필수 검증) |
| `28..31` | `4` | `ATTR` | 파일 속성 |
| `32..` | `NAME_SIZE` | `NAME` | 엔트리 상대 경로 |

5. **보안 게이트 및 미지원 플래그 Fail-Closed**:
   - `flags & (0x0001 SPLIT_BEFORE | 0x0002 SPLIT_AFTER | 0x0010 SOLID | 0x0100 LARGE)` 또는 `(flags & 0x00E0) == 0x00E0` (디렉터리) 발견 시 오류를 반환하고 건너뛴다.
   - `is_safe_rel_path(NAME)`로 빈 문자열, 선행 `/`/`\`, 백슬래시 `\`, 콜론 `:`, 제어문자(`< 0x20`), 경로 세그먼트 `.`/`..` 및 `//`을 전부 거부하여 Zip-Slip 디렉터리 탈출을 차단한다.

---

## 3. 스트림 암호 복호화 (`rar15_cipher.c`)

파일 헤더에 `RAR15_LHD_PASSWORD (0x0004)`가 설정되어 있으면 `PACK_SIZE` 바이트의 페이로드를 **엔트리마다 바이트 인덱스 `0`부터 새로 생성한 키스트림**과 XOR하여 복호화한다.

1. **표준 CRC-32 테이블 ($T_{\text{ref}}$)**: IEEE 802.3 반사 다항식 $\texttt{0xEDB88320}$에서 정의대로 스택 지역에 생성한다(전역 가변 상태 없음).
2. **키 스케줄 (유효 접두어 $e = \text{pw}[:77]$, 무부호 바이트 $c \in \{0..255\}$)**:

$$R = \texttt{0xFFFFFFFF}, \qquad H = 0, \qquad S_0 = \texttt{0xFFFF}$$

$$R \leftarrow (R \gg 8) \oplus T_{\text{ref}}[(R \oplus c) \mathbin{\&} \texttt{0xFF}], \quad H \leftarrow (H + (T_{\text{ref}}[c] \gg 16) + c) \bmod 2^{16}, \quad S_0 \leftarrow S_0 \oplus ((T_{\text{ref}}[c] \mathbin{\&} \texttt{0xFFFF}) \oplus c)$$

3. **5-워드 상태 초기화 및 매 바이트 갱신 ($i = 0, \dots, \text{PACK\_SIZE}-1$)**:
   - 초기값: $W = R \mathbin{\&} \texttt{0xFFFF}$, $M = \text{rol}_{16}((R \gg 16) \mathbin{\&} \texttt{0xFFFF}, 1)$, $K_2 = \texttt{0xFFFF}$, $H_r = H$, $S = S_0$.
   - 스텝 갱신:

$$W' = (W + \texttt{0x1234}) \bmod 2^{16}, \qquad u = (W' \gg 1) \mathbin{\&} \texttt{0xFF}, \qquad t = T_{\text{ref}}[u]$$

$$S \leftarrow (S + (t \gg 16)) \bmod 2^{16}, \qquad M \leftarrow \text{ror}_{16}(M, 2) \oplus \text{ror}_{16}(t \mathbin{\&} \texttt{0xFFFF}, 1)$$

$$K_2 \leftarrow K_2 \oplus M, \qquad H_r \leftarrow \text{ror}_{16}(H_r, 2), \qquad W \leftarrow W' \oplus S \oplus H_r \oplus K_2$$

   - 복호화: $\text{ks}[i] = (W \gg 8) \mathbin{\&} \texttt{0xFF}$, $\text{data}[i] \leftarrow \text{data}[i] \oplus \text{ks}[i]$.
4. **오답 비밀번호 Fail-Closed**: 잘못된 비밀번호는 임의의 잘못된 키스트림을 생성하므로, Store 모드는 `FILE_CRC` 검증에서 `RAR15_ERR_CRC`로, 압축 모드는 `u15_unpack` 허프만/스트림 오류(`RAR15_ERR_FORMAT`) 또는 `FILE_CRC` 불일치로 반드시 실패한다.

---

## 4. 압축 해제 알고리즘 (`rar15_unpack.c` & `u15_unpack.{h,c}`)

`rar15_unpack(method, in, in_len, out, out_len)`는 복호화된 버퍼 `in`에서 정확히 `out_len = UNP_SIZE` 바이트를 복원한다. 방식 `0x30` 외에는 `0x31..0x35` 5단계 압축 레벨 모두 **동일한 단일 디코더(`u15_unpack`)**를 사용한다(압축률 옵션 `-m1..-m5`는 인코더 탐색 깊이 차이일 뿐 비트스트림 문법은 같다).

### 4.1 Store 방식 (`0x30`)
- `in_len != out_len`이면 `RAR15_ERR_FORMAT`, 일치하면 `memcpy(out, in, out_len)` 후 `RAR15_OK` 반환.

### 4.2 압축 방식 (`0x31..0x35` — 적응형 LZSS + 정규 허프만 디코더)

1. **초기화 (`u15_init`)**: 비솔리드 아카이브에서는 파일마다 새 `u15_state`를 초기화한다(솔리드 모드는 본 라이브러리에서 정책적으로 차단).
   - $64\text{ KiB}$ 원형 슬라이딩 윈도우 `win[65536]`(`total = 0`, `total < d` 구간의 미기록 참조는 `0x00` 반환), 최근 거리 `recent[4]`, `dlist[i] = i + 1` ($i=0..255$).
   - 3개의 적응형 빈도 리스트(`u15_list`): `lit`(`lim = 161`), `flag`(`lim = 255`, `sym[0] = 0xFF`, `sym[1..255] = 0..254`), `block`(`lim = 255`, 초기 `cnt[i] = 7 - (i >> 5)`).
   - 제어 상태: `S = 53`, `kv = 288` (`U = 144` 기준 $2U$), `pfar = true`, `swapped = false`, `lmode = false`.

2. **MSB-first 비트스트림 및 정규 허프만 테이블 (`u15_unpack.c:15-34`)**:
   - `get_bit`: 각 바이트의 MSB(`7 - (pos & 7)`)부터 비트를 추출.
   - `get_code`: 11개의 고정 정규 코드(`T0, T1, T2, T3, TL, CR, CC, QA, QB, QAF, QBF`)로 비트 길이별 코드 개수 테이블을 순회하며 심볼 인덱스 `idx`를 복원(코드 인덱스 `>= 256` 범위 초과 시 `U15_E_UNOBSERVED`, 입력 소진 시 `U15_E_TRUNCATED`).

| 코드명 | `maxlen` | 비트 길이별 코드 수 (`count[1..maxlen]`) | 주 용도 |
|---|---:|---|---|
| `T0` | 12 | `5:4, 6:40, 7:16, 8:16, 9:4, 11:47, 12:130` | 기본 리터럴 / 중간 블록 |
| `T1` | 10 | `5:2, 6:5, 7:46, 8:64, 9:116, 10:24` | 리터럴, 플래그 바이트, `dlist`, 근거리 위치 |
| `T2` | 10 | `6:2, 7:14, 8:202, 9:33, 10:6` | 편중 리터럴 테이블 (`10590 <= S < 16758`) |
| `T3` | 9 | `8:255, 9:2` | 극편중 리터럴 테이블 (`S >= 16758`) |
| `TL` | 12 | `4:8, 5:8, 6:8, 7:9, 12:224` | 저엔트로피 리터럴 (`S <= -9971`) / 블록 (`kd < 1799`) |
| `CR` | 12 | `2:2, 3:1, 4:2, 5:2, 6:4, 7:5, 8:4, 9:4, 10:8, 12:224` | `step_r` / `step_p(layout=1)` 길이 코드 |
| `CC` | 12 | `3:5, 4:2, 5:2, 6:4, 7:5, 8:4, 9:4, 10:8, 11:2, 12:220` | `step_p(layout=2)` / `Q_FAR` 길이 코드 |
| `QA` / `QAF` | 8 | `QA: 1:1, 3:2, 4:3, 5:1, 6:1, 7:1, 8:2` (`QAF: 3:1, 4:5`) | `qs < 39`일 때 단거리/반복/이스케이프 분기 |
| `QB` / `QBF` | 6 | `QB: 2:1, 3:4, 4:3, 5:1, 6:2` (`QBF: 3:3, 4:5`) | `qs >= 39`일 때 단거리/반복/이스케이프 분기 |

3. **적응형 순위 리스트 (`list_use`)**:
   - 코드 인덱스 `r`이 선택되면 `s = sym[r]`를 출력하고 `cs = ++cnt[s]`를 1 증가시킨 뒤, 앞에서부터 `cnt[sym[i]] < cs`인 첫 위치 `i`와 자리를 맞바꿔(swap) 빈도 순위를 적응시킨다.
   - `cs > lim`이면 전체 `cnt[sym[i]] = 7 - (i >> 5)`로 재정규화하고 `cnt[s]++`한다.

4. **디코딩 루프 (`step` 분기 및 극성 반전 카운터 `kind_step`)**:
   - `fleft == 0`일 때 `T1` + `flag` 리스트에서 새 `8`비트 플래그 바이트를 읽는다. 단, 직전 리터럴 연속 횟수 `run >= 17`이면 곧바로 **리터럴 모드(`lmode = true`)**로 진입한다(`step_lmode`).
   - 일반 모드에서는 플래그 비트 `f`와 보조 비트 `k`로 3가지 토큰을 분기한다:
     - **Literal** (`(!swapped && f == 0)` 또는 `(swapped && f == 1 && k == 0)`): `S` 값 구간(`-9971 / 310 / 10590 / 16758`)에 따라 `TL/T0/T1/T2/T3` 중 하나로 순위 `r`을 디코딩하고, $S \leftarrow S + r - 53 - \lfloor S / 256 \rfloor$로 이동평균을 갱신한 뒤 `kind_step(false)`를 호출한다.
     - **P-Match (`step_p`)**: 일반 장거리 LZSS 매치. `ky`(`<66 / <125 / >=125`)로 3가지 길이 부호화(단항+8비트 / `CR` / `CC`) 중 하나를 선택해 길이 `n = 3 + t`를 읽고, `kd`(`<1799 / <10537 / >=10537`)로 `TL/T0/T1` 블록 상위 8비트와 7비트 하위 오프셋을 결합해 거리 `d = (blk << 7) | low`를 계산한다. `d <= 256`이면 `n += 8`, `d > 8192 && (pfar || d >= 32512)`이면 `n += 1`을 더한 뒤 `kind_step(true)`를 호출한다.
     - **Q-Match (`step_q`)**: 단거리/반복 매치. `rep >= 2`이면 1비트로 직전 매치(`last_n, last_d`) 즉시 반복, 그렇지 않으면 `QA/QB`(또는 `far` 활성 시 `QAF/QBF`)에서 길이 `2..10`, `Q_REP`(직전 반복), `Q_R`(`step_r`: 최근 4개 거리 `recent[0..3]` 선택 + `CR`, `sel==0 && x==255`면 `far=true` 이스케이프), 또는 `Q_FAR`(거리 `32768 + low15`, 길이 `5 + CC`)를 복호화한다. 일반 거리 심볼은 인접 전위 테이블 `dlist[256]`을 사용해 `d`를 찾고 한 칸 앞으로 스왑한다.
   - `kind_step` 카운터는 리터럴과 매치 빈도 비율에 따라 `kv`(`U = 144`, 임계치 `153 / 145`, 매 7번째 A-스텝마다 반감)를 추적해 **플래그 비트 `f=0`의 의미(`swapped`)를 동적으로 반전**시켜 플래그 비트 엔트로피를 절감한다.
   - **리터럴 모드(`step_lmode`)**: `lit_table(S)`로 코드 `i`를 읽고, `i >= 1`이면 리터럴 `i - 1` 출력; `i == 0` 뒤 비트 `1`이면 `lmode` 탈출, 비트 `0`이면 `b3` + `T1` + `low5`로 길이 `3 + b3`, 거리 `d = (pos << 5) | low` 단매치를 복사한다.

---

## 5. 무결성 검증 및 오류 처리 코드 (`rar15_crc32.c`, `rar15.h`)

- `rar15_unpack(...) == RAR15_OK` 이후, `rar15_crc32(plain, UNP_SIZE) == FILE_CRC`가 일치할 때만 평문 파일을 디스크에 기록한다.
- 주요 반환 코드: `RAR15_OK (0)`, `RAR15_ERR_PARAM (-1)`, `RAR15_ERR_FORMAT (-2)`, `RAR15_ERR_UNSUPPORTED (-3)`, `RAR15_ERR_CRC (-5)`, `RAR15_ERR_BUFFER (-6)`.

---

## 6. 검증 예제 데이터 (`tests/fixtures/gate/` 실측표)

유니버설 게이트(`6/6` 실행 = `600`개 CSPRNG 비밀번호, `1,800`개 엔트리 바이트 일치) 및 오프라인 `P4` 아카이브(`36`개 아카이브 / `108`개 엔트리) 검증에 사용된 테스트 파일 명세는 다음과 같다.

| 파일명 | 종류 | 평문 크기 | 평문 CRC-32 | 평문 SHA-256 | 출처 및 권리 상태 |
|---|---|---:|---|---|---|
| `T.TXT` | Text | `15,360` B | `0xc2d40f28` | `c0e95aceb33a2ebc079bd110e3e1b28e3e0664d8944ec4d9d525703d61562eee` | AI 자동 생성 합성 예제 (`owner-confirmed`, 저작권 문제 없음) |
| `I.PNG` | PNG Image | `6,447` B | `0x19cd3b82` | `eb09fa49118607699bb85ad6f24400cdccec0dc019d40604274da2fa13275be7` | AI 자동 생성 합성 예제 (`owner-confirmed`, 저작권 문제 없음) |
| `X.EXE` | DOS Binary Sample | `1,024` B | `0x162a76f8` | `e72c206a044108250eb7c236c536ef14214ed66b2352058b8dca0ab8f177adb1` | AI 자동 생성 합성 예제 (`owner-confirmed`, 저작권 문제 없음) |

각 압축 방식(`0x30..0x35`)에서의 암호화된 `PACK_SIZE` 실측 예(`work/archive/323-.../files/{0010..0040}-m*_0.rar`, `HEAD_FLAGS = 0x8004`):

| 압축 방식 (`METHOD`) | `T.TXT` `PACK_SIZE` | `I.PNG` `PACK_SIZE` | `X.EXE` `PACK_SIZE` | 비고 |
|---|---:|---:|---:|---|
| `0x30 (Store, -m0)` | `15,360` B | `6,447` B | `1,024` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x31 (Fastest, -m1)` | `421` B | `6,456` B | `312` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x32 (Fast, -m2)` | `313` B | `6,457` B | `312` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x33 (Normal, -m3)` | `275` B | `6,456` B | `314` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x34 (Good, -m4)` | `273` B | `6,457` B | `314` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
| `0x35 (Best, -m5)` | `272` B | `6,457` B | `313` B | `HEAD_FLAGS = 0x8004` (`PASSWORD`) |
