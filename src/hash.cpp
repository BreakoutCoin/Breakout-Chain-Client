#include "hash.h"

// Computes HMAC-SHA512 in one shot.
// out must point to a buffer of at least 64 bytes; out_len receives the actual
// length.
void HMAC_SHA512(const void* pkey,
                 size_t keylen,
                 const void* pdata,
                 size_t datalen,
                 unsigned char* out,
                 size_t& out_len)
{
    EvpMac mac("HMAC");
    EvpMacCtx ctx(mac.mac);

    OSSL_PARAM params[] = { OSSL_PARAM_construct_utf8_string("digest",
                                                             const_cast<char*>(
                                                                 "SHA512"),
                                                             0),
                            OSSL_PARAM_END };

    if (!EVP_MAC_init(ctx.ctx,
                      reinterpret_cast<const unsigned char*>(pkey),
                      keylen,
                      params))
    {
        throw std::runtime_error("HMAC_SHA512: EVP_MAC_init failed");
    }

    if (!EVP_MAC_update(ctx.ctx,
                        reinterpret_cast<const unsigned char*>(pdata),
                        datalen))
    {
        throw std::runtime_error("HMAC_SHA512: EVP_MAC_update failed");
    }

    if (!EVP_MAC_final(ctx.ctx, out, &out_len, 64))
    {
        throw std::runtime_error("HMAC_SHA512: EVP_MAC_final failed");
    }
}

CHMAC_SHA512::CHMAC_SHA512(const void* pkey, size_t keylen)
    : mac("HMAC"), ctx(mac.mac)
{
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA512"), 0),
        OSSL_PARAM_END
    };
    if (!EVP_MAC_init(ctx.ctx,
                      reinterpret_cast<const unsigned char*>(pkey), keylen,
                      params))
        throw std::runtime_error("CHMAC_SHA512: EVP_MAC_init failed");
}

CHMAC_SHA512& CHMAC_SHA512::Write(const void* pdata, size_t datalen)
{
    if (!EVP_MAC_update(ctx.ctx,
                        reinterpret_cast<const unsigned char*>(pdata), datalen))
        throw std::runtime_error("CHMAC_SHA512: EVP_MAC_update failed");
    return *this;
}

void CHMAC_SHA512::Finalize(unsigned char* out)
{
    size_t out_len = 64;
    if (!EVP_MAC_final(ctx.ctx, out, &out_len, 64))
        throw std::runtime_error("CHMAC_SHA512: EVP_MAC_final failed");
}

