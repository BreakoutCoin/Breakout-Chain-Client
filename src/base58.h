// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


//
// Why base-58 instead of standard base-64 encoding?
// - Don't want 0OIl characters that look the same in some fonts and
//      could be used to create visually identical looking account numbers.
// - A string with non-alphanumeric characters is not as easily accepted as an account number.
// - E-mail usually won't line-break if there's no punctuation to break at.
// - Double-clicking selects the whole number as one word if it's all alphanumeric.
//
#ifndef BITCOIN_BASE58_H
#define BITCOIN_BASE58_H

#include <string>
#include <vector>
#include "bignum.h"
#include "key.h"
#include "script.h"
#include "colors.h"

static const char* pszBase58 = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

const int PUBKEY_ADDRESS = 19;      // br(oc), bx
const int SCRIPT_ADDRESS = 1;       // 4B, 3X
// testnet
const int PUBKEY_ADDRESS_TEST = 6;  // Br, Bx
const int SCRIPT_ADDRESS_TEST = 4;  // 8B, 8X


inline bool GetVecColorID(int nVersion, int nColor, std::vector<unsigned char>* (&pVecColorID))
{
    if (!CheckColor(nColor))
    {
        if (fDebug)
        {
              printf("GetVecColorID(): Invalid currency number %d\n", nColor);
        }
        return false;
    }
    switch (nVersion)
    {
        case PUBKEY_ADDRESS :
                pVecColorID = &COLOR_ID[MAIN_PUBKEY_IDX][nColor];
                break;
        case SCRIPT_ADDRESS :
                pVecColorID = &COLOR_ID[MAIN_SCRIPT_IDX][nColor];
                break;
        case PUBKEY_ADDRESS_TEST :
                pVecColorID = &COLOR_ID[TEST_PUBKEY_IDX][nColor];
                break;
        case SCRIPT_ADDRESS_TEST :
                pVecColorID = &COLOR_ID[TEST_SCRIPT_IDX][nColor];
                break;
        default :
                if (fDebug)
                {
                      printf("GetVecColorID(): Did not recognize version byte %d\n", nVersion);
                }
                return false;
    }
    return true;
}


 
inline bool GetMapColorID(int nVersion, std::map<std::vector <unsigned char>, int >* &pMapVer) 
{ 
    switch (nVersion) 
    { 
        case PUBKEY_ADDRESS : 
                pMapVer = &MAPS_COLOR_ID[MAIN_PUBKEY_IDX]; 
                break;
        case SCRIPT_ADDRESS : 
                pMapVer = &MAPS_COLOR_ID[MAIN_SCRIPT_IDX]; 
                break;
        case PUBKEY_ADDRESS_TEST : 
                pMapVer = &MAPS_COLOR_ID[TEST_PUBKEY_IDX]; 
                break;
        case SCRIPT_ADDRESS_TEST : 
                pMapVer = &MAPS_COLOR_ID[TEST_SCRIPT_IDX]; 
                break;
        default : 
                if (fDebug) 
                { 
                        printf("GetMapVersion(): Did not recognize version byte %d\n", nVersion); 
                } 
                return false; 
    } 
    return true; 
} 



// Encode a byte sequence as a base58-encoded string
inline std::string EncodeBase58(const unsigned char* pbegin, const unsigned char* pend)
{
    CAutoBN_CTX pctx;
    CBigNum bn58 = 58;
    CBigNum bn0 = 0;

    // Convert big endian data to little endian
    // Extra zero at the end make sure bignum will interpret as a positive number
    std::vector<unsigned char> vchTmp(pend-pbegin+1, 0);
    reverse_copy(pbegin, pend, vchTmp.begin());

    // Convert little endian data to bignum
    CBigNum bn;
    bn.setvch(vchTmp);

    // Convert bignum to std::string
    std::string str;
    // Expected size increase from base58 conversion is approximately 137%
    // use 138% to be safe
    str.reserve((pend - pbegin) * 138 / 100 + 1);
    CBigNum dv;
    CBigNum rem;
    while (bn > bn0)
    {
        if (!BN_div(&dv, &rem, &bn, &bn58, pctx))
            throw bignum_error("EncodeBase58 : BN_div failed");
        bn = dv;
        unsigned int c = rem.getulong();
        str += pszBase58[c];
    }

    // Leading zeroes encoded as base58 zeros
    for (const unsigned char* p = pbegin; p < pend && *p == 0; p++)
        str += pszBase58[0];

    // Convert little endian std::string to big endian
    reverse(str.begin(), str.end());
    return str;
}

// Encode a byte vector as a base58-encoded string
inline std::string EncodeBase58(const std::vector<unsigned char>& vch)
{
    return EncodeBase58(&vch[0], &vch[0] + vch.size());
}

// Decode a base58-encoded string psz into byte vector vchRet
// returns true if decoding is successful
inline bool DecodeBase58(const char* psz, std::vector<unsigned char>& vchRet)
{
    CAutoBN_CTX pctx;
    vchRet.clear();
    CBigNum bn58 = 58;
    CBigNum bn = 0;
    CBigNum bnChar;
    while (isspace(*psz))
        psz++;

    // Convert big endian string to bignum
    for (const char* p = psz; *p; p++)
    {
        const char* p1 = strchr(pszBase58, *p);
        if (p1 == NULL)
        {
            while (isspace(*p))
                p++;
            if (*p != '\0')
                return false;
            break;
        }
        bnChar.setulong(p1 - pszBase58);
        if (!BN_mul(&bn, &bn, &bn58, pctx))
            throw bignum_error("DecodeBase58 : BN_mul failed");
        bn += bnChar;
    }

    // Get bignum as little endian data
    std::vector<unsigned char> vchTmp = bn.getvch();

    // Trim off sign byte if present
    if (vchTmp.size() >= 2 && vchTmp.end()[-1] == 0 && vchTmp.end()[-2] >= 0x80)
        vchTmp.erase(vchTmp.end()-1);

    // Restore leading zeros
    int nLeadingZeros = 0;
    for (const char* p = psz; *p == pszBase58[0]; p++)
        nLeadingZeros++;
    vchRet.assign(nLeadingZeros + vchTmp.size(), 0);

    // Convert little endian data to big endian
    reverse_copy(vchTmp.begin(), vchTmp.end(), vchRet.end() - vchTmp.size());
    return true;
}

// Decode a base58-encoded string str into byte vector vchRet
// returns true if decoding is successful
inline bool DecodeBase58(const std::string& str, std::vector<unsigned char>& vchRet)
{
    return DecodeBase58(str.c_str(), vchRet);
}




// Encode a byte vector to a base58-encoded string, including checksum
inline std::string EncodeBase58Check(const std::vector<unsigned char>& vchIn)
{
    // add 4-byte hash check to the end
    std::vector<unsigned char> vch(vchIn);
    uint256 hash = Hash(vch.begin(), vch.end());
    vch.insert(vch.end(), (unsigned char*)&hash, (unsigned char*)&hash + 4);
    return EncodeBase58(vch);
}

// Decode a base58-encoded string psz that includes a checksum, into byte vector vchRet
// returns true if decoding is successful
inline bool DecodeBase58Check(const char* psz, std::vector<unsigned char>& vchRet)
{
    if (!DecodeBase58(psz, vchRet))
        return false;
    if (vchRet.size() < 4)
    {
        vchRet.clear();
        return false;
    }
    uint256 hash = Hash(vchRet.begin(), vchRet.end()-4);
    if (memcmp(&hash, &vchRet.end()[-4], 4) != 0)
    {
        vchRet.clear();
        return false;
    }
    vchRet.resize(vchRet.size()-4);
    return true;
}

// Decode a base58-encoded string str that includes a checksum, into byte vector vchRet
// returns true if decoding is successful
inline bool DecodeBase58Check(const std::string& str, std::vector<unsigned char>& vchRet)
{
    return DecodeBase58Check(str.c_str(), vchRet);
}


/** Base class for all base58-encoded data */
class CBase58Data
{
protected:
    // the version byte
    unsigned char nVersion;

    // the actually encoded data
    std::vector<unsigned char> vchData;

    CBase58Data()
    {
        nVersion = 0;
        vchData.clear();
    }

    ~CBase58Data()
    {
        // zero the memory, as it may contain sensitive data
        if (!vchData.empty())
            memset(&vchData[0], 0, vchData.size());
    }

    void SetData(int nVersionIn, const void* pdata, size_t nSize)
    {
        nVersion = nVersionIn;
        vchData.resize(nSize);
        if (!vchData.empty())
            memcpy(&vchData[0], pdata, nSize);
    }

    void SetData(int nVersionIn, const unsigned char *pbegin, const unsigned char *pend)
    {
        SetData(nVersionIn, (void*)pbegin, pend - pbegin);
    }

public:
    bool SetString(const char* psz)
    {
        std::vector<unsigned char> vchTemp;
        DecodeBase58Check(psz, vchTemp);
        if (vchTemp.empty())
        {
            vchData.clear();
            nVersion = 0;
            return false;
        }
        nVersion = vchTemp[0];
        vchData.resize(vchTemp.size() - 1);
        if (!vchData.empty())
            memcpy(&vchData[0], &vchTemp[1], vchData.size());
        memset(&vchTemp[0], 0, vchTemp.size());
        return true;
    }

    bool SetString(const std::string& str)
    {
        return SetString(str.c_str());
    }

    std::string ToString() const
    {
        std::vector<unsigned char> vch(1, nVersion);
        vch.insert(vch.end(), vchData.begin(), vchData.end());
        return EncodeBase58Check(vch);
    }

    int CompareTo(const CBase58Data& b58) const
    {
        if (nVersion < b58.nVersion) return -1;
        if (nVersion > b58.nVersion) return  1;
        if (vchData < b58.vchData)   return -1;
        if (vchData > b58.vchData)   return  1;
        return 0;
    }

    bool operator==(const CBase58Data& b58) const { return CompareTo(b58) == 0; }
    bool operator<=(const CBase58Data& b58) const { return CompareTo(b58) <= 0; }
    bool operator>=(const CBase58Data& b58) const { return CompareTo(b58) >= 0; }
    bool operator< (const CBase58Data& b58) const { return CompareTo(b58) <  0; }
    bool operator> (const CBase58Data& b58) const { return CompareTo(b58) >  0; }
};

/** base58-encoded addresses with color.
 * Public-key-hash-addresses have version 25 (or 111 testnet).
 * The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
 * Script-hash-addresses have version 85 (or 196 testnet).
 * The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
 */
class CBitcoinAddress;
class CBitcoinAddressVisitor : public boost::static_visitor<bool>
{
private:
    CBitcoinAddress *addr;
public:
    CBitcoinAddressVisitor(CBitcoinAddress *addrIn) : addr(addrIn) { }
    // have colored operators for stealth address and no destination for completeness
    bool operator()(const CKeyID &id) const;
    bool operator()(const CScriptID &id) const;
    bool operator()(const CStealthAddress &stxAddr) const;
    bool operator()(const CNoDestination &no) const;
};

class CBitcoinAddress : public CBase58Data
{
public:

    // colors are defined in colors.h as enum
    int nColor;

    bool Set(const CKeyID &id) {
        SetData(fTestNet ? PUBKEY_ADDRESS_TEST : PUBKEY_ADDRESS, &id, 20);
        return true;
    }

    bool Set(const CScriptID &id) {
        SetData(fTestNet ? SCRIPT_ADDRESS_TEST : SCRIPT_ADDRESS, &id, 20);
        return true;
    }

    bool Set(const CTxDestination &dest)
    {
        return boost::apply_visitor(CBitcoinAddressVisitor(this), dest);
    }

    bool IsValid() const
    {
        if (this->nColor < 1 || this->nColor > N_COLORS)
        {
            return false;
        }
        unsigned int nExpectedSize = 20;
        bool fExpectTestNet = false;
        switch(nVersion)
        {
            case PUBKEY_ADDRESS:
                nExpectedSize = 20; // Hash of public key
                fExpectTestNet = false;
                break;
            case SCRIPT_ADDRESS:
                nExpectedSize = 20; // Hash of CScript
                fExpectTestNet = false;
                break;

            case PUBKEY_ADDRESS_TEST:
                nExpectedSize = 20;
                fExpectTestNet = true;
                break;
            case SCRIPT_ADDRESS_TEST:
                nExpectedSize = 20;
                fExpectTestNet = true;
                break;

            default:
                return false;
        }
        // Why is this assertion here?, you ask...
        // Address encoding and decoding assumes 160 bit address data, always.
        // It's crashing because you added a new data type with other than
        // 160 bit size. To fix it you need to make changes in
        // CBitcoinAddress::ToString() and CBitcoinAddress::SetString().
        assert (nExpectedSize == 20);
        return fExpectTestNet == fTestNet && vchData.size() == nExpectedSize;
    }

    CBitcoinAddress()
    {
    }

    CBitcoinAddress(const CTxDestination &dest, int nColorIn)
    {
        this->nColor = nColorIn;
        Set(dest);
    }

    bool SetString(const char *psz)
    {


        std::vector<unsigned char> vchTemp;

#if USE_QUALIFIED_ADDRESSES
        std::string address;
        int nCheckColor = BREAKOUT_COLOR_NONE;
        {
            if (!SplitQualifiedAddress(std::string(psz), address, nCheckColor, fDebug))
            {
                      if (fDebug)
                      {
                              printf("Could not split address %s\n", strAddress.c_str());
                      }
                      return false;
            }
        }
        DecodeBase58Check(address.c_str(), vchTemp);
#else
        DecodeBase58Check(psz, vchTemp);
#endif

        if (vchTemp.empty())
        {
            vchData.clear();
            nVersion = 0;
            return false;
        }

        int vchTempSize = (int) vchTemp.size();

        // addresses are 1 byte version, 20 byte key/hash, n bytes of color
        if (vchTempSize < (21 + N_COLOR_BYTES))
        {
              if (fDebug)
              {
                      printf("Address data too short %s\n", psz);
              }
              vchData.clear();
              nVersion = 0;
              return false;
        }
        // first byte is version
        nVersion = vchTemp[0];
            
        std::map <std::vector <unsigned char>, int >* pMapVer;
        if (!GetMapColorID(nVersion, pMapVer))
        {
                    vchData.clear();
                    nVersion = 0;
                    return false;
        }


        std::vector<unsigned char> vColorBytes(N_COLOR_BYTES);
        memcpy(&vColorBytes[0], &vchTemp[1], N_COLOR_BYTES);

        if (pMapVer->find(vColorBytes) == pMapVer->end())
        {
              if (fDebug)
              {
                     printf("Could not determine currency %s\n", psz);
              }
              vchData.clear();
              nVersion = 0;
              return false;
        }

        this->nColor = (*pMapVer)[vColorBytes];
                   
#if USE_QUALIFIED_ADDRESSES
        if (this->nColor != nCheckColor)
        {

              if (fDebug)
              {
                     printf("Prefix currency did not match encoded currency %s\n",
                                                                   strAddress.c_str());
              }
              vchData.clear();
              nVersion = 0;
              return false;
        }
#endif

        // this will always be 20
        // this->vchData.resize(vchTempSize - (1 + nColorBytes));
        this->vchData.resize(20);

        // for key/hash lenght != 20 (revisit if assumption of 20 changes)
        // if (!vchData.empty())
        // {
        //     memcpy(&vchData[0], &vchTemp[1], vchData.size());
        // }
        memcpy(&vchData[0], &vchTemp[1 + N_COLOR_BYTES], vchData.size());
        // why zero this vector?
        memset(&vchTemp[0], 0, vchTemp.size());
        return true;
    }

    bool SetString(const std::string &strAddress)
    {
        return SetString(strAddress.c_str());
    }

    CBitcoinAddress(const std::string& strAddress)
    {
        SetString(strAddress);
    }

    CBitcoinAddress(const char* pszAddress)
    {
        SetString(pszAddress);
    }

    // color bytes go after the version byte
    // color ticker is prepended with the delimeter
    std::string ToString() const
    {
        std::vector<unsigned char> vch(1, nVersion);

        std::vector<unsigned char>* pVecColorID;
        if (!GetVecColorID(this->nVersion, nColor, pVecColorID))
        {
              return std::string("<not valid>");
        }

        vch.insert(vch.end(), pVecColorID->begin(), pVecColorID->end());
        vch.insert(vch.end(), vchData.begin(), vchData.end());

        std::string sAddr;

#if USE_QUALIFIED_ADDRESSES
        sAddr = strprintf("%s"xstr(ADDRESS_DELIMETER)"%s",
                                     COLOR_TICKER[nColor], EncodeBase58Check(vch).c_str());
#else
        sAddr = EncodeBase58Check(vch);
#endif

        return sAddr;
    }


    CTxDestination Get() const {
        if (!IsValid())
            return CNoDestination();
        switch (nVersion) {
        case PUBKEY_ADDRESS:
        case PUBKEY_ADDRESS_TEST: {
            uint160 id;
            memcpy(&id, &vchData[0], 20);
            CKeyID ck(id, nColor);
            return ck;
        }
        case SCRIPT_ADDRESS:
        case SCRIPT_ADDRESS_TEST: {
            uint160 id;
            memcpy(&id, &vchData[0], 20);
            CScriptID cs(id);
            cs.nColor = this->nColor;
            return cs;
        }
        }
        return CNoDestination();
    }

    bool GetKeyID(CKeyID &keyID) const {
        if (!IsValid())
            return false;
        switch (nVersion) {
        case PUBKEY_ADDRESS:
        case PUBKEY_ADDRESS_TEST: {
            uint160 id;
            memcpy(&id, &vchData[0], 20);
            keyID = CKeyID(id, nColor);
            return true;
        }
        default: return false;
        }
    }

    bool IsScript() const {
        if (!IsValid())
            return false;
        switch (nVersion) {
        case SCRIPT_ADDRESS:
        case SCRIPT_ADDRESS_TEST: {
            return true;
        }
        default: return false;
        }
    }
};

bool inline CBitcoinAddressVisitor::operator()(const CKeyID &id) const
{ 
    return addr->Set(id);
}
bool inline CBitcoinAddressVisitor::operator()(const CScriptID &id) const
{
    return addr->Set(id);
}
bool inline CBitcoinAddressVisitor::operator()(const CStealthAddress &stxAddr) const  { return false; }
bool inline CBitcoinAddressVisitor::operator()(const CNoDestination &id) const        { return false; }

/** A base58-encoded secret key */
class CBitcoinSecret : public CBase58Data
{
public:
    void SetSecret(const CSecret& vchSecret, bool fCompressed)
    {
        assert(vchSecret.size() == 32);
        SetData(128 + (fTestNet ? PUBKEY_ADDRESS_TEST : PUBKEY_ADDRESS), &vchSecret[0], vchSecret.size());
        if (fCompressed)
            vchData.push_back(1);
    }

    CSecret GetSecret(bool &fCompressedOut)
    {
        CSecret vchSecret;
        vchSecret.resize(32);
        memcpy(&vchSecret[0], &vchData[0], 32);
        fCompressedOut = vchData.size() == 33;
        return vchSecret;
    }


    void SetKey(const CKey& vchSecret)
    {
        assert(vchSecret.IsValid());
        SetData(128 + (fTestNet ? PUBKEY_ADDRESS_TEST : PUBKEY_ADDRESS), vchSecret.begin(), vchSecret.size());
        if (vchSecret.IsCompressed())
            vchData.push_back(1);
    }

    CKey GetKey()
    {
        CKey ret;
        ret.Set(&vchData[0], &vchData[32], vchData.size() > 32 && vchData[32] == 1);
        return ret;
    }


    bool IsValid() const
    {
        bool fExpectTestNet = false;
        switch(nVersion)
        {
            case (128 + PUBKEY_ADDRESS):
                break;

            case (128 + PUBKEY_ADDRESS_TEST):
                fExpectTestNet = true;
                break;

            default:
                return false;
        }
        return fExpectTestNet == fTestNet && (vchData.size() == 32 || (vchData.size() == 33 && vchData[32] == 1));
    }

    bool SetString(const char* pszSecret)
    {
        return CBase58Data::SetString(pszSecret) && IsValid();
    }

    bool SetString(const std::string& strSecret)
    {
        return SetString(strSecret.c_str());
    }

    CBitcoinSecret(const CSecret& vchSecret, bool fCompressed)
    {
        SetSecret(vchSecret, fCompressed);
    }

    CBitcoinSecret(const CKey& vchSecret)
    {
        SetKey(vchSecret);
    }


    CBitcoinSecret()
    {
    }
};




#endif
