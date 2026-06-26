// Copyright (c) 2009-2012 Bitcoin Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <iostream>
#include <fstream>

#include "init.h" // for pwalletMain
#include "bitcoinrpc.h"
#include "ui_interface.h"
#include "base58.h"
#include "pbkdf2.h"

#include "json_spirit.h"

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/variant/get.hpp>
#include <boost/algorithm/string.hpp>

// OpenSSL headers (replaces CryptoPP dependency)
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <openssl/err.h>


#define printf OutputDebugStringF

using namespace json_spirit;
using namespace std;

void EnsureWalletIsUnlocked();

namespace bt = boost::posix_time;

// Extended DecodeDumpTime implementation, see this page for details:
// http://stackoverflow.com/questions/3786201/parsing-of-date-time-from-string-boost
const std::locale formats[] = {
    std::locale(std::locale::classic(),
                new bt::time_input_facet("%Y-%m-%dT%H:%M:%SZ")),
    std::locale(std::locale::classic(),
                new bt::time_input_facet("%Y-%m-%d %H:%M:%S")),
    std::locale(std::locale::classic(),
                new bt::time_input_facet("%Y/%m/%d %H:%M:%S")),
    std::locale(std::locale::classic(),
                new bt::time_input_facet("%d.%m.%Y %H:%M:%S")),
    std::locale(std::locale::classic(),
                new bt::time_input_facet("%Y-%m-%d"))
};

const size_t formats_n = sizeof(formats)/sizeof(formats[0]);

std::time_t pt_to_time_t(const bt::ptime& pt)
{
    bt::ptime timet_start(boost::gregorian::date(1970,1,1));
    bt::time_duration diff = pt - timet_start;
    return diff.ticks()/bt::time_duration::rep_type::ticks_per_second;
}

int64_t DecodeDumpTime(const std::string& s)
{
    bt::ptime pt;

    for(size_t i=0; i<formats_n; ++i)
    {
        std::istringstream is(s);
        is.imbue(formats[i]);
        is >> pt;
        if(pt != bt::ptime()) break;
    }

    return pt_to_time_t(pt);
}

std::string static EncodeDumpTime(int64_t nTime) {
    return DateTimeStrFormat("%Y-%m-%dT%H:%M:%SZ", nTime);
}

std::string static EncodeDumpString(const std::string &str) {
    std::stringstream ret;
    BOOST_FOREACH(unsigned char c, str) {
        if (c <= 32 || c >= 128 || c == '%') {
            ret << '%' << HexStr(&c, &c + 1);
        } else {
            ret << c;
        }
    }
    return ret.str();
}

std::string DecodeDumpString(const std::string &str)
{
    std::stringstream ret;
    for (unsigned int pos = 0; pos < str.length(); pos++)
    {
        unsigned char c = str[pos];
        if (c == '%' && pos+2 < str.length())
        {
            c = (((str[pos+1]>>6)*9+((str[pos+1]-'0')&15)) << 4) | 
                ((str[pos+2]>>6)*9+((str[pos+2]-'0')&15));
            pos += 2;
        }
        ret << c;
    }
    return ret.str();
}

class CTxDump
{
public:
    CBlockIndex *pindex;
    int64_t nValue;
    bool fSpent;
    CWalletTx* ptx;
    int nOut;
    CTxDump(CWalletTx* ptx = NULL, int nOut = -1)
    {
        pindex = NULL;
        nValue = 0;
        fSpent = false;
        this->ptx = ptx;
        this->nOut = nOut;
    }
};

Value importprivkey(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
    {
        throw runtime_error(
            "importprivkey <breakoutprivkey> [label] [ticker]\n"
            "Adds a private key (as returned by dumpprivkey) to your wallet.\n"
            "If [ticker] is not given, updates address book for default currency."
        );
    }

    int nColor = CheckDefaultCurrency();

    string strUserSecret = params[0].get_str();
    string strLabel = "";
    if (params.size() > 1)
    {
        strLabel = params[1].get_str();
    }

    if (params.size() > 2)
    {
        std::string strTicker = params[2].get_str();
        if (!GetColorFromTicker(strTicker, nColor))
        {
            throw runtime_error(
                     strprintf("ticker %s is not valid\n", strTicker.c_str()));
        }
    }

    // This is a "temporary" hack for people who want
    //    to import testnet keys on main net.
    valtype vchTemp;
    DecodeBase58Check(strUserSecret, vchTemp);
    if (vchTemp.empty())
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Private key is empty.");
    }
    if ((vchTemp[0] != 128 + PUBKEY_ADDRESS_TEST) &&
        (vchTemp[0] != 128 + PUBKEY_ADDRESS))
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Private has bad version byte.");
    }
    vchTemp[0] = fTestNet ? 128 + PUBKEY_ADDRESS_TEST
                          : 128 + PUBKEY_ADDRESS;
    // this is kind of the hack part
    string strSecret = EncodeBase58Check(vchTemp);

    CBitcoinSecret vchSecret;
    bool fGood = vchSecret.SetString(strSecret);

    if (!fGood)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY,
                           "Invalid private key");
    }

    if (fWalletUnlockStakingOnly)
    {
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED,
                           "Wallet is unlocked for staking only.");
    }

    CKey key = vchSecret.GetKey();
    CPubKey pubkey = key.GetPubKey();
    CKeyID vchAddress = pubkey.GetID();
    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBookName(vchAddress, nColor, strLabel);

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
        {
            return Value::null;
        }

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(key, pubkey))
        {
            throw JSONRPCError(RPC_WALLET_ERROR,
                               "Error adding key to wallet");
        }

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'

        pwalletMain->ScanForWalletTransactions(pindexGenesisBlock, true);
        pwalletMain->ReacceptWalletTransactions();
    }

    return Value::null;
}

Value importwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
    {
        throw runtime_error(
            "importwallet <filename> [ticker]\n"
            "If [ticker] is not given, updates address book for default currency.\n"
            "Imports keys from a wallet dump file (see dumpwallet)."
       );
    }

    int nColor = CheckDefaultCurrency();

    EnsureWalletIsUnlocked();

    if (params.size() > 1)
    {
          std::string strTicker = params[1].get_str();
          if (!GetColorFromTicker(strTicker, nColor))
          {
                throw runtime_error(
                        strprintf("ticker %s is not valid\n", strTicker.c_str()));
          }
    }

    ifstream file;
    file.open(params[0].get_str().c_str());
    if (!file.is_open())
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                           "Cannot open wallet dump file");
    }

    int64_t nTimeBegin = pindexBest->nTime;

    bool fGood = true;

    while (file.good()) {
        std::string line;
        std::getline(file, line);
        if (line.empty() || line[0] == '#')
            continue;

        std::vector<std::string> vstr;
        boost::split(vstr, line, boost::is_any_of(" "));
        if (vstr.size() < 2)
            continue;
        CBitcoinSecret vchSecret;
        if (!vchSecret.SetString(vstr[0]))
            continue;
        CKey key = vchSecret.GetKey();
        CPubKey pubkey = key.GetPubKey();
        CKeyID keyid = pubkey.GetID();
        if (pwalletMain->HaveKey(keyid)) {
            printf("Skipping import of %s (key already present)\n",
                                     CBitcoinAddress(keyid, nColor).ToString().c_str());
            continue;
        }
        int64_t nTime = DecodeDumpTime(vstr[1]);
        std::string strLabel;
        bool fLabel = true;
        for (unsigned int nStr = 2; nStr < vstr.size(); nStr++) {
            if (boost::algorithm::starts_with(vstr[nStr], "#"))
                break;
            if (vstr[nStr] == "change=1")
                fLabel = false;
            if (vstr[nStr] == "reserve=1")
                fLabel = false;
            if (boost::algorithm::starts_with(vstr[nStr], "label=")) {
                strLabel = DecodeDumpString(vstr[nStr].substr(6));
                fLabel = true;
            }
        }
        printf("Importing %s...\n", CBitcoinAddress(keyid, nColor).ToString().c_str());
        if (!pwalletMain->AddKey(key)) {
            fGood = false;
            continue;
        }
        pwalletMain->mapKeyMetadata[keyid].nCreateTime = nTime;
        if (fLabel)
            pwalletMain->SetAddressBookName(keyid, nColor, strLabel);
        nTimeBegin = std::min(nTimeBegin, nTime);
    }
    file.close();

    CBlockIndex *pindex = pindexBest;
    while (pindex && pindex->pprev && pindex->nTime > nTimeBegin - 7200)
        pindex = pindex->pprev;

    if (!pwalletMain->nTimeFirstKey || nTimeBegin < pwalletMain->nTimeFirstKey)
        pwalletMain->nTimeFirstKey = nTimeBegin;

    printf("Rescanning last %i blocks\n", pindexBest->nHeight - pindex->nHeight + 1);
    pwalletMain->ScanForWalletTransactions(pindex);
    pwalletMain->ReacceptWalletTransactions();
    pwalletMain->MarkDirty();

    if (!fGood)
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding some keys to wallet");

    return Value::null;
}

// this is from bitcoin core -- required to declare for recursive call in ImportScript()
// void ImportAddress(CWallet*, const CTxDestination& dest, const std::string& strLabel);

// TODO: make isRedeemScript meaningful
void ImportScript(CWallet* const pwallet, const CScript& script,
                           const std::string& strLabel, bool isRedeemScript)
{

    // use loosest definition of ownership wrt multisig
    static const bool fMultiSig = true;

    if (!isRedeemScript && (IsMine(*pwallet, script, fMultiSig) & ISMINE_SIGNABLE))
    {
        throw JSONRPCError(RPC_WALLET_ERROR, "The wallet already contains the private key for this address or script");
    }

    pwallet->MarkDirty();

    if (!pwallet->HaveWatchOnly(script) && !pwallet->AddWatchOnly(script, 0 /* nCreateTime */)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Error adding address to wallet");
    }

// import redeem scripts will be added later
#if 0
    if (isRedeemScript) {
        if (!pwallet->HaveCScript(script) && !pwallet->AddCScript(script)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding p2sh redeemScript to wallet");
        }
        ImportAddress(pwallet, CScriptID(script), strLabel);
    } else {
        CTxDestination destination;
        if (ExtractDestination(script, destination)) {
            pwallet->SetAddressBook(destination, strLabel, "receive");
        }
    }
#else
    CTxDestination destination;
    if (ExtractDestination(script, destination))
    {
        int nColor = boost::apply_visitor(GetAddressColorVisitor(), destination);
        pwallet->SetAddressBookName(destination, nColor, strLabel);
    }
#endif
}

void ImportAddress(CWallet* const pwallet, const CTxDestination& dest, const std::string& strLabel)
{
    CScript script = GetScriptForDestination(dest);
    ImportScript(pwallet, script, strLabel, false);
    // add to address book or update label

    if (IsValidDestination(dest))
    {
        int nColor = boost::apply_visitor(GetAddressColorVisitor(), dest);
        pwallet->SetAddressBookName(dest, nColor, strLabel);
    }
}

Value importaddress(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 3)
    {
        throw runtime_error(
             "importaddress <address> [label] [rescan]\n"
             "If [rescan] is true (default true), then the wallet will be rescanned.\n"
             "This call can take a long time to complete if [rescan] is true.");
    }

    std::string strLabel = "";
    if (params.size() > 1)
    {
        strLabel = params[1].get_str();
    }

    bool fRescan = true;
    if (params.size() > 2)
    {
        fRescan = params[2].get_bool();
    }

    CBitcoinAddress address(params[0].get_str());
    bool isValid = address.IsValid();

    if (!isValid)
    {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Breakout address");
    }

    CTxDestination dest = address.Get();

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);
        ImportAddress(pwalletMain, dest, strLabel);

        if (fRescan)
        {
            // whenever a key is imported, we need to scan the whole chain
            pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'

            string strProgressLabel("Progress of ScanForWalletTransactions");
            CProgressHelper progress(&logProgress, &strProgressLabel, 1000);
            pwalletMain->ScanForWalletTransactions(pindexGenesisBlock,
                                                   true,
                                                   &progress);
            strProgressLabel = "Progress of ReacceptWalletTransactions";
            progress.setContext(&strProgressLabel);
            pwalletMain->ReacceptWalletTransactions(&progress);
        }
    }

    return Value::null;
}

Value dumpprivkey(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "dumpprivkey <breakoutaddress>\n"
            "Reveals the private key corresponding to <breakoutaddress>.");

    EnsureWalletIsUnlocked();

    string strAddress = params[0].get_str();
    CBitcoinAddress address;
    if (!address.SetString(strAddress))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid breakout address");
    if (fWalletUnlockStakingOnly)
        throw JSONRPCError(RPC_WALLET_UNLOCK_NEEDED, "Wallet is unlocked for staking only.");
    CKeyID keyID;
    if (!address.GetKeyID(keyID))
        throw JSONRPCError(RPC_TYPE_ERROR, "Address does not refer to a key");
    CKey vchSecret;
    if (!pwalletMain->GetKey(keyID, vchSecret))
        throw JSONRPCError(RPC_WALLET_ERROR, "Private key for address " + strAddress + " is not known");
    return CBitcoinSecret(vchSecret).ToString();
}

Value dumpwallet(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 1)
        throw runtime_error(
            "dumpwallet <filename>\n"
            "Dumps all wallet keys in a human-readable format.");

    int nColor = CheckDefaultCurrency();

    EnsureWalletIsUnlocked();

    if (params.size() > 1)
    {
          std::string strTicker = params[1].get_str();
          if (!GetColorFromTicker(strTicker, nColor))
          {
                throw runtime_error(
                        strprintf("ticker %s is not valid\n", strTicker.c_str()));
          }
    }

    ofstream file;
    file.open(params[0].get_str().c_str());
    if (!file.is_open())
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Cannot open wallet dump file");

    std::map<CKeyID, int64_t> mapKeyBirth;

    std::set<CKeyID> setKeyPool;

    pwalletMain->GetKeyBirthTimes(mapKeyBirth);

    pwalletMain->GetAllReserveKeys(setKeyPool);

    // sort time/key pairs
    std::vector<std::pair<int64_t, CKeyID> > vKeyBirth;
    for (std::map<CKeyID, int64_t>::const_iterator it = mapKeyBirth.begin(); it != mapKeyBirth.end(); it++) {
        vKeyBirth.push_back(std::make_pair(it->second, it->first));
    }
    mapKeyBirth.clear();
    std::sort(vKeyBirth.begin(), vKeyBirth.end());

    // produce output
    file << strprintf("# Wallet dump created by breakout %s (%s)\n", CLIENT_BUILD.c_str(), CLIENT_DATE.c_str());
    file << strprintf("# * Created on %s\n", EncodeDumpTime(GetTime()).c_str());
    file << strprintf("# * Best block at time of backup was %i (%s),\n", nBestHeight, hashBestChain.ToString().c_str());
    file << strprintf("#   mined on %s\n", EncodeDumpTime(pindexBest->nTime).c_str());
    file << "\n";
    for (std::vector<std::pair<int64_t, CKeyID> >::const_iterator it = vKeyBirth.begin(); it != vKeyBirth.end(); it++) {
        const CKeyID &keyid = it->second;
        std::string strTime = EncodeDumpTime(it->first);
        std::string strAddr = CBitcoinAddress(keyid, nColor).ToString();

        CKey key;
        if (pwalletMain->GetKey(keyid, key)) {
            if (pwalletMain->mapAddressBook.count(keyid)) {
                file << strprintf("%s %s label=%s # addr=%s\n", CBitcoinSecret(key).ToString().c_str(), strTime.c_str(), EncodeDumpString(pwalletMain->mapAddressBook[keyid]).c_str(), strAddr.c_str());
            } else if (setKeyPool.count(keyid)) {
                file << strprintf("%s %s reserve=1 # addr=%s\n", CBitcoinSecret(key).ToString().c_str(), strTime.c_str(), strAddr.c_str());
            } else {
                file << strprintf("%s %s change=1 # addr=%s\n", CBitcoinSecret(key).ToString().c_str(), strTime.c_str(), strAddr.c_str());
            }
        }
    }
    file << "\n";
    file << "# End of dump\n";
    file.close();
    return Value::null;
}

Value encodebase58(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "encodebase58 <jsonbytearray> <check=false>\n"
            "The <jsonbytearray> should be base 10 bytes, e.g.: '[1, 0, 110, 255]'\n"
            "Encodes the byte array as base58 or base58check.");

    Value value;

    bool fCheck = false;
    if (params.size() > 1)
    {
        fCheck = params[1].get_bool();
    }

    json_spirit::read(params[0].get_str(), value);
    if (value.type() != json_spirit::array_type)
    {
        throw runtime_error("Not a json array.\n");
    }
    Array ary = value.get_array();
    int count = ary.size();
    if (count < 1)
    {
        throw runtime_error("Array is empty.\n");
    }
    valtype decoded;
    for (int i = 0; i < count; ++i)
    {
        if (ary[i].type() != json_spirit::int_type)
        {
             throw runtime_error(strprintf("Value at %d is wrong type.\n", i));
        }
        int v = ary[i].get_int();
        if (v < 0 || v > 255)
        {
             throw runtime_error(strprintf("Value at %d is not byte data.\n", i));
        }
        decoded.push_back(v);
    }
    if (fCheck)
    {
        return EncodeBase58Check(decoded);
    }
    else
    {
        return EncodeBase58(decoded);
    }
}

Value pbkdf2(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 4)
        throw runtime_error(
            "pbkdf2 <password> <salt> <rounds> <length>\n"
            "Derrive key from the password using pbkdf2 hmac with sha256.");

    if ((params[0].type() != json_spirit::str_type) ||
        (params[1].type() != json_spirit::str_type) ||
        (params[2].type() != json_spirit::int_type) ||
        (params[3].type() != json_spirit::int_type))
    {
        throw runtime_error(
            "pbkdf2 <password> <salt> <rounds>\n"
            "Password and salt are strings, rounds is a positive integer.");
    }

    int rounds = (int) params[2].get_int();
    int keylen = (int) params[3].get_int();
    if (rounds < 1)
    {
        throw runtime_error(
           "Rounds must be positive, non-zero.");
    }
    if (rounds > 65536)
    {
        throw runtime_error(
           "Rounds must be less than or equal to 65536.");
    }

    if (keylen < 1)
    {
        throw runtime_error(
           "Key length must be positive, non-zero.");
    }
    if (keylen > 1024)
    {
        throw runtime_error(
           "Key length must be less or equal to 1024.");
    }

    const std::string& strPass = params[0].get_str();
    const std::string& strSalt = params[1].get_str();

    unsigned char derived[1024];

    if (PKCS5_PBKDF2_HMAC(
            strPass.c_str(), static_cast<int>(strPass.size()),
            reinterpret_cast<const unsigned char*>(strSalt.c_str()),
            static_cast<int>(strSalt.size()),
            rounds,
            EVP_sha256(),
            keylen,
            derived) != 1)
    {
        throw runtime_error("PBKDF2 derivation failed.");
    }

    return HexStr(derived, derived + keylen);
}



Value importencryptedkey(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 2)
    {
        throw runtime_error(
            "importencryptedkey <walletfile> <password>\n"
            "Import the encrypted wallet from the first coin sale.");
    }

    std::string strFilename = params[0].get_str();
    std::string strJson; 
    ifstream wfile(strFilename.c_str());
    if (wfile.is_open())
    {
        std::stringstream buffer;
        buffer << wfile.rdbuf();
        strJson = buffer.str();
    }
    else
    {
        throw runtime_error(strprintf("Unable to open file '%s'", strFilename.c_str()));
    }
    Value value;
    json_spirit::read(strJson, value);
    if (value.type() != obj_type)
    {
        throw runtime_error(strprintf("File '%s' is not json format", strFilename.c_str()));
    }

    Object obj = value.get_obj();

    Object::iterator it;
    
    for (it = obj.begin(); it != obj.end(); ++it)
    {
        if (it->name_ == "btcaddr")
        {
             break;
        }
    }
    if (it == obj.end())
    {
       throw runtime_error(strprintf("File '%s' is not a wallet file", strFilename.c_str()));
    }
    std::string strBtcAddr = (std::string) it->value_.get_str();

    for (it = obj.begin(); it != obj.end(); ++it)
    {
        if (it->name_ == "encseed")
        {
             break;
        }
    }
    if (it == obj.end())
    {
       throw runtime_error(strprintf("File '%s' is not a wallet file", strFilename.c_str()));
    }
    std::string strEncSeedHex = (std::string) it->value_.get_str();

    // TODO: refactor
    // coinsale wallet: key 16, rounds 2000, salt == password
    static const size_t IVLEN = 16;  // AES block size
    static const size_t KEYLEN = 16;
    static const unsigned int ROUNDS = 2000;
    static const unsigned int SHA3LEN = 32;  // 256/8

    const std::string& strPass = params[1].get_str();

    // --- PBKDF2-SHA256: derive AES key (salt == password) ---
    unsigned char aDerived[KEYLEN];
    if (PKCS5_PBKDF2_HMAC(
            strPass.c_str(), static_cast<int>(strPass.size()),
            reinterpret_cast<const unsigned char*>(strPass.c_str()),
            static_cast<int>(strPass.size()),
            ROUNDS,
            EVP_sha256(),
            KEYLEN,
            aDerived) != 1)
    {
        throw runtime_error("PBKDF2 derivation failed.");
    }

    // --- Hex-decode the encrypted seed ---
    std::string strEncSeed;
    strEncSeed.reserve(strEncSeedHex.size() / 2);
    for (size_t i = 0; i + 1 < strEncSeedHex.size(); i += 2)
    {
        unsigned char hi = strEncSeedHex[i];
        unsigned char lo = strEncSeedHex[i + 1];
        auto hexval = [](unsigned char c) -> unsigned char {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0;
        };
        strEncSeed.push_back(static_cast<char>((hexval(hi) << 4) | hexval(lo)));
    }

    if (strEncSeed.size() <= IVLEN)
    {
        throw runtime_error("Encrypted seed is too short.");
    }

    // First IVLEN bytes are the IV; remainder is ciphertext
    unsigned char aIV[IVLEN];
    std::memcpy(aIV, strEncSeed.data(), IVLEN);
    const unsigned char* pCText =
        reinterpret_cast<const unsigned char*>(strEncSeed.data()) + IVLEN;
    int cTextLen = static_cast<int>(strEncSeed.size() - IVLEN);

    // --- AES-128-CBC decrypt ---
    std::string strPText;
    strPText.resize(cTextLen + static_cast<int>(IVLEN)); // generous upper bound

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        throw runtime_error("Failed to create cipher context.");

    int outLen1 = 0, outLen2 = 0;
    bool decryptOk =
        EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), nullptr, aDerived, aIV) == 1 &&
        EVP_DecryptUpdate(ctx,
                          reinterpret_cast<unsigned char*>(&strPText[0]),
                          &outLen1, pCText, cTextLen) == 1 &&
        EVP_DecryptFinal_ex(ctx,
                            reinterpret_cast<unsigned char*>(&strPText[outLen1]),
                            &outLen2) == 1;

    EVP_CIPHER_CTX_free(ctx);

    if (!decryptOk)
    {
        throw runtime_error("Incorrect password.");
    }
    strPText.resize(outLen1 + outLen2);

    // --- SHA3-256 hash of the plaintext seed to obtain the private key ---
    unsigned char aPrivKey[SHA3LEN];
    EVP_MD_CTX* mdCtx = EVP_MD_CTX_new();
    if (!mdCtx)
        throw runtime_error("Failed to create digest context.");

    const EVP_MD* sha3_256 = EVP_sha3_256();
    if (!sha3_256 ||
        EVP_DigestInit_ex(mdCtx, sha3_256, nullptr) != 1 ||
        EVP_DigestUpdate(mdCtx, strPText.data(), strPText.size()) != 1 ||
        EVP_DigestFinal_ex(mdCtx, aPrivKey, nullptr) != 1)
    {
        EVP_MD_CTX_free(mdCtx);
        throw runtime_error("SHA3-256 digest failed.");
    }
    EVP_MD_CTX_free(mdCtx);

    std::vector<unsigned char> vbyteSecret(aPrivKey, aPrivKey + SHA3LEN);

    CKey ckeySecret;
    ckeySecret.Set(vbyteSecret.begin(), vbyteSecret.end(), false);

    CPubKey pubkey = ckeySecret.GetPubKey();
    CKeyID vchAddress = pubkey.GetID();
    CBitcoinAddress address(vchAddress, nDefaultCurrency);

    std::string strLabel = "First Coin Sale";

    CBitcoinSecret cSecret;
    cSecret.SetKey(ckeySecret);

    Object result; 

    result.push_back(Pair("privkey", cSecret.ToString()));
    result.push_back(Pair("pubkey", HexStr(pubkey.Raw())));
    result.push_back(Pair("address", address.ToString()));
    result.push_back(Pair("ticker", std::string(COLOR_TICKER[nDefaultCurrency])));

    {
        LOCK2(cs_main, pwalletMain->cs_wallet);

        pwalletMain->MarkDirty();
        pwalletMain->SetAddressBookName(vchAddress, nDefaultCurrency, strLabel);

        // Don't throw error in case a key is already there
        if (pwalletMain->HaveKey(vchAddress))
        {
            result.push_back(Pair("already had", "true"));
            return result;
        }

        result.push_back(Pair("already had", "false"));

        pwalletMain->mapKeyMetadata[vchAddress].nCreateTime = 1;

        if (!pwalletMain->AddKeyPubKey(ckeySecret, pubkey))
            throw JSONRPCError(RPC_WALLET_ERROR, "Error adding key to wallet");

        // whenever a key is imported, we need to scan the whole chain
        pwalletMain->nTimeFirstKey = 1; // 0 would be considered 'no value'

        pwalletMain->ScanForWalletTransactions(pindexGenesisBlock, true);
        pwalletMain->ReacceptWalletTransactions();
    }

    return result;
}
