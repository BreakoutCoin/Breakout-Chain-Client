// Copyright (c) 2013 NovaCoin Developers
// Updated for OpenSSL 3.x / 3.6+ EVP API compatibility.
//
// The original implementation used the low-level SHA256_CTX / SHA256_Init /
// SHA256_Update / SHA256_Final API.  Those symbols were deprecated in
// OpenSSL 3.0 and may be removed in a future release.  This version replaces
// every low-level call with the EVP_MD_CTX digesting API, which is the
// supported path going forward.

#include <string.h>
#include <stdexcept>

#include <openssl/evp.h>
#include <openssl/err.h>

#include "pbkdf2.h"

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

// Encode a 32-bit unsigned integer in big-endian byte order.
static inline void
be32enc(void *pp, uint32_t x)
{
    uint8_t *p = static_cast<uint8_t *>(pp);
    p[0] = (x >> 24) & 0xff;
    p[1] = (x >> 16) & 0xff;
    p[2] = (x >>  8) & 0xff;
    p[3] =  x        & 0xff;
}

// Thin RAII wrapper so EVP_MD_CTX* is freed on any early-exit path.
struct EvpMdCtxGuard {
    EVP_MD_CTX *ctx;
    explicit EvpMdCtxGuard(EVP_MD_CTX *c) : ctx(c) {}
    ~EvpMdCtxGuard() { EVP_MD_CTX_free(ctx); }
    // Non-copyable.
    EvpMdCtxGuard(const EvpMdCtxGuard &) = delete;
    EvpMdCtxGuard &operator=(const EvpMdCtxGuard &) = delete;
};

// Allocate a fresh EVP_MD_CTX or throw on failure.
static EVP_MD_CTX *
ctx_new()
{
    EVP_MD_CTX *c = EVP_MD_CTX_new();
    if (!c)
        throw std::runtime_error("EVP_MD_CTX_new failed");
    return c;
}

// ---------------------------------------------------------------------------
// Public HMAC-SHA256 API
// ---------------------------------------------------------------------------

/*
 * HMAC_SHA256_Init
 *
 * Initialise *ctx so that subsequent Update/Final calls produce HMAC-SHA256
 * output.  The inner and outer EVP_MD_CTX objects are allocated here and must
 * be freed by the caller via HMAC_SHA256_Final or an explicit
 * EVP_MD_CTX_free() if the context is abandoned early (see PBKDF2_SHA256 for
 * an example using memset-to-zero cleanup).
 *
 * Compatible with OpenSSL 1.1.x through 3.x.
 */
void
HMAC_SHA256_Init(HMAC_SHA256_CTX *ctx, const void *_K, size_t Klen)
{
    unsigned char pad[64];
    unsigned char khash[32];
    const unsigned char *K = static_cast<const unsigned char *>(_K);

    const EVP_MD *md = EVP_sha256();

    // Allocate the two persistent contexts that live inside *ctx.
    ctx->ictx = ctx_new();
    ctx->octx = ctx_new();

    // If the key is longer than the block size (64 bytes), hash it first.
    if (Klen > 64) {
        EVP_MD_CTX *tmp = ctx_new();
        EvpMdCtxGuard g(tmp);

        unsigned int hlen = 0;
        if (EVP_DigestInit_ex(tmp, md, nullptr) != 1 ||
            EVP_DigestUpdate(tmp, K, Klen)       != 1 ||
            EVP_DigestFinal_ex(tmp, khash, &hlen) != 1)
            throw std::runtime_error("EVP key-hash failed");

        K    = khash;
        Klen = 32;
    }

    // Inner context: absorb K xor ipad (0x36).
    memset(pad, 0x36, 64);
    for (size_t i = 0; i < Klen; i++)
        pad[i] ^= K[i];

    if (EVP_DigestInit_ex(ctx->ictx, md, nullptr) != 1 ||
        EVP_DigestUpdate(ctx->ictx, pad, 64)       != 1)
        throw std::runtime_error("EVP inner init failed");

    // Outer context: absorb K xor opad (0x5c).
    memset(pad, 0x5c, 64);
    for (size_t i = 0; i < Klen; i++)
        pad[i] ^= K[i];

    if (EVP_DigestInit_ex(ctx->octx, md, nullptr) != 1 ||
        EVP_DigestUpdate(ctx->octx, pad, 64)       != 1)
        throw std::runtime_error("EVP outer init failed");

    // Scrub key material from the stack.
    memset(khash, 0, sizeof(khash));
    memset(pad,   0, sizeof(pad));
}

/* Feed additional data into the HMAC-SHA256 inner digest. */
void
HMAC_SHA256_Update(HMAC_SHA256_CTX *ctx, const void *in, size_t len)
{
    if (EVP_DigestUpdate(ctx->ictx, in, len) != 1)
        throw std::runtime_error("EVP_DigestUpdate (inner) failed");
}

/*
 * HMAC_SHA256_Final
 *
 * Finalises the HMAC computation, writes 32 bytes into *digest, and frees
 * the two EVP_MD_CTX objects held inside *ctx.  The context must not be
 * used after this call.
 */
void
HMAC_SHA256_Final(unsigned char digest[32], HMAC_SHA256_CTX *ctx)
{
    unsigned char ihash[32];
    unsigned int  hlen = 0;

    // Finalise inner digest.
    if (EVP_DigestFinal_ex(ctx->ictx, ihash, &hlen) != 1)
        throw std::runtime_error("EVP_DigestFinal_ex (inner) failed");

    EVP_MD_CTX_free(ctx->ictx);
    ctx->ictx = nullptr;

    // Feed inner hash into outer digest and finalise.
    if (EVP_DigestUpdate(ctx->octx, ihash, 32)         != 1 ||
        EVP_DigestFinal_ex(ctx->octx, digest, &hlen)   != 1)
        throw std::runtime_error("EVP_DigestFinal_ex (outer) failed");

    EVP_MD_CTX_free(ctx->octx);
    ctx->octx = nullptr;

    memset(ihash, 0, sizeof(ihash));
}

// ---------------------------------------------------------------------------
// PBKDF2-SHA256
// ---------------------------------------------------------------------------

/*
 * PBKDF2_SHA256
 *
 * Derives a key of dkLen bytes from passwd/salt using c iterations of
 * HMAC-SHA256 as the PRF (RFC 2898 / PKCS #5 v2.0).
 *
 * dkLen must be at most 32 * (2^32 - 1).
 *
 * The password/salt are processed once up-front (PShctx) so that each
 * per-block inner iteration only needs to feed the 32-byte U value into a
 * fresh copy of that pre-computed state rather than re-processing the full
 * password/salt every time.
 */
void
PBKDF2_SHA256(const uint8_t *passwd, size_t passwdlen,
              const uint8_t *salt,   size_t saltlen,
              uint64_t c,
              uint8_t  *buf,         size_t dkLen)
{
    // Pre-compute the HMAC state after absorbing P and S.
    HMAC_SHA256_CTX PShctx;
    HMAC_SHA256_Init(&PShctx, passwd, passwdlen);
    HMAC_SHA256_Update(&PShctx, salt, saltlen);

    uint8_t ivec[4];
    uint8_t U[32];
    uint8_t T[32];

    for (size_t i = 0; i * 32 < dkLen; i++) {

        // Encode the 1-based block counter as a 4-byte big-endian integer.
        be32enc(ivec, static_cast<uint32_t>(i + 1));

        // U_1 = PRF(P, S || INT(i+1))
        // Clone PShctx so we can continue using the pre-computed state later.
        HMAC_SHA256_CTX hctx;
        hctx.ictx = EVP_MD_CTX_new();
        hctx.octx = EVP_MD_CTX_new();
        if (!hctx.ictx || !hctx.octx)
            throw std::runtime_error("EVP_MD_CTX_new failed in PBKDF2 loop");

        if (EVP_MD_CTX_copy_ex(hctx.ictx, PShctx.ictx) != 1 ||
            EVP_MD_CTX_copy_ex(hctx.octx, PShctx.octx) != 1)
            throw std::runtime_error("EVP_MD_CTX_copy_ex failed");

        HMAC_SHA256_Update(&hctx, ivec, 4);
        HMAC_SHA256_Final(U, &hctx);   // frees hctx.ictx / hctx.octx

        // T_i starts as U_1.
        memcpy(T, U, 32);

        // U_j = PRF(P, U_{j-1}),  T_i ^= U_j  for j = 2..c
        for (uint64_t j = 2; j <= c; j++) {
            HMAC_SHA256_Init(&hctx, passwd, passwdlen);
            HMAC_SHA256_Update(&hctx, U, 32);
            HMAC_SHA256_Final(U, &hctx);

            for (int k = 0; k < 32; k++)
                T[k] ^= U[k];
        }

        // Copy the relevant slice of T_i into the output buffer.
        size_t clen = dkLen - i * 32;
        if (clen > 32)
            clen = 32;
        memcpy(&buf[i * 32], T, clen);
    }

    // Free the pre-computed PShctx without calling _Final on it.
    EVP_MD_CTX_free(PShctx.ictx);
    EVP_MD_CTX_free(PShctx.octx);
    PShctx.ictx = nullptr;
    PShctx.octx = nullptr;
}
