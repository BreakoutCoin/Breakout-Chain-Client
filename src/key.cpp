// Copyright (c) 2009-2017 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <openssl/evp.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/obj_mac.h>
#include <openssl/param_build.h>
#include <openssl/core_names.h>

#include "key.h"


// anonymous namespace with local implementation code (OpenSSL interaction)
namespace {

//
// RAII helpers
//

struct BnCtx {
    BN_CTX* ctx;
    BnCtx() : ctx(BN_CTX_new()) {
        if (!ctx) throw std::runtime_error("BN_CTX_new failed");
        BN_CTX_start(ctx);
    }
    ~BnCtx() { BN_CTX_end(ctx); BN_CTX_free(ctx); }
    BnCtx(const BnCtx&) = delete;
    BnCtx& operator=(const BnCtx&) = delete;
    operator BN_CTX*() { return ctx; }
};

struct Bn {
    BIGNUM* bn;
    Bn() : bn(BN_new()) { if (!bn) throw std::runtime_error("BN_new failed"); }
    explicit Bn(BIGNUM* b) : bn(b) { if (!bn) throw std::runtime_error("Bn: null BIGNUM"); }
    ~Bn() { BN_clear_free(bn); }
    Bn(const Bn&) = delete;
    Bn& operator=(const Bn&) = delete;
    operator BIGNUM*() { return bn; }
};

struct EcGroup {
    EC_GROUP* group;
    explicit EcGroup(int nid) : group(EC_GROUP_new_by_curve_name(nid)) {
        if (!group) throw std::runtime_error("EC_GROUP_new_by_curve_name failed");
    }
    ~EcGroup() { EC_GROUP_free(group); }
    EcGroup(const EcGroup&) = delete;
    EcGroup& operator=(const EcGroup&) = delete;
    operator EC_GROUP*() { return group; }
};

struct EcPoint {
    EC_POINT* point;
    explicit EcPoint(const EC_GROUP* group) : point(EC_POINT_new(group)) {
        if (!point) throw std::runtime_error("EC_POINT_new failed");
    }
    ~EcPoint() { EC_POINT_free(point); }
    EcPoint(const EcPoint&) = delete;
    EcPoint& operator=(const EcPoint&) = delete;
    operator EC_POINT*() { return point; }
};

struct EvpPkey {
    EVP_PKEY* pkey;
    EvpPkey() : pkey(nullptr) {}
    explicit EvpPkey(EVP_PKEY* p) : pkey(p) {}
    // Move-only
    EvpPkey(EvpPkey&& o) : pkey(o.pkey) { o.pkey = nullptr; }
    EvpPkey& operator=(EvpPkey&& o) {
        if (this != &o) { EVP_PKEY_free(pkey); pkey = o.pkey; o.pkey = nullptr; }
        return *this;
    }
    ~EvpPkey() { EVP_PKEY_free(pkey); }
    EvpPkey(const EvpPkey&) = delete;
    EvpPkey& operator=(const EvpPkey&) = delete;
    operator EVP_PKEY*() const { return pkey; }
};

struct EvpPkeyCtx {
    EVP_PKEY_CTX* ctx;
    explicit EvpPkeyCtx(EVP_PKEY_CTX* c) : ctx(c) {
        if (!ctx) throw std::runtime_error("EVP_PKEY_CTX creation failed");
    }
    ~EvpPkeyCtx() { EVP_PKEY_CTX_free(ctx); }
    EvpPkeyCtx(const EvpPkeyCtx&) = delete;
    EvpPkeyCtx& operator=(const EvpPkeyCtx&) = delete;
    operator EVP_PKEY_CTX*() { return ctx; }
};

struct EvpMdCtx {
    EVP_MD_CTX* ctx;
    EvpMdCtx() : ctx(EVP_MD_CTX_new()) {
        if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    ~EvpMdCtx() { EVP_MD_CTX_free(ctx); }
    EvpMdCtx(const EvpMdCtx&) = delete;
    EvpMdCtx& operator=(const EvpMdCtx&) = delete;
    operator EVP_MD_CTX*() { return ctx; }
};

struct OsslParamBld {
    OSSL_PARAM_BLD* bld;
    OsslParamBld() : bld(OSSL_PARAM_BLD_new()) {
        if (!bld) throw std::runtime_error("OSSL_PARAM_BLD_new failed");
    }
    ~OsslParamBld() { OSSL_PARAM_BLD_free(bld); }
    OsslParamBld(const OsslParamBld&) = delete;
    OsslParamBld& operator=(const OsslParamBld&) = delete;
    operator OSSL_PARAM_BLD*() { return bld; }
};

struct OsslParams {
    OSSL_PARAM* params;
    explicit OsslParams(OSSL_PARAM* p) : params(p) {
        if (!params) throw std::runtime_error("OSSL_PARAM_BLD_to_param failed");
    }
    ~OsslParams() { OSSL_PARAM_free(params); }
    OsslParams(const OsslParams&) = delete;
    OsslParams& operator=(const OsslParams&) = delete;
    operator OSSL_PARAM*() { return params; }
};

struct EcdsaSig {
    ECDSA_SIG* sig;
    // Throwing constructor: use for ECDSA_SIG_new() where nullptr means OOM.
    EcdsaSig() : sig(ECDSA_SIG_new()) {
        if (!sig) throw std::runtime_error("ECDSA_SIG_new failed");
    }
    // Non-throwing constructor: use when parsing untrusted input via d2i_ECDSA_SIG,
    // where nullptr means a malformed signature rather than an OOM condition.
    // Caller must check sig != nullptr before use.
    struct from_untrusted_t {};
    static constexpr from_untrusted_t from_untrusted{};
    explicit EcdsaSig(from_untrusted_t, ECDSA_SIG* s) noexcept : sig(s) {}
    ~EcdsaSig() { ECDSA_SIG_free(sig); }
    EcdsaSig(const EcdsaSig&) = delete;
    EcdsaSig& operator=(const EcdsaSig&) = delete;
    operator ECDSA_SIG*() { return sig; }
};

//
// Build an EVP_PKEY for secp256k1 from a raw 32-byte private key scalar.
//
// point_format controls the point-conversion form recorded on the resulting
// EVP_PKEY ("uncompressed" or "compressed"). This matters when the caller will
// later serialise the key via i2d_PrivateKey: the encoder embeds the public
// point in this form. For pure in-memory use (signing, verifying, deriving
// pubkey bytes manually) the value is irrelevant, so it defaults to nullptr
// (which means "don't set it"; the provider default is "uncompressed").
//
// Honoured since OpenSSL 3.0.8; for earlier 3.x the encoder ignored this and
// always wrote compressed. This code targets 3.6+, so behaviour is reliable.
//
static EvpPkey BuildPrivateKey(const unsigned char vch[32],
                               const char* point_format = nullptr)
{
    EcGroup group(NID_secp256k1);
    Bn bn_priv(BN_bin2bn(vch, 32, BN_new()));

    // Compute the public key point
    EcPoint pub_point(group);
    {
        BnCtx ctx;
        if (!EC_POINT_mul(group, pub_point, bn_priv, nullptr, nullptr, ctx))
            throw std::runtime_error("BuildPrivateKey: EC_POINT_mul failed");
    }

    // Serialise the public point in uncompressed form (the param-builder takes
    // a single canonical encoding; the OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT
    // param below is what controls the form on later export).
    unsigned char pub_buf[65];
    {
        BnCtx ctx;
        size_t pub_len = EC_POINT_point2oct(group, pub_point,
                                            POINT_CONVERSION_UNCOMPRESSED,
                                            pub_buf, sizeof(pub_buf), ctx);
        if (pub_len != 65)
            throw std::runtime_error("BuildPrivateKey: EC_POINT_point2oct failed");
    }

    OsslParamBld bld;
    if (!OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "secp256k1", 0) ||
        !OSSL_PARAM_BLD_push_BN(bld, OSSL_PKEY_PARAM_PRIV_KEY, bn_priv) ||
        !OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub_buf, 65))
        throw std::runtime_error("BuildPrivateKey: OSSL_PARAM_BLD_push failed");

    if (point_format &&
        !OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT,
                                         point_format, 0))
        throw std::runtime_error("BuildPrivateKey: OSSL_PARAM_BLD_push point-format failed");

    OsslParams params(OSSL_PARAM_BLD_to_param(bld));

    EvpPkeyCtx kctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_fromdata_init(kctx) <= 0 ||
        EVP_PKEY_fromdata(kctx, &raw, EVP_PKEY_KEYPAIR, params) <= 0)
        throw std::runtime_error("BuildPrivateKey: EVP_PKEY_fromdata failed");

    return EvpPkey(raw);
}

//
// Build an EVP_PKEY for secp256k1 from a DER-encoded private key (SEC1 or PKCS8).
//
static EvpPkey BuildPrivateKeyFromDER(const unsigned char* der, size_t derlen)
{
    const unsigned char* p = der;
    EVP_PKEY* raw = d2i_PrivateKey(EVP_PKEY_EC, nullptr, &p, static_cast<long>(derlen));
    if (!raw)
        throw std::runtime_error("BuildPrivateKeyFromDER: d2i_PrivateKey failed");
    return EvpPkey(raw);
}

//
// Build an EVP_PKEY for secp256k1 from raw public key bytes (33 compressed or 65 uncompressed).
//
static EvpPkey BuildPublicKey(const unsigned char* pub, size_t publen)
{
    OsslParamBld bld;
    if (!OSSL_PARAM_BLD_push_utf8_string(bld, OSSL_PKEY_PARAM_GROUP_NAME, "secp256k1", 0) ||
        !OSSL_PARAM_BLD_push_octet_string(bld, OSSL_PKEY_PARAM_PUB_KEY, pub, publen))
        throw std::runtime_error("BuildPublicKey: OSSL_PARAM_BLD_push failed");

    OsslParams params(OSSL_PARAM_BLD_to_param(bld));

    EvpPkeyCtx kctx(EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr));
    EVP_PKEY* raw = nullptr;
    if (EVP_PKEY_fromdata_init(kctx) <= 0 ||
        EVP_PKEY_fromdata(kctx, &raw, EVP_PKEY_PUBLIC_KEY, params) <= 0)
        throw std::runtime_error("BuildPublicKey: EVP_PKEY_fromdata failed");

    return EvpPkey(raw);
}

//
// Extract the raw 32-byte private scalar from an EVP_PKEY.
//
static void GetPrivScalar(EVP_PKEY* pkey, unsigned char vch[32])
{
    BIGNUM* bn = nullptr;
    if (!EVP_PKEY_get_bn_param(pkey, OSSL_PKEY_PARAM_PRIV_KEY, &bn))
        throw std::runtime_error("GetPrivScalar: EVP_PKEY_get_bn_param failed");
    Bn priv(bn); // takes ownership
    int nBytes = BN_num_bytes(priv);
    memset(vch, 0, 32);
    BN_bn2bin(priv, &vch[32 - nBytes]);
}

//
// Extract public key bytes from an EVP_PKEY in compressed or uncompressed form.
// out must be at least 65 bytes. Returns the actual byte length written.
//
static size_t GetPubKeyBytes(EVP_PKEY* pkey, unsigned char out[65], bool fCompressed)
{
    // First fetch the raw public key bytes (OpenSSL gives uncompressed by default)
    unsigned char raw[65];
    size_t rawlen = sizeof(raw);
    if (!EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY,
                                         raw, sizeof(raw), &rawlen))
        throw std::runtime_error("GetPubKeyBytes: EVP_PKEY_get_octet_string_param failed");

    if (!fCompressed) {
        memcpy(out, raw, rawlen);
        return rawlen;
    }

    // Re-encode as compressed via EC_POINT
    EcGroup group(NID_secp256k1);
    BnCtx ctx;
    EcPoint point(group);
    if (!EC_POINT_oct2point(group, point, raw, rawlen, ctx))
        throw std::runtime_error("GetPubKeyBytes: EC_POINT_oct2point failed");
    size_t len = EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED, out, 33, ctx);
    if (len != 33)
        throw std::runtime_error("GetPubKeyBytes: EC_POINT_point2oct compressed failed");
    return len;
}

//
// Perform ECDSA key recovery (SEC1 4.1.6) for secp256k1.
// On success, writes the recovered uncompressed public key (65 bytes) to out_pub
// and sets out_pub_len = 65. Returns true on success.
//
static bool RecoverPublicKey(const unsigned char* msg, int msglen,
                             const ECDSA_SIG* ecsig, int recid, bool check,
                             unsigned char out_pub[65], size_t& out_pub_len)
{
    const BIGNUM* sig_r;
    const BIGNUM* sig_s;
    ECDSA_SIG_get0(ecsig, &sig_r, &sig_s);

    EcGroup group(NID_secp256k1);
    BnCtx ctx;

    BIGNUM* order  = BN_CTX_get(ctx);
    BIGNUM* field  = BN_CTX_get(ctx);
    BIGNUM* x      = BN_CTX_get(ctx);
    BIGNUM* e      = BN_CTX_get(ctx);
    BIGNUM* zero   = BN_CTX_get(ctx);
    BIGNUM* rr     = BN_CTX_get(ctx);
    BIGNUM* sor    = BN_CTX_get(ctx);
    BIGNUM* eor    = BN_CTX_get(ctx);
    if (!eor) return false;

    int i = recid / 2;
    int n = EC_GROUP_get_degree(group);

    if (!EC_GROUP_get_order(group, order, ctx))                        return false;
    if (!BN_copy(x, order))                                            return false;
    if (!BN_mul_word(x, i))                                            return false;
    if (!BN_add(x, x, sig_r))                                         return false;
    if (!EC_GROUP_get_curve(group, field, nullptr, nullptr, ctx))      return false;
    if (BN_cmp(x, field) >= 0)                                        return false;

    EcPoint R(group);
    if (!EC_POINT_set_compressed_coordinates(group, R, x, recid % 2, ctx)) return false;

    if (check) {
        EcPoint O(group);
        if (!EC_POINT_mul(group, O, nullptr, R, order, ctx))          return false;
        if (!EC_POINT_is_at_infinity(group, O))                        return false;
    }

    EcPoint Q(group);
    if (!BN_bin2bn(msg, msglen, e))                                    return false;
    if (8 * msglen > n) BN_rshift(e, e, 8 - (n & 7));
    BN_zero(zero);
    if (!BN_mod_sub(e, zero, e, order, ctx))                           return false;
    if (!BN_mod_inverse(rr, sig_r, order, ctx))                        return false;
    if (!BN_mod_mul(sor, sig_s, rr, order, ctx))                       return false;
    if (!BN_mod_mul(eor, e, rr, order, ctx))                           return false;
    if (!EC_POINT_mul(group, Q, eor, R, sor, ctx))                     return false;

    out_pub_len = EC_POINT_point2oct(group, Q, POINT_CONVERSION_UNCOMPRESSED,
                                     out_pub, 65, ctx);
    return out_pub_len == 65;
}


//
// CECKey: wraps an EVP_PKEY secp256k1 key pair using the OpenSSL 3.0 EVP API.
//
class CECKey {
private:
    EvpPkey pkey;

public:
    CECKey() : pkey(nullptr) {}

    void GetSecretBytes(unsigned char vch[32]) const {
        GetPrivScalar(pkey.pkey, vch);
    }

    void SetSecretBytes(const unsigned char vch[32]) {
        pkey = BuildPrivateKey(vch);
    }

    void GetPrivKey(CPrivKey& privkey, bool fCompressed) {
        // Re-encode the public key in the requested compression form so that
        // the embedded public key in the SEC1 ECPrivateKey structure matches
        // the compression form the caller expects. Without this, round-tripping
        // through GetPrivKey/SetPrivKey loses the compression form.
        //
        // Strategy: extract the raw private scalar and build a fresh EVP_PKEY
        // that carries the OSSL_PKEY_PARAM_EC_POINT_CONVERSION_FORMAT param set
        // to the requested form. i2d_PrivateKey on this key produces SEC1
        // ECPrivateKey DER with the embedded public point encoded in that form.
        // (The encoder honours this param since OpenSSL 3.0.8.)
        unsigned char priv_buf[32];
        GetPrivScalar(pkey.pkey, priv_buf);

        EvpPkey tmp;
        try {
            tmp = BuildPrivateKey(priv_buf,
                                  fCompressed ? "compressed" : "uncompressed");
        } catch (...) {
            OPENSSL_cleanse(priv_buf, sizeof(priv_buf));
            throw;
        }
        OPENSSL_cleanse(priv_buf, sizeof(priv_buf));

        int nSize = i2d_PrivateKey(tmp, nullptr);
        if (nSize <= 0)
            throw std::runtime_error("CECKey::GetPrivKey: i2d_PrivateKey sizing failed");
        privkey.resize(nSize);
        unsigned char* pbegin = &privkey[0];
        int nSize2 = i2d_PrivateKey(tmp, &pbegin);
        assert(nSize == nSize2);
    }

    bool SetPrivKey(const CPrivKey& privkey, bool fSkipCheck) {
        try {
            pkey = BuildPrivateKeyFromDER(&privkey[0], privkey.size());
        } catch (...) {
            return false;
        }
        if (fSkipCheck)
            return true;
        EvpPkeyCtx kctx(EVP_PKEY_CTX_new_from_pkey(nullptr, pkey, nullptr));
        return EVP_PKEY_check(kctx) == 1;
    }

    void GetPubKey(CPubKey& pubkey, bool fCompressed) {
        unsigned char c[65];
        size_t nSize = GetPubKeyBytes(pkey.pkey, c, fCompressed);
        pubkey.Set(&c[0], &c[nSize]);
    }

    bool SetPubKey(const CPubKey& pubkey) {
        try {
            pkey = BuildPublicKey(pubkey.begin(), pubkey.size());
            return true;
        } catch (...) {
            return false;
        }
    }

    bool Sign(const uint256& hash, valtype& vchSig) {
        vchSig.clear();

        EvpPkeyCtx pctx(EVP_PKEY_CTX_new(pkey, nullptr));
        if (EVP_PKEY_sign_init(pctx) <= 0)
            return false;

        const unsigned char* msg = reinterpret_cast<const unsigned char*>(&hash);
        size_t siglen = 0;
        if (EVP_PKEY_sign(pctx, nullptr, &siglen, msg, sizeof(hash)) <= 0)
            return false;

        vchSig.resize(siglen);
        if (EVP_PKEY_sign(pctx, &vchSig[0], &siglen, msg, sizeof(hash)) <= 0) {
            vchSig.clear();
            return false;
        }
        vchSig.resize(siglen);

        // Enforce low-S normalisation
        {
            const unsigned char* sigptr = &vchSig[0];
            EcdsaSig sig(EcdsaSig::from_untrusted, d2i_ECDSA_SIG(nullptr, &sigptr, vchSig.size()));
            if (!sig.sig) return false;

            const BIGNUM* sig_r;
            const BIGNUM* sig_s;
            ECDSA_SIG_get0(sig, &sig_r, &sig_s);

            EcGroup group(NID_secp256k1);
            BnCtx ctx;
            Bn order, halforder;
            EC_GROUP_get_order(group, order, ctx);
            BN_rshift1(halforder, order);

            if (BN_cmp(sig_s, halforder) > 0) {
                Bn new_r, new_s;
                BN_copy(new_r, sig_r);
                BN_sub(new_s, order, sig_s);
                ECDSA_SIG_set0(sig, new_r.bn, new_s.bn);
                new_r.bn = nullptr; // ownership transferred to sig
                new_s.bn = nullptr;
            }

            unsigned char* pos = nullptr;
            int nSize = i2d_ECDSA_SIG(sig, &pos);
            if (nSize <= 0) return false;
            vchSig.assign(pos, pos + nSize);
            OPENSSL_free(pos);
        }

        return true;
    }

    bool Verify(const uint256& hash, const valtype& vchSigParam)
    {
        // Strip non-canonical extra length bytes
        valtype vchSig(vchSigParam.begin(), vchSigParam.end());
        if (vchSig.size() > 1 && vchSig[1] & 0x80)
        {
            unsigned char nLengthBytes = vchSig[1] & 0x7f;
            if (static_cast<int>(vchSig.size()) < 2 + nLengthBytes)
            {
                return false;
            }
            if (nLengthBytes > 4)
            {
                unsigned char nExtraBytes = nLengthBytes - 4;
                for (unsigned char i = 0; i < nExtraBytes; i++)
                {
                    if (vchSig[2 + i])
                    {
                        return false;
                    }
                }
                vchSig.erase(vchSig.begin() + 2,
                             vchSig.begin() + 2 + nExtraBytes);
                vchSig[1] = 0x80 | (nLengthBytes - nExtraBytes);
            }
        }
        if (vchSig.empty())
        {
            return false;
        }

        // Re-serialise to canonical DER
        const unsigned char* sigptr = &vchSig[0];
        EcdsaSig norm_sig(EcdsaSig::from_untrusted,
                          d2i_ECDSA_SIG(nullptr, &sigptr, vchSig.size()));
        if (!norm_sig.sig)
        {
            return false;
        }

        unsigned char* norm_der = nullptr;
        int derlen = i2d_ECDSA_SIG(norm_sig, &norm_der);
        if (derlen <= 0) return false;

        EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new(pkey, nullptr);
        bool ret = false;
        if (pctx)
        {
            if (EVP_PKEY_verify_init(pctx) > 0)
            {
                ret = EVP_PKEY_verify(pctx,
                                      norm_der,
                                      derlen,
                                      reinterpret_cast<const unsigned char*>(
                                          &hash),
                                      sizeof(hash)) == 1;
            }
            EVP_PKEY_CTX_free(pctx);
        }
        OPENSSL_free(norm_der);
        return ret;
    }

    bool SignCompact(const uint256& hash, unsigned char* p64, int& rec) {
        valtype vchSig;
        if (!Sign(hash, vchSig))
            return false;

        const unsigned char* sigptr = &vchSig[0];
        EcdsaSig sig(EcdsaSig::from_untrusted, d2i_ECDSA_SIG(nullptr, &sigptr, vchSig.size()));

        const BIGNUM* sig_r;
        const BIGNUM* sig_s;
        ECDSA_SIG_get0(sig, &sig_r, &sig_s);

        int nBitsR = BN_num_bits(sig_r);
        int nBitsS = BN_num_bits(sig_s);
        if (nBitsR > 256 || nBitsS > 256)
            return false;

        // Get our own compressed public key to match against recovered candidates
        CPubKey pubkey;
        GetPubKey(pubkey, true);

        const unsigned char* msg = reinterpret_cast<const unsigned char*>(&hash);
        bool fOk = false;
        for (int i = 0; i < 4; i++) {
            unsigned char rec_pub[65];
            size_t rec_pub_len = 0;
            if (!RecoverPublicKey(msg, sizeof(hash), sig, i, true, rec_pub, rec_pub_len))
                continue;

            // Compress the recovered point for comparison
            unsigned char rec_compressed[33];
            {
                EcGroup group(NID_secp256k1);
                BnCtx ctx;
                EcPoint point(group);
                EC_POINT_oct2point(group, point, rec_pub, rec_pub_len, ctx);
                EC_POINT_point2oct(group, point, POINT_CONVERSION_COMPRESSED,
                                   rec_compressed, 33, ctx);
            }
            CPubKey pubkeyRec;
            pubkeyRec.Set(rec_compressed, rec_compressed + 33);
            if (pubkeyRec == pubkey) {
                rec = i;
                fOk = true;
                break;
            }
        }

        if (!fOk) return false;
        memset(p64, 0, 64);
        BN_bn2bin(sig_r, &p64[32 - (nBitsR + 7) / 8]);
        BN_bn2bin(sig_s, &p64[64 - (nBitsS + 7) / 8]);
        return true;
    }

    bool Recover(const uint256& hash, const unsigned char* p64, int rec) {
        if (rec < 0 || rec >= 4)
            return false;

        EcdsaSig sig;
        Bn sig_r(BN_bin2bn(&p64[0],  32, BN_new()));
        Bn sig_s(BN_bin2bn(&p64[32], 32, BN_new()));
        ECDSA_SIG_set0(sig, sig_r.bn, sig_s.bn);
        sig_r.bn = nullptr; // ownership transferred to sig
        sig_s.bn = nullptr;

        unsigned char pub_buf[65];
        size_t pub_len = 0;
        if (!RecoverPublicKey(reinterpret_cast<const unsigned char*>(&hash),
                              sizeof(hash), sig, rec, false, pub_buf, pub_len))
            return false;

        try {
            pkey = BuildPublicKey(pub_buf, pub_len);
        } catch (...) {
            return false;
        }
        return true;
    }

    static bool TweakSecret(unsigned char vchSecretOut[32],
                            const unsigned char vchSecretIn[32],
                            const unsigned char vchTweak[32])
    {
        try {
            BnCtx ctx;
            EcGroup group(NID_secp256k1);
            Bn bnOrder;
            EC_GROUP_get_order(group, bnOrder, ctx);

            Bn bnTweak(BN_bin2bn(vchTweak, 32, BN_new()));
            if (BN_cmp(bnTweak, bnOrder) >= 0)
                return false; // extremely unlikely

            Bn bnSecret(BN_bin2bn(vchSecretIn, 32, BN_new()));
            BN_add(bnSecret, bnSecret, bnTweak);
            BN_nnmod(bnSecret, bnSecret, bnOrder, ctx);
            if (BN_is_zero(bnSecret))
                return false; // ridiculously unlikely

            int nBits = BN_num_bits(bnSecret);
            memset(vchSecretOut, 0, 32);
            BN_bn2bin(bnSecret, &vchSecretOut[32 - (nBits + 7) / 8]);
            return true;
        } catch (...) {
            return false;
        }
    }

    bool TweakPublic(const unsigned char vchTweak[32]) {
        try {
            BnCtx ctx;
            EcGroup group(NID_secp256k1);
            Bn bnOrder;
            EC_GROUP_get_order(group, bnOrder, ctx);

            Bn bnTweak(BN_bin2bn(vchTweak, 32, BN_new()));
            if (BN_cmp(bnTweak, bnOrder) >= 0)
                return false; // extremely unlikely

            // Extract current public point
            unsigned char pub_buf[65];
            size_t pub_len = sizeof(pub_buf);
            if (!EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY,
                                                 pub_buf, sizeof(pub_buf), &pub_len))
                return false;

            EcPoint point(group);
            if (!EC_POINT_oct2point(group, point, pub_buf, pub_len, ctx))
                return false;

            // point = tweak*G + 1*point
            Bn bnOne;
            BN_one(bnOne);
            if (!EC_POINT_mul(group, point, bnTweak, point, bnOne, ctx))
                return false;
            if (EC_POINT_is_at_infinity(group, point))
                return false;

            // Re-encode and rebuild the EVP_PKEY
            unsigned char new_pub[65];
            size_t new_pub_len = EC_POINT_point2oct(group, point,
                                                    POINT_CONVERSION_UNCOMPRESSED,
                                                    new_pub, sizeof(new_pub), ctx);
            if (new_pub_len != 65)
                return false;

            pkey = BuildPublicKey(new_pub, new_pub_len);
            return true;
        } catch (...) {
            return false;
        }
    }
};


int CompareBigEndian(const unsigned char* c1, size_t c1len,
                     const unsigned char* c2, size_t c2len)
{
    while (c1len > c2len) { if (*c1) return  1; c1++; c1len--; }
    while (c2len > c1len) { if (*c2) return -1; c2++; c2len--; }
    while (c1len > 0) {
        if (*c1 > *c2) return  1;
        if (*c2 > *c1) return -1;
        c1++; c2++; c1len--;
    }
    return 0;
}

// Order of secp256k1's generator minus 1.
const unsigned char vchMaxModOrder[32] = {
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
    0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,
    0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x40
};

// Half of the order of secp256k1's generator minus 1.
const unsigned char vchMaxModHalfOrder[32] = {
    0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
    0x5D,0x57,0x6E,0x73,0x57,0xA4,0x50,0x1D,
    0xDF,0xE9,0x2F,0x46,0x68,0x1B,0x20,0xA0
};

const unsigned char vchZero[0] = {};

} // end of anonymous namespace


//
// CKey / CPubKey / CExtKey implementations
//

bool CKey::SetSecret(const CSecret& vchSecret, bool fCompressedIn)
{
    if (vchSecret.size() != 32)
        throw key_error("CKey::SetSecret() : secret must be 32 bytes");
    memcpy(vch, &vchSecret[0], 32);
    fValid = Check(vch);
    fCompressed = fCompressedIn;
    return fValid;
}

bool CKey::Check(const unsigned char* vch) {
    static const unsigned char vchMax[32] = {
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFE,
        0xBA,0xAE,0xDC,0xE6,0xAF,0x48,0xA0,0x3B,
        0xBF,0xD2,0x5E,0x8C,0xD0,0x36,0x41,0x40
    };
    bool fIsZero = true;
    for (int i = 0; i < 32 && fIsZero; i++)
        if (vch[i] != 0) fIsZero = false;
    if (fIsZero) return false;
    for (int i = 0; i < 32; i++) {
        if (vch[i] < vchMax[i]) return true;
        if (vch[i] > vchMax[i]) return false;
    }
    return true;
}

bool CKey::CheckSignatureElement(const unsigned char* vch, int len, bool half) {
    return CompareBigEndian(vch, len, vchZero, 0) > 0 &&
           CompareBigEndian(vch, len, half ? vchMaxModHalfOrder : vchMaxModOrder, 32) <= 0;
}

bool CKey::ReserealizeSignature(valtype& vchSig) {
    if (vchSig.empty())
        return false;
    const unsigned char* pos = &vchSig[0];
    EcdsaSig sig(EcdsaSig::from_untrusted, d2i_ECDSA_SIG(nullptr, &pos, vchSig.size()));
    if (!sig.sig) return false;
    int nSize = i2d_ECDSA_SIG(sig, nullptr);
    if (nSize <= 0) return false;
    vchSig.resize(nSize);
    unsigned char* wpos = &vchSig[0];
    i2d_ECDSA_SIG(sig, &wpos);
    return true;
}

void CKey::MakeNewKey(bool fCompressedIn) {
    do {
        RAND_bytes(vch, sizeof(vch));
    } while (!Check(vch));
    fValid = true;
    fCompressed = fCompressedIn;
}

bool CKey::SetPrivKey(const CPrivKey& privkey, bool fCompressedIn) {
    CECKey key;
    if (!key.SetPrivKey(privkey, false))
        return false;
    key.GetSecretBytes(vch);
    fCompressed = fCompressedIn;
    fValid = true;
    return true;
}

CPrivKey CKey::GetPrivKey() const {
    assert(fValid);
    CECKey key;
    key.SetSecretBytes(vch);
    CPrivKey privkey;
    key.GetPrivKey(privkey, fCompressed);
    return privkey;
}

CPubKey CKey::GetPubKey() const
{
    assert(fValid);
    CECKey key;
    key.SetSecretBytes(vch);
    CPubKey pubkey;
    key.GetPubKey(pubkey, fCompressed);
    return pubkey;
}

bool CKey::Sign(const uint256& hash, valtype& vchSig) const
{
    if (!fValid)
    {
        return false;
    }
    CECKey key;
    key.SetSecretBytes(vch);
    return key.Sign(hash, vchSig);
}

bool CKey::SignCompact(const uint256& hash, valtype& vchSig) const
{
    if (!fValid)
    {
        return false;
    }
    CECKey key;
    key.SetSecretBytes(vch);
    vchSig.resize(65);
    int rec = -1;
    if (!key.SignCompact(hash, &vchSig[1], rec))
    {
        return false;
    }
    assert(rec != -1);
    vchSig[0] = 27 + rec + (fCompressed ? 4 : 0);
    return true;
}

bool CKey::Load(CPrivKey& privkey, CPubKey& vchPubKey, bool fSkipCheck)
{
    CECKey key;
    if (!key.SetPrivKey(privkey, fSkipCheck))
    {
        return false;
    }
    key.GetSecretBytes(vch);
    fCompressed = vchPubKey.IsCompressed();
    fValid = true;
    if (fSkipCheck)
    {
        return true;
    }
    if (GetPubKey() != vchPubKey)
    {
        return false;
    }
    return true;
}

bool CPubKey::Verify(const uint256& hash, const valtype& vchSig) const
{
    if (!IsValid())
    {
        return false;
    }
    CECKey key;
    if (!key.SetPubKey(*this))
    {
        return false;
    }
    return key.Verify(hash, vchSig);
}

bool CPubKey::RecoverCompact(const uint256& hash, const valtype& vchSig) {
    if (vchSig.size() != 65) return false;
    CECKey key;
    if (!key.Recover(hash, &vchSig[1], (vchSig[0] - 27) & ~4))
        return false;
    key.GetPubKey(*this, (vchSig[0] - 27) & 4);
    return true;
}

bool CPubKey::VerifyCompact(const uint256& hash, const valtype& vchSig) const {
    if (!IsValid()) return false;
    if (vchSig.size() != 65) return false;
    CECKey key;
    if (!key.Recover(hash, &vchSig[1], (vchSig[0] - 27) & ~4))
        return false;
    CPubKey pubkeyRec;
    key.GetPubKey(pubkeyRec, IsCompressed());
    return *this == pubkeyRec;
}

bool CPubKey::IsFullyValid() const {
    if (!IsValid()) return false;
    CECKey key;
    return key.SetPubKey(*this);
}

bool CPubKey::Decompress() {
    if (!IsValid()) return false;
    CECKey key;
    if (!key.SetPubKey(*this)) return false;
    key.GetPubKey(*this, false);
    return true;
}

void static BIP32Hash(const unsigned char chainCode[32],
                      unsigned int nChild,
                      unsigned char header,
                      const unsigned char data[32],
                      unsigned char output[64])
{
    unsigned char num[4];
    num[0] = (nChild >> 24) & 0xFF;
    num[1] = (nChild >> 16) & 0xFF;
    num[2] = (nChild >>  8) & 0xFF;
    num[3] = (nChild >>  0) & 0xFF;

    CHMAC_SHA512(chainCode, 32)
        .Write(&header, 1)
        .Write(data, 32)
        .Write(num, 4)
        .Finalize(output);
}

bool CKey::Derive(CKey& keyChild, unsigned char ccChild[32], unsigned int nChild, const unsigned char cc[32]) const {
    assert(IsValid());
    assert(IsCompressed());
    unsigned char out[64];
    LockObject(out);
    if ((nChild >> 31) == 0) {
        CPubKey pubkey = GetPubKey();
        assert(pubkey.begin() + 33 == pubkey.end());
        BIP32Hash(cc, nChild, *pubkey.begin(), pubkey.begin() + 1, out);
    } else {
        assert(begin() + 32 == end());
        BIP32Hash(cc, nChild, 0, begin(), out);
    }
    memcpy(ccChild, out + 32, 32);
    // begin() returns const unsigned char*; const_cast is safe here because
    // keyChild.vch is our own writable buffer, not a truly const object.
    bool ret = CECKey::TweakSecret(const_cast<unsigned char*>(keyChild.begin()), begin(), out);
    UnlockObject(out);
    keyChild.fCompressed = true;
    keyChild.fValid = ret;
    return ret;
}

bool CPubKey::Derive(CPubKey& pubkeyChild, unsigned char ccChild[32], unsigned int nChild, const unsigned char cc[32]) const {
    assert(IsValid());
    assert((nChild >> 31) == 0);
    assert(begin() + 33 == end());
    unsigned char out[64];
    BIP32Hash(cc, nChild, *begin(), begin() + 1, out);
    memcpy(ccChild, out + 32, 32);
    CECKey key;
    bool ret = key.SetPubKey(*this);
    ret &= key.TweakPublic(out);
    key.GetPubKey(pubkeyChild, true);
    return ret;
}

bool CExtKey::Derive(CExtKey& out, unsigned int nChild) const {
    out.nDepth = nDepth + 1;
    CKeyID id = key.GetPubKey().GetID();
    memcpy(&out.vchFingerprint[0], &id, 4);
    out.nChild = nChild;
    return key.Derive(out.key, out.vchChainCode, nChild, vchChainCode);
}

void CExtKey::SetMaster(const unsigned char* seed, unsigned int nSeedLen) {
    static const char hashkey[] = {'B','i','t','c','o','i','n',' ','s','e','e','d'};
    unsigned char out[64];
    LockObject(out);
    CHMAC_SHA512(hashkey, sizeof(hashkey))
        .Write(seed, nSeedLen)
        .Finalize(out);
    key.Set(&out[0], &out[32], true);
    memcpy(vchChainCode, &out[32], 32);
    UnlockObject(out);
    nDepth = 0;
    nChild = 0;
    memset(vchFingerprint, 0, sizeof(vchFingerprint));
}

CExtPubKey CExtKey::Neuter() const {
    CExtPubKey ret;
    ret.nDepth = nDepth;
    memcpy(&ret.vchFingerprint[0], &vchFingerprint[0], 4);
    ret.nChild = nChild;
    ret.pubkey = key.GetPubKey();
    memcpy(&ret.vchChainCode[0], &vchChainCode[0], 32);
    return ret;
}

void CExtKey::Encode(unsigned char code[74]) const {
    code[0] = nDepth;
    memcpy(code + 1, vchFingerprint, 4);
    code[5] = (nChild >> 24) & 0xFF; code[6] = (nChild >> 16) & 0xFF;
    code[7] = (nChild >>  8) & 0xFF; code[8] = (nChild >>  0) & 0xFF;
    memcpy(code + 9, vchChainCode, 32);
    code[41] = 0;
    assert(key.size() == 32);
    memcpy(code + 42, key.begin(), 32);
}

void CExtKey::Decode(const unsigned char code[74]) {
    nDepth = code[0];
    memcpy(vchFingerprint, code + 1, 4);
    nChild = (code[5] << 24) | (code[6] << 16) | (code[7] << 8) | code[8];
    memcpy(vchChainCode, code + 9, 32);
    key.Set(code + 42, code + 74, true);
}

void CExtPubKey::Encode(unsigned char code[74]) const {
    code[0] = nDepth;
    memcpy(code + 1, vchFingerprint, 4);
    code[5] = (nChild >> 24) & 0xFF; code[6] = (nChild >> 16) & 0xFF;
    code[7] = (nChild >>  8) & 0xFF; code[8] = (nChild >>  0) & 0xFF;
    memcpy(code + 9, vchChainCode, 32);
    assert(pubkey.size() == 33);
    memcpy(code + 41, pubkey.begin(), 33);
}

void CExtPubKey::Decode(const unsigned char code[74]) {
    nDepth = code[0];
    memcpy(vchFingerprint, code + 1, 4);
    nChild = (code[5] << 24) | (code[6] << 16) | (code[7] << 8) | code[8];
    memcpy(vchChainCode, code + 9, 32);
    pubkey.Set(code + 41, code + 74);
}

bool CExtPubKey::Derive(CExtPubKey& out, unsigned int nChild) const {
    out.nDepth = nDepth + 1;
    CKeyID id = pubkey.GetID();
    memcpy(&out.vchFingerprint[0], &id, 4);
    out.nChild = nChild;
    return pubkey.Derive(out.pubkey, out.vchChainCode, nChild, vchChainCode);
}

bool ECC_InitSanityCheck() {
    // Verify secp256k1 is accessible via the EVP API
    try {
        unsigned char dummy[32] = {};
        dummy[31] = 1; // scalar = 1, valid non-zero key
        EvpPkey pkey = BuildPrivateKey(dummy);
        return pkey.pkey != nullptr;
    } catch (...) {
        return false;
    }
}

CKey PubKeyWithSecret(CSecret vchSecret, CPubKey& cpkT)
{
    CKey ckey;
    ckey.SetSecret(vchSecret, true);
    cpkT = ckey.GetPubKey();
    return ckey;
}
