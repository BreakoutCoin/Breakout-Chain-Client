// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_HASH_H
#define BITCOIN_HASH_H

#include "uint256.h"
#include "serialize.h"

#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <openssl/evp.h> 

#include <vector>

class CBlock;

unsigned int MurmurHash3(unsigned int nHashSeed, const valtype& vDataToHash);

void HMAC_SHA512(const void* pkey,
                 size_t keylen,
                 const void* pdata,
                 size_t datalen,
                 unsigned char* out,
                 size_t& out_len);

// RAII wrapper for EVP_MAC_CTX
struct EvpMacCtx
{
    EVP_MAC_CTX* ctx;
    explicit EvpMacCtx(EVP_MAC* mac)
    : ctx(EVP_MAC_CTX_new(mac))
    {
        if (!ctx)
        {
            throw std::runtime_error("EVP_MAC_CTX_new failed");
        }
    }
    ~EvpMacCtx()
    {
        EVP_MAC_CTX_free(ctx);
    }
    EvpMacCtx(const EvpMacCtx&) = delete;
    EvpMacCtx& operator=(const EvpMacCtx&) = delete;
};

// RAII wrapper for EVP_MAC
struct EvpMac
{
    EVP_MAC* mac;
    explicit EvpMac(const char* algorithm)
    : mac(EVP_MAC_fetch(nullptr, algorithm, nullptr))
    {
        if (!mac)
        {
            throw std::runtime_error(
                std::string("EVP_MAC_fetch failed for: ") + algorithm);
        }
    }
    ~EvpMac()
    {
        EVP_MAC_free(mac);
    }
    EvpMac(const EvpMac&) = delete;
    EvpMac& operator=(const EvpMac&) = delete;
};

class CHMAC_SHA512
{
private:
    EvpMac mac;
    EvpMacCtx ctx;
public:
    explicit CHMAC_SHA512(const void* pkey, size_t keylen);
    CHMAC_SHA512& Write(const void* pdata, size_t datalen);
    void Finalize(unsigned char* out);
};

#endif
