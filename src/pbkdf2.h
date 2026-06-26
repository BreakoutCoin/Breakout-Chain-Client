// Copyright (c) 2013 NovaCoin Developers
// Updated for OpenSSL 3.x EVP API compatibility

#ifndef PBKDF2_H
#define PBKDF2_H

#include <openssl/evp.h>
#include <stdint.h>
#include <stddef.h>

// Opaque HMAC-SHA256 context backed by EVP_MD_CTX pairs.
// Two contexts are held: one for the inner hash state (after the ipad key
// has been absorbed) and one for the outer hash state (after the opad key
// has been absorbed).  Keeping them separate mirrors the original
// SHA256_CTX ictx/octx layout so the rest of the call-sites are unchanged.
typedef struct HMAC_SHA256Context {
    EVP_MD_CTX *ictx;   // inner  SHA-256 state (K xor ipad consumed)
    EVP_MD_CTX *octx;   // outer  SHA-256 state (K xor opad consumed)
} HMAC_SHA256_CTX;

void
HMAC_SHA256_Init(HMAC_SHA256_CTX *ctx, const void *_K, size_t Klen);

void
HMAC_SHA256_Update(HMAC_SHA256_CTX *ctx, const void *in, size_t len);

void
HMAC_SHA256_Final(unsigned char digest[32], HMAC_SHA256_CTX *ctx);

void
PBKDF2_SHA256(const uint8_t *passwd, size_t passwdlen,
              const uint8_t *salt,   size_t saltlen,
              uint64_t c,
              uint8_t *buf,          size_t dkLen);

#endif // PBKDF2_H
