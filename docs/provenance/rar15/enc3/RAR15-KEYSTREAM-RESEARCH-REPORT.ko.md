# RAR 1.5 키스트림 생성기 클린룸 복원, 구성적 증명 및 MIT 공개 검증 보고서

- **프로젝트**: `fultaarc-cleanroom-rar15-enc3` (`STATE: DONE`)
- **작성일**: `2026-11-24`
- **산출물**: 독립 실행형 C99 라이브러리 및 CLI (`tools/libcleanroom/`, 순수 MIT 라이선스)
- **인식론적 분류**: `APPROVED-PRIOR` (`AMENDMENT-2026-11-16`: `RECALL-208`, `RECALL-209`) + `DATA-DERIVED` (`Cycles 001..322`, `Cycle 324` `FORCED`로 구성적 연결 완료) + `BLIND-VALIDATED` (`Cycle 323`, `Cycle 324`, `universal_decryption_gate.py` 6회 전수 통과)

---

## 1. 요약 및 문서 목적

본 보고서는 블랙박스 입출력 관측과 수학적 유도만으로 **RAR 1.5 키스트림 생성기**(`tools/libcleanroom/src/rar15_cipher.c`의 `rar15_keystream`)를 복원·구현·하드닝하고, **외부 독자가 이 문서만 읽고도 알고리즘의 수학적 동작 원리, `Cycle 322 -> Cycle 323 -> Cycle 324` 구성적 증명, 전수 검증 결과, 그리고 독립형 C99 라이브러리의 MIT 라이선스 공개 근거와 한계를 완전히 이해할 수 있도록** 기술한 공개용 연구 보고서다.

본 보고서는 다음 두 질문을 엄격히 구분한다.

1. **수학적·실험적 동치성**: 전달된 C99 함수가 `1993 RAR.EXE`의 실제 키스트림과 임의 입력·길이·원시 바이트에 대해 비트 단위로 일치하는가?
   - **검증 결과**: `Cycle 323`(`P1..P3`)의 전체 전수 코퍼스 대비 미관측 `234,752 / 235,520` B에서 `0` 오류, `Cycle 324`(`P6`, `len >= 4`)의 엄격히 분리된 `25,600 / 25,600` 미관측 B에서 `0` 오류, 압축+암호화(`0x30..0x35`) `rarkit unpack -K` `108 / 108` 성공, 그리고 SHA-256이 고정된 `tests/universal_decryption_gate.py` **총 6회** 독립 전수 실행(하드닝 전 `bad0d94` `3회` [외부감사 `REVIEW.ko.txt` 포함] + 하드닝 후 `3회` [작성자 `2회` + 독립 리뷰어 `1회`, [FINAL-REVIEW-324.md](FINAL-REVIEW-324.md)])에서 **`600 / 600` 신선한 CSPRNG 암호, `1,800 / 1,800` 파일 바이트 일치(`RESULT: ACHIEVED`)**를 달성했다.
2. **클린룸 출처 및 MIT 단독 공개 적합성**: 서드파티 코드나 `RAR.EXE` 바이너리/관측 테이블 없이 독립 MIT 프로젝트로 배포 가능한가?
   - **기술 감사 결과 (`CLEAN`, 법률 자문 아님)**: 배포 대상인 `tools/libcleanroom/`의 모든 C99 코드는 순수 창작/오너 제공 MIT 코드이며 서드파티 아카이버 소스, `RAR.EXE` 역어셈블리/덤프, 런타임 오라클 호출, 하드코딩 키스트림이 **단 하나도 포함되지 않았다** (`nm -u rar15_cipher.o` 외부 심볼 `0`개). 단, `AMENDMENT-2026-11-16`(`RECALL-208`/`209`), `Cycle 322 -> 323` 사이 `71`분 비아카이브 간극(`Cycle 324`로 사후 구성적 도출 가능성을 증명하되 역사적 사실은 보존), `RECALL-ZIP` 폐기/격리 이력, LLM 사전학습 가중치 한계는 6절에 투명하게 공개하며, 본 문서의 기술 감사는 정식 법률 자문(formal legal advice)을 대체하지 않는다.

---

## 2. RAR 1.5 키스트림 생성기의 완전한 수학적 명세 및 C99 구현

### 2.1 반사형 IEEE 802.3 CRC-32 기본 연산

$\mathbb{Z}_{2^{16}} = \{0, \dots, 65535\}$, $\mathbb{Z}_{2^{32}} = \{0, \dots, 2^{32}-1\}$ 위의 비트 XOR($\oplus$), 논리 시프트($\gg, \ll$), 그리고 $16$비트 순환 회전을 다음과 같이 정의한다($r' = r \bmod 16$).

$$\text{rol}_{16}(x, r) = \bigl((x \ll r') \mathbin{\&} \texttt{0xFFFF}\bigr) \mid (x \gg (16 - r')), \qquad \text{ror}_{16}(x, r) = \bigl((x \gg r') \mid (x \ll (16 - r'))\bigr) \mathbin{\&} \texttt{0xFFFF}.$$

표준 IEEE 802.3 다항식 $P_{\text{CRC32}}(x) = x^{32} + x^{26} + x^{23} + x^{22} + x^{16} + x^{12} + x^{11} + x^{10} + x^8 + x^7 + x^5 + x^4 + x^2 + x + 1$(반사형 상수 $\texttt{0xEDB88320}$)의 256-원소 테이블 $T_{\text{ref}}$는 정의로부터 다음과 같이 순수 수학적으로 생성된다.

$$T_{\text{ref}}[u] = \mathcal{F}^8(u), \qquad \mathcal{F}(x) = \begin{cases} \texttt{0xEDB88320} \oplus (x \gg 1) & (x \bmod 2 = 1), \\ x \gg 1 & (x \bmod 2 = 0). \end{cases}$$

이를 하위/상위 16비트 절반으로 분할한다.

$$T_{\text{LO}}[u] = T_{\text{ref}}[u] \mathbin{\&} \texttt{0xFFFF}, \qquad T_{\text{HI}}[u] = T_{\text{ref}}[u] \gg 16.$$

### 2.2 키 스케줄 (비밀번호 $\mapsto$ 초기 5개 16비트 작업 상태)

NUL 종료 바이트열 $P$에 대해 최대 $77$바이트까지 절단한 무부호(`uint8_t`, $0\dots 255$) 바이트열 $e = (c_0, \dots, c_{N-1})$, $N = \min(|P|, 77)$을 취한다. 초기값 $R^{(0)} = \texttt{0xFFFFFFFF}$, $H^{(0)} = \texttt{0x0000}$, $S_0^{(0)} = \texttt{0xFFFF}$에서 $j = 0, \dots, N-1$에 대해 다음을 계산한다.

$$R^{(j+1)} = \bigl(R^{(j)} \gg 8\bigr) \oplus T_{\text{ref}}\!\left[(R^{(j)} \oplus c_j) \mathbin{\&} \texttt{0xFF}\right]$$

$$H^{(j+1)} = \bigl(H^{(j)} + T_{\text{HI}}[c_j] + c_j\bigr) \bmod 2^{16}$$

$$S_0^{(j+1)} = S_0^{(j)} \oplus \bigl(T_{\text{LO}}[c_j] \oplus c_j\bigr).$$

$R = R^{(N)}$(최종 XOR 전의 미완료 IEEE CRC-32, 즉 $\text{CRC32}(e) \oplus \texttt{0xFFFFFFFF}$), $H = H^{(N)}$, $b_x = \bigoplus_{j=0}^{N-1} c_j$라 두면 $S_0$는 $b_x$의 단일 함수로 축약된다.

$$S_0(b_x) = \texttt{0xFFFF} \oplus T_{\text{LO}}[b_x] \oplus b_x \qquad \Bigl(\because \bigoplus_{j=0}^{N-1} T_{\text{LO}}[c_j] = T_{\text{LO}}\!\left[\bigoplus_{j=0}^{N-1} c_j\right]\Bigr).$$

실제 루프에서 갱신되는 **5개의 16비트 워드** $(W, M, K_2, H_r, S) \in (\mathbb{Z}_{2^{16}})^5$ (총 작업 저장 공간 $80$비트, 단 초기 유효 상태 크기는 $R \gg 1$의 $31$비트, $H$의 $16$비트, $b_x$의 $7$–$8$비트 구조로 인해 **54비트 이하**)는 다음과 같이 초기화된다.

$$W_0 = R \mathbin{\&} \texttt{0xFFFF}, \qquad M_0 = \text{rol}_{16}(R \gg 16, 1), \qquad K_{2,0} = \texttt{0xFFFF}, \qquad H_{r,0} = H, \qquad S_0 = \texttt{0xFFFF} \oplus T_{\text{LO}}[b_x] \oplus b_x.$$

### 2.3 바이트별 상태 전이 및 출력식

각 스트림 바이트 인덱스 $i = 0, 1, 2, \dots$에 대해:

$$W'_i = (W_i + \texttt{0x1234}) \bmod 2^{16}$$

$$u_i = (W'_i \gg 1) \mathbin{\&} \texttt{0xFF}, \qquad t_i = T_{\text{ref}}[u_i]$$

$$S_{i+1} = (S_i + (t_i \gg 16)) \bmod 2^{16}$$

$$M_{i+1} = \text{ror}_{16}(M_i, 2) \oplus \text{ror}_{16}(t_i \mathbin{\&} \texttt{0xFFFF}, 1)$$

$$K_{2,i+1} = K_{2,i} \oplus M_{i+1}$$

$$H_{r,i+1} = \text{ror}_{16}(H_{r,i}, 2)$$

$$W_{i+1} = W'_i \oplus S_{i+1} \oplus H_{r,i+1} \oplus K_{2,i+1}$$

$$k_i = (W_{i+1} \gg 8) \mathbin{\&} \texttt{0xFF}.$$

$M_0 = \text{rol}_{16}(R_{\text{HI}}, 1)$과 첫 루프의 $\text{ror}_{16}(M_0, 2)$가 결합되어 스텝 $0$에서만 실질 회전량이 $\text{ror}_{16}(R_{\text{HI}}, 1)$이 되고 이후 $i \ge 1$에서 매 스텝 $\text{ror}_{16}(M, 2)$로 회전하는 구조다. 복호화는 저장/압축 페이로드에 관계없이 `decrypted[i] = stored[i] ^ k_i`로 동일하게 수행된다.

### 2.4 검증용 단계별 추적 예시 (KAT 벡터)

`rar15_cipher.c` 및 (CLI 전달 가능 입력의 경우) 실제 `RAR.EXE` 오라클과 비트 단위 일치한 추적값:

1. **빈 비밀번호 `""` ($N = 0$)**:
   - 초기값: $R = \texttt{0xffffffff}$, $H = \texttt{0x0000}$, $S_0 = \texttt{0xffff}$, $W_0 = \texttt{0xffff}$, $M_0 = \texttt{0xffff}$, $K_{2,0} = \texttt{0xffff}$, $H_{r,0} = \texttt{0x0000}$.
   - $i=0$: $W'_0 = \texttt{0x1233}, u_0 = \texttt{0x19}, T[u_0] = \texttt{0x646ba8c0} \Rightarrow S_1 = \texttt{0x646a}, M_1 = \texttt{0xab9f}, K_{2,1} = \texttt{0x5460}, W_1 = \texttt{0x2239}, k_0 = \texttt{0x22}$.
   - $i=1$: $W'_1 = \texttt{0x346d}, u_1 = \texttt{0x36}, T[u_1] = \texttt{0xcfba9599} \Rightarrow S_2 = \texttt{0x3424}, M_2 = \texttt{0x202b}, K_{2,2} = \texttt{0x744b}, W_2 = \texttt{0x7402}, k_1 = \texttt{0x74}$.
   - 첫 `16`바이트: `2274e027ab03ee65362750e914a86a6c`.
2. **비밀번호 `"a"` ($c_0 = \texttt{0x61}$, 오라클 대조 완료)**:
   - 초기값: $R = \texttt{0x174841bc}$, $H = \texttt{0x3b16}$, $S_0 = \texttt{0xae50}$, $W_0 = \texttt{0x41bc}$, $M_0 = \texttt{0x2e90}$, $K_{2,0} = \texttt{0xffff}$, $H_{r,0} = \texttt{0x3b16}$.
   - $i=0$: $W'_0 = \texttt{0x53f0}, u_0 = \texttt{0xf8}, T[u_0] = \texttt{0xb3667a2e} \Rightarrow S_1 = \texttt{0x61b6}, M_1 = \texttt{0x36b3}, K_{2,1} = \texttt{0xc94c}, H_{r,1} = \texttt{0x8ec5}, W_1 = \texttt{0x75cf}, k_0 = \texttt{0x75}$.
   - 첫 `16`바이트: `752fa544495d1c9257bd3d4d73a4f54b`.
3. **비ASCII 원시 바이트 `b"\x80\xaa\xff"` (무부호 `uint8_t`, 오라클 대조 완료)**:
   - 초기값: $R = \texttt{0x98618683}$, $H = \texttt{0x52e6}$, $S_0 = \texttt{0xd971}$, $W_0 = \texttt{0x8683}$, $M_0 = \texttt{0x30c3}$, $K_{2,0} = \texttt{0xffff}$, $H_{r,0} = \texttt{0x52e6}$.
   - 첫 `16`바이트: `ad4faf0cb1273ccde5d966633ba885c8`.

---

## 3. 단계적 실증 유도 체인 (`Cycles 001..322`)

1. **기본 의미론 (`001, 002, 100, 109, 112`)**: 결정적 키스트림 `stored = P ^ ks`, 정확히 $77$바이트 비밀번호 절단(`002`), 압축 방식 `0x30..0x35` 공통 키스트림(`112`), DOS 셸 금지 8개 바이트를 제외한 $248/255$ 단일 바이트 및 $0\text{x}80\dots 0\text{xFF}$ 무부호(`uint8_t`) 승격 규명(`100, 109`).
2. **선형 성분과 상태 하한 (`208, 250, 257..265`)**: $\mathbb{F}_2$ 위 역방향 선형 점화식을 통해 **33차** 다항식 $\texttt{0x30D4326D9} = (x+1)\cdot \texttt{0x104C11DB7}$과 **32차 원시 인자** $\texttt{0x104C11DB7}$ 도출(`208`); 충돌 수 기반 **상태 지지도 크기(support size) 통계적 하한** $\ge 2^{41.28}$ (`250`, 비밀번호 보안 강도가 아님을 명확히 구분); `512`바이트가 $(\text{crc} \gg 1, b_x, \text{acc} \gg 1)$에 의해 결정되며 CRC 최하위 비트 `bit 0`가 키스트림에 전혀 영향이 없음 증명(`257, 259, 262, 265`).
3. **바이트 0 분리 및 $3573$ 가중치 소거 (`235, 238, 239, 247, 248`)**: $b_x = \texttt{0x4c}$ 고정 `byte 0` 식과 $v < 26$ 캐리 경계 발견(`235, 239`), $(-3573)\cdot \texttt{0x89A3} \equiv 1 \pmod{2^{16}}$ 모듈러 역수로 $256/256$ 바이트 가중치를 $H \oplus \texttt{0x8000}\cdot \text{par}_{73}(b_x)$로 붕괴(`238`), 전 `128` $b_x$ 구간에서의 $\text{out}[0] = P_s(s) \oplus Z(\text{crc}, b_x)$ 가법/XOR 분리 규명(`247, 248`).
4. **캐리 사슬 해부 (`319..322`)**: 비순환 캐리 `mask6 = 9`(`319, 320`), 2차 캐리 `inv5 = 4` `((z+2)>>2)&31`(`321`), `A_C10`/`K_r`의 `17_wq` $5/7$ 정확 선형 규칙 및 `40`-쌍 CRC 비트 플립(`322`) 확립.

---

## 4. `Cycle 322 -> Cycle 323` 구성적 연결 증명 (`Cycle 324`, `FORCED`)

`2026-11-23` 당시 `Cycle 322`와 `Cycle 323` 사이의 약 `71`분 오프라인 합성 과정이 아카이브되지 않았던 역사적 간극을 숨기지 않고(`docs/ROLES.md:107-108`), `Cycle 324`(`tools/lab/c324_constructive_bridge.py`, `archive #13`)는 **하드코딩된 정답 상수 0개**만으로 `Cycle 323` 이전 아카이브 데이터(`c248, c239, c247, k109, c322`) + 승인된 출발 가설 클래스(`RECALL-208/209`)로부터 전체 생성기와 정확한 게이지 궤도(gauge orbit)를 직접 재도출했다 (`R1 #18/#19`, `R2 #20/#21` 독립 스크립트 및 `R3` 만장일치 `CONFIRMED 3/3`). 참고용 역사 투명성 부록(핵심 증명 사슬의 전제 아님)으로 해당 71분 구간의 `82`개 탐색 명령 마스킹 정제본도 [APPENDIX-2026-11-23-c322-c323-session-log.md](APPENDIX-2026-11-23-c322-c323-session-log.md)(`.jsonl`)에 첨부했다.

### 정리 1 (Stage I — `c248` 동일 `(R, bx)` 파이버에서의 `hr_rot` 유일 복원)
동일 `(R, b_x)` 쌍에서는 `W_0, S_0, M_1, K_{2,1}, u_0`가 동일하므로 $\text{out}^{(a)}[0] \oplus \text{out}^{(b)}[0] = (\text{ror}_{16}(H^{(a)}, r_h) \gg 8) \oplus (\text{ror}_{16}(H^{(b)}, r_h) \gg 8)$만 남는다. `5,888`쌍을 전수 검사한 결과 **`r_h = 2`만 유일 생존**한다(`0` 오차 vs 차선 `5,857` 오차).

### 정리 2 (Stage II — `c239` $\mathbb{F}_2$ 좌영공간 비트 필링)
`Y_0 = out[0] ^ (ror16(H, 2) >> 8)`의 각 비트 $b \in \{0,\dots,7\}$ ($k = 8 + b$)에서 `((R_LO + add_c) >> k)_0 ^ ((s_0 + T_HI[u_0]) >> k)_0 ^ (Y_0)_b`는 `X(u_0) = [1 | R_HI | T_LO[u_0]]`의 $\mathbb{F}_2$ 선형결합에 속하므로 좌영공간 `K`(`dim >= 217`)를 직교화해야 한다.
- `b = 0` 전수 스캔(`65,536` 쌍)으로 `392`개 생존 (`add_c mod 512 in {0x34, 0xb4, 0x134, 0x1b4}`).
- `k = 9..15` 순차 필링 결과 **`add_c mod 2^15 = 0x1234` 유일 확정** 및 $s_0(\texttt{0x4c}) \bmod 2^{15} \in [\texttt{0x7200}, \texttt{0x7220}]$(`33`개) 도출 — $\max_u (T_{\text{HI}}[u] \mathbin{\&} \texttt{0xFF}) = \texttt{0xDF}$이고 $\texttt{0xDF} + \texttt{0x20} = \texttt{0xFF} < 256$이므로 상위 바이트로 캐리가 발생하지 않는 대수적 밴드와 정확히 일치한다.
- 전체 `2,095`행 `c239` 회전 스캔 결과 `(m1_rot = 1, t_rot = 1, const_hi = 0x7F)` 유일 확정 (`stage2_z0_gf2_err = 0`).

### 정리 3 (Stage III & 블랙박스 $\mathbb{Z}_2^4$ 게이지 불변성 정리 + 패리티 붕괴 보조정리)
관측된 `out[0]`을 $W_1$ 상위 바이트로 대입하여 byte `1`을 필링하면 `(s0_lo8 = 0x08, k2_lo8 in {0xFE, 0xFF})`가 도출되고, `4,096`-튜플 전수 스캔으로 **`m_rot = 2, t_rot = 1, acc_k2 = True`** 및 **정확한 `8`-원소 $\mathbb{Z}_2^3$ 궤도**(`R[0]` 포함 시 `16`-원소 $\mathbb{Z}_2^4$)가 회수된다.

$$\mathcal{O} = \Bigl\{\bigl(R \oplus r,\;\; \texttt{0x1234} \oplus a \cdot 2^{15},\;\; \texttt{0x7208} \oplus b \cdot 2^{15},\;\; \texttt{0x7FFE} \oplus (a \oplus b)\cdot 2^{15} \oplus d\bigr) : (r,a,b,d) \in \mathbb{Z}_2^4\Bigr\}.$$

**증명**: (1) `add_c`가 **짝수**이면 덧셈 `W + add_c`가 bit `0`에서 bit `1`로 캐리를 넘기지 않으므로 $R[0]$($r$)과 $K_{2,0}[0]$($d$)의 변화가 오직 $W[0]$에만 머물며 $u_i = (W'_i \gg 1) \mathbin{\&} \texttt{0xFF}$ 및 출력 바이트 $W_{i+1} \gg 8$에 영원히 전파되지 않는다. (2) 16비트 모듈러 덧셈에서 최상위 bit `15`(`0x8000`)는 상위 캐리가 없으므로 `(a, b, a^b)`를 동시에 반전하면 `W'_i ^ S_{i+1} ^ K_{2,i+1}`이 모든 $i \ge 0$에서 항등적으로 불변이다. 따라서 `Cycle 323`의 대표원 `(0x1234, 0xF208, 0xFFFF)`는 `(a=0, b=1, d=1)` 선택에 불과하며 블랙박스 I/O로는 16개 궤도 원소를 구별할 수 없다 (`500 x 256` B `max_diff = 0`).

**따름정리 (패리티 붕괴 보조정리)**: `add_c`가 **홀수**면 `bit 0`이 `(W + add_c) >> 1` 캐리를 통해 `u_i`와 결합하므로 솔버 궤도가 `8`개에서 `4`개(MSB-only)로 붕괴한다. 실제 합성 대조군(`archive #8/#9`)에서 `Planted Even` (`0x5A7C`)은 `8`개 궤도, `Planted Odd` (`0x5A7D`)는 `4`개 궤도(`0` 오차; 배제된 `bit 0` 파트너는 $\ge 31{,}301$ B 오차)를 회수했으며, 무작위·셔플·비누적 `K2`·`CRC-32C` 4개 음성 대조군은 모두 `0`개 생존자로 기각되었다.

### 정리 4 & 5 (`c247`/`c322`/`c323` 대수적 정합성 & `P6` 미관측 블라인드 홀드아웃)
- Stage IV (`c247` `128` $b_x$ 구간 $\times 65{,}536$): `128/128` 유일 $S_0(b_x) = \texttt{0xFFFF} \oplus T_{\text{LO}}[b_x] \oplus b_x$ (`0/128` 오차); `k109`에서 `uint8_t` `0/400` vs `signed int8_t` `240/400`; `c238` `256/256`; `c322` `K_r` `68/68`; `0xED02` 및 `mc_FF` 정확 일치.
- `P6` (`len >= 4`, `enum12`/`enum3`/`c323` 및 모든 기존 데이터와 `0` 중복): `80` `P6_safe`(`20,480` B) `0` 오차, `40` `P6_raw`(`5,120` B) `0` 오차(vs 부호 `int8_t` `40/40` 실패, `5,101` B 오차), `8`개 궤도 대표원 모두 `0` 오차, `8/8` (`+ R1 8/8`) 라이브 `RAR.EXE` 재생 일치.

---

## 5. 감사 지적 사항 정정 및 보안 하드닝 요약

3회 독립 감사 및 외부 보고서 `REVIEW.ko.txt`에서 제기된 모든 수치/사실 지적을 전수 재검증·정정했다 (`Cycle 324 #11/#19/#21`):

- **`Cycle 208`**: `0x104C11DB7`는 **32차 원시 인자**, `(x + 1) * 0x104C11DB7 = 0x30D4326D9`가 **33차** 다항식이다.
- **`Cycle 250`**: `~41.28`비트는 **상태 지지도(support-size) 통계적 하한**이며 암호 강도 주장이 아니다(실제 초기 유효 상태 크기는 `54`비트 이하, 루프 가변 상태는 **5개 16비트 워드**).
- **`Cycle 323` 중복 계수 정정**: `P1`(`250`행)의 실제 유효 `[:77]` 고유 입력은 `220`개(`30`개 접두어 쌍 공유)이며, 전체 전수 코퍼스(`enum12` `9`건 + `enum3` `3`건 [`b'/SM'`, `b'jAa'`, `b'Ui['`]) 대비 중복 `12`건(`768` B)을 제외한 `P1..P3` 순수 미관측 바이트는 **`234,752 / 235,520` B**다 (`P5`는 `REVIEW.ko.txt` 지적 `7`건/`>=448` B 중복 확인). 또한 기존 보정(calibration) 데이터 `44,545`행은 종단간 `20,245`행(`4,559,680` B)과 상태 시드 `c257` `24,300`행(`777,600` B)의 합산이며 독립 영가설 모형선택 시행으로 취급하지 않는다. `Cycle 324`의 `P6`는 전수 코퍼스 포함 **완전 비중복(`25,600 / 25,600` B)** 이다.
- **코드 보안/품질 하드닝 (Recommendation B 완료)**:
  1. `rar15_cipher.c`, `rar15_crc32.c`: 정적 가변 전역 테이블 제거 -> 스택 지역 CRC-32 테이블로 완전 스레드 안전(reentrant)화, `strlen` 제거로 **`nm -u rar15_cipher.o` 외부 심볼 `0`개**.
  2. `rar15_container.c`, `rarkit.c`: `head_size < 11u`에서의 4바이트 OOB `rd32` 방지 및 32비트 `size_t` 덧셈 오버플로(wraparound) 차단, `name_size <= head_size - 32u` 검증.
  3. `rar15_unpack.c`: 압축 방식 `0x30..0x35` 범위 외 `RAR15_ERR_UNSUPPORTED` 반환.
  4. `rarextract.c`: Zip-Slip 경로 탈출(`..`, `/`, `\`, `:`, 제어문자, 빈 세그먼트) 거부, 미지원 플래그(`SPLIT_BEFORE|SPLIT_AFTER|SOLID|LARGE` 및 디렉터리 `0x00E0`) 차단, `1.2 MB` 스택 배열 힙 이동, `#ifdef _WIN32` `mkdir` 이식성 및 구버전 오류 메시지 정리, 내부 형제 리포지토리 주석 정리(`u15_unpack.h`의 기존 MIT 저작권 표시 원문 보존).
  5. 검증: `make clean all test` (`21/21 ok`, 경고 `0`개), ASan+UBSan (`0` 진단), Zip-Slip/플래그 음수 테스트 통과, `tests/universal_decryption_gate.py` 하드닝 후 **3회 전수 통과 (`300/300` 암호, `900/900` 파일)** -> [FINAL-REVIEW-324.md](FINAL-REVIEW-324.md).

---

## 6. 출처 투명성 및 MIT 라이선스 외부 공개 가능성 평가

1. **공개 대상 코드의 독립성과 MIT 부합성**:
   - 루트 [LICENSE](../../../../LICENSE)를 순수 표준 MIT 텍스트로 정리하고 출처·경계 문서는 [PROVENANCE.md](PROVENANCE.md)로 분리했다.
   - `tools/libcleanroom/` 내 코드는 오직 `C99` 표준 라이브러리 일부만 사용하며(암호 모듈 `rar15_cipher.o`·`rar15_crc32.o`·`rar15_container.o`는 외부 심볼 참조 자체가 없음), `unrar`/`7-Zip`/`WinRAR` 소스, `RAR.EXE` 바이너리/역어셈블리, 관측 테이블, 런타임 오라클을 일절 포함하지 않는다. 게이트 테스트 픽스처(`T.TXT`, `I.PNG`, `X.EXE`) 역시 오너가 확인한(`2026-11-23` / `2026-11-24`) 본 프로젝트용 **AI 자동 생성 합성 예제 파일**로서 외부 저작물이나 상용 바이너리가 전혀 포함되어 있지 않아 MIT 공개 시 저작권 문제가 없다.
   - `tools_private/` 및 `work/`는 `.gitignore`로 버전 관리 및 공개 배포에서 제외된다.
2. **투명하게 공개해야 할 이력 및 비법률자문 고지 (Non-Legal-Advice Notice)**:
   - `AMENDMENT-2026-11-16`(`RECALL-208`, `RECALL-209`), 폐기·격리된 `RECALL-ZIP`, `OWNER-SUGGESTION-2026-11-20`, `71`분 오프라인 합성 간극(`Cycle 324`로 구성적 도출을 보완했으나 `2026-11-23` 당시 이력 자체를 지우지는 않음; 참고용 마스킹 정제본은 [APPENDIX-2026-11-23-c322-c323-session-log.md](APPENDIX-2026-11-23-c322-c323-session-log.md)에 별도 첨부하며 핵심 증명 사슬의 전제가 아님), 초기 `cycle.py verify` collision 기록(`001 #7, 112 #3, 274 #9, 002`), 그리고 LLM 사전학습 가중치 가능성은 모두 투명하게 문서화했다.
   - 따라서 **기술·인식론적 관점에서 `tools/libcleanroom/`은 독립된 상호운용성 구현체로서 MIT 라이선스로 GitHub에 공개 가능하다**. 다만 본 보고서의 평가는 **기술 및 출처 감사이지 정식 법률 자문이 아니며**, 엄격한 전통적 법적 클린룸(사전 정보 완전히 차단된 인적 격리 팀) 요건을 상업적으로 보증해야 하는 경우에는 별도의 법률 전문가 검토가 권장된다.
