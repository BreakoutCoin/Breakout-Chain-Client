// Copyright (c) 2014 The ShadowCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

// ============================================================
// OpenSSL 3.x / 3.6+ compatibility notes:
//
//  - EC_POINT_bn2point / EC_POINT_point2bn were removed in OpenSSL 3.0.
//    Replaced with EC_POINT_oct2point / EC_POINT_point2oct throughout.
//
//  - RandAddSeedPerfmon() was removed in OpenSSL 3.0.
//    OpenSSL 3 seeds itself automatically; the call is simply dropped.
//
//  - SHA256() one-shot helper is still available but now declared in
//    <openssl/sha.h>; added explicit include for clarity.
//
//  - All BIGNUM intermediates that were previously passed as inline
//    BN_new() arguments are now allocated separately so ownership and
//    error paths are unambiguous.
//
//  - EC_POINT_point2oct writes raw bytes directly into a caller-supplied
//    buffer; a BN_CTX is no longer required for the serialisation step.
// ============================================================

#include "stealth.h"
#include "base58.h"

#include <openssl/rand.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

//const uint8_t stealth_version_byte = 0x2a;
const uint8_t stealth_version_byte = 0x28;


// ---------------------------------------------------------------------------
// Helper: serialise an EC_POINT to a compressed 33-byte buffer.
// Returns true on success.
// ---------------------------------------------------------------------------
static bool point_to_bytes(const EC_GROUP* grp, const EC_POINT* pt,
                            uint8_t* out, size_t out_len, BN_CTX* ctx)
{
    size_t n = EC_POINT_point2oct(grp, pt, POINT_CONVERSION_COMPRESSED,
                                  out, out_len, ctx);
    return n == out_len;
}

// ---------------------------------------------------------------------------
// Helper: deserialise a compressed 33-byte buffer into an EC_POINT.
// Returns a newly allocated EC_POINT (caller must EC_POINT_free), or NULL.
// ---------------------------------------------------------------------------
static EC_POINT* bytes_to_point(const EC_GROUP* grp,
                                 const uint8_t* in, size_t in_len, BN_CTX* ctx)
{
    EC_POINT* pt = EC_POINT_new(grp);
    if (!pt) return NULL;
    if (!EC_POINT_oct2point(grp, pt, in, in_len, ctx))
    {
        EC_POINT_free(pt);
        return NULL;
    }
    return pt;
}


// color information is stored starting in the second byte so that
// addresses for different colors have distinct starting sequences
bool CStealthAddress::SetEncoded(const std::string& encodedAddress)
{
    data_chunk raw;

    if (!DecodeBase58(encodedAddress, raw))
    {
        if (fDebug)
            printf("CStealthAddress::SetEncoded DecodeBase58 failed.\n");
        return false;
    };

    if (!VerifyChecksum(raw))
    {
        if (fDebug)
            printf("CStealthAddress::SetEncoded verify_checksum failed.\n");
        return false;
    };

    size_t nRawSize = raw.size();

    // see CStealthAddress::Encoded()
    // Put color first so that stealth addresses of different currencies look different
    // N_COLOR_BYTES, 1-version, 1-options, 33-scan_pubkey, 1-#spend_pubkeys,
    // 33-spend_pubkey, 1-#sigs, 1-?, 4 checksum
    if (nRawSize < N_COLOR_BYTES + 1 + 1 + 33 + 1 + 33 + 1 + 1 + 4)
    {
        if (fDebug)
            printf("CStealthAddress::SetEncoded() too few bytes provided.\n");
        return false;
    };

    const uint8_t* p = &raw[0];

    // Stealth addresses store color as a simple index, little bytes first.
    this->nColor = 0;
    for (int i = 0; i < N_COLOR_BYTES; ++i)
    {
        this->nColor += static_cast<int>(pow(256.0, i)) * static_cast<int>(*p);
        ++p;
    }

    if (!CheckColor(nColor))
    {
        printf("CStealthAddress::SetEncoded(): Color %d is not valid (1...%d).\n",
               this->nColor, N_COLORS);
    }

    uint8_t version = *p++;

    if (version != stealth_version_byte)
    {
        printf("CStealthAddress::SetEncoded version mismatch 0x%x != 0x%x.\n",
               version, stealth_version_byte);
        return false;
    };

    options = *p++;

    scan_pubkey.resize(33);
    memcpy(&scan_pubkey[0], p, 33);
    p += 33;
    p++; // skip number of spend pubkeys

    spend_pubkey.resize(33);
    memcpy(&spend_pubkey[0], p, 33);

    return true;
};

std::string CStealthAddress::Encoded() const
{
    // https://wiki.unsystem.net/index.php/DarkWallet/Stealth#Address_format
    // [version] [options] [scan_key] [N] ... [Nsigs] [prefix_length] ...

    data_chunk raw;

    if (!AppendColorBytes(this->nColor, raw))
    {
        printf("Could not append color bytes\n");
    }

    raw.push_back(stealth_version_byte);
    raw.push_back(options);

    raw.insert(raw.end(), scan_pubkey.begin(), scan_pubkey.end());
    raw.push_back(1); // number of spend pubkeys
    raw.insert(raw.end(), spend_pubkey.begin(), spend_pubkey.end());
    raw.push_back(0); // number of signatures
    raw.push_back(0); // reserved

    AppendChecksum(raw);

    return EncodeBase58(raw);
};


uint32_t BitcoinChecksum(const uint8_t* p, uint32_t nBytes)
{
    if (!p || nBytes == 0)
        return 0;

    uint8_t hash1[32];
    SHA256(p, nBytes, hash1);
    uint8_t hash2[32];
    SHA256(hash1, sizeof(hash1), hash2);

    // checksum is the first 4 bytes of the double-hash
    uint32_t checksum = from_little_endian<uint32_t>(&hash2[0]);

    return checksum;
};

void AppendChecksum(data_chunk& data)
{
    uint32_t checksum = BitcoinChecksum(&data[0], static_cast<uint32_t>(data.size()));

    // to_little_endian
    for (int i = 0; i < 4; ++i)
    {
        data.push_back(static_cast<uint8_t>(checksum & 0xFF));
        checksum >>= 8;
    };
};

bool VerifyChecksum(const data_chunk& data)
{
    if (data.size() < 4)
        return false;

    uint32_t checksum = from_little_endian<uint32_t>(data.end() - 4);

    return BitcoinChecksum(&data[0], static_cast<uint32_t>(data.size()) - 4) == checksum;
};


int GenerateRandomSecret(ec_secret& out)
{
    // RandAddSeedPerfmon() was removed in OpenSSL 3.0.
    // OpenSSL 3 seeds the DRBG automatically; no explicit seeding is needed.

    static uint256 max("FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364140");
    static uint256 min(16000); // increase? min valid key is 1

    uint256 test;

    int i;
    // check max, try max 32 times
    for (i = 0; i < 32; ++i)
    {
        if (RAND_bytes(reinterpret_cast<unsigned char*>(test.begin()), 32) != 1)
        {
            printf("Error: GenerateRandomSecret RAND_bytes failed.\n");
            return 1;
        }
        if (test > min && test < max)
        {
            memcpy(&out.e[0], test.begin(), 32);
            break;
        };
    };

    if (i > 31)
    {
        printf("Error: GenerateRandomSecret failed to generate a valid key.\n");
        return 1;
    };

    return 0;
};

int SecretToPublicKey(const ec_secret& secret, ec_point& out)
{
    // public key = private * G
    int rv = 0;

    EC_GROUP* ecgrp = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!ecgrp)
    {
        printf("SecretToPublicKey(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };

    BN_CTX* bnCtx = BN_CTX_new();
    BIGNUM*  bnIn = BN_new();
    EC_POINT* pub = NULL;

    if (!bnCtx || !bnIn)
    {
        printf("SecretToPublicKey(): allocation failed.\n");
        rv = 1;
        goto End;
    };

    if (!BN_bin2bn(&secret.e[0], ec_secret_size, bnIn))
    {
        printf("SecretToPublicKey(): BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };

    pub = EC_POINT_new(ecgrp);
    if (!pub)
    {
        printf("SecretToPublicKey(): EC_POINT_new failed.\n");
        rv = 1;
        goto End;
    };

    if (!EC_POINT_mul(ecgrp, pub, bnIn, NULL, NULL, NULL))
    {
        printf("SecretToPublicKey(): EC_POINT_mul failed.\n");
        rv = 1;
        goto End;
    };

    out.resize(ec_compressed_size);
    // EC_POINT_point2bn removed in OpenSSL 3; use EC_POINT_point2oct instead.
    if (!point_to_bytes(ecgrp, pub, &out[0], ec_compressed_size, bnCtx))
    {
        printf("SecretToPublicKey(): point_to_bytes failed.\n");
        rv = 1;
    };

    End:
    if (pub)   EC_POINT_free(pub);
    if (bnIn)  BN_free(bnIn);
    if (bnCtx) BN_CTX_free(bnCtx);
    EC_GROUP_free(ecgrp);

    return rv;
};


int StealthSecret(ec_secret& secret, ec_point& pubkey, const ec_point& pkSpend,
                  ec_secret& sharedSOut, ec_point& pkOut)
{
    /*
    send:
        secret = ephem_secret, pubkey = scan_pubkey

    receive:
        secret = scan_secret, pubkey = ephem_pubkey
        c = H(dP)

    Q = public scan key (EC point, 33 bytes)
    d = private scan key (integer, 32 bytes)
    R = public spend key
    f = private spend key

    Q = dG
    R = fG

    Sender (has Q and R, not d or f):
        P = eG
        c = H(eQ) = H(dP)
        R' = R + cG

    Recipient gets R' and P
    */

    int rv = 0;
    uint8_t vchOutQ[ec_compressed_size];

    BN_CTX*   bnCtx   = NULL;
    BIGNUM*   bnEphem = NULL;
    EC_POINT* Q       = NULL;
    BIGNUM*   bnc     = NULL;
    EC_POINT* C       = NULL;
    EC_POINT* R       = NULL;
    EC_POINT* Rout    = NULL;

    EC_GROUP* ecgrp = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!ecgrp)
    {
        printf("StealthSecret(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };

    if (!(bnCtx = BN_CTX_new()))
    {
        printf("StealthSecret(): BN_CTX_new failed.\n");
        rv = 1;
        goto End;
    };

    bnEphem = BN_new();
    if (!bnEphem || !BN_bin2bn(&secret.e[0], ec_secret_size, bnEphem))
    {
        printf("StealthSecret(): bnEphem BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };

    // Decode scan pubkey Q from compressed bytes.
    // EC_POINT_bn2point removed in OpenSSL 3; use EC_POINT_oct2point instead.
    Q = bytes_to_point(ecgrp, &pubkey[0], pubkey.size(), bnCtx);
    if (!Q)
    {
        printf("StealthSecret(): Q bytes_to_point failed.\n");
        rv = 1;
        goto End;
    };

    // eQ
    if (!EC_POINT_mul(ecgrp, Q, NULL, Q, bnEphem, bnCtx))
    {
        printf("StealthSecret(): eQ EC_POINT_mul failed.\n");
        rv = 1;
        goto End;
    };

    // Serialise eQ to compressed bytes.
    if (!point_to_bytes(ecgrp, Q, vchOutQ, ec_compressed_size, bnCtx))
    {
        printf("StealthSecret(): eQ point_to_bytes failed.\n");
        rv = 1;
        goto End;
    };

    // c = SHA256(eQ)
    SHA256(vchOutQ, ec_compressed_size, &sharedSOut.e[0]);

    bnc = BN_new();
    if (!bnc || !BN_bin2bn(&sharedSOut.e[0], ec_secret_size, bnc))
    {
        printf("StealthSecret(): bnc BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };

    // cG
    C = EC_POINT_new(ecgrp);
    if (!C)
    {
        printf("StealthSecret(): C EC_POINT_new failed.\n");
        rv = 1;
        goto End;
    };

    if (!EC_POINT_mul(ecgrp, C, bnc, NULL, NULL, bnCtx))
    {
        printf("StealthSecret(): cG EC_POINT_mul failed.\n");
        rv = 1;
        goto End;
    };

    // Decode spend pubkey R.
    R = bytes_to_point(ecgrp, &pkSpend[0], pkSpend.size(), bnCtx);
    if (!R)
    {
        printf("StealthSecret(): R bytes_to_point failed.\n");
        rv = 1;
        goto End;
    };

    // Rout = R + cG
    Rout = EC_POINT_new(ecgrp);
    if (!Rout)
    {
        printf("StealthSecret(): Rout EC_POINT_new failed.\n");
        rv = 1;
        goto End;
    };

    if (!EC_POINT_add(ecgrp, Rout, R, C, bnCtx))
    {
        printf("StealthSecret(): Rout EC_POINT_add failed.\n");
        rv = 1;
        goto End;
    };

    pkOut.resize(ec_compressed_size);
    if (!point_to_bytes(ecgrp, Rout, &pkOut[0], ec_compressed_size, bnCtx))
    {
        printf("StealthSecret(): Rout point_to_bytes failed.\n");
        rv = 1;
    };

    End:
    if (Rout)    EC_POINT_free(Rout);
    if (R)       EC_POINT_free(R);
    if (C)       EC_POINT_free(C);
    if (bnc)     BN_free(bnc);
    if (Q)       EC_POINT_free(Q);
    if (bnEphem) BN_free(bnEphem);
    if (bnCtx)   BN_CTX_free(bnCtx);
    EC_GROUP_free(ecgrp);

    return rv;
};


int StealthSecretSpend(ec_secret& scanSecret, ec_point& ephemPubkey,
                       ec_secret& spendSecret, ec_secret& secretOut)
{
    /*
    c  = H(dP)
    R' = R + cG     [without decrypting wallet]
       = (f + c)G   [after decryption of wallet]
    */

    int rv = 0;
    uint8_t vchOutP[ec_compressed_size];

    BN_CTX*   bnCtx        = NULL;
    BIGNUM*   bnScanSecret = NULL;
    EC_POINT* P            = NULL;
    BIGNUM*   bnc          = NULL;
    BIGNUM*   bnOrder      = NULL;
    BIGNUM*   bnSpend      = NULL;

    EC_GROUP* ecgrp = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!ecgrp)
    {
        printf("StealthSecretSpend(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };

    if (!(bnCtx = BN_CTX_new()))
    {
        printf("StealthSecretSpend(): BN_CTX_new failed.\n");
        rv = 1;
        goto End;
    };

    bnScanSecret = BN_new();
    if (!bnScanSecret || !BN_bin2bn(&scanSecret.e[0], ec_secret_size, bnScanSecret))
    {
        printf("StealthSecretSpend(): bnScanSecret BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };

    // Decode ephemeral pubkey P.
    P = bytes_to_point(ecgrp, &ephemPubkey[0], ephemPubkey.size(), bnCtx);
    if (!P)
    {
        printf("StealthSecretSpend(): P bytes_to_point failed.\n");
        rv = 1;
        goto End;
    };

    // dP
    if (!EC_POINT_mul(ecgrp, P, NULL, P, bnScanSecret, bnCtx))
    {
        printf("StealthSecretSpend(): dP EC_POINT_mul failed.\n");
        rv = 1;
        goto End;
    };

    if (!point_to_bytes(ecgrp, P, vchOutP, ec_compressed_size, bnCtx))
    {
        printf("StealthSecretSpend(): dP point_to_bytes failed.\n");
        rv = 1;
        goto End;
    };

    {
        uint8_t hash1[32];
        SHA256(vchOutP, ec_compressed_size, hash1);

        bnc = BN_new();
        if (!bnc || !BN_bin2bn(hash1, 32, bnc))
        {
            printf("StealthSecretSpend(): bnc BN_bin2bn failed.\n");
            rv = 1;
            goto End;
        };
    }

    bnOrder = BN_new();
    if (!bnOrder || !EC_GROUP_get_order(ecgrp, bnOrder, bnCtx))
    {
        printf("StealthSecretSpend(): EC_GROUP_get_order failed.\n");
        rv = 1;
        goto End;
    };

    bnSpend = BN_new();
    if (!bnSpend || !BN_bin2bn(&spendSecret.e[0], ec_secret_size, bnSpend))
    {
        printf("StealthSecretSpend(): bnSpend BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };

    if (!BN_mod_add(bnSpend, bnSpend, bnc, bnOrder, bnCtx))
    {
        printf("StealthSecretSpend(): BN_mod_add failed.\n");
        rv = 1;
        goto End;
    };

    if (BN_is_zero(bnSpend))
    {
        printf("StealthSecretSpend(): bnSpend is zero.\n");
        rv = 1;
        goto End;
    };

    // BN_bn2binpad (OpenSSL 1.1+) zero-pads to the exact length,
    // which is safer than BN_bn2bin when BN_num_bytes < ec_secret_size.
    if (BN_bn2binpad(bnSpend, &secretOut.e[0], ec_secret_size) != (int)ec_secret_size)
    {
        printf("StealthSecretSpend(): BN_bn2binpad failed.\n");
        rv = 1;
        goto End;
    };

    End:
    if (bnSpend)      BN_free(bnSpend);
    if (bnOrder)      BN_free(bnOrder);
    if (bnc)          BN_free(bnc);
    if (P)            EC_POINT_free(P);
    if (bnScanSecret) BN_free(bnScanSecret);
    if (bnCtx)        BN_CTX_free(bnCtx);
    EC_GROUP_free(ecgrp);

    return rv;
};


int StealthSharedToSecretSpend(ec_secret& sharedS, ec_secret& spendSecret, ec_secret& secretOut)
{
    int rv = 0;

    BN_CTX*  bnCtx   = NULL;
    BIGNUM*  bnc     = NULL;
    BIGNUM*  bnOrder = NULL;
    BIGNUM*  bnSpend = NULL;

    EC_GROUP* ecgrp = EC_GROUP_new_by_curve_name(NID_secp256k1);
    if (!ecgrp)
    {
        printf("StealthSharedToSecretSpend(): EC_GROUP_new_by_curve_name failed.\n");
        return 1;
    };

    if (!(bnCtx = BN_CTX_new()))
    {
        printf("StealthSharedToSecretSpend(): BN_CTX_new failed.\n");
        rv = 1;
        goto End;
    };

    bnc = BN_new();
    if (!bnc || !BN_bin2bn(&sharedS.e[0], ec_secret_size, bnc))
    {
        printf("StealthSharedToSecretSpend(): bnc BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };

    bnOrder = BN_new();
    if (!bnOrder || !EC_GROUP_get_order(ecgrp, bnOrder, bnCtx))
    {
        printf("StealthSharedToSecretSpend(): EC_GROUP_get_order failed.\n");
        rv = 1;
        goto End;
    };

    bnSpend = BN_new();
    if (!bnSpend || !BN_bin2bn(&spendSecret.e[0], ec_secret_size, bnSpend))
    {
        printf("StealthSharedToSecretSpend(): bnSpend BN_bin2bn failed.\n");
        rv = 1;
        goto End;
    };

    if (!BN_mod_add(bnSpend, bnSpend, bnc, bnOrder, bnCtx))
    {
        printf("StealthSharedToSecretSpend(): BN_mod_add failed.\n");
        rv = 1;
        goto End;
    };

    if (BN_is_zero(bnSpend))
    {
        printf("StealthSharedToSecretSpend(): bnSpend is zero.\n");
        rv = 1;
        goto End;
    };

    if (BN_bn2binpad(bnSpend, &secretOut.e[0], ec_secret_size) != (int)ec_secret_size)
    {
        printf("StealthSharedToSecretSpend(): BN_bn2binpad failed.\n");
        rv = 1;
        goto End;
    };

    End:
    if (bnSpend)  BN_free(bnSpend);
    if (bnOrder)  BN_free(bnOrder);
    if (bnc)      BN_free(bnc);
    if (bnCtx)    BN_CTX_free(bnCtx);
    EC_GROUP_free(ecgrp);

    return rv;
};


// stealth addresses have the ticker suffix
bool IsStealthAddress(const std::string& qualAddress)
{
    std::string encodedAddress;

#if USE_QUALIFIED_ADDRESSES
    int nColor;
    if (!SplitQualifiedAddress(qualAddress, encodedAddress, nColor, fDebug))
    {
        if (fDebug)
            printf("StealthAddress::IsStealthAddress: could not split address.\n");
        return false;
    }
#else
    encodedAddress = qualAddress;
#endif

    data_chunk raw;

    if (!DecodeBase58(encodedAddress, raw))
    {
        if (fDebug)
            printf("IsStealthAddress: DecodeBase58 failed.\n");
        return false;
    };

    if (!VerifyChecksum(raw))
    {
        if (fDebug)
            printf("IsStealthAddress: verify_checksum failed.\n");
        return false;
    };

    if (raw.size() < N_COLOR_BYTES + 1 + 1 + 33 + 1 + 33 + 1 + 1 + 4)
    {
        if (fDebug)
            printf("IsStealthAddress: too few bytes provided.\n");
        return false;
    };

    const uint8_t* p = &raw[0];
    int nColor = 0;
    for (int i = 0; i < N_COLOR_BYTES; ++i)
    {
        nColor += static_cast<int>(pow(256.0, i)) * static_cast<int>(*p);
        ++p;
    }

    if (!CheckColor(nColor))
    {
        if (fDebug)
            printf("IsStealthAddress: Invalid currency %d.\n", nColor);
    }

    uint8_t version = *p++;

    if (version != stealth_version_byte)
    {
        if (fDebug)
            printf("IsStealthAddress version mismatch 0x%x != 0x%x.\n",
                   version, stealth_version_byte);
        return false;
    };

    return true;
};
