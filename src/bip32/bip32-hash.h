////////////////////////////////////////////////////////////////////////////////
//
// bip32-hash.h
//
// Copyright (c) 2011-2012 Eric Lombrozo
// Copyright (c) 2011-2016 Ciphrex Corp.
//
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.
//
// Breakout Explore port note:
//   This is a trimmed version of the original bip32 hash.h. It keeps only the
//   OpenSSL-backed digests the extended-key derivation path actually uses
//   (sha256, sha256d, ripemd160, hash160, hmac_sha256, hmac_sha512) and drops
//   the Stealth-specific hash9/keccak/sha3/scrypt helpers (which pulled in
//   core-hashes.hpp / hashblock.h / scrypt.h that Breakout does not have).
//   Renamed from hash.h to bip32-hash.h to avoid a basename clash with
//   Breakout's own src/hash.h.
//

#ifndef __BIP32_HASH_H___
#define __BIP32_HASH_H___

#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>

#include <stdexcept>

#include "uchar_vector.h"


// All inputs and outputs are big endian

template<typename Allocator = std::allocator<unsigned char> >
inline basic_uchar_vector<Allocator> sha256(const basic_uchar_vector<Allocator>& data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_MD_CTX ctx;
    EVP_MD_CTX_init(&ctx);
    EVP_DigestInit_ex(&ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(&ctx, &data[0], data.size());
    unsigned int len;
    EVP_DigestFinal_ex(&ctx, hash, &len);
    EVP_MD_CTX_cleanup(&ctx);
#else
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, &data[0], data.size());
    unsigned int len;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);
#endif
    return basic_uchar_vector<Allocator>(hash, SHA256_DIGEST_LENGTH);
}

template<typename Allocator = std::allocator<unsigned char> >
inline basic_uchar_vector<Allocator> sha256d(const basic_uchar_vector<Allocator>& data)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
#if OPENSSL_VERSION_NUMBER < 0x10100000L
    EVP_MD_CTX ctx;
    EVP_MD_CTX_init(&ctx);
    EVP_DigestInit_ex(&ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(&ctx, &data[0], data.size());
    unsigned int len;
    EVP_DigestFinal_ex(&ctx, hash, &len);
    EVP_DigestInit_ex(&ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(&ctx, hash, SHA256_DIGEST_LENGTH);
    EVP_DigestFinal_ex(&ctx, hash, &len);
    EVP_MD_CTX_cleanup(&ctx);
#else
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, &data[0], data.size());
    unsigned int len;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_DigestInit_ex(ctx, EVP_sha256(), NULL);
    EVP_DigestUpdate(ctx, hash, SHA256_DIGEST_LENGTH);
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);
#endif
    return basic_uchar_vector<Allocator>(hash, SHA256_DIGEST_LENGTH);
}

template<typename Allocator = std::allocator<unsigned char> >
inline basic_uchar_vector<Allocator> sha256_2(const basic_uchar_vector<Allocator>& data)
{
    return sha256d(data);
}

// RIPEMD-160 via EVP (the same approach Breakout's util.h uses, known to work
// against the Deps OpenSSL 3.x build).
template<typename Allocator = std::allocator<unsigned char> >
inline basic_uchar_vector<Allocator> ripemd160(const basic_uchar_vector<Allocator>& data)
{
    static const unsigned char pblank[1] = {};
    unsigned char hash[RIPEMD160_DIGEST_LENGTH];
    unsigned int len = 0;
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx)
    {
        throw std::runtime_error("ripemd160(): failed to create context");
    }
    if (EVP_DigestInit_ex(ctx, EVP_ripemd160(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, data.empty() ? pblank : &data[0], data.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &len) != 1)
    {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("ripemd160(): digest failed");
    }
    EVP_MD_CTX_free(ctx);
    return basic_uchar_vector<Allocator>(hash, hash + RIPEMD160_DIGEST_LENGTH);
}

template<typename Allocator = std::allocator<unsigned char> >
inline basic_uchar_vector<Allocator> hash160(const basic_uchar_vector<Allocator>& data)
{
    return ripemd160(sha256(data));
}

template<typename Allocator = std::allocator<unsigned char> >
inline basic_uchar_vector<Allocator> mdsha(const basic_uchar_vector<Allocator>& data)
{
    return ripemd160(sha256(data));
}

template<typename Allocator = std::allocator<unsigned char> >
inline basic_uchar_vector<Allocator> hmac_sha256(const basic_uchar_vector<Allocator>& key, const basic_uchar_vector<Allocator>& data)
{
    unsigned char* digest = HMAC(EVP_sha256(), (unsigned char*)&key[0], key.size(), (unsigned char*)&data[0], data.size(), NULL, NULL);
    return basic_uchar_vector<Allocator>(digest, 32);
}

template<typename Allocator = std::allocator<unsigned char> >
inline basic_uchar_vector<Allocator> hmac_sha512(const basic_uchar_vector<Allocator>& key, const basic_uchar_vector<Allocator>& data)
{
    unsigned char* digest = HMAC(EVP_sha512(), (unsigned char*)&key[0], key.size(), (unsigned char*)&data[0], data.size(), NULL, NULL);
    return basic_uchar_vector<Allocator>(digest, 64);
}

#endif  // __BIP32_HASH_H__
