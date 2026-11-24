# FultaArc - build the static library and the tests.
#
#   make            build/host/libfultaarc.a
#   make test       build and run the host tests (sanitizers on); fixtures are found through FULTA_ARC_FIXTURES
#                   (default ops/fixtures) and the imported test data; missing ones skip
#   make bench      build/bench/bench: an optimised library and the timing tool (no sanitizers)
#   make fuzz       build/fuzz/fuzz_open: the libFuzzer harness (clang, ASan, UBSan)
#   make deep       make tsan (threads under ThreadSanitizer) and make fuzz-smoke (FUZZ_SECONDS of fuzzing)
#   make prove      the CBMC proofs (tests/proofs; CBMC=<path>)
#   make win64      cross-build the library and the tests for Windows x64 (mingw-w64, UCRT)
#   make clean
#
# FultaArc's own code is built with -std=c23 -Wall -Wextra -pedantic -Werror; vendored code with -w.

CC        ?= gcc
# the UCRT-suffixed driver where a distribution ships one, else the plain mingw-w64 driver (UCRT by default on
# current Arch mingw-w64-gcc); MINGW_AR follows the chosen driver's prefix so the two cannot drift
MINGW_CC  ?= $(shell command -v x86_64-w64-mingw32ucrt-gcc >/dev/null 2>&1 && echo x86_64-w64-mingw32ucrt-gcc || echo x86_64-w64-mingw32-gcc)
MINGW_AR  ?= $(MINGW_CC:gcc=ar)
AR        ?= ar

OWN_WARN  = -std=c23 -Wall -Wextra -pedantic -Werror
INCLUDES  = -Iinclude -Isrc -Ivendor/proven/include -Ivendor/proven/platform -Ivendor/proven/crypto/include -Ivendor/lzma -Ivendor/miniz -Ivendor/bzip2 -Ivendor/zstd
SAN       ?= -fsanitize=address,undefined -fno-sanitize-recover=all
OPT       ?= -O2 -g
DEPFLAGS  = -MMD -MP

MINIZ_DEF = -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ARCHIVE_WRITING_APIS \
            -DMINIZ_NO_ZLIB_COMPATIBLE_NAMES
LZMA_DEF  = -DZ7_ST
BZ_DEF    = -DBZ_NO_STDIO

# Scope: ZIP, 7z, RAR 1.4-7.0, ALZ and EGG (incl. AZO) and their codecs/crypto. ALZ/EGG/AZO were integrated from
# the fultaarc-cleanroom-alzegg clean room (2026-10-09); $(BZA_SRCS) is ALZ's modified bzip2. src/rar/rar15/ is the
# fultaarc-cleanroom-rar15 room's RAR 1.4 / 1.5 reader and src/rar/rar15_enc/rar15_cipher.c is the
# fultaarc-cleanroom-rar15-enc3 room's RAR 1.5 cipher, both byte-identical (tests/tools/rar15-pin.sh, D-029, D-048);
# own-code warnings apply and they are clean under them.
OWN_SRCS    = $(wildcard src/core/*.c src/codec/*.c src/crypto/*.c src/text/*.c src/platform/*.c \
                         src/zip/*.c src/7z/*.c src/write/*.c src/rar/*.c src/rar/codec/*.c src/alz/*.c src/egg/*.c src/azo/*.c src/filter/*.c src/tar/*.c src/ar/*.c src/cpio/*.c src/iso/*.c src/cab/*.c src/udf/*.c src/lha/*.c src/lha/lhadec/src/*.c src/rar/rar15/*.c src/rar/rar15_enc/*.c src/proven/*.c)
PROVEN_SRCS = $(wildcard vendor/proven/src/proven/*.c vendor/proven/platform/*.c vendor/proven/crypto/src/*.c vendor/proven/crypto/platform/*.c)
LZMA_SRCS   = $(wildcard vendor/lzma/*.c)
MINIZ_SRCS  = vendor/miniz/miniz.c
BZ_SRCS     = vendor/bzip2/bzlib.c vendor/bzip2/decompress.c vendor/bzip2/compress.c vendor/bzip2/blocksort.c vendor/bzip2/huffman.c vendor/bzip2/crctable.c \
              vendor/bzip2/randtable.c
BZA_SRCS    = $(patsubst vendor/bzip2/%,vendor/bzip2_alz/%,$(filter-out %/compress.c %/blocksort.c,$(BZ_SRCS)))
ZSTD_DEF  = -DZSTD_NO_TRACE
ZSTD_SRCS   = vendor/zstd/zstd.c
TEST_SRCS   = $(wildcard tests/test_*.c)

# ---- host ------------------------------------------------------------------------------------------------------

H = build/host
H_OWN    = $(patsubst %.c,$(H)/%.o,$(OWN_SRCS))
H_VENDOR = $(patsubst %.c,$(H)/%.o,$(PROVEN_SRCS) $(LZMA_SRCS) $(MINIZ_SRCS) $(BZ_SRCS) $(BZA_SRCS) $(ZSTD_SRCS))
H_LIB    = $(H)/libfultaarc.a
H_TESTS  = $(patsubst tests/%.c,$(H)/tests/%,$(TEST_SRCS))

all: $(H_LIB)

$(H)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(OWN_WARN) $(OPT) $(SAN) $(MINIZ_DEF) $(LZMA_DEF) $(INCLUDES) -Ivendor/bzip2_alz $(DEPFLAGS) -c $< -o $@

# lhadec (src/lha/lhadec/): the LHA clean room's deliverable, in FultaArc's edition since D-038 (owner 2026-11-02:
# allocation through proven's allocator, edited directly; the byte pin is retired).
# Own-code warnings apply (it is clean under them); the shim renames its symbols
# to fa_lha_*. A more specific pattern than src/%.c, so GNU make picks this rule for these files.
LHADEC_SHIM = -include src/lha/lhadec_shim.h

$(H)/src/lha/lhadec/src/%.o: src/lha/lhadec/src/%.c src/lha/lhadec_shim.h src/lha/lhadec_rename.h
	@mkdir -p $(dir $@)
	$(CC) $(OWN_WARN) $(OPT) $(SAN) $(LHADEC_SHIM) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(H)/vendor/proven/%.o: vendor/proven/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c23 -w $(OPT) $(SAN) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(H)/vendor/lzma/%.o: vendor/lzma/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -w $(OPT) $(SAN) -fno-sanitize=alignment $(LZMA_DEF) -Ivendor/lzma $(DEPFLAGS) -c $< -o $@

$(H)/vendor/miniz/%.o: vendor/miniz/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -w $(OPT) $(SAN) $(MINIZ_DEF) -Ivendor/miniz $(DEPFLAGS) -c $< -o $@

$(H)/vendor/bzip2/%.o: vendor/bzip2/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -w $(OPT) $(SAN) $(BZ_DEF) -Ivendor/bzip2 $(DEPFLAGS) -c $< -o $@

$(H)/vendor/bzip2_alz/%.o: vendor/bzip2_alz/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -w $(OPT) $(SAN) $(BZ_DEF) -include vendor/bzip2_alz/alz_bzip2_rename.h -Ivendor/bzip2_alz $(DEPFLAGS) -c $< -o $@

$(H)/vendor/zstd/%.o: vendor/zstd/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 -w $(OPT) $(SAN) $(ZSTD_DEF) -Ivendor/zstd $(DEPFLAGS) -c $< -o $@

$(H_LIB): $(H_OWN) $(H_VENDOR)
	@rm -f $@
	$(AR) rcs $@ $^

$(H)/tests/%: tests/%.c tests/testlib.h $(H_LIB)
	@mkdir -p $(dir $@)
	$(CC) $(OWN_WARN) $(OPT) $(SAN) $(INCLUDES) -Itests $< $(H_LIB) -lm -lpthread -o $@

test: warn $(H_TESTS)
	@sh tests/tools/rar15-pin.sh
	@sh tests/tools/no-libc-alloc.sh
	@fail=0; for t in $(H_TESTS); do echo "== $$t"; $$t || fail=1; done; \
	if [ $$fail = 0 ]; then echo "make test: all passed"; else echo "make test: FAILED"; exit 1; fi

# warn: compile our own sources at -Og so GCC's -Wmaybe-uninitialized (which the -O2 test/bench builds do not
# raise) is enforced under -Werror. This caught the sevenzip.c uninitialised-read crash of 2026-10-09 — an
# uninitialised read is invisible to the ASAN/UBSAN test build but faults a plain optimised build. Objects are
# throwaway (build/warn, not linked); vendor sources (-w) never trip it.
warn:
	@$(MAKE) -s OPT="-Og -DNDEBUG" SAN="" H=build/warn $(patsubst %.c,build/warn/%.o,$(OWN_SRCS))
	@echo "make warn: -Og -Werror clean"

# ---- benchmark build: optimised, no sanitizers (tools/bench/README.md) ------------------------------------------

bench:
	$(MAKE) -s OPT="-O2 -DNDEBUG" SAN="" H=build/bench build/bench/libfultaarc.a
	$(CC) $(OWN_WARN) -O2 $(INCLUDES) tools/bench/bench.c build/bench/libfultaarc.a -lm -lpthread -o build/bench/bench

# ---- Windows x64 (mingw-w64, UCRT): build only -----------------------------------------------------------------

W = build/win64
W_OWN    = $(patsubst %.c,$(W)/%.o,$(OWN_SRCS))
W_VENDOR = $(patsubst %.c,$(W)/%.o,$(PROVEN_SRCS) $(LZMA_SRCS) $(MINIZ_SRCS) $(BZ_SRCS) $(BZA_SRCS) $(ZSTD_SRCS))
W_LIB    = $(W)/libfultaarc.a
W_TESTS  = $(patsubst tests/%.c,$(W)/tests/%.exe,$(TEST_SRCS))

win64: $(W_LIB) $(W_TESTS)

$(W)/src/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(MINGW_CC) $(OWN_WARN) -O2 $(MINIZ_DEF) $(LZMA_DEF) $(INCLUDES) -Ivendor/bzip2_alz $(DEPFLAGS) -c $< -o $@

$(W)/src/lha/lhadec/src/%.o: src/lha/lhadec/src/%.c src/lha/lhadec_shim.h src/lha/lhadec_rename.h
	@mkdir -p $(dir $@)
	$(MINGW_CC) $(OWN_WARN) -O2 $(LHADEC_SHIM) $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(W)/vendor/proven/%.o: vendor/proven/%.c
	@mkdir -p $(dir $@)
	$(MINGW_CC) -std=c23 -w -O2 $(INCLUDES) $(DEPFLAGS) -c $< -o $@

$(W)/vendor/lzma/%.o: vendor/lzma/%.c
	@mkdir -p $(dir $@)
	$(MINGW_CC) -std=c11 -w -O2 $(LZMA_DEF) -Ivendor/lzma $(DEPFLAGS) -c $< -o $@

$(W)/vendor/miniz/%.o: vendor/miniz/%.c
	@mkdir -p $(dir $@)
	$(MINGW_CC) -std=c11 -w -O2 $(MINIZ_DEF) -Ivendor/miniz $(DEPFLAGS) -c $< -o $@

$(W)/vendor/zstd/%.o: vendor/zstd/%.c
	@mkdir -p $(dir $@)
	$(MINGW_CC) -std=c11 -w -O2 $(ZSTD_DEF) -Ivendor/zstd $(DEPFLAGS) -c $< -o $@

$(W)/vendor/bzip2/%.o: vendor/bzip2/%.c
	@mkdir -p $(dir $@)
	$(MINGW_CC) -std=c11 -w -O2 $(BZ_DEF) -Ivendor/bzip2 $(DEPFLAGS) -c $< -o $@

$(W)/vendor/bzip2_alz/%.o: vendor/bzip2_alz/%.c
	@mkdir -p $(dir $@)
	$(MINGW_CC) -std=c11 -w -O2 $(BZ_DEF) -include vendor/bzip2_alz/alz_bzip2_rename.h -Ivendor/bzip2_alz $(DEPFLAGS) -c $< -o $@

$(W_LIB): $(W_OWN) $(W_VENDOR)
	@rm -f $@
	$(MINGW_AR) rcs $@ $^

$(W)/tests/%.exe: tests/%.c tests/testlib.h $(W_LIB)
	@mkdir -p $(dir $@)
	$(MINGW_CC) $(OWN_WARN) -O2 $(INCLUDES) -Itests $< $(W_LIB) -static -lbcrypt -o $@

# fuzz: the libFuzzer harness (tests/fuzz/fuzz_open.c) over the whole library, built with clang, ASan and UBSan in
# build/fuzz. Run it on a corpus of fixtures (see the harness); findings are inputs that crash, leak or overflow.
FUZZ_SAN = -fno-sanitize-recover=all
fuzz:
	$(MAKE) H=build/fuzz CC=clang OPT="-O1 -g" OWN_WARN="-std=c23 -Wall -Wextra" SAN="-fsanitize=address,undefined,fuzzer-no-link $(FUZZ_SAN)" build/fuzz/libfultaarc.a
	clang -std=c23 -O1 -g -fsanitize=address,undefined,fuzzer $(FUZZ_SAN) -Iinclude tests/fuzz/fuzz_open.c build/fuzz/libfultaarc.a -lm -lpthread -o build/fuzz/fuzz_open

# tsan: the threads test against a ThreadSanitizer build (clang, build/tsan). fuzz-smoke: FUZZ_SECONDS of fuzzing
# on the saved findings and the small fixtures (tests/tools/fuzz-smoke.sh). deep: both; scripts/gate.sh runs it at t2.
FUZZ_SECONDS ?= 120
tsan:
	$(MAKE) H=build/tsan CC=clang OPT="-O1 -g" OWN_WARN="-std=c23 -Wall -Wextra" SAN="-fsanitize=thread" build/tsan/libfultaarc.a
	clang -std=c23 -O1 -g -fsanitize=thread -Iinclude -Itests tests/test_threads.c build/tsan/libfultaarc.a -lm -lpthread -o build/tsan/test_threads
	TSAN_OPTIONS=halt_on_error=1 build/tsan/test_threads

fuzz-smoke: fuzz
	sh tests/tools/fuzz-smoke.sh $(FUZZ_SECONDS)

deep: tsan fuzz-smoke

# prove: the CBMC proofs in tests/proofs (memory safety and the stated properties for every input up to each
# harness's bound). CBMC=<path> when it is not on PATH. About 10 minutes.
CBMC ?= cbmc
prove: brute
	sh tests/proofs/run.sh $(CBMC)

# brute: every input up to 3 bytes through RAR 4's name decoder and Deflate/Deflate64, under ASan + UBSan, where the
# CBMC models do not fit (tests/proofs/brute.c). About 5 minutes.
brute: $(H_LIB)
	@mkdir -p build/proofs
	$(CC) -std=c23 -O2 -g $(SAN) -w $(INCLUDES) tests/proofs/brute.c $(H_LIB) -lm -lpthread -o build/proofs/brute
	build/proofs/brute

clean:
	rm -rf build/host build/win64

.PHONY: all test warn win64 clean bench fuzz tsan fuzz-smoke deep prove brute

# header-dependency tracking (-MMD): rebuild an object when any header it includes changes
-include $(H_OWN:.o=.d) $(H_VENDOR:.o=.d) $(W_OWN:.o=.d) $(W_VENDOR:.o=.d)
