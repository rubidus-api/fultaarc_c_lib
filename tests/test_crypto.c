/* test_crypto.c - FultaArc's crypto against published test vectors (FIPS 197 C.1-C.3, FIPS 180-4 examples,
 * RFC 2202 / RFC 4231 HMAC, RFC 6070 PBKDF2-SHA1, KISA's LEA reference values). */
#include "core/internal.h"
#include "testlib.h"

static void hex(const char *h, uint8_t *out) {
    for (size_t i = 0; h[2 * i]; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t)v;
    }
}

static int eqhex(const uint8_t *b, const char *h) {
    uint8_t t[128];
    hex(h, t);
    return memcmp(b, t, strlen(h) / 2) == 0;
}

int main(void) {
    uint8_t key[32], pt[16], ct[16], out[64];
    hex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", key);
    hex("00112233445566778899aabbccddeeff", pt);
    const char *ex[3] = {"69c4e0d86a7b0430d8cdb78070b4c55a", "dda97ca4864cdfe06eaf70a0ec0d7191",
                         "8ea2b7ca516745bfeafc49904b496089"};
    for (int k = 0; k < 3; k++) {
        fa_aes_t a;
        fa_aes_init_enc(&a, key, 16 + 8 * (size_t)k);
        fa_aes_encrypt(&a, pt, ct);
        CHECK(eqhex(ct, ex[k]), "AES-%d encrypt", 128 + 64 * k);
        fa_aes_decrypt(&a, ct, out);
        CHECK(memcmp(out, pt, 16) == 0, "AES-%d decrypt", 128 + 64 * k);
    }
    fa_sha1_t s1;
    fa_sha1_init(&s1);
    fa_sha1_update(&s1, "abc", 3);
    fa_sha1_final(&s1, out);
    CHECK(eqhex(out, "a9993e364706816aba3e25717850c26c9cd0d89d"), "SHA-1 abc");
    fa_sha256_t s2;
    fa_sha256_init(&s2);
    fa_sha256_update(&s2, "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56);
    fa_sha256_final(&s2, out);
    CHECK(eqhex(out, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"), "SHA-256 two blocks");
    uint8_t k20[20];
    memset(k20, 0x0b, 20);
    fa_hmac_sha1(k20, 20, (const uint8_t *)"Hi There", 8, out);
    CHECK(eqhex(out, "b617318655057264e28bc0b6fb378c8ef146be00"), "HMAC-SHA1 RFC 2202 #1");
    fa_hmac_sha256(k20, 20, (const uint8_t *)"Hi There", 8, out);
    CHECK(eqhex(out, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"), "HMAC-SHA256 RFC 4231 #1");
    fa_pbkdf2_sha1((const uint8_t *)"password", 8, (const uint8_t *)"salt", 4, 4096, out, 20);
    CHECK(eqhex(out, "4b007901b765489abead49d926f721d065a429c1"), "PBKDF2-SHA1 RFC 6070 4096");
    const char *lk[3] = {"0f1e2d3c4b5a69788796a5b4c3d2e1f0", "0f1e2d3c4b5a69788796a5b4c3d2e1f0f0e1d2c3b4a59687",
                         "0f1e2d3c4b5a69788796a5b4c3d2e1f0f0e1d2c3b4a5968778695a4b3c2d1e0f"};
    const char *lp[3] = {"101112131415161718191a1b1c1d1e1f", "202122232425262728292a2b2c2d2e2f",
                         "303132333435363738393a3b3c3d3e3f"};
    const char *lc[3] = {"9fc84e3528c6c6185532c7a704648bfd", "6fb95e325aad1b878cdcf5357674c6f2",
                         "d651aff647b189c13a8900ca27f9e197"};
    for (int k = 0; k < 3; k++) {
        fa_lea_t l;
        hex(lk[k], key);
        hex(lp[k], pt);
        fa_lea_init(&l, key, 16 + 8 * (size_t)k);
        fa_lea_encrypt(&l, pt, ct);
        CHECK(eqhex(ct, lc[k]), "LEA-%d", 128 + 64 * k);
    }
    CHECK(fa_crc32(0, fa_bytes("123456789", 9)) == 0xCBF43926u, "CRC-32 check value");

    /* AZO (EGG method 3) end-record strictness: fa_stream_limit_strict validates the trailer the consumer stops
     * short of. A stored AZO chunk ("hello") + the all-zero end record decodes and ends cleanly; dropping the end
     * record is rejected, because the strict limit draws one more byte and forces the decoder to read the trailer. */
    {
        static const uint8_t azo_ok[] = {
            0x31, 0x00,                                          /* profile 0x31, no transform */
            0,0,0,5, 0,0,0,5, 0,0,0,0, 'h','e','l','l','o',      /* chunk U=C=5 (stored), X=U^C=0, then the data */
            0,0,0,0, 0,0,0,0, 0,0,0,0                            /* all-zero end record */
        };
        for (int trunc = 0; trunc < 2; trunc++) {
            size_t len = sizeof azo_ok - (trunc ? 12u : 0u);     /* truncated: drop the end record */
            fa_stream_t *mem = NULL, *azo = NULL, *lim = NULL;
            CHECK(fa_stream_memory(proven_heap_allocator(), azo_ok, len, &mem) == FULTA_ARC_OK && mem, "azo%d: mem stream", trunc);
            if (!mem) continue;
            CHECK(fa_dec_azo(mem, NULL, &azo) == FULTA_ARC_OK && azo, "azo%d: decoder opens", trunc);
            if (!azo) { fa_stream_destroy(mem); continue; }
            CHECK(fa_stream_limit_strict(azo, 5, &lim) == FULTA_ARC_OK && lim, "azo%d: strict limit", trunc);
            if (!lim) { fa_stream_destroy(azo); continue; }
            uint8_t got5[5];
            fulta_arc_err_t e = fa_stream_read_exact(lim, fa_bytes_mut(got5, 5));
            if (trunc) {
                /* the strict limit draws the trailer the moment the declared size is reached, so a missing end
                 * record is caught right here, in the read that delivers the last data byte */
                CHECK(e != FULTA_ARC_OK, "azo truncated: missing end record is rejected (got %s)", fulta_arc_strerror(e));
            } else {
                CHECK(e == FULTA_ARC_OK && memcmp(got5, "hello", 5) == 0, "azo ok: data decodes to 'hello'");
                uint8_t x; size_t g = 123;
                fulta_arc_err_t e2 = lim->read(lim, &x, 1, &g);
                CHECK(e2 == FULTA_ARC_OK && g == 0, "azo ok: clean end at the end record");
            }
            fa_stream_destroy(lim);
        }
    }
    return t_report("test_crypto");
}
