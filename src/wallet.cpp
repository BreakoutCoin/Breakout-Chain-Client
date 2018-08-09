// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// #include <openssl/bn.h>
// #include <openssl/ecdsa.h>
// #include <openssl/obj_mac.h>

#include "main.h"
#include "txdb.h"
#include "wallet.h"
#include "walletdb.h"
#include "crypter.h"
#include "ui_interface.h"
#include "base58.h"
#include "kernel.h"
#include "coincontrol.h"
#include "bitcoinrpc.h"

#include "key.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/replace.hpp>

using namespace std;

int64_t nStakeSplitAge = 21 * 24 * 60 * 60;  // 21 days

extern const int64_t STAKE_COMBINE_THRESHOLD[N_COLORS];

//////////////////////////////////////////////////////////////////////////////
//
// mapWallet
//

struct CompareValueOnly
{
    bool operator()(const pair<int64_t, pair<const CWalletTx*, unsigned int> >& t1,
                    const pair<int64_t, pair<const CWalletTx*, unsigned int> >& t2) const
    {
        return t1.first < t2.first;
    }
};

CPubKey CWallet::GenerateNewKey()
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    bool fCompressed = CanSupportFeature(FEATURE_COMPRPUBKEY); // default to compressed public keys if we want 0.6.0 wallets

    RandAddSeedPerfmon();
    CKey secret;
    secret.MakeNewKey(fCompressed);

    // Compressed public keys were introduced in version 0.6.0
    if (fCompressed)
        SetMinVersion(FEATURE_COMPRPUBKEY);

    CPubKey pubkey = secret.GetPubKey();

    // Create new metadata
    int64_t nCreationTime = GetTime();
    mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
    if (!nTimeFirstKey || nCreationTime < nTimeFirstKey)
        nTimeFirstKey = nCreationTime;

    if (!AddKeyPubKey(secret, pubkey))
        throw std::runtime_error("CWallet::GenerateNewKey() : AddKey failed");
    return pubkey;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey &pubkey)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey))
        return false;
    if (!fFileBacked)
        return true;
    if (!IsCrypted()) {
        return CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}

#if 0
bool CWallet::AddKey(const CKey& key)
{

    AssertLockHeld(cs_wallet); // mapKeyMetadata
    CPubKey pubkey = key.GetPubKey();

    if (!CCryptoKeyStore::AddKey(key))
        return false;
    if (!fFileBacked)
        return true;
    if (!IsCrypted())
    {
        key.GetPrivKey();
        pubkey.GetID();
        mapKeyMetadata[pubkey.GetID()];
        // CWalletDB _w = CWalletDB(strWalletFile);
        return CWalletDB(strWalletFile).WriteKey(pubkey, key.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
    }
    return true;
}
#endif

bool CWallet::AddCryptedKey(const CPubKey &vchPubKey, const vector<unsigned char> &vchCryptedSecret)
{
    if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret))
        return false;
    if (!fFileBacked)
        return true;
    {
        LOCK(cs_wallet);
        if (pwalletdbEncryption)
            return pwalletdbEncryption->WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
        else
            return CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
    }
    return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &meta)
{
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey))
        nTimeFirstKey = meta.nCreateTime;

    mapKeyMetadata[pubkey.GetID()] = meta;
    return true;
}

bool CWallet::LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret)
{
    return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript& redeemScript)
{
    if (!CCryptoKeyStore::AddCScript(redeemScript))
        return false;
    if (!fFileBacked)
        return true;
    return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

// optional setting to unlock wallet for staking only
// serves to disable the trivial sendmoney when OS account compromised
// provides no real security
bool fWalletUnlockStakingOnly = false;

bool CWallet::LoadCScript(const CScript& redeemScript)
{
    /* A sanity check was added in pull #3843 to avoid adding redeemScripts
     * that never can be redeemed. However, old wallets may still contain
     * these. Do not add them to the wallet and warn. */
    if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE)
    {
        // we have no source of color info, so use NONE
        std::string strAddr = CBitcoinAddress(redeemScript.GetID(), BREAKOUT_COLOR_NONE).ToString();
        printf("%s: Warning: This wallet contains a redeemScript of size %"PRIszu" which exceeds maximum size %i thus can never be redeemed. Do not use address %s.\n",
            __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr.c_str());
        return true;
    }

    return CCryptoKeyStore::AddCScript(redeemScript);
}

// adding these from bitcoin for compatibility
bool CWallet::AddWatchOnly(const CScript& dest)
{
    if (!CCryptoKeyStore::AddWatchOnly(dest))
    {
        return false;
    }
    uint160 in = uint160(Hash160(dest.begin(), dest.end()));
    const CKeyMetadata& meta = m_script_metadata[CScriptID(in)];
    // from bitcoin core
    // UpdateTimeFirstKey(meta.nCreateTime);
    NotifyWatchonlyChanged(true);
    return CWalletDB(strWalletFile).WriteWatchOnly(dest, meta);
}

bool CWallet::AddWatchOnly(const CScript& dest, int64_t nCreateTime)
{
    uint160 in = uint160(Hash160(dest.begin(), dest.end()));
    m_script_metadata[CScriptID(in)].nCreateTime = nCreateTime;
    return AddWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript &dest)
{
    AssertLockHeld(cs_wallet);
    if (!CCryptoKeyStore::RemoveWatchOnly(dest))
    {
        return false;
    }
    if (!HaveWatchOnly())
    {
        NotifyWatchonlyChanged(false);
    }
    if (!CWalletDB(strWalletFile).EraseWatchOnly(dest))
    {
        return false;
    }
    return true;
}

bool CWallet::LoadWatchOnly(const CScript &dest)
{
    return CCryptoKeyStore::AddWatchOnly(dest);
}


bool CWallet::Lock()
{
    if (IsLocked())
        return true;
    if (fDebug)
        printf("Locking wallet.\n");
    {
        LOCK(cs_wallet);
        CWalletDB wdb(strWalletFile);
        CStealthAddress sxAddrTemp;
        std::set<CStealthAddress>::iterator it;
        for (it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
        {
            if (it->scan_secret.size() < 32)
                continue;
            CStealthAddress &sxAddr = const_cast<CStealthAddress&>(*it);
            if (fDebug)
                printf("Recrypting stealth key %s\n", sxAddr.Encoded().c_str());
            sxAddrTemp.scan_pubkey = sxAddr.scan_pubkey;
            if (!wdb.ReadStealthAddress(sxAddrTemp))
            {
                printf("Error: Failed to read stealth key from db %s\n", sxAddr.Encoded().c_str());
                continue;
            }
            sxAddr.spend_secret = sxAddrTemp.spend_secret;
        };
    }
    return LockKeyStore();
}

bool CWallet::Unlock(const SecureString& strWalletPassphrase)
{
    if (!IsLocked())
        return false;

    CCrypter crypter;
    CKeyingMaterial vMasterKey;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH(const MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                return false;
            if (!CCryptoKeyStore::Unlock(vMasterKey))
                return false;
            break;
        }
        
        UnlockStealthAddresses(vMasterKey);
        return true;
    }
    return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase)
{
    bool fWasLocked = IsLocked();

    {
        LOCK(cs_wallet);
        Lock();

        CCrypter crypter;
        CKeyingMaterial vMasterKey;
        BOOST_FOREACH(MasterKeyMap::value_type& pMasterKey, mapMasterKeys)
        {
            if(!crypter.SetKeyFromPassphrase(strOldWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                return false;
            if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey))
                return false;
            if (CCryptoKeyStore::Unlock(vMasterKey)
                && UnlockStealthAddresses(vMasterKey))
            {
                int64_t nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

                nStartTime = GetTimeMillis();
                crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
                pMasterKey.second.nDeriveIterations = (pMasterKey.second.nDeriveIterations + pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

                if (pMasterKey.second.nDeriveIterations < 25000)
                    pMasterKey.second.nDeriveIterations = 25000;

                printf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

                if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt, pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
                    return false;
                if (!crypter.Encrypt(vMasterKey, pMasterKey.second.vchCryptedKey))
                    return false;
                CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
                if (fWasLocked)
                    Lock();
                return true;
            }
        }
    }

    return false;
}

void CWallet::SetBestChain(const CBlockLocator& loc)
{
    CWalletDB walletdb(strWalletFile);
    walletdb.WriteBestBlock(loc);
}

// This class implements an addrIncoming entry that causes pre-0.4
// clients to crash on startup if reading a private-key-encrypted wallet.
class CCorruptAddress
{
public:
    IMPLEMENT_SERIALIZE
    (
        if (nType & SER_DISK)
            READWRITE(nVersion);
    )
};

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit)
{
    LOCK(cs_wallet); // nWalletVersion
    if (nWalletVersion >= nVersion)
        return true;

    // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
    if (fExplicit && nVersion > nWalletMaxVersion)
            nVersion = FEATURE_LATEST;

    nWalletVersion = nVersion;

    if (nVersion > nWalletMaxVersion)
        nWalletMaxVersion = nVersion;

    if (fFileBacked)
    {
        CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
        if (nWalletVersion >= 40000)
        {
            // Versions prior to 0.4.0 did not support the "minversion" record.
            // Use a CCorruptAddress to make them crash instead.
            CCorruptAddress corruptAddress;
            pwalletdb->WriteSetting("addrIncoming", corruptAddress);
        }
        if (nWalletVersion > 40000)
            pwalletdb->WriteMinVersion(nWalletVersion);
        if (!pwalletdbIn)
            delete pwalletdb;
    }

    return true;
}

bool CWallet::SetMaxVersion(int nVersion)
{
    LOCK(cs_wallet); // nWalletVersion, nWalletMaxVersion
    // cannot downgrade below current version
    if (nWalletVersion > nVersion)
        return false;

    nWalletMaxVersion = nVersion;

    return true;
}

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase)
{
    if (IsCrypted())
        return false;

    CKeyingMaterial vMasterKey;
    RandAddSeedPerfmon();

    vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
    RAND_bytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

    CMasterKey kMasterKey(nDerivationMethodIndex);

    RandAddSeedPerfmon();
    kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
    RAND_bytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

    CCrypter crypter;
    int64_t nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

    nStartTime = GetTimeMillis();
    crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod);
    kMasterKey.nDeriveIterations = (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) / 2;

    if (kMasterKey.nDeriveIterations < 25000)
        kMasterKey.nDeriveIterations = 25000;

    printf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

    if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations, kMasterKey.nDerivationMethod))
        return false;
    if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey))
        return false;

    {
        LOCK(cs_wallet);
        mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
        if (fFileBacked)
        {
            pwalletdbEncryption = new CWalletDB(strWalletFile);
            if (!pwalletdbEncryption->TxnBegin())
                return false;
            pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
        }

        if (!EncryptKeys(vMasterKey))
        {
            if (fFileBacked)
                pwalletdbEncryption->TxnAbort();
            exit(1); //We now probably have half of our keys encrypted in memory, and half not...die and let the user reload their unencrypted wallet.
        }
        std::set<CStealthAddress>::iterator it;
        for (it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
        {
            if (it->scan_secret.size() < 32)
                continue; // stealth address not owned
            // -- CStealthAddress is only sorted on spend_pubkey
            CStealthAddress &sxAddr = const_cast<CStealthAddress&>(*it);

            if (fDebug)
                printf("Encrypting stealth key %s\n", sxAddr.Encoded().c_str());

            std::vector<unsigned char> vchCryptedSecret;

            CSecret vchSecret;
            vchSecret.resize(32);
            memcpy(&vchSecret[0], &sxAddr.spend_secret[0], 32);

            uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());
            if (!EncryptSecret(vMasterKey, vchSecret, iv, vchCryptedSecret))
            {
                printf("Error: Failed encrypting stealth key %s\n", sxAddr.Encoded().c_str());
                continue;
            };

            sxAddr.spend_secret = vchCryptedSecret;
            pwalletdbEncryption->WriteStealthAddress(sxAddr);
        };
        // Encryption was introduced in version 0.4.0
        SetMinVersion(FEATURE_WALLETCRYPT, pwalletdbEncryption, true);

        if (fFileBacked)
        {
            if (!pwalletdbEncryption->TxnCommit())
                exit(1); //We now have keys encrypted in memory, but no on disk...die to avoid confusion and let the user reload their unencrypted wallet.

            delete pwalletdbEncryption;
            pwalletdbEncryption = NULL;
        }

        Lock();
        Unlock(strWalletPassphrase);
        NewKeyPool();
        Lock();

        // Need to completely rewrite the wallet file; if we don't, bdb might keep
        // bits of the unencrypted private key in slack space in the database file.
        CDB::Rewrite(strWalletFile);

    }
    NotifyStatusChanged(this);

    return true;
}

int64_t CWallet::IncOrderPosNext(CWalletDB *pwalletdb)
{
    AssertLockHeld(cs_wallet); // nOrderPosNext
    int64_t nRet = nOrderPosNext++;
    if (pwalletdb) {
        pwalletdb->WriteOrderPosNext(nOrderPosNext);
    } else {
        CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
    }
    return nRet;
}

CWallet::TxItems CWallet::OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount)
{
    AssertLockHeld(cs_wallet); // mapWallet
    CWalletDB walletdb(strWalletFile);

    // First: get all CWalletTx and CAccountingEntry into a sorted-by-order multimap.
    TxItems txOrdered;

    // Note: maintaining indices in the database of (account,time) --> txid and (account, time) --> acentry
    // would make this much faster for applications that do this a lot.
    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        CWalletTx* wtx = &((*it).second);
        txOrdered.insert(make_pair(wtx->nOrderPos, TxPair(wtx, (CAccountingEntry*)0)));
    }
    acentries.clear();
    walletdb.ListAccountCreditDebit(strAccount, acentries);
    BOOST_FOREACH(CAccountingEntry& entry, acentries)
    {
        txOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));
    }

    return txOrdered;
}

void CWallet::WalletUpdateSpent(const CTransaction &tx, bool fBlock)
{
    // no reason why multisig txs can't be marked spent (?)
    static const bool fMultiSig = true;

    // Anytime a signature is successfully verified, it's proof the outpoint is spent.
    // Update the wallet spent flag if it doesn't know due to wallet.dat being
    // restored from backup or the user making copies of wallet.dat.
    {
        LOCK(cs_wallet);
        BOOST_FOREACH(const CTxIn& txin, tx.vin)
        {
            map<uint256, CWalletTx>::iterator mi = mapWallet.find(txin.prevout.hash);
            if (mi != mapWallet.end())
            {
                CWalletTx& wtx = (*mi).second;
                if (txin.prevout.n >= wtx.vout.size())
                    printf("WalletUpdateSpent: bad wtx %s\n", wtx.GetHash().ToString().c_str());
                else if (!wtx.IsSpent(txin.prevout.n) &&
                         (IsMine(wtx.vout[txin.prevout.n], fMultiSig) & ISMINE_ALL))
                {
                    printf("WalletUpdateSpent found spent coin %s %s %s\n",
                           FormatMoney(wtx.GetCredit(wtx.vout[txin.prevout.n].nColor, fMultiSig),
                                                     wtx.vout[txin.prevout.n].nColor).c_str(),
                                                   COLOR_TICKER[wtx.vout[txin.prevout.n].nColor],
                                                                 wtx.GetHash().ToString().c_str());
                    wtx.MarkSpent(txin.prevout.n);
                    wtx.WriteToDisk();
                    NotifyTransactionChanged(this, txin.prevout.hash, CT_UPDATED);
                }
            }
        }

        if (fBlock)
        {
            uint256 hash = tx.GetHash();
            map<uint256, CWalletTx>::iterator mi = mapWallet.find(hash);
            CWalletTx& wtx = (*mi).second;

            BOOST_FOREACH(const CTxOut& txout, tx.vout)
            {
                if (IsMine(txout, fMultiSig) & ISMINE_ALL)
                {
                    wtx.MarkUnspent(&txout - &tx.vout[0]);
                    wtx.WriteToDisk();
                    NotifyTransactionChanged(this, hash, CT_UPDATED);
                }
            }
        }

    }
}

void CWallet::MarkDirty()
{
    {
        LOCK(cs_wallet);
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
            item.second.MarkDirty();
    }
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn)
{
    uint256 hash = wtxIn.GetHash();
    {
        LOCK(cs_wallet);
        // Inserts only if not already there, returns tx inserted or tx found
        pair<map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(make_pair(hash, wtxIn));
        CWalletTx& wtx = (*ret.first).second;
        wtx.BindWallet(this);
        bool fInsertedNew = ret.second;
        if (fInsertedNew)
        {
            wtx.nTimeReceived = GetAdjustedTime();
            wtx.nOrderPos = IncOrderPosNext();

            wtx.nTimeSmart = wtx.nTimeReceived;
            if (wtxIn.hashBlock != 0)
            {
                if (mapBlockIndex.count(wtxIn.hashBlock))
                {
                    unsigned int latestNow = wtx.nTimeReceived;
                    unsigned int latestEntry = 0;
                    {
                        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
                        int64_t latestTolerated = latestNow + 300;
                        std::list<CAccountingEntry> acentries;
                        TxItems txOrdered = OrderedTxItems(acentries);
                        for (TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it)
                        {
                            CWalletTx *const pwtx = (*it).second.first;
                            if (pwtx == &wtx)
                                continue;
                            CAccountingEntry *const pacentry = (*it).second.second;
                            int64_t nSmartTime;
                            if (pwtx)
                            {
                                nSmartTime = pwtx->nTimeSmart;
                                if (!nSmartTime)
                                    nSmartTime = pwtx->nTimeReceived;
                            }
                            else
                                nSmartTime = pacentry->nTime;
                            if (nSmartTime <= latestTolerated)
                            {
                                latestEntry = nSmartTime;
                                if (nSmartTime > latestNow)
                                    latestNow = nSmartTime;
                                break;
                            }
                        }
                    }

                    unsigned int& blocktime = mapBlockIndex[wtxIn.hashBlock]->nTime;
                    wtx.nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
                }
                else
                    printf("AddToWallet() : found %s in block %s not in index\n",
                           wtxIn.GetHash().ToString().substr(0,10).c_str(),
                           wtxIn.hashBlock.ToString().c_str());
            }
        }

        bool fUpdated = false;
        if (!fInsertedNew)
        {
            // Merge
            if (wtxIn.hashBlock != 0 && wtxIn.hashBlock != wtx.hashBlock)
            {
                wtx.hashBlock = wtxIn.hashBlock;
                fUpdated = true;
            }
            if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex))
            {
                wtx.vMerkleBranch = wtxIn.vMerkleBranch;
                wtx.nIndex = wtxIn.nIndex;
                fUpdated = true;
            }
            if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe)
            {
                wtx.fFromMe = wtxIn.fFromMe;
                fUpdated = true;
            }
            fUpdated |= wtx.UpdateSpent(wtxIn.vfSpent);
        }

        //// debug print
        printf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString().substr(0,10).c_str(), (fInsertedNew ? "new" : ""), (fUpdated ? "update" : ""));

        // Write to disk
        if (fInsertedNew || fUpdated)
            if (!wtx.WriteToDisk())
                return false;
#ifndef QT_GUI
        // If default receiving address gets used, replace it with a new one
        if (vchDefaultKey.IsValid()) {
            CScript scriptDefaultKey;
            scriptDefaultKey.SetDestination(vchDefaultKey.GetID());
            BOOST_FOREACH(const CTxOut& txout, wtx.vout)
            {
                if (txout.scriptPubKey == scriptDefaultKey)
                {
                    CPubKey newDefaultKey;
                    if (GetKeyFromPool(newDefaultKey, false))
                    {
                        SetDefaultKey(newDefaultKey);
                        SetAddressBookName(vchDefaultKey.GetID(), txout.nColor, "");
                    }
                }
            }
        }
#endif
        // since AddToWallet is called directly for self-originating transactions, check for consumption of own coins
        WalletUpdateSpent(wtx, (wtxIn.hashBlock != 0));

        // Notify UI of new or updated transaction
        NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

        // notify an external script when a wallet transaction comes in or is updated
        std::string strCmd = GetArg("-walletnotify", "");

        if ( !strCmd.empty())
        {
            boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
            boost::thread t(runCommand, strCmd); // thread runs free
        }
    }
    return true;
}

// Add a transaction to the wallet, or update it.
// pblock is optional, but should be provided if the transaction is known to be in a block.
// If fUpdate is true, existing transactions will be updated.
bool CWallet::AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate, bool fFindBlock)
{
    // no reason why multisig txs can't be added to the wallet
    static const bool fMultiSig = true;

    uint256 hash = tx.GetHash();
    {
        LOCK(cs_wallet);
        bool fExisted = mapWallet.count(hash);
        if (fExisted && !fUpdate) return false;
        mapValue_t mapNarr;
        FindStealthTransactions(tx, mapNarr);
        // involving me is interpreted in the broadest possible terms
        if (fExisted || (IsMine(tx, fMultiSig) & ISMINE_ALL) || IsFromMe(tx, fMultiSig))
        {
            CWalletTx wtx(this,tx);
            if (!mapNarr.empty())
                wtx.mapValue.insert(mapNarr.begin(), mapNarr.end());
            // Get merkle branch if transaction was found in a block
            if (pblock)
                wtx.SetMerkleBranch(pblock);
            return AddToWallet(wtx);
        }
        else
            WalletUpdateSpent(tx);
    }
    return false;
}

bool CWallet::EraseFromWallet(uint256 hash)
{
    if (!fFileBacked)
        return false;
    {
        LOCK(cs_wallet);
        if (mapWallet.erase(hash))
            CWalletDB(strWalletFile).EraseTx(hash);
    }
    return true;
}

isminetype CWallet::IsMine(const CTxIn &txin, bool fMultiSig) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
            {
                // inclusive because `& ISMINE_*` filtering is used downstream
                return IsMine(prev.vout[txin.prevout.n], fMultiSig);
            }
        }
    }
    return ISMINE_NO;
}

// multisig is dependent on context
std::pair<int, int64_t> CWallet::GetDebit(const CTxIn &txin, bool fMultiSig) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
            {
                if (IsMine(prev.vout[txin.prevout.n], fMultiSig) & ISMINE_ALL)
                {
                    return std::make_pair(prev.vout[txin.prevout.n].nColor,
                                                 prev.vout[txin.prevout.n].nValue);
                }
            }
        }
    }
    return std::make_pair(BREAKOUT_COLOR_NONE, 0);
}

int64_t CWallet::GetDebit(const CTxIn &txin, int nColor, bool fMultiSig) const
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            const CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size())
            {
                if ((IsMine(prev.vout[txin.prevout.n], fMultiSig) & ISMINE_ALL) &&
                                  prev.vout[txin.prevout.n].nColor == nColor)
                {
                    return prev.vout[txin.prevout.n].nValue;
                }
            }
        }
    }
    return 0;
}

bool CWallet::IsChange(const CTxOut& txout) const
{
    // change status does not impact spendable balance, so enable multisig
    // also mutlisig is either
    //      (1) same address or
    //      (2) all keys of prev multisig with one or more substitutions
    //          by the originator of the tx
    static const bool fMultiSig = true;

    CTxDestination address;

    // TODO: fix handling of 'change' outputs. The assumption is that any
    // payment to a TX_PUBKEYHASH that is mine but isn't in the address book
    // is change. That assumption is likely to break when we implement multisignature
    // wallets that return change back into a multi-signature-protected address;
    // a better way of identifying which outputs are 'the send' and which are
    // 'the change' will need to be implemented (maybe extend CWalletTx to remember
    // which output, if any, was change).
    if (ExtractDestination(txout.scriptPubKey, address) &&
        (::IsMine(*this, address, fMultiSig) & ISMINE_ALL))
    {
        LOCK(cs_wallet);
        if (!mapAddressBook.count(address))
            return true;
    }
    return false;
}

int64_t CWalletTx::GetTxTime() const
{
    int64_t n = nTimeSmart;
    return n ? n : nTimeReceived;
}

int CWalletTx::GetRequestCount() const
{
    // Returns -1 if it wasn't being tracked
    int nRequests = -1;
    {
        LOCK(pwallet->cs_wallet);
        if (IsCoinBase() || IsCoinStake())
        {
            // Generated block
            if (hashBlock != 0)
            {
                map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                if (mi != pwallet->mapRequestCount.end())
                    nRequests = (*mi).second;
            }
        }
        else
        {
            // Did anyone request this transaction?
            map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(GetHash());
            if (mi != pwallet->mapRequestCount.end())
            {
                nRequests = (*mi).second;

                // How about the block it's in?
                if (nRequests == 0 && hashBlock != 0)
                {
                    map<uint256, int>::const_iterator mi = pwallet->mapRequestCount.find(hashBlock);
                    if (mi != pwallet->mapRequestCount.end())
                        nRequests = (*mi).second;
                    else
                        nRequests = 1; // If it's in someone else's block it must have got out
                }
            }
        }
    }
    return nRequests;
}

// this is not used outside of RPC, so multisig is optional
void CWalletTx::GetAmounts(int nColor, list<pair<CTxDestination,
                           int64_t> >& listReceived,
                           list<pair<CTxDestination, int64_t> >& listSent,
                           int64_t& nFee, string& strSentAccount, bool fMultiSig) const
{
    if (!CheckColor(nColor))
    {
         throw runtime_error("CWalletTx::GetAmounts(): invalid color");
    }

    nFee = 0;
    listReceived.clear();
    listSent.clear();
    strSentAccount = strFromAccount;

    // Compute fee in nColor
    int64_t nDebit = GetDebit(nColor, fMultiSig);
    if (nDebit > 0) // debit>0 means we signed/sent this transaction
    {
        int64_t nValueOut = GetValueOut(nColor);
        nFee = nDebit - nValueOut;
    }

    // Sent/received.
    BOOST_FOREACH(const CTxOut& txout, vout)
    {
        // only tally color of interest
        if (txout.nColor != nColor)
        {
            continue;
        }

        // Skip special stake out
        if (txout.scriptPubKey.empty())
            continue;
        
        opcodetype firstOpCode;
        CScript::const_iterator pc = txout.scriptPubKey.begin();
        if (txout.scriptPubKey.GetOp(pc, firstOpCode)
            && firstOpCode == OP_RETURN)
            continue;
        
        bool fIsMine;
        // Only need to handle txouts if AT LEAST one of these is true:
        //   1) they debit from us (sent)
        //   2) the output is to us (received)
        if (nDebit > 0)
        {
            // Don't report 'change' txouts
            if (pwallet->IsChange(txout))
                continue;
            fIsMine = (pwallet->IsMine(txout, fMultiSig) & ISMINE_ALL);
        }
        else if (!(fIsMine = (pwallet->IsMine(txout, fMultiSig) & ISMINE_ALL)))
            continue;

        // In either case, we need to get the destination address
        CTxDestination address;
        if (!ExtractDestination(txout.scriptPubKey, address))
        {
            printf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n",
                   this->GetHash().ToString().c_str());
            address = CNoDestination();
        }

        // If we are debited by the transaction, add the output as a "sent" entry
        if (nDebit > 0)
            listSent.push_back(make_pair(address, txout.nValue));

        // If we are receiving the output, add it as a "received" entry
        if (fIsMine)
            listReceived.push_back(make_pair(address, txout.nValue));
    }

}

// this is not used outside of RPC, so multisig is optional
void CWalletTx::GetAccountAmounts(int nColor, const string& strAccount,
                                  int64_t& nReceived, int64_t& nSent, int64_t& nFee, bool fMultiSig) const
{
    nReceived = nSent = nFee = 0;

    int64_t allFee;
    string strSentAccount;
    list<pair<CTxDestination, int64_t> > listReceived;
    list<pair<CTxDestination, int64_t> > listSent;
    GetAmounts(nColor, listReceived, listSent, allFee, strSentAccount, fMultiSig);

    if (strAccount == strSentAccount)
    {
        BOOST_FOREACH(const PAIRTYPE(CTxDestination,int64_t)& s, listSent)
            nSent += s.second;
        nFee = allFee;
    }
    {
        LOCK(pwallet->cs_wallet);
        BOOST_FOREACH(const PAIRTYPE(CTxDestination,int64_t)& r, listReceived)
        {
            if (pwallet->mapAddressBook.count(r.first))
            {
                map<CTxDestination, string>::const_iterator mi = pwallet->mapAddressBook.find(r.first);
                if (mi != pwallet->mapAddressBook.end() && (*mi).second == strAccount)
                    nReceived += r.second;
            }
            else if (strAccount.empty())
            {
                nReceived += r.second;
            }
        }
    }
}

void CWalletTx::AddSupportingTransactions(CTxDB& txdb)
{
    vtxPrev.clear();

    const int COPY_DEPTH = 3;
    if (SetMerkleBranch() < COPY_DEPTH)
    {
        vector<uint256> vWorkQueue;
        BOOST_FOREACH(const CTxIn& txin, vin)
            vWorkQueue.push_back(txin.prevout.hash);

        // This critsect is OK because txdb is already open
        {
            LOCK(pwallet->cs_wallet);
            map<uint256, const CMerkleTx*> mapWalletPrev;
            set<uint256> setAlreadyDone;
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                uint256 hash = vWorkQueue[i];
                if (setAlreadyDone.count(hash))
                    continue;
                setAlreadyDone.insert(hash);

                CMerkleTx tx;
                map<uint256, CWalletTx>::const_iterator mi = pwallet->mapWallet.find(hash);
                if (mi != pwallet->mapWallet.end())
                {
                    tx = (*mi).second;
                    BOOST_FOREACH(const CMerkleTx& txWalletPrev, (*mi).second.vtxPrev)
                        mapWalletPrev[txWalletPrev.GetHash()] = &txWalletPrev;
                }
                else if (mapWalletPrev.count(hash))
                {
                    tx = *mapWalletPrev[hash];
                }
                else if (txdb.ReadDiskTx(hash, tx))
                {
                    ;
                }
                else
                {
                    printf("ERROR: AddSupportingTransactions() : unsupported transaction\n");
                    continue;
                }

                int nDepth = tx.SetMerkleBranch();
                vtxPrev.push_back(tx);

                if (nDepth < COPY_DEPTH)
                {
                    BOOST_FOREACH(const CTxIn& txin, tx.vin)
                        vWorkQueue.push_back(txin.prevout.hash);
                }
            }
        }
    }

    reverse(vtxPrev.begin(), vtxPrev.end());
}

bool CWalletTx::WriteToDisk()
{
    return CWalletDB(pwallet->strWalletFile).WriteTx(GetHash(), *this);
}

// Scan the block chain (starting in pindexStart) for transactions
// from or to us. If fUpdate is true, found transactions that already
// exist in the wallet will be updated.
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate, void (*pProgress)(int))
{
    int ret = 0;

    CBlockIndex* pindex = pindexStart;
    {
        LOCK2(cs_main, cs_wallet);
        while (pindex)
        {
            if (pProgress != NULL) {
                  (*pProgress)(pindex->nHeight);
            }
            // no need to read and scan block, if block was created before
            // our wallet birthday (as adjusted for block time variability)
            if (nTimeFirstKey && (pindex->nTime < (nTimeFirstKey - 7200))) {
                pindex = pindex->pnext;
                continue;
            }

            CBlock block;
            block.ReadFromDisk(pindex, true);
            BOOST_FOREACH(CTransaction& tx, block.vtx)
            {
                if (AddToWalletIfInvolvingMe(tx, &block, fUpdate))
                    ret++;
            }
            pindex = pindex->pnext;
        }
    }
    return ret;
}

int CWallet::ScanForWalletTransaction(const uint256& hashTx)
{
    CTransaction tx;
    tx.ReadFromDisk(COutPoint(hashTx, 0));
    if (AddToWalletIfInvolvingMe(tx, NULL, true, true))
        return 1;
    return 0;
}

void CWallet::ReacceptWalletTransactions()
{
    // no reason to exlcude multisig transactions from wallet acceptance
    static const bool fMultiSig = true;

    CTxDB txdb("r");
    bool fRepeat = true;
    while (fRepeat)
    {
        LOCK2(cs_main, cs_wallet);
        fRepeat = false;
        vector<CDiskTxPos> vMissingTx;
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
        {
            CWalletTx& wtx = item.second;
            if ((wtx.IsCoinBase() && wtx.IsSpent(0)) || (wtx.IsCoinStake() && wtx.IsSpent(1)))
                continue;

            CTxIndex txindex;
            bool fUpdated = false;
            CTxOut txSpentOut;
            if (txdb.ReadTxIndex(wtx.GetHash(), txindex))
            {
                // Update fSpent if a tx got spent somewhere else by a copy of wallet.dat
                if (txindex.vSpent.size() != wtx.vout.size())
                {
                    printf("ERROR: ReacceptWalletTransactions() : txindex.vSpent.size() %"PRIszu
                                          " != wtx.vout.size() %"PRIszu"\n",
                                                   txindex.vSpent.size(), wtx.vout.size());
                    continue;
                }
                for (unsigned int i = 0; i < txindex.vSpent.size(); i++)
                {
                    if (wtx.IsSpent(i))
                        continue;
                    if (!txindex.vSpent[i].IsNull() && (IsMine(wtx.vout[i], fMultiSig) & ISMINE_ALL))
                    {
                        wtx.MarkSpent(i);
                        fUpdated = true;
                        txSpentOut = wtx.vout[i];
                        vMissingTx.push_back(txindex.vSpent[i]);
                    }
                }
                if (fUpdated)
                {
                    printf("ReacceptWalletTransactions found spent coin %s %s %s\n",
                                FormatMoney(wtx.GetCredit(txSpentOut.nColor, fMultiSig),
                                         txSpentOut.nColor).c_str(), 
                                         COLOR_TICKER[txSpentOut.nColor],
                                         wtx.GetHash().ToString().c_str());
                    wtx.MarkDirty();
                    wtx.WriteToDisk();
                }
            }
            else
            {
                // Re-accept any txes of ours that aren't already in a block
                if (!(wtx.IsCoinBase() || wtx.IsCoinStake()))
                    wtx.AcceptWalletTransaction(txdb);
            }
        }
        if (!vMissingTx.empty())
        {
            // TODO: optimize this to scan just part of the block chain?
            if (ScanForWalletTransactions(pindexGenesisBlock))
                fRepeat = true;  // Found missing transactions: re-do re-accept.
        }
    }
}

void CWalletTx::RelayWalletTransaction(CTxDB& txdb)
{
    BOOST_FOREACH(const CMerkleTx& tx, vtxPrev)
    {
        if (!(tx.IsCoinBase() || tx.IsCoinStake()))
        {
            uint256 hash = tx.GetHash();
            if (!txdb.ContainsTx(hash))
                RelayTransaction((CTransaction)tx, hash);
        }
    }
    if (!(IsCoinBase() || IsCoinStake()))
    {
        uint256 hash = GetHash();
        if (!txdb.ContainsTx(hash))
        {
            printf("Relaying wtx %s\n", hash.ToString().substr(0,10).c_str());
            RelayTransaction((CTransaction)*this, hash);
        }
    }
}

void CWalletTx::RelayWalletTransaction()
{
   CTxDB txdb("r");
   RelayWalletTransaction(txdb);
}

void CWallet::ResendWalletTransactions(bool fForce)
{
    if (!fForce)
    {
        // Do this infrequently and randomly to avoid giving away
        // that these are our transactions.
        static int64_t nNextTime;
        if (GetTime() < nNextTime)
            return;
        bool fFirst = (nNextTime == 0);
        nNextTime = GetTime() + GetRand(30 * 60);
        if (fFirst)
            return;

        // Only do it if there's been a new block since last time
        static int64_t nLastTime;
        if (nTimeBestReceived < nLastTime)
            return;
        nLastTime = GetTime();
    }

    // Rebroadcast any of our txes that aren't in a block yet
    printf("ResendWalletTransactions()\n");
    CTxDB txdb("r");
    {
        LOCK(cs_wallet);
        // Sort them in chronological order
        multimap<unsigned int, CWalletTx*> mapSorted;
        BOOST_FOREACH(PAIRTYPE(const uint256, CWalletTx)& item, mapWallet)
        {
            CWalletTx& wtx = item.second;
            // Don't rebroadcast until it's had plenty of time that
            // it should have gotten in already by now.
            if (fForce || nTimeBestReceived - (int64_t)wtx.nTimeReceived > 5 * 60)
                mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
        }
        BOOST_FOREACH(PAIRTYPE(const unsigned int, CWalletTx*)& item, mapSorted)
        {
            CWalletTx& wtx = *item.second;
            if (wtx.CheckTransaction())
                wtx.RelayWalletTransaction(txdb);
            else
                printf("ResendWalletTransactions() : CheckTransaction failed for transaction %s\n", wtx.GetHash().ToString().c_str());
        }
    }
}






//////////////////////////////////////////////////////////////////////////////
//
// Actions
//


// gets balance for coin of nColor
int64_t CWallet::GetBalance(int nColor) const
{
    // balance does not apply to multisigs in a straightforward way
    static const bool fMultiSig = false;

    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin();
                                                           it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            nTotal += pcoin->GetAvailableCredit(nColor, fMultiSig);
        }
    }
    return nTotal;
}

// TODO: make this a map and report only those colors that were ever known to wallet.
// Fills vector of length N_COLORS with all balances, indexed by color.
void CWallet::GetBalances(int nMinDepth, std::vector<int64_t> &vBalance) const
{
    // concept of a balance does not apply to multisigs
    static const bool fMultiSig = false;

    vBalance.clear();
    vBalance.resize(N_COLORS, 0);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin();
                                                 it != mapWallet.end(); ++it)
    {
        const CWalletTx& wtx = (*it).second;

        if (!wtx.IsTrusted())
            continue;

        int64_t allFee;
        string strSentAccount;
        list<pair<CTxDestination, int64_t> > listReceived;
        list<pair<CTxDestination, int64_t> > listSent;

        std::set<int> setColorsOut;
        wtx.GetColorsOut(setColorsOut);

        // iterates over each color, so fee color does not need to be explicit
        std::set<int>::const_iterator cit;
        for (cit = setColorsOut.begin(); cit != setColorsOut.end(); ++cit)
        {
              int nColor = (int) *cit;
              wtx.GetAmounts(nColor, listReceived, listSent, allFee, strSentAccount, fMultiSig);
              if (wtx.GetDepthInMainChain() >= nMinDepth && wtx.GetBlocksToMaturity() == 0)
              {
                  BOOST_FOREACH(const PAIRTYPE(CTxDestination,int64_t)& r, listReceived)
                      vBalance[nColor] += r.second;
              }
              BOOST_FOREACH(const PAIRTYPE(CTxDestination,int64_t)& r, listSent)
              {
                  vBalance[nColor] -= r.second;
              }
              vBalance[nColor] -= allFee;
        }
    }
}

// TODO: very nested, need to do some factoring
void CWallet::GetPrivateKeys(std::set<int> setColors, bool fMultiSig,
                             mapSecretByAddressByColor_t &mapAddrs) const
{
    mapAddrs.clear();
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin();
                                                     it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 0)
                continue;
            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                CTxOut out = pcoin->vout[i];
                if ((setColors.find(out.nColor) != setColors.end()) &&
                    !(pcoin->IsSpent(i)) &&
                    // spendable only
                    (IsMine(out, fMultiSig) & ISMINE_SPENDABLE) &&
                    out.nValue >= vMinimumInputValue[out.nColor])
                {
                    CTxDestination dest;
                    if (!ExtractDestination(out.scriptPubKey, dest))
                    {
                         // FIXME: add error message here
                         continue;
                    }
                    CBitcoinAddress address(dest, out.nColor);
                    CKeyID keyID;
                    if (!address.GetKeyID(keyID))
                    {
                         // FIXME: add error message here
                         continue;
                    }
                    // mit->first == color, mit->second == { address: (secret, value), ... }
                    mapSecretByAddressByColor_t::iterator mit = mapAddrs.find(out.nColor);
                    if (mit == mapAddrs.end())
                    {
                        // color doesn't exist, add color and address
                        CKey vchSecret;
                        if (!GetKey(keyID, vchSecret))
                        {
                             // FIXME: add error message here
                             continue;
                        }
                        CBitcoinSecret secret(vchSecret);
                        mapSecretByAddress_t mapSecrets;
                        mapSecrets[address] = make_pair(secret, out.nValue);
                        mapAddrs[out.nColor] = mapSecrets;
                    }
                    else
                    {
                        // color exists, find address
                        mapSecretByAddress_t &mapSecrets = mit->second;
                        // mmit->first == address, mmit->second == (secret, value)
                        mapSecretByAddress_t::iterator mmit = mapSecrets.find(address);
                        if (mmit == mapSecrets.end())
                        {
                            // address doesn't exist, add address and secret
                            CKey vchSecret;
                            if (!GetKey(keyID, vchSecret))
                            {
                                 // FIXME: add error message here
                                 continue;
                            }
                            CBitcoinSecret secret(vchSecret);
                            mapSecrets[address] = make_pair(secret, out.nValue);
                        }
                        else
                        {
                            // address exists, add out.nValue to second
                            pairAddressValue_t &pairAddrVal = mmit->second;
                            pairAddrVal.second += out.nValue;
                        }
                    }
                }
            }
        }
    }
}

// specific to breakout but a pointer could be passed for IsDeck()
//      and cardSorter to generalize
void CWallet::GetHand(int nMinDepth, std::vector<int> &vCards) const
{
    vCards.clear();

    std::vector<int64_t> vBalance;
    GetBalances(nMinDepth, vBalance);

    for (int nColor = 1; nColor < N_COLORS; ++nColor)
    {
        if (!IsDeck(nColor) || (vBalance[nColor] == 0))
        {
            continue;
        }
        // If this tests true, there's a bug in the protocol!
        if (vBalance[nColor] != COIN[nColor])
        {
            throw runtime_error(
                    strprintf("CRITICAL: Illegal amount %" PRId64 " of deck card %s.",
                                       vBalance[nColor], COLOR_TICKER[nColor]));
        }
        vCards.push_back(nColor);
    }
    std::sort(vCards.begin(), vCards.end(), cardSorter);
}


int64_t CWallet::GetUnconfirmedBalance(int nColor) const
{
    // balance does not apply to multisigs in a straightforward way
    static const bool fMultiSig = false;

    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;
            if (!pcoin->IsFinal() || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
                nTotal += pcoin->GetAvailableCredit(nColor, fMultiSig);
        }
    }
    return nTotal;
}

int64_t CWallet::GetImmatureBalance(int nColor) const
{
    // immature balance is not going to typically be multisig
    static const bool fMultiSig = false;

    int64_t nTotal = 0;
    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        {
            const CWalletTx& coin = (*it).second;
            if (coin.IsCoinBase() && coin.GetBlocksToMaturity() > 0 && coin.IsInMainChain())
                nTotal += GetCredit(coin, nColor, fMultiSig);
        }
    }
    return nTotal;
}

// populate vCoins with vector of spendable COutputs
// multisig will be considered available only in specific circumstances
void CWallet::AvailableCoins(int nColor, vector<COutput>& vCoins,
                bool fMultiSig, bool fOnlyConfirmed, const CCoinControl *coinControl) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin();
                                                     it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;

            if (!pcoin->IsFinal())
                continue;

            if (fOnlyConfirmed && !pcoin->IsTrusted())
                continue;

            if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            if(pcoin->IsCoinStake() && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < 0)
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                if ((pcoin->vout[i].nColor == nColor) &&
                    !(pcoin->IsSpent(i)) &&
                    // spendable only
                    (IsMine(pcoin->vout[i], fMultiSig) & ISMINE_SPENDABLE) &&
                    pcoin->vout[i].nValue >= vMinimumInputValue[pcoin->vout[i].nColor] &&
                    (!coinControl ||
                     !coinControl->HasSelected() ||
                     coinControl->IsSelected((*it).first, i)))
                {
                    vCoins.push_back(COutput(pcoin, i, nDepth));
                }
            }

        }
    }
}

// multisig will be considered available only in specific circumstances
void CWallet::AvailableCoinsMinConf(int nColor, vector<COutput>& vCoins, int nConf, bool fMultiSig) const
{
    vCoins.clear();

    {
        LOCK2(cs_main, cs_wallet);
        for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin();
                                                           it != mapWallet.end(); ++it)
        {
            const CWalletTx* pcoin = &(*it).second;

            if (!pcoin->IsFinal())
                continue;

            if(pcoin->GetDepthInMainChain() < nConf)
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                if ((pcoin->vout[i].nColor == nColor) &&
                     !(pcoin->IsSpent(i)) &&
                     // spendable only
                     (IsMine(pcoin->vout[i], fMultiSig) & ISMINE_SPENDABLE) &&
                     pcoin->vout[i].nValue >= vMinimumInputValue[pcoin->vout[i].nColor])
                {
                     vCoins.push_back(COutput(pcoin, i, pcoin->GetDepthInMainChain()));
                }
            }
        }
    }
}

static void ApproximateBestSubset(vector<pair<int64_t,
                                              pair<const CWalletTx*, unsigned int> > >vValue,
                                  int64_t nTotalLower, int64_t nTargetValue,
                                  vector<char>& vfBest, int64_t& nBest, int iterations = 1000)
{
    vector<char> vfIncluded;

    vfBest.assign(vValue.size(), true);
    nBest = nTotalLower;

    for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++)
    {
        vfIncluded.assign(vValue.size(), false);
        int64_t nTotal = 0;
        bool fReachedTarget = false;
        for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++)
        {
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                if (nPass == 0 ? rand() % 2 : !vfIncluded[i])
                {
                    nTotal += vValue[i].first;
                    vfIncluded[i] = true;
                    if (nTotal >= nTargetValue)
                    {
                        fReachedTarget = true;
                        if (nTotal < nBest)
                        {
                            nBest = nTotal;
                            vfBest = vfIncluded;
                        }
                        nTotal -= vValue[i].first;
                        vfIncluded[i] = false;
                    }
                }
            }
        }
    }
}

// ppcoin: total coins staked (non-spendable until maturity)
int64_t CWallet::GetStake(int nColor) const
{
    // in principle it is possible to stake multisig, but will ignore the issue here
    static const bool fMultiSig = false;

    int64_t nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (pcoin->IsCoinStake() &&
            (pcoin->GetBlocksToMaturity() > 0) && (pcoin->GetDepthInMainChain() > 0))
        {
            nTotal += CWallet::GetCredit(*pcoin, nColor, fMultiSig);
        }
    }
    return nTotal;
}

int64_t CWallet::GetNewMint(int nColor) const
{
    // in principle it is possible to mint multisig, but will avoid the issue here
    static const bool fMultiSig = false;

    int64_t nTotal = 0;
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
    {
        const CWalletTx* pcoin = &(*it).second;
        if (pcoin->IsCoinBase() &&
            (pcoin->GetBlocksToMaturity() > 0) && (pcoin->GetDepthInMainChain() > 0))
        {
            nTotal += CWallet::GetCredit(*pcoin, nColor, fMultiSig);
        }
    }
    return nTotal;
}

// vcoins is filtered on color, so no need to check color here!
bool CWallet::SelectCoinsMinConf(int64_t nTargetValue, unsigned int nSpendTime,
                         int nConfMine, int nConfTheirs, vector<COutput> vCoins,
                         set<pair<const CWalletTx*,unsigned int> >& setCoinsRet,
                         int64_t& nValueRet, bool fMultiSig) const
{
    setCoinsRet.clear();
    nValueRet = 0;

    // List of values less than target
    pair<int64_t, pair<const CWalletTx*,unsigned int> > coinLowestLarger;
    coinLowestLarger.first = std::numeric_limits<int64_t>::max();
    coinLowestLarger.second.first = NULL;
    vector<pair<int64_t, pair<const CWalletTx*,unsigned int> > > vValue;
    int64_t nTotalLower = 0;

    // vCoins is already single color
    int nColor = (int) BREAKOUT_COLOR_NONE;

    if (vCoins.size() > 0)
    {
         nColor = vCoins[0].tx->vout[vCoins[0].i].nColor;
    }

    random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);

    BOOST_FOREACH(COutput output, vCoins)
    {
        const CWalletTx *pcoin = output.tx;

        if (output.nDepth < (pcoin->IsFromMe(fMultiSig) ? nConfMine : nConfTheirs))
            continue;

        int i = output.i;

        // Follow the timestamp rules
        if (pcoin->nTime > nSpendTime)
            continue;

        int64_t n = pcoin->vout[i].nValue;

        pair<int64_t,pair<const CWalletTx*,unsigned int> > coin = make_pair(n,make_pair(pcoin, i));

        if (n == nTargetValue)
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            return true;
        }
        else if (n < nTargetValue + CENT[nColor])
        {
            vValue.push_back(coin);
            nTotalLower += n;
        }
        else if (n < coinLowestLarger.first)
        {
            coinLowestLarger = coin;
        }
    }

    if (nTotalLower == nTargetValue)
    {
        for (unsigned int i = 0; i < vValue.size(); ++i)
        {
            setCoinsRet.insert(vValue[i].second);
            nValueRet += vValue[i].first;
        }
        return true;
    }

    if (nTotalLower < nTargetValue)
    {
        if (coinLowestLarger.second.first == NULL)
            return false;
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
        return true;
    }

    // Solve subset sum by stochastic approximation
    sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
    vector<char> vfBest;
    int64_t nBest;

    ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);
    if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT[nColor])
        ApproximateBestSubset(vValue, nTotalLower,
                                        nTargetValue + CENT[nColor], vfBest, nBest, 1000);

    // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
    //                                   or the next bigger coin is closer), return the bigger coin
    if (coinLowestLarger.second.first &&
        ((nBest != nTargetValue && nBest < nTargetValue + CENT[nColor]) ||
                                                        coinLowestLarger.first <= nBest))
    {
        setCoinsRet.insert(coinLowestLarger.second);
        nValueRet += coinLowestLarger.first;
    }
    else {
        for (unsigned int i = 0; i < vValue.size(); i++)
            if (vfBest[i])
            {
                setCoinsRet.insert(vValue[i].second);
                nValueRet += vValue[i].first;
            }

        if (fDebug && GetBoolArg("-printpriority"))
        {
            //// debug print
            printf("SelectCoins() best subset: ");
            for (unsigned int i = 0; i < vValue.size(); i++)
            {
                if (vfBest[i])
                {
                    printf("%s ", FormatMoney(vValue[i].first, nColor).c_str());
                }
            }
            printf("total %s\n", FormatMoney(nBest, nColor).c_str());
        }
    }

    return true;
}

bool CWallet::SelectCoins(int64_t nTargetValue, int nColor, unsigned int nSpendTime,
                          set<pair<const CWalletTx*,unsigned int> >& setCoinsRet,
                          int64_t& nValueRet, bool fMultiSig, const CCoinControl* coinControl) const
{
    vector<COutput> vCoins;
    AvailableCoins(nColor, vCoins, fMultiSig, true, coinControl);

    // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
    if (coinControl && coinControl->HasSelected())
    {
        BOOST_FOREACH(const COutput& out, vCoins)
        {
            nValueRet += out.tx->vout[out.i].nValue;
            setCoinsRet.insert(make_pair(out.tx, out.i));
        }
        return (nValueRet >= nTargetValue);
    }

    // no color checking from here because AvailableCoins() already filtered
    return (SelectCoinsMinConf(nTargetValue, nSpendTime, 1, 6,
                           vCoins, setCoinsRet, nValueRet, fMultiSig) ||
            SelectCoinsMinConf(nTargetValue, nSpendTime, 1, 1,
                           vCoins, setCoinsRet, nValueRet, fMultiSig) ||
            SelectCoinsMinConf(nTargetValue, nSpendTime, 0, 1,
                           vCoins, setCoinsRet, nValueRet, fMultiSig));
}

// Select some coins without random shuffle or best subset approximation
bool CWallet::SelectCoinsSimple(int64_t nTargetValue, int nColor,
                              unsigned int nSpendTime, int nMinConf,
                              set<pair<const CWalletTx*,unsigned int> >& setCoinsRet,
                              int64_t& nValueRet, bool fMultiSig) const
{
    vector<COutput> vCoins;
    AvailableCoinsMinConf(nColor, vCoins, nMinConf, fMultiSig);

    setCoinsRet.clear();
    nValueRet = 0;

    BOOST_FOREACH(COutput output, vCoins)
    {
        const CWalletTx *pcoin = output.tx;
        int i = output.i;

        // Stop if we've chosen enough inputs
        if (nValueRet >= nTargetValue)
            break;

        // Follow the timestamp rules
        if (pcoin->nTime > nSpendTime)
            continue;

        int64_t n = pcoin->vout[i].nValue;

        pair<int64_t,pair<const CWalletTx*,unsigned int> > coin = make_pair(n,make_pair(pcoin, i));

        if (n >= nTargetValue)
        {
            // If input value is greater or equal to target then simply insert
            //    it into the current subset and exit
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
            break;
        }
        else if (n < nTargetValue + CENT[nColor])
        {
            setCoinsRet.insert(coin.second);
            nValueRet += coin.first;
        }
    }

    return true;
}

bool CWallet::CreateTransaction(const vector<pair<CScript, int64_t> >& vecSend, int nColor,
                                CWalletTx& wtxNew, CReserveKey& reservekey, CReserveKey &reservefeekey,
				int64_t& nFeeRet, int32_t& nChangePos, int32_t& nFeeChangePos,
                                std::string strTxComment, unsigned int nServiceTypeID,
                                const CCoinControl* coinControl)
{
    // create transaction assumes 100% ownership
    static const bool fMultiSig = false;

    // not a valid color
    if (nColor < 1 || nColor > N_COLORS)
    {
          return false;
    }
    int nFeeColor = FEE_COLOR[nColor];
    int64_t nValue = 0;
    BOOST_FOREACH (const PAIRTYPE(CScript, int64_t)& s, vecSend)
    {
        if (nValue < 0)
            return false;
        nValue += s.second;
    }
    if (vecSend.empty() || nValue < 0)
        return false;

    wtxNew.BindWallet(this);

    // product
    wtxNew.nServiceTypeID = nServiceTypeID;
    // transaction comment
    wtxNew.strTxComment = strTxComment;

    // just chop it off??? [TODO] Reject this transaction?
    if (wtxNew.strTxComment.length() > MAX_TX_COMMENT_LEN) {
           wtxNew.strTxComment.resize(MAX_TX_COMMENT_LEN);
    }

    {
        LOCK2(cs_main, cs_wallet);
        // txdb must be opened before the mapWallet lock
        CTxDB txdb("r");
        {
            nFeeRet = MIN_TX_FEE[nColor];
            while (true)
            {
                wtxNew.vin.clear();
                wtxNew.vout.clear();
                wtxNew.fFromMe = true;

                int64_t nTotalValue;
                int64_t nFeeValue;
                if (nColor == nFeeColor)
                {
                    nTotalValue = nValue + nFeeRet;
                    nFeeValue = 0;
                }
                else
                {
                    nTotalValue = nValue;
                    nFeeValue = nFeeRet;
                }
                // why is dPriority even calculated here?
                double dPriority = 0;
                // vouts to the payees
                BOOST_FOREACH (const PAIRTYPE(CScript, int64_t)& s, vecSend)
                {
                    wtxNew.vout.push_back(CTxOut(s.second, nColor, s.first));
                }

                // Choose coins to use
                set<pair<const CWalletTx*,unsigned int> > setCoins;
                int64_t nValueIn = 0;
                int64_t nFeeValueIn = 0;
                if (!SelectCoins(nTotalValue, nColor, wtxNew.nTime, setCoins,
                                              nValueIn, fMultiSig, coinControl))
                {
                    return false;
                }

                // this is a no-op if nFeeValue == 0 (nColor == nFeeColor)
                // TODO: find some way to use coin control for delegated fees
                set<pair<const CWalletTx*,unsigned int> > setFeeCoins;
                if (nFeeValue > 0)
                {
                    if (!SelectCoins(nFeeValue, nFeeColor, wtxNew.nTime, 
                                                setFeeCoins, nFeeValueIn, fMultiSig))
                    {
                        return false;
                    }
                }

                // dPriority isn't used, why are we doing this?
                BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
                {
                    int64_t nCredit = pcoin.first->vout[pcoin.second].nValue;
                    dPriority += (double)nCredit * pcoin.first->GetDepthInMainChain() *
                                                                     PRIORITY_MULTIPLIER[nColor];
                }

                if (nColor != nFeeColor)
                {
                    // dPriority isn't used, why are we doing this?
                    BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setFeeCoins)
                    {
                        int64_t nCredit = pcoin.first->vout[pcoin.second].nValue;
                        dPriority += (double)nCredit * pcoin.first->GetDepthInMainChain() *
                                                                         PRIORITY_MULTIPLIER[nFeeColor];
                    }
                }

                int64_t nChange;
                int64_t nFeeChange;
                if (nColor == nFeeColor)
                {
                     nChange = nValueIn - nValue - nFeeRet;
                     nFeeChange = 0;
                }
                else
                {
                     nChange = nValueIn - nValue;
                     nFeeChange = nFeeValueIn - nFeeValue;
                }

                // if sub-cent/10 change is required, the fee must be raised to at least MIN_TX_FEE
                // or until nChange becomes zero
                // NOTE: this depends on the exact behaviour of GetMinFee
                int64_t minTxFee = MIN_TX_FEE[nFeeColor];
                if (nColor == nFeeColor)
                {
                    if ((nFeeRet < minTxFee) && (nChange > 0) && (nChange < (CENT[nColor]/10)))
                    {
                        int64_t nMoveToFee = min(nChange, minTxFee - nFeeRet);
                        nChange -= nMoveToFee;
                        nFeeRet += nMoveToFee;
                    }
                }
                else
                {
                    // fee delegate:
                    // if sub-cent/10 change is required, the fee must be raised to at least MIN_TX_FEE
                    // or until nFeeChange becomes zero, taking the increase from the fee change
                    if ((nFeeRet < minTxFee) && (nChange > 0) && (nChange < (CENT[nColor]/10)))
                    {
                        int64_t nMoveToFee = min(nFeeChange, minTxFee - nFeeRet);
                        nFeeChange -= nMoveToFee;
                        nFeeRet += nMoveToFee;
                    }
                }

                //
                // change
                //
                // breakout: sub-tenth of a cent change is moved to fee
                if (nChange > (CENT[nColor] / 10))
                {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-bitcoin-address
                    CScript scriptChange;

                    // coin control: send change to custom address
                    if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange))
                    {
                        scriptChange.SetDestination(coinControl->destChange);
                    }
                    // no coin control: send change to newly generated address
                    else
                    {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        assert(reservekey.GetReservedKey(vchPubKey, nColor)); // should never fail, as we just unlocked

                        scriptChange.SetDestination(vchPubKey.GetID());
                    }
                    // Insert change txn at random position:
                    vector<CTxOut>::iterator position = wtxNew.vout.begin()+GetRandInt(wtxNew.vout.size() + 1);

                    // -- don't put change output between value and narration outputs
                    if (position > wtxNew.vout.begin() && position < wtxNew.vout.end())
                    {
                         while (position > wtxNew.vout.begin())
                         {
                             if (position->nValue != 0)
                                break;
                             position--;
                          };
                     };
                    wtxNew.vout.insert(position, CTxOut(nChange, nColor, scriptChange));
                    nChangePos = std::distance(wtxNew.vout.begin(), position);
                }
                else
                {
                    reservekey.ReturnKey();
                }

                //
                // fee delegate change
                //
                // breakout: sub-tenth of a cent fee change is moved to fee
                if ((nColor != nFeeColor) && (nFeeChange > (CENT[nFeeColor] / 10)))
                {
                    // Fill a vout to ourself
                    // TODO: pass in scriptChange instead of reservekey so
                    // change transaction isn't always pay-to-bitcoin-address
                    CScript scriptFeeChange;

                    // TODO: coin control for delegated fees
                    // no coin control: send change to newly generated address
                    {
                        // Note: We use a new key here to keep it from being obvious which side is the change.
                        //  The drawback is that by not reusing a previous key, the change may be lost if a
                        //  backup is restored, if the backup doesn't have the new private key for the change.
                        //  If we reused the old key, it would be possible to add code to look for and
                        //  rediscover unknown transactions that were written with keys of ours to recover
                        //  post-backup change.

                        // Reserve a new key pair from key pool
                        CPubKey vchPubKey;
                        // should never fail, as we just unlocked
                        assert(reservefeekey.GetReservedKey(vchPubKey, nFeeColor));

                        scriptFeeChange.SetDestination(vchPubKey.GetID());
                    }
                    // Insert change txn at random position...
                    //   ...but not first, which indicates the transaction color
                    vector<CTxOut>::iterator position = (wtxNew.vout.begin() + 1) +
                                                                GetRandInt(wtxNew.vout.size());

                    // -- don't put change output between value and narration outputs
                    if (position < wtxNew.vout.end())
                    {
                         while (position > wtxNew.vout.begin())
                         {
                             if (position->nValue != 0)
                                break;
                             position--;
                         };
                         // don't put fee delegate change in vout[0], so wrap around to end
                         if (position == wtxNew.vout.begin())
                         {
                              position = wtxNew.vout.end();
                         }
                    };
                    wtxNew.vout.insert(position, CTxOut(nFeeChange, nFeeColor, scriptFeeChange));
                    nFeeChangePos = std::distance(wtxNew.vout.begin(), position);
                }
                else
                {
                    reservefeekey.ReturnKey();
                }

                // Fill vin for payment
                BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
                {
                    wtxNew.vin.push_back(CTxIn(coin.first->GetHash(),coin.second));
                }

                // Fill vin for delegated fee
                if (nColor != nFeeColor)
                {
                    BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setFeeCoins)
                    {
                        wtxNew.vin.push_back(CTxIn(coin.first->GetHash(),coin.second));
                    }
                }

                // Sign
                int nIn = 0;
                BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setCoins)
                {
                    if (SignSignature(*this, *coin.first, wtxNew, nIn++) != 0)
                    {
                        return false;
                    }
                }

                // sign delegated fee inputs
                if (nColor != nFeeColor)
                {
                    BOOST_FOREACH(const PAIRTYPE(const CWalletTx*,unsigned int)& coin, setFeeCoins)
                    {
                        if (SignSignature(*this, *coin.first, wtxNew, nIn++) != 0)
                        {
                            return false;
                        }
                    }
                }

                // Limit size
                unsigned int nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew, SER_NETWORK, PROTOCOL_VERSION);
                if (nBytes >= MAX_BLOCK_SIZE_GEN/5)
                {
                    return false;
                }
                // waste CPUs doing this for no reason
                dPriority /= nBytes;

                // Check that enough fee is included
                int64_t nPayFee = vTransactionFee[nFeeColor] * (1 + (int64_t)nBytes / 1000);
                // Improve chances for quick confirm, pay as if block is 2% full
                int64_t nMinFee = wtxNew.GetMinFee(MAX_BLOCK_SIZE/50, GMF_SEND, nBytes);

                if (nFeeRet < max(nPayFee, nMinFee))
                {
                    nFeeRet = max(nPayFee, nMinFee);
                    continue;
                }

                // Fill vtxPrev by copying from previous transactions vtxPrev
                wtxNew.AddSupportingTransactions(txdb);
                wtxNew.fTimeReceivedIsTxTime = true;

                break;
            }
        }
    }
    return true;
}

bool CWallet::CreateTransaction(const vector<pair<CScript, int64_t> >& vecSend, int nColor,
                                CWalletTx& wtxNew, CReserveKey& reservekey, CReserveKey &reservefeekey,
                                int64_t& nFeeRet, const CCoinControl* coinControl) {
    int nChangePos;
    int nFeeChangePos;
    bool rv = CreateTransaction(vecSend, nColor, wtxNew, reservekey, reservefeekey, nFeeRet,
                                  nChangePos, nFeeChangePos, "", SERVICE_NONE, coinControl);
    return rv;

}

bool CWallet::CreateTransaction(CScript scriptPubKey, int64_t nValue, int nColor, std::string& sNarr,
                                CWalletTx& wtxNew, CReserveKey& reservekey, CReserveKey& reservefeekey,
                                int64_t& nFeeRet, std::string strTxComment, unsigned int nServiceTypeID,
                                const CCoinControl* coinControl)
{
    vector< pair<CScript, int64_t> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));
    
    if (sNarr.length() > 0)
    {
        std::vector<uint8_t> vNarr(sNarr.c_str(), sNarr.c_str() + sNarr.length());
        std::vector<uint8_t> vNDesc;
        
        vNDesc.resize(2);
        vNDesc[0] = 'n';
        vNDesc[1] = 'p';
        
        CScript scriptN = CScript() << OP_RETURN << vNDesc << OP_RETURN << vNarr;
        
        vecSend.push_back(make_pair(scriptN, 0));
    } else {
        sNarr = "";
    }
    
    // -- CreateTransaction won't place change between value and narr output.
    //    narration output will be for preceding output
    
    int nChangePos;
    int nFeeChangePos;
    bool rv = CreateTransaction(vecSend, nColor, wtxNew, reservekey, reservefeekey, nFeeRet,
                                nChangePos, nFeeChangePos, strTxComment, nServiceTypeID,
                                coinControl);

    // -- narration will be added to mapValue later in FindStealthTransactions From CommitTransaction

    return rv;
}

bool CWallet::CreateTransaction(CScript scriptPubKey, int64_t nValue, int nColor,
                                CWalletTx& wtxNew, CReserveKey& reservekey, CReserveKey& reservefeekey,
                                int64_t& nFeeRet, const CCoinControl*  coinControl)
{
    vector< pair<CScript, int64_t> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));
    int nChangePos;
    int nFeeChangePos;
    bool rv = CreateTransaction(vecSend, nColor, wtxNew, reservekey, reservefeekey, nFeeRet,
                                nChangePos, nFeeChangePos, "", SERVICE_NONE, coinControl);
    return rv;
}



bool CWallet::NewStealthAddress(int nColor, std::string& sError, std::string& sLabel, CStealthAddress& sxAddr)
{
    ec_secret scan_secret;
    ec_secret spend_secret;

    if (GenerateRandomSecret(scan_secret) != 0
        || GenerateRandomSecret(spend_secret) != 0)
    {
        sError = "GenerateRandomSecret failed.";
        printf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };

    ec_point scan_pubkey, spend_pubkey;
    if (SecretToPublicKey(scan_secret, scan_pubkey) != 0)
    {
        sError = "Could not get scan public key.";
        printf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };

    if (SecretToPublicKey(spend_secret, spend_pubkey) != 0)
    {
        sError = "Could not get spend public key.";
        printf("Error CWallet::NewStealthAddress - %s\n", sError.c_str());
        return false;
    };

    if (fDebug)
    {
        printf("getnewstealthaddress: ");
        printf("scan_pubkey ");
        for (uint32_t i = 0; i < scan_pubkey.size(); ++i)
          printf("%02x", scan_pubkey[i]);
        printf("\n");

        printf("spend_pubkey ");
        for (uint32_t i = 0; i < spend_pubkey.size(); ++i)
          printf("%02x", spend_pubkey[i]);
        printf("\n");
    };

    sxAddr.label = sLabel;
    sxAddr.scan_pubkey = scan_pubkey;
    sxAddr.spend_pubkey = spend_pubkey;
    sxAddr.nColor = nColor;

    sxAddr.scan_secret.resize(32);
    memcpy(&sxAddr.scan_secret[0], &scan_secret.e[0], 32);
    sxAddr.spend_secret.resize(32);
    memcpy(&sxAddr.spend_secret[0], &spend_secret.e[0], 32);

    return true;
}

bool CWallet::AddStealthAddress(CStealthAddress& sxAddr)
{
    LOCK(cs_wallet);

    // must add before changing spend_secret
    stealthAddresses.insert(sxAddr);

    bool fOwned = sxAddr.scan_secret.size() == ec_secret_size;

    if (fOwned)
    {
        // -- owned addresses can only be added when wallet is unlocked
        if (IsLocked())
        {
            printf("Error: CWallet::AddStealthAddress wallet must be unlocked.\n");
            stealthAddresses.erase(sxAddr);
            return false;
        };

        if (IsCrypted())
        {
            std::vector<unsigned char> vchCryptedSecret;
            CSecret vchSecret;
            vchSecret.resize(32);
            memcpy(&vchSecret[0], &sxAddr.spend_secret[0], 32);

            uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());
            if (!EncryptSecret(vMasterKey, vchSecret, iv, vchCryptedSecret))
            {
                printf("Error: Failed encrypting stealth key %s\n", sxAddr.Encoded().c_str());
                stealthAddresses.erase(sxAddr);
                return false;
            };
            sxAddr.spend_secret = vchCryptedSecret;
        };
    };


    bool rv = CWalletDB(strWalletFile).WriteStealthAddress(sxAddr);

    if (rv)
        NotifyAddressBookChanged(this, sxAddr, sxAddr.nColor, sxAddr.label, fOwned, CT_NEW);

    return rv;
}

bool CWallet::UnlockStealthAddresses(const CKeyingMaterial& vMasterKeyIn)
{
    // -- decrypt spend_secret of stealth addresses
    std::set<CStealthAddress>::iterator it;
    for (it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
    {
        if (it->scan_secret.size() < 32)
            continue; // stealth address is not owned

        // -- CStealthAddress are only sorted on spend_pubkey
        CStealthAddress &sxAddr = const_cast<CStealthAddress&>(*it);

        if (fDebug)
            printf("Decrypting stealth key %s\n", sxAddr.Encoded().c_str());

        CSecret vchSecret;
        uint256 iv = Hash(sxAddr.spend_pubkey.begin(), sxAddr.spend_pubkey.end());
        if(!DecryptSecret(vMasterKeyIn, sxAddr.spend_secret, iv, vchSecret)
            || vchSecret.size() != 32)
        {
            printf("Error: Failed decrypting stealth key %s\n", sxAddr.Encoded().c_str());
            continue;
        };

        ec_secret testSecret;
        memcpy(&testSecret.e[0], &vchSecret[0], 32);
        ec_point pkSpendTest;

        if (SecretToPublicKey(testSecret, pkSpendTest) != 0
            || pkSpendTest != sxAddr.spend_pubkey)
        {
            printf("Error: Failed decrypting stealth key, public key mismatch %s\n", sxAddr.Encoded().c_str());
            continue;
        };

        sxAddr.spend_secret.resize(32);
        memcpy(&sxAddr.spend_secret[0], &vchSecret[0], 32);
    };

    CryptedKeyMap::iterator mi = mapCryptedKeys.begin();
    for (; mi != mapCryptedKeys.end(); ++mi)
    {
        CPubKey &pubKey = (*mi).second.first;
        std::vector<unsigned char> &vchCryptedSecret = (*mi).second.second;
        if (vchCryptedSecret.size() != 0)
            continue;

        CKeyID ckid = pubKey.GetID();
        CBitcoinAddress addr(ckid, BREAKOUT_COLOR_NONE);

        StealthKeyMetaMap::iterator mi = mapStealthKeyMeta.find(ckid);
        if (mi == mapStealthKeyMeta.end())
        {
            printf("Error: No metadata found to add secret for %s\n", addr.ToString().c_str());
            continue;
        };

        CStealthKeyMetadata& sxKeyMeta = mi->second;

        CStealthAddress sxFind;
        sxFind.scan_pubkey = sxKeyMeta.pkScan.Raw();

        std::set<CStealthAddress>::iterator si = stealthAddresses.find(sxFind);
        if (si == stealthAddresses.end())
        {
            printf("No stealth key found to add secret for %s\n", addr.ToString().c_str());
            continue;
        };

        if (fDebug)
            printf("Expanding secret for %s\n", addr.ToString().c_str());

        ec_secret sSpendR;
        ec_secret sSpend;
        ec_secret sScan;

        if (si->spend_secret.size() != ec_secret_size
            || si->scan_secret.size() != ec_secret_size)
        {
            printf("Stealth Address has no secret key for %s\n", addr.ToString().c_str());
            continue;
        }
        memcpy(&sScan.e[0], &si->scan_secret[0], ec_secret_size);
        memcpy(&sSpend.e[0], &si->spend_secret[0], ec_secret_size);

        ec_point pkEphem = sxKeyMeta.pkEphem.Raw();
        if (StealthSecretSpend(sScan, pkEphem, sSpend, sSpendR) != 0)
        {
            printf("StealthSecretSpend() failed.\n");
            continue;
        };

        ec_point pkTestSpendR;
        if (SecretToPublicKey(sSpendR, pkTestSpendR) != 0)
        {
            printf("SecretToPublicKey() failed.\n");
            continue;
        };

        CSecret vchSecret;
        vchSecret.resize(ec_secret_size);

        memcpy(&vchSecret[0], &sSpendR.e[0], ec_secret_size);


        CPubKey cpkT;
        CKey ckey;

# if 0
                      // TODO: this seems like an extra step
                      CBitcoinSecret cbs(vchSecret, false);
                      ckey = cbs.GetKey();
                      // cpkT = ckey.GetPubKey();
#else

        // TODO: this was a quick hack for the new key system
        try
        {
            ckey = PubKeyWithSecret(vchSecret, cpkT);
        }
        catch (std::exception& e)
        {
            printf("ckey.SetSecret() threw: %s.\n", e.what());
            continue;
        };

#endif

        if (!cpkT.IsValid())
        {
            printf("cpkT is invalid.\n");
            continue;
        };

        if (cpkT != pubKey)
        {
            printf("Error: Generated secret does not match.\n");
            if (fDebug)
            {
                printf("cpkT   %s\n", HexStr(cpkT.Raw()).c_str());
                printf("pubKey %s\n", HexStr(pubKey.Raw()).c_str());
            };
            continue;
        };

        if (!ckey.IsValid())
        {
            printf("Here 2: Reconstructed key is invalid.\n");
            continue;
        };

        if (fDebug)
        {
            CKeyID keyID = cpkT.GetID();
            CBitcoinAddress coinAddress(keyID, BREAKOUT_COLOR_NONE);
            printf("Adding secret to key %s.\n", coinAddress.ToString().c_str());
        };

        if (!AddKey(ckey))
        {
            printf("AddKey failed.\n");
            continue;
        };

        if (!CWalletDB(strWalletFile).EraseStealthKeyMeta(ckid))
            printf("EraseStealthKeyMeta failed for %s\n", addr.ToString().c_str());
    };
    return true;
}

bool CWallet::UpdateStealthAddress(std::string &addr, std::string &label, bool addIfNotExist)
{
    if (fDebug)
        printf("UpdateStealthAddress %s\n", addr.c_str());


    CStealthAddress sxAddr;

    if (!sxAddr.SetEncoded(addr))
        return false;

    std::set<CStealthAddress>::iterator it;
    it = stealthAddresses.find(sxAddr);

    ChangeType nMode = CT_UPDATED;
    CStealthAddress sxFound;
    if (it == stealthAddresses.end())
    {
        if (addIfNotExist)
        {
            sxFound = sxAddr;
            sxFound.label = label;
            stealthAddresses.insert(sxFound);
            nMode = CT_NEW;
        } else
        {
            printf("UpdateStealthAddress %s, not in set\n", addr.c_str());
            return false;
        };
    } else
    {
        sxFound = const_cast<CStealthAddress&>(*it);

        if (sxFound.label == label)
        {
            // no change
            return true;
        };

        it->label = label; // update in .stealthAddresses

        if (sxFound.scan_secret.size() == ec_secret_size)
        {
            CStealthAddress sxOwned;

            if (!CWalletDB(strWalletFile).ReadStealthAddress(sxFound))
            {
                printf("UpdateStealthAddress: sxFound not in db.\n");
                return false;
            }
        };
    };

    sxFound.label = label;

    if (!CWalletDB(strWalletFile).WriteStealthAddress(sxFound))
    {
        printf("UpdateStealthAddress(%s) Write to db failed.\n", addr.c_str());
        return false;
    };

    bool fOwned = sxFound.scan_secret.size() == ec_secret_size;
    NotifyAddressBookChanged(this, sxFound, sxFound.nColor, sxFound.label, fOwned, nMode);

    return true;
}

bool CWallet::CreateStealthTransaction(CScript scriptPubKey, int64_t nValue, int nColor,
                                       std::vector<uint8_t>& P, std::vector<uint8_t>& narr,
                                       std::string& sNarr, CWalletTx& wtxNew,
                                       CReserveKey& reservekey, CReserveKey& reservefeekey,
                                       int64_t& nFeeRet, const CCoinControl* coinControl)
{
    vector< pair<CScript, int64_t> > vecSend;
    vecSend.push_back(make_pair(scriptPubKey, nValue));

    CScript scriptP = CScript() << OP_RETURN << P;
    if (narr.size() > 0)
        scriptP = scriptP << OP_RETURN << narr;

    vecSend.push_back(make_pair(scriptP, 0));

    // -- shuffle inputs, change output won't mix enough as it must be not fully random for plantext narrations
    std::random_shuffle(vecSend.begin(), vecSend.end());

    bool rv = CreateTransaction(vecSend, nColor, wtxNew, reservekey, reservefeekey, nFeeRet, coinControl);

    // -- the change txn is inserted in a random pos, check here to match narr to output
    if (rv && narr.size() > 0)
    {
        for (unsigned int k = 0; k < wtxNew.vout.size(); ++k)
        {
            if (wtxNew.vout[k].scriptPubKey != scriptPubKey
                || wtxNew.vout[k].nValue != nValue)
                continue;

            char key[64];
            if (snprintf(key, sizeof(key), "n_%u", k) < 1)
            {
                printf("CreateStealthTransaction(): Error creating narration key.");
                break;
            };
            wtxNew.mapValue[key] = sNarr;
            break;
        };
    };

    return rv;
}

string CWallet::SendStealthMoney(CScript scriptPubKey, int64_t nValue, int nColor,
                                 std::vector<uint8_t>& P, std::vector<uint8_t>& narr,
                                 std::string& sNarr, CWalletTx& wtxNew, bool fAskFee)
{
    CReserveKey reservekey(this);
    CReserveKey reservefeekey(this);
    int64_t nFeeRequired;

    if (IsLocked())
    {
        string strError = _("Error: Wallet locked, unable to create transaction  ");
        printf("SendStealthMoney() : %s", strError.c_str());
        return strError;
    }
    if (fWalletUnlockStakingOnly)
    {
        string strError = _("Error: Wallet unlocked for staking only, unable to create transaction.");
        printf("SendStealthMoney() : %s", strError.c_str());
        return strError;
    }

    int nFeeColor = FEE_COLOR[nColor];
    if (!CreateStealthTransaction(scriptPubKey, nValue, nColor, P,
                                  narr, sNarr, wtxNew, reservekey, reservefeekey, nFeeRequired))
    {
        string strError;

        if (nValue > GetBalance(nColor))
        {
            // strError = strprintf(_("Error: insufficient funds."));
            strError = _("Error: insufficient funds.");
        }
        else if ( ((nColor == nFeeColor) && (nValue + nFeeRequired > GetBalance(nColor))) ||
                  ((nColor != nFeeColor) && (nFeeRequired > GetBalance(nFeeColor))) )
        {
            strError = strprintf(_("Error: This transaction requires a transaction fee of at "
                                   "least %s %s because of its amount, complexity, "
                                   "or use of recently received funds."),
                                      FormatMoney(nFeeRequired, nFeeColor).c_str(),
                                      COLOR_TICKER[nFeeColor]);
        }
        else
        {
            strError = _("Error: Transaction creation failed  ");
        }
        printf("SendStealthMoney() : %s", strError.c_str());
        return strError;
    }

    if (fAskFee && !uiInterface.ThreadSafeAskFee(nFeeRequired, nFeeColor, _("Sending...")))
        return "ABORTED";

    if (!CommitTransaction(wtxNew, reservekey, reservefeekey))
        return _("Error: The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

    return "";
}

bool CWallet::SendStealthMoneyToDestination(CStealthAddress& sxAddress, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew, std::string& sError, bool fAskFee)
{
    // -- Check amount
    if (nValue <= 0)
    {
        sError = "Invalid amount";
        return false;
    };

    // tx color
    int nColor = sxAddress.nColor;

    int nFeeColor = FEE_COLOR[nColor];


    if (nFeeColor == nColor)
    {
        if (nValue + vTransactionFee[nColor] > GetBalance(nColor))
        {
            sError = "Insufficient funds";
            return false;
        }
    }
    else
    {
        if ((nValue > GetBalance(nColor)) || (vTransactionFee[nFeeColor] > GetBalance(nFeeColor)))
        {
            sError = "Insufficient funds";
            return false;
        }
    }

    ec_secret ephem_secret;
    ec_secret secretShared;
    ec_point pkSendTo;
    ec_point ephem_pubkey;

    if (GenerateRandomSecret(ephem_secret) != 0)
    {
        sError = "GenerateRandomSecret failed.";
        return false;
    };

    if (StealthSecret(ephem_secret, sxAddress.scan_pubkey, sxAddress.spend_pubkey, secretShared, pkSendTo) != 0)
    {
        sError = "Could not generate receiving public key.";
        return false;
    };

    CPubKey cpkTo(pkSendTo);
    if (!cpkTo.IsValid())
    {
        sError = "Invalid public key generated.";
        return false;
    };

    CKeyID ckidTo = cpkTo.GetID();

    CBitcoinAddress addrTo(ckidTo, nColor);

    if (SecretToPublicKey(ephem_secret, ephem_pubkey) != 0)
    {
        sError = "Could not generate ephem public key.";
        return false;
    };

    if (fDebug)
    {
        printf("Stealth send to generated pubkey %"PRIszu": %s\n", pkSendTo.size(), HexStr(pkSendTo).c_str());
        printf("hash %s\n", addrTo.ToString().c_str());
        printf("ephem_pubkey %"PRIszu": %s\n", ephem_pubkey.size(), HexStr(ephem_pubkey).c_str());
    };

    std::vector<unsigned char> vchNarr;
    if (sNarr.length() > 0)
    {
        SecMsgCrypter crypter;
        crypter.SetKey(&secretShared.e[0], &ephem_pubkey[0]);

        if (!crypter.Encrypt((uint8_t*)&sNarr[0], sNarr.length(), vchNarr))
        {
            sError = "Narration encryption failed.";
            return false;
        };

        if (vchNarr.size() > 48)
        {
            sError = "Encrypted narration is too long.";
            return false;
        };
    } else {
        sNarr = "";
    }

    // -- Parse Bitcoin address
    CScript scriptPubKey;
    scriptPubKey.SetDestination(addrTo.Get());

    if ((sError = SendStealthMoney(scriptPubKey, nValue, nColor,
                                   ephem_pubkey, vchNarr, sNarr, wtxNew, fAskFee)) != "")
        return false;


    return true;
}

bool CWallet::FindStealthTransactions(const CTransaction& tx, mapValue_t& mapNarr)
{
    if (fDebug)
        printf("FindStealthTransactions() tx: %s\n", tx.GetHash().GetHex().c_str());

    mapNarr.clear();

    LOCK(cs_wallet);
    ec_secret sSpendR;
    ec_secret sSpend;
    ec_secret sScan;
    ec_secret sShared;

    ec_point pkExtracted;

    std::vector<uint8_t> vchEphemPK;
    std::vector<uint8_t> vchDataB;
    std::vector<uint8_t> vchENarr;
    opcodetype opCode;
    char cbuf[256];

    int32_t nOutputIdOuter = -1;
    BOOST_FOREACH(const CTxOut& txout, tx.vout)
    {
        nOutputIdOuter++;
        // -- for each OP_RETURN need to check all other valid outputs

        //printf("txout scriptPubKey %s\n",  txout.scriptPubKey.ToString().c_str());
        CScript::const_iterator itTxA = txout.scriptPubKey.begin();

        if (!txout.scriptPubKey.GetOp(itTxA, opCode, vchEphemPK)
            || opCode != OP_RETURN)
            continue;
        else
        if (!txout.scriptPubKey.GetOp(itTxA, opCode, vchEphemPK)
            || vchEphemPK.size() != 33)
        {
            // -- look for plaintext narrations
            if (vchEphemPK.size() > 1
                && vchEphemPK[0] == 'n'
                && vchEphemPK[1] == 'p')
            {
                if (txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
                    && opCode == OP_RETURN
                    && txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
                    && vchENarr.size() > 0)
                {
                    std::string sNarr = std::string(vchENarr.begin(), vchENarr.end());

                    snprintf(cbuf, sizeof(cbuf), "n_%d", nOutputIdOuter-1); // plaintext narration always matches preceding value output
                    mapNarr[cbuf] = sNarr;
                } else
                {
                    printf("Warning: FindStealthTransactions() tx: %s, Could not extract plaintext narration.\n", tx.GetHash().GetHex().c_str());
                };
            }

            continue;
        }

        int32_t nOutputId = -1;
        nStealth++;
        BOOST_FOREACH(const CTxOut& txoutB, tx.vout)
        {
            nOutputId++;

            // printf("The nOutputID is: %d\n", nOutputId);

            if (&txoutB == &txout)
                continue;

            bool txnMatch = false; // only 1 txn will match an ephem pk
            //printf("txoutB scriptPubKey %s\n",  txoutB.scriptPubKey.ToString().c_str());

            CTxDestination address;
            if (!ExtractDestination(txoutB.scriptPubKey, address))
            {
                if (fDebug)
                {
                    printf("CWallet::FindStealthTransactions: Could not extract destination %s.\n",
                                      CBitcoinAddress(address, txoutB.nColor).ToString().c_str());
                }
                continue;
            }

            if (address.type() != typeid(CKeyID))
            {
                if (fDebug)
                {
                    printf("CWallet::FindStealthTransactions: Address is not right type %s.\n",
                                      CBitcoinAddress(address, txoutB.nColor).ToString().c_str());
                }
                continue;
            }

            CKeyID ckidMatch = boost::get<CKeyID>(address);

            // The vchEphemPK needs to be extracted to scan for encrypted narrations
            // even if the key has already been added.
            std::set<CStealthAddress>::iterator it;
            for (it = stealthAddresses.begin(); it != stealthAddresses.end(); ++it)
            {
                if (it->scan_secret.size() != ec_secret_size)
                {
                    continue; // Stealth Address is not owned
                }

                //printf("it->Encodeded() %s\n",  it->Encoded().c_str());
                memcpy(&sScan.e[0], &it->scan_secret[0], ec_secret_size);

                if (StealthSecret(sScan, vchEphemPK, it->spend_pubkey, sShared, pkExtracted) != 0)
                {
                    printf("CWallet::FindStealthTransactions: StealthSecret failed.\n");
                    continue;
                };
                //printf("pkExtracted %"PRIszu": %s\n", pkExtracted.size(), HexStr(pkExtracted).c_str());

                CPubKey cpkE(pkExtracted);

                if (!cpkE.IsValid())
                    continue;
                CKeyID ckidE = cpkE.GetID();

                if (ckidMatch != ckidE)
                    continue;

                if (fDebug)
                    printf("Found stealth txn to address %s\n", it->Encoded().c_str());

                if (!HaveKey(ckidMatch)) // no point adding if already have key
                {
                  if (IsLocked())
                  {
                      if (fDebug)
                          printf("Wallet is locked, adding key without secret.\n");

                      // -- add key without secret
                      std::vector<uint8_t> vchEmpty;
                      AddCryptedKey(cpkE, vchEmpty);
                      CKeyID keyId = cpkE.GetID();
                      CBitcoinAddress coinAddress(keyId, txoutB.nColor);
                      std::string sLabel = it->Encoded();
                      SetAddressBookName(keyId, txoutB.nColor, sLabel);

                      CPubKey cpkEphem(vchEphemPK);
                      CPubKey cpkScan(it->scan_pubkey);
                      CStealthKeyMetadata lockedSkMeta(cpkEphem, cpkScan);

                      if (!CWalletDB(strWalletFile).WriteStealthKeyMeta(keyId, lockedSkMeta))
                          printf("WriteStealthKeyMeta failed for %s\n", coinAddress.ToString().c_str());

                      mapStealthKeyMeta[keyId] = lockedSkMeta;
                      nFoundStealth++;
                  }
                  else
                  {
                      if (it->spend_secret.size() != ec_secret_size)
                          continue;
                      memcpy(&sSpend.e[0], &it->spend_secret[0], ec_secret_size);


                      if (StealthSharedToSecretSpend(sShared, sSpend, sSpendR) != 0)
                      {
                          printf("StealthSharedToSecretSpend() failed.\n");
                          continue;
                      };

                      ec_point pkTestSpendR;
                      if (SecretToPublicKey(sSpendR, pkTestSpendR) != 0)
                      {
                          printf("SecretToPublicKey() failed.\n");
                          continue;
                      };

                      CSecret vchSecret;
                      vchSecret.resize(ec_secret_size);

                      memcpy(&vchSecret[0], &sSpendR.e[0], ec_secret_size);

                      CPubKey cpkT;
                      CKey ckey;

# if 0
                      // TODO: this seems like an extra step
                      CBitcoinSecret cbs(vchSecret, false);
                      ckey = cbs.GetKey();
                      // cpkT = ckey.GetPubKey();
#else

                      try
                      {
                          ckey = PubKeyWithSecret(vchSecret, cpkT);
                      }
                      catch (std::exception& e)
                      {
                          printf("ckey.SetSecret() threw: %s.\n", e.what());
                          continue;
                      };


#endif

                      if (!cpkT.IsValid())
                      {
                          printf("cpkT is invalid.\n");
                          continue;
                      };

                      if (!ckey.IsValid())
                      {
                          printf("Here: Reconstructed key is invalid.\n");
                          continue;
                      };

                      CKeyID keyID = cpkT.GetID();
                      if (fDebug)
                      {
                          CBitcoinAddress coinAddress(keyID, txoutB.nColor);
                          printf("Adding key %s.\n", coinAddress.ToString().c_str());
                      };

                      if (!AddKey(ckey))
                      {
                          printf("AddKey failed.\n");
                          continue;
                      };

                      std::string sLabel = it->Encoded();
                      SetAddressBookName(keyID, txoutB.nColor, sLabel);
                      nFoundStealth++;
                  };
                }

                // on rescan, must decrypt any encrypted narrations
                if (txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
                    && opCode == OP_RETURN
                    && txout.scriptPubKey.GetOp(itTxA, opCode, vchENarr)
                    && vchENarr.size() > 0)
                {
                    SecMsgCrypter crypter;
                    crypter.SetKey(&sShared.e[0], &vchEphemPK[0]);
                    std::vector<uint8_t> vchNarr;
                    if (!crypter.Decrypt(&vchENarr[0], vchENarr.size(), vchNarr))
                    {
                        printf("Decrypt narration failed.\n");
                        continue;
                    };
                    std::string sNarr = std::string(vchNarr.begin(), vchNarr.end());

                    snprintf(cbuf, sizeof(cbuf), "n_%d", nOutputId);
                    mapNarr[cbuf] = sNarr;
                };

                txnMatch = true;
                break;
            };
            if (txnMatch)
                break;
        };
    };

    return true;
};

// NovaCoin: get current stake weight for currency of nColor
bool CWallet::GetStakeWeightByColor(int nColor, const CKeyStore& keystore, uint64_t& nWeight)
{
    // staking is not for multisigs
    static const bool fMultiSig = false;

    // ADVISORY: static is an optimization, may not be suitable for forks
    static int nCoinbaseMaturity = GetCoinbaseMaturity();

    // this check is to prevent crash when indexing vReserveBalance
    if (!CanStake(nColor))
    {
           printf("GetStakeWeightByColor(): currency can't stake: %s\n",
                                                     COLOR_TICKER[nColor]);
           return false;
    }

    // Choose coins to use
    int64_t nBalance = GetBalance(nColor);

    int64_t nResBal = vReserveBalance[nColor];

    if (nBalance <= nResBal)
        return false;

    vector<const CWalletTx*> vwtxPrev;

    set<pair<const CWalletTx*,unsigned int> > setCoins;
    int64_t nValueIn = 0;

    if (!SelectCoinsSimple(nBalance - nResBal, nColor,
                  GetTime(), nCoinbaseMaturity + 1, setCoins, nValueIn, fMultiSig))
        return false;

    if (setCoins.empty())
        return false;

    int nStakeMinConfs = GetStakeMinConfirmations(nColor);

    LOCK2(cs_main, cs_wallet);
    BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
    {
        if (pcoin.first->GetDepthInMainChain() >= nStakeMinConfs)
        {
            nWeight += pcoin.first->vout[pcoin.second].nValue / COIN[nColor];
        }
    }

    nWeight *= GetWeightMultiplier(nColor, pindexBest->nTime);

    return true;
}

// get the weighted sum of stake weights for all staking currencies
bool CWallet::GetStakeWeight(const CKeyStore& keystore, uint64_t& nWeight)
{
    nWeight = 0;
    for (int nColor = 1; nColor < N_COLORS; ++nColor)
    {
          if (CanStake(nColor))
          {
               if (!GetStakeWeightByColor(nColor, keystore, nWeight))
               {
                        return false;
               }
          }
    }
    return true;
}


// coin stake is CBlock::vtx[1] and has vout[0] empty
// mint for staking block is put int vtx[0] that has vout[0] (and vout[1]?) empty
bool CWallet::CreateCoinStake(int nStakeColor, const CKeyStore& keystore,
                                unsigned int nBits, int64_t nSearchInterval,
                                  int64_t nFees[], CTransaction& txMint, CTransaction& txStake, CKey& key)
{
    // would not make sense to stake partail-ownership multisig
    static const bool fMultiSig = false;

    if (!CanStake(nStakeColor))
    {
           return false;
    }

    CBlockIndex* pindexPrev = pindexBest;
    CBigNum bnTargetPerCoinDay;
    bnTargetPerCoinDay.SetCompact(nBits);

    // txMint.vin.clear();
    txMint.vout.clear();
    txStake.vin.clear();
    txStake.vout.clear();

    // Mark mint and stake transactions
    CScript scriptEmpty;
    scriptEmpty.clear();

    // Mark coin mint transaction just like PoW (first of PoS block):
    //   1. vin[0].prevout.IsNull()
    //   2. vin.size() == 1
    //   3. !vout[0].IsEmpty()
    //   4. vout.size() >= 2         -- Allow for fees
    txMint.vin.resize(1);
    txMint.vin[0].prevout.SetNull();

    // Mark coin stake transaction (second of PoS block):
    //   1. vout[0].IsEmpty()
    txStake.vout.push_back(CTxOut(0, nStakeColor, scriptEmpty));

    // Choose coins to use
    int64_t nBalance = GetBalance(nStakeColor);
    int64_t nResBal = vReserveBalance[nStakeColor];

    if (nBalance <= nResBal)
    {
        return false;
    }

    vector<const CWalletTx*> vwtxPrev;

    set<pair<const CWalletTx*,unsigned int> > setCoins;

    // unused, placeholder
    int64_t nValueIn = 0;

    int nStakeMinConfs = GetStakeMinConfirmations(nStakeColor);

    // Select coins with suitable depth and correct color
    if (!SelectCoinsSimple(nBalance - nResBal, nStakeColor, txStake.nTime,
                              nStakeMinConfs, setCoins, nValueIn, fMultiSig)) {
        return false;
    }

    if (setCoins.empty()) {
        return false;
    }

    int64_t nStakeCredit = 0;
    CScript scriptPubKeyKernel;
    CTxDB txdb("r");
    BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
    {
        CTxIndex txindex;
        {
            LOCK2(cs_main, cs_wallet);
            if (!txdb.ReadTxIndex(pcoin.first->GetHash(), txindex))
                continue;
        }

        // Read block header
        CBlock block;
        {
            LOCK2(cs_main, cs_wallet);
            if (!block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                continue;
        }


        static int nMaxStakeSearchInterval = 60;

        bool fKernelFound = false;
        for (unsigned int n=0; n<min(nSearchInterval,(int64_t)nMaxStakeSearchInterval) && !fKernelFound && !fShutdown && pindexPrev == pindexBest; n++)
        {
            // Search backward in time from the given txStake timestamp 
            // Search nSearchInterval seconds back up to nMaxStakeSearchInterval
            COutPoint prevoutStake = COutPoint(pcoin.first->GetHash(), pcoin.second);
            int64_t nBlockTime;
            if (CheckKernel(nStakeColor, pindexPrev, nBits,
                            txStake.nTime - n, prevoutStake, &nBlockTime))
            {
                // Found a kernel
                if (fDebug && GetBoolArg("-printcoinstake"))
                    printf("CreateCoinStake : kernel found\n");
                vector<valtype> vSolutions;
                txnouttype whichType;
                CScript scriptPubKeyOut;
                scriptPubKeyKernel = pcoin.first->vout[pcoin.second].scriptPubKey;
                if (!Solver(scriptPubKeyKernel, whichType, vSolutions))
                {
                    if (fDebug && GetBoolArg("-printcoinstake"))
                        printf("CreateCoinStake : failed to parse kernel\n");
                    break;
                }
                if (fDebug && GetBoolArg("-printcoinstake"))
                    printf("CreateCoinStake : parsed kernel type=%d\n", whichType);
                if (whichType != TX_PUBKEY && whichType != TX_PUBKEYHASH)
                {
                    if (fDebug && GetBoolArg("-printcoinstake"))
                        printf("CreateCoinStake : no support for kernel type=%d\n", whichType);
                    break;  // only support pay to public key and pay to address
                }
                if (whichType == TX_PUBKEYHASH) // pay to address type
                {
                    CKeyID ckid(uint160(vSolutions[0]), nStakeColor);
                    // convert to pay to public key type
                    if (!keystore.GetKey(ckid, key))
                    {
                        if (fDebug && GetBoolArg("-printcoinstake"))
                            printf("CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                        break;  // unable to find corresponding public key
                    }
                    scriptPubKeyOut << key.GetPubKey() << OP_CHECKSIG;
                }
                if (whichType == TX_PUBKEY)
                {
                    valtype& vchPubKey = vSolutions[0];
                    CKeyID ckid(Hash160(vchPubKey), nStakeColor);
                    if (!keystore.GetKey(ckid, key))
                    {
                        if (fDebug && GetBoolArg("-printcoinstake"))
                            printf("CreateCoinStake : failed to get key for kernel type=%d\n", whichType);
                        break;  // unable to find corresponding public key
                    }

                if (key.GetPubKey() != vchPubKey)
                {
                    if (fDebug && GetBoolArg("-printcoinstake"))
                        printf("CreateCoinStake : invalid key for kernel type=%d\n", whichType);
                        break; // keys mismatch
                    }

                    scriptPubKeyOut = scriptPubKeyKernel;
                }

                txStake.nTime -= n;
                txStake.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
                nStakeCredit += pcoin.first->vout[pcoin.second].nValue;
                vwtxPrev.push_back(pcoin.first);
                txStake.vout.push_back(CTxOut(0, nStakeColor, scriptPubKeyOut));
                if (GetWeight(block.GetBlockTime(), (int64_t)txStake.nTime, nStakeColor, pindexPrev->nTime) < nStakeSplitAge)
                {
                    txStake.vout.push_back(CTxOut(0, nStakeColor, scriptPubKeyOut)); //split stake
                }
                if (fDebug && GetBoolArg("-printcoinstake"))
                {
                    printf("CreateCoinStake : added kernel type=%d\n", whichType);
                }
                fKernelFound = true;
                break;
            }
        }

        if (fKernelFound || fShutdown)
            break; // if kernel is found stop searching
    }

    if ((nStakeCredit == 0) || (nStakeCredit > (nBalance - nResBal)))
    {
        return false;
    }

    BOOST_FOREACH(PAIRTYPE(const CWalletTx*, unsigned int) pcoin, setCoins)
    {
        // Attempt to add more inputs
        // Only add coins of the same key/address as kernel
        if (txStake.vout.size() == 2 && ((pcoin.first->vout[pcoin.second].scriptPubKey == scriptPubKeyKernel ||
                                          pcoin.first->vout[pcoin.second].scriptPubKey == txStake.vout[1].scriptPubKey)) &&
            pcoin.first->GetHash() != txStake.vin[0].prevout.hash)
        {
            // Stop adding more inputs if already too many inputs
            if (txStake.vin.size() >= 100)
                break;
            // Stop adding more inputs if value is already pretty significant
            if (nStakeCredit >= STAKE_COMBINE_THRESHOLD[nStakeColor])
                break;
            // Stop adding inputs if reached reserve limit
            if (nStakeCredit + pcoin.first->vout[pcoin.second].nValue > (nBalance - nResBal))
                break;
            // Do not add additional significant input
            if (pcoin.first->vout[pcoin.second].nValue >= STAKE_COMBINE_THRESHOLD[nStakeColor])
                continue;
            // Do not add input that is still too young

            txStake.vin.push_back(CTxIn(pcoin.first->GetHash(), pcoin.second));
            nStakeCredit += pcoin.first->vout[pcoin.second].nValue;
            vwtxPrev.push_back(pcoin.first);
        }
    }

    // Calculate PoS reward
    {
#if COINAGE_DEPENDENT_POS
        // breakout uses block rewards that do not depend on coin age
        uint64_t nCoinAge;
        CTxDB txdb("r");
        if (!txStake.GetCoinAge(txdb, nCoinAge))
            return error("CreateCoinStake : failed to calculate coin age");
#endif

        struct AMOUNT reward = GetProofOfStakeReward(pindexPrev, nStakeColor);

        if (reward.nValue <= 0) {
              printf("CreateCoinStake: Reward is not greater than 0.\n");
              return false;
        }
        // TODO: save some CPU cycles modifying the existing CTxOut
        // Set mint output amount and color -- mint to same pub key as stake kernel
        assert (txMint.vout.size() == 0);
        txMint.vout.push_back(CTxOut(reward.nValue, reward.nColor, txStake.vout[1].scriptPubKey));
    }

    // Set stake output amount and color
    if (txStake.vout.size() == 3)
    {
        txStake.vout[1].nValue = ((nStakeCredit / 2) / CENT[nStakeColor]) * CENT[nStakeColor];
        txStake.vout[1].nColor = nStakeColor;
        txStake.vout[2].nValue = nStakeCredit - txStake.vout[1].nValue;
        txStake.vout[2].nColor = nStakeColor;
    }
    else
    {
        txStake.vout[1].nValue = nStakeCredit;
        txStake.vout[1].nColor = nStakeColor;
    }

    // small stakes will split into the full stake and a zero output
    // remove zero outputs for coinstake except for vout[0]
    for (int i = 1; i < (int) txStake.vout.size(); ++i)
    {
        if (txStake.vout[i].nValue == 0)
        { 
            txStake.vout.erase(txStake.vout.begin() + i);
        }
    }

    // Sign
    int nIn = 0;
    BOOST_FOREACH(const CWalletTx* pcoin, vwtxPrev)
    {
        if (SignSignature(*this, *pcoin, txStake, nIn++) != 0)
            return error("CreateCoinStake : failed to sign coinstake");
    }

    // Limit size
    unsigned int nBytes = ::GetSerializeSize(txMint, SER_NETWORK, PROTOCOL_VERSION) +
                          ::GetSerializeSize(txStake, SER_NETWORK, PROTOCOL_VERSION);
    if (nBytes >= MAX_BLOCK_SIZE_GEN/5)
        return error("CreateCoinStake : exceeded coinstake size limit");

    // Successfully generated coinstake
    return true;
}


// Call after CreateTransaction unless you want to abort
bool CWallet::CommitTransaction(CWalletTx& wtxNew,
                                CReserveKey& reservekey, CReserveKey &reservefeekey)
{
    mapValue_t mapNarr;
    FindStealthTransactions(wtxNew, mapNarr);

    if (!mapNarr.empty())
    {
           BOOST_FOREACH(const PAIRTYPE(string,string)& item, mapNarr)
                               wtxNew.mapValue[item.first] = item.second;
    }

    {
        LOCK2(cs_main, cs_wallet);
        printf("CommitTransaction:\n%s", wtxNew.ToString().c_str());
        {
            // This is only to keep the database open to defeat the auto-flush for the
            // duration of this scope.  This is the only place where this optimization
            // maybe makes sense; please don't do it anywhere else.
            CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile,"r") : NULL;

            // Take key pair from key pool so it won't be used again
            reservekey.KeepKey();
            reservefeekey.KeepKey();

            // Add tx to wallet, because if it has change it's also ours,
            // otherwise just for transaction history.
            AddToWallet(wtxNew);

            // Mark old coins as spent
            set<CWalletTx*> setCoins;
            BOOST_FOREACH(const CTxIn& txin, wtxNew.vin)
            {
                CWalletTx &coin = mapWallet[txin.prevout.hash];
                coin.BindWallet(this);
                coin.MarkSpent(txin.prevout.n);
                coin.WriteToDisk();
                NotifyTransactionChanged(this, coin.GetHash(), CT_UPDATED);
            }

            if (fFileBacked)
                delete pwalletdb;
        }

        // Track how many getdata requests our transaction gets
        mapRequestCount[wtxNew.GetHash()] = 0;

        // Broadcast
        if (!wtxNew.AcceptToMemoryPool())
        {
            // This must not fail. The transaction has already been signed and recorded.
            printf("CommitTransaction() : Error: Transaction not valid\n");
            return false;
        }
        wtxNew.RelayWalletTransaction();
    }
    return true;
}



// SendMoney will never try to send multisig inputs
string CWallet::SendMoney(CScript scriptPubKey, int64_t nValue, int nColor,
                          std::string& sNarr, CWalletTx& wtxNew, bool fAskFee,
                          std::string strTxComment, int nServiceTypeID)
{
    CReserveKey reservekey(this);
    CReserveKey reservefeekey(this);
    int64_t nFeeRequired;

    if (IsLocked())
    {
        string strError = _("Error: Wallet locked, unable to create transaction  ");
        printf("SendMoney() : %s", strError.c_str());
        return strError;
    }
    if (fWalletUnlockStakingOnly)
    {
        string strError = _("Error: Wallet unlocked for staking only, unable to create transaction.");
        printf("SendMoney() : %s", strError.c_str());
        return strError;
    }
    int nFeeColor = FEE_COLOR[nColor];
    if (!CreateTransaction(scriptPubKey, nValue, nColor, sNarr, wtxNew, reservekey, reservefeekey,
                                nFeeRequired, strTxComment, nServiceTypeID))
    {

        string strError;

        if (nValue > GetBalance(nColor))
        {
            // strError = strprintf(_("Error: insufficient funds."));
            strError = _("Error: insufficient funds.");
        }
        else if ( ((nColor == nFeeColor) && (nValue + nFeeRequired > GetBalance(nColor))) ||
                  ((nColor != nFeeColor) && (nFeeRequired > GetBalance(nFeeColor))) )
        {
            strError = strprintf(_("Error: This transaction requires a transaction fee of at "
                                   "least %s %s because of its amount, complexity, "
                                   "or use of recently received funds."),
                                      FormatMoney(nFeeRequired, nFeeColor).c_str(),
                                      COLOR_TICKER[nFeeColor]);
        }
        else
        {
            strError = _("Error: Transaction creation failed  ");
        }
        printf("SendMoney() : %s", strError.c_str());
        return strError;
    }

    if (fAskFee && !uiInterface.ThreadSafeAskFee(nFeeRequired, nFeeColor, _("Sending...")))
        return "ABORTED";

    if (!CommitTransaction(wtxNew, reservekey, reservefeekey))
        return _("Error: The transaction was rejected.  This might happen if some of the coins in your wallet were already spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent here.");

    return "";
}

string CWallet::SendMoneyToDestination(const CTxDestination& address, int64_t nValue, int nColor,
                                       std::string& sNarr, CWalletTx& wtxNew, bool fAskFee,
                                       std::string strTxComment, int nServiceTypeID, bool burn)
{
    // Check amount
    if (nValue <= 0)
        return _("Invalid amount [8]");

    int nFeeColor = FEE_COLOR[nColor];

    if (nFeeColor == nColor)
    {
        if (nValue + vTransactionFee[nColor] > GetBalance(nColor))
        {
            return strprintf("Insufficient funds [8 Pay(%d)==Fee(%d)]"
                                "nValue: %f,  fee: %f,  balance: %f",
                                nColor, nFeeColor,
                                ValueFromAmount(nValue, nColor).get_real(),
                                ValueFromAmount(vTransactionFee[nFeeColor], nFeeColor).get_real(),
                                ValueFromAmount(GetBalance(nColor), nColor).get_real());
        }
    }
    else
    {
        if ((nValue > GetBalance(nColor)) || (vTransactionFee[nFeeColor] > GetBalance(nFeeColor)))
        {
            string ret;
            ret = strprintf("Insufficient funds [8 Pay(%d)!=Fee(%d)]\n"
                            "nValue: %f,  balance: %f,  fee: %f,  fee balance: %f",
                            nColor, nFeeColor,
                            ValueFromAmount(nValue, nColor).get_real(),
                            ValueFromAmount(GetBalance(nColor), nColor).get_real(),
                            ValueFromAmount(vTransactionFee[nFeeColor], nFeeColor).get_real(),
                            ValueFromAmount(GetBalance(nFeeColor), nFeeColor).get_real());
            return ret;
        }
    }


    if (sNarr.length() > 24)
        return _("Narration must be 24 characters or less. [8]");
    
    // Parse Bitcoin address
    CScript scriptPubKey;

    // An output is not spendable if it ends with OP_RETURN
    if (burn)
    {
        scriptPubKey << OP_RETURN;
    }
    else
    {
        scriptPubKey.SetDestination(address);
    }

    return SendMoney(scriptPubKey, nValue, nColor, sNarr, wtxNew, fAskFee, strTxComment, nServiceTypeID);
}




DBErrors CWallet::LoadWallet(bool& fFirstRunRet)
{
    if (!fFileBacked)
        return DB_LOAD_OK;
    fFirstRunRet = false;
    DBErrors nLoadWalletRet = CWalletDB(strWalletFile,"cr+").LoadWallet(this);
    if (nLoadWalletRet == DB_NEED_REWRITE)
    {
        if (CDB::Rewrite(strWalletFile, "\x04pool"))
        {
            LOCK(cs_wallet);
            setKeyPool.clear();
            // Note: can't top-up keypool here, because wallet is locked.
            // User will be prompted to unlock wallet the next operation
            // the requires a new key.
        }
    }

    if (nLoadWalletRet != DB_LOAD_OK)
        return nLoadWalletRet;
    fFirstRunRet = !vchDefaultKey.IsValid();

    NewThread(ThreadFlushWalletDB, &strWalletFile);
    return DB_LOAD_OK;
}

// TODO: CTxDestination is colored, so there is no reason to pass nColor here
bool CWallet::SetAddressBookName(const CTxDestination& address, int nColor, const string& strName)
{
    // no reason why a multisig can't go into the address book
    static const bool fMultiSig = true;

    bool fOwned;
    ChangeType nMode;
    {
        LOCK(cs_wallet); // mapAddressBook
        std::map<CTxDestination, std::string>::iterator mi = mapAddressBook.find(address);
        nMode = (mi == mapAddressBook.end()) ? CT_NEW : CT_UPDATED;
        fOwned = (::IsMine(*this, address, fMultiSig) & ISMINE_ALL);
        
        mapAddressBook[address] = strName;
    }
    
    if (fOwned)
    {
        CBitcoinAddress caddress(address, nColor);
        SecureMsgWalletKeyChanged(caddress.ToString(), strName, nMode);
    }
    NotifyAddressBookChanged(this, address, nColor, strName, fOwned, nMode);
    
    if (!fFileBacked)
        return false;
    return CWalletDB(strWalletFile).WriteName(CBitcoinAddress(address, nColor).ToString(), strName);
}

bool CWallet::DelAddressBookName(const CTxDestination& address)
{
    // what goes in must come out
    static const bool fMultiSig = true;

    {
        LOCK(cs_wallet); // mapAddressBook

        mapAddressBook.erase(address);
    }

    int nColor = boost::apply_visitor(GetAddressColorVisitor(), address);
    
    bool fOwned = (::IsMine(*this, address, fMultiSig) & ISMINE_ALL);
    string sName = "";
    if (fOwned)
    {
        CBitcoinAddress caddress(address, nColor);
        SecureMsgWalletKeyChanged(caddress.ToString(), sName, CT_DELETED);
    }
    NotifyAddressBookChanged(this, address, (int) BREAKOUT_COLOR_NONE, "", fOwned, CT_DELETED);

    if (!fFileBacked)
        return false;
    return CWalletDB(strWalletFile).EraseName(CBitcoinAddress(address, nColor).ToString());
}


void CWallet::PrintWallet(const CBlock& block)
{
    // no reason why multisig can't be user configurable here
    bool fMultiSig = GetBoolArg("-enablemultisigs", false);

    {
        LOCK(cs_wallet);
        if (block.IsProofOfWork() && mapWallet.count(block.vtx[0].GetHash()))
        {
            CWalletTx& wtx = mapWallet[block.vtx[0].GetHash()];
            printf("    mine:  %d  %d  %" PRId64 " %s", wtx.GetDepthInMainChain(),
                          wtx.GetBlocksToMaturity(), wtx.GetCredit(wtx.GetColor(), fMultiSig),
                                                         COLOR_TICKER[wtx.GetColor()]);
        }
        if (block.IsProofOfStake() && mapWallet.count(block.vtx[1].GetHash()))
        {
            CWalletTx& wtx = mapWallet[block.vtx[1].GetHash()];
            printf("    stake: %d  %d  %" PRId64 " %s", wtx.GetDepthInMainChain(),
                          wtx.GetBlocksToMaturity(), wtx.GetCredit(wtx.GetColor(), fMultiSig),
                                                         COLOR_TICKER[wtx.GetColor()]);
        }

    }
    printf("\n");
}

bool CWallet::GetTransaction(const uint256 &hashTx, CWalletTx& wtx)
{
    {
        LOCK(cs_wallet);
        map<uint256, CWalletTx>::iterator mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end())
        {
            wtx = (*mi).second;
            return true;
        }
    }
    return false;
}

bool CWallet::SetDefaultKey(const CPubKey &vchPubKey)
{
    if (fFileBacked)
    {
        if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey))
            return false;
    }
    vchDefaultKey = vchPubKey;
    return true;
}

bool GetWalletFile(CWallet* pwallet, string &strWalletFileOut)
{
    if (!pwallet->fFileBacked)
        return false;
    strWalletFileOut = pwallet->strWalletFile;
    return true;
}

//
// Mark old keypool keys as used,
// and generate all new keys
//
bool CWallet::NewKeyPool()
{
    {
        LOCK(cs_wallet);
        CWalletDB walletdb(strWalletFile);
        BOOST_FOREACH(int64_t nIndex, setKeyPool)
            walletdb.ErasePool(nIndex);
        setKeyPool.clear();

        if (IsLocked())
            return false;

        int64_t nKeys = max(GetArg("-keypool", 100), (int64_t)0);
        for (int i = 0; i < nKeys; i++)
        {
            int64_t nIndex = i+1;
            walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
            setKeyPool.insert(nIndex);
        }
        printf("CWallet::NewKeyPool wrote %" PRId64 " new keys\n", nKeys);
    }
    return true;
}

bool CWallet::TopUpKeyPool(unsigned int nSize)
{
    {
        LOCK(cs_wallet);

        if (IsLocked())
            return false;

        CWalletDB walletdb(strWalletFile);

        // Top up key pool
        unsigned int nTargetSize;
        if (nSize > 0)
            nTargetSize = nSize;
        else
            nTargetSize = max(GetArg("-keypool", 100), (int64_t)0);

        while (setKeyPool.size() < (nTargetSize + 1))
        {
            int64_t nEnd = 1;
            if (!setKeyPool.empty())
                nEnd = *(--setKeyPool.end()) + 1;
            if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
                throw runtime_error("TopUpKeyPool() : writing generated key failed");
            setKeyPool.insert(nEnd);
            printf("keypool added key %" PRId64 ", size=%"PRIszu"\n", nEnd, setKeyPool.size());
        }
    }
    return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool)
{
    nIndex = -1;
    keypool.vchPubKey = CPubKey();
    {
        LOCK(cs_wallet);

        if (!IsLocked())
            TopUpKeyPool();

        // Get the oldest key
        if(setKeyPool.empty())
            return;

        CWalletDB walletdb(strWalletFile);

        nIndex = *(setKeyPool.begin());
        setKeyPool.erase(setKeyPool.begin());
        if (!walletdb.ReadPool(nIndex, keypool))
            throw runtime_error("ReserveKeyFromKeyPool() : read failed");
        if (!HaveKey(keypool.vchPubKey.GetID()))
            throw runtime_error("ReserveKeyFromKeyPool() : unknown key in key pool");
        assert(keypool.vchPubKey.IsValid());
        if (fDebug && GetBoolArg("-printkeypool"))
            printf("keypool reserve %" PRId64 "\n", nIndex);
    }
}

int64_t CWallet::AddReserveKey(const CKeyPool& keypool)
{
    {
        LOCK2(cs_main, cs_wallet);
        CWalletDB walletdb(strWalletFile);

        int64_t nIndex = 1 + *(--setKeyPool.end());
        if (!walletdb.WritePool(nIndex, keypool))
            throw runtime_error("AddReserveKey() : writing added key failed");
        setKeyPool.insert(nIndex);
        return nIndex;
    }
    return -1;
}

void CWallet::KeepKey(int64_t nIndex)
{
    // Remove from key pool
    if (fFileBacked)
    {
        CWalletDB walletdb(strWalletFile);
        walletdb.ErasePool(nIndex);
    }
    if(fDebug)
        printf("keypool keep %" PRId64 "\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex)
{
    // Return to key pool
    {
        LOCK(cs_wallet);
        setKeyPool.insert(nIndex);
    }
    if(fDebug)
        printf("keypool return %" PRId64 "\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result, bool fAllowReuse)
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    {
        LOCK(cs_wallet);
        ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex == -1)
        {
            if (fAllowReuse && vchDefaultKey.IsValid())
            {
                result = vchDefaultKey;
                return true;
            }
            if (IsLocked()) return false;
            result = GenerateNewKey();
            return true;
        }
        KeepKey(nIndex);
        result = keypool.vchPubKey;
    }
    return true;
}

int64_t CWallet::GetOldestKeyPoolTime()
{
    int64_t nIndex = 0;
    CKeyPool keypool;
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1)
        return GetTime();
    ReturnKey(nIndex);
    return keypool.nTime;
}

// not colorized because it has never been used, may never be
std::map<CTxDestination, int64_t> CWallet::GetAddressBalances()
{
    // address balances can include partial owner multisigs
    static const int fMultiSig = true;

    map<CTxDestination, int64_t> balances;

    {
        LOCK(cs_wallet);
        BOOST_FOREACH(PAIRTYPE(uint256, CWalletTx) walletEntry, mapWallet)
        {
            CWalletTx *pcoin = &walletEntry.second;

            if (!pcoin->IsFinal() || !pcoin->IsTrusted())
                continue;

            if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetBlocksToMaturity() > 0)
                continue;

            int nDepth = pcoin->GetDepthInMainChain();
            if (nDepth < (pcoin->IsFromMe(fMultiSig) ? 0 : 1))
                continue;

            for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            {
                CTxDestination addr;
                if (!(IsMine(pcoin->vout[i], fMultiSig) & ISMINE_ALL))
                    continue;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, addr))
                    continue;

                int64_t n = pcoin->IsSpent(i) ? 0 : pcoin->vout[i].nValue;

                if (!balances.count(addr))
                    balances[addr] = 0;
                balances[addr] += n;
            }
        }
    }

    return balances;
}

// not colorized because not used yet, may never be
set< set<CTxDestination> > CWallet::GetAddressGroupings()
{
    // no idea what this function is supposed to do, go conservatively for multsigs
    static const bool fMultiSig = false;

    AssertLockHeld(cs_wallet); // mapWallet
    set< set<CTxDestination> > groupings;
    set<CTxDestination> grouping;

    BOOST_FOREACH(PAIRTYPE(uint256, CWalletTx) walletEntry, mapWallet)
    {
        CWalletTx *pcoin = &walletEntry.second;

        if (pcoin->vin.size() > 0 && (IsMine(pcoin->vin[0], fMultiSig) & ISMINE_ALL))
        {
            // group all input addresses with each other
            BOOST_FOREACH(CTxIn txin, pcoin->vin)
            {
                CTxDestination address;
                if(!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address))
                    continue;
                grouping.insert(address);
            }

            // group change with input addresses
            BOOST_FOREACH(CTxOut txout, pcoin->vout)
                if (IsChange(txout))
                {
                    CWalletTx tx = mapWallet[pcoin->vin[0].prevout.hash];
                    CTxDestination txoutAddr;
                    if(!ExtractDestination(txout.scriptPubKey, txoutAddr))
                        continue;
                    grouping.insert(txoutAddr);
                }
            groupings.insert(grouping);
            grouping.clear();
        }

        // group lone addrs by themselves
        for (unsigned int i = 0; i < pcoin->vout.size(); i++)
            if (IsMine(pcoin->vout[i], fMultiSig) & ISMINE_ALL)
            {
                CTxDestination address;
                if(!ExtractDestination(pcoin->vout[i].scriptPubKey, address))
                    continue;
                grouping.insert(address);
                groupings.insert(grouping);
                grouping.clear();
            }
    }

    set< set<CTxDestination>* > uniqueGroupings; // a set of pointers to groups of addresses
    map< CTxDestination, set<CTxDestination>* > setmap;  // map addresses to the unique group containing it
    BOOST_FOREACH(set<CTxDestination> grouping, groupings)
    {
        // make a set of all the groups hit by this new group
        set< set<CTxDestination>* > hits;
        map< CTxDestination, set<CTxDestination>* >::iterator it;
        BOOST_FOREACH(CTxDestination address, grouping)
            if ((it = setmap.find(address)) != setmap.end())
                hits.insert((*it).second);

        // merge all hit groups into a new single group and delete old groups
        set<CTxDestination>* merged = new set<CTxDestination>(grouping);
        BOOST_FOREACH(set<CTxDestination>* hit, hits)
        {
            merged->insert(hit->begin(), hit->end());
            uniqueGroupings.erase(hit);
            delete hit;
        }
        uniqueGroupings.insert(merged);

        // update setmap
        BOOST_FOREACH(CTxDestination element, *merged)
            setmap[element] = merged;
    }

    set< set<CTxDestination> > ret;
    BOOST_FOREACH(set<CTxDestination>* uniqueGrouping, uniqueGroupings)
    {
        ret.insert(*uniqueGrouping);
        delete uniqueGrouping;
    }

    return ret;
}

// ppcoin: check 'spent' consistency between wallet and txindex
// ppcoin: fix wallet spent state according to txindex
void CWallet::FixSpentCoins(std::vector<int>& vMismatchFound,
                                 std::vector<int64_t>& vBalanceInQuestion, bool fCheckOnly)
{
    // spent coins are multisig aware, so this should be too
    static const bool fMultiSig = true;

    for (int i = 0; i < N_COLORS; ++i)
    {
        vMismatchFound[i] = 0;
        vBalanceInQuestion[i] = 0;
    }

    LOCK(cs_wallet);
    vector<CWalletTx*> vCoins;
    vCoins.reserve(mapWallet.size());
    for (map<uint256, CWalletTx>::iterator it = mapWallet.begin(); it != mapWallet.end(); ++it)
        vCoins.push_back(&(*it).second);

    CTxDB txdb("r");
    BOOST_FOREACH(CWalletTx* pcoin, vCoins)
    {
        // Find the corresponding transaction index
        CTxIndex txindex;
        if (!txdb.ReadTxIndex(pcoin->GetHash(), txindex))
            continue;
        for (unsigned int n=0; n < pcoin->vout.size(); n++)
        {
            int nColor = pcoin->vout[n].nColor;
            if ((IsMine(pcoin->vout[n], fMultiSig) & ISMINE_ALL) &&
                                pcoin->IsSpent(n) &&
                                (txindex.vSpent.size() <= n || txindex.vSpent[n].IsNull()))
            {
                printf("FixSpentCoins found lost coin %s %s %s[%d], %s\n",
                    FormatMoney(pcoin->vout[n].nValue, nColor).c_str(),
                                COLOR_TICKER[nColor],
                                pcoin->GetHash().ToString().c_str(), n,
                                fCheckOnly? "repair not attempted" : "repairing");
                vMismatchFound[nColor]++;
                vBalanceInQuestion[nColor] += pcoin->vout[n].nValue;
                if (!fCheckOnly)
                {
                    pcoin->MarkUnspent(n);
                    pcoin->WriteToDisk();
                }
            }
            else if ((IsMine(pcoin->vout[n], fMultiSig) & ISMINE_ALL) &&
                     !pcoin->IsSpent(n) &&
                     (txindex.vSpent.size() > n && !txindex.vSpent[n].IsNull()))
            {
                printf("FixSpentCoins found spent coin %s %s %s[%d], %s\n",
                    FormatMoney(pcoin->vout[n].nValue, nColor).c_str(),
                               COLOR_TICKER[nColor], pcoin->GetHash().ToString().c_str(),
                               n, fCheckOnly? "repair not attempted" : "repairing");
                vMismatchFound[nColor]++;
                vBalanceInQuestion[nColor] += pcoin->vout[n].nValue;
                if (!fCheckOnly)
                {
                    pcoin->MarkSpent(n);
                    pcoin->WriteToDisk();
                }
            }
        }
    }
}

// ppcoin: disable transaction (only for coinstake)
void CWallet::DisableTransaction(const CTransaction &tx)
{
    // give no consideration multisig coinstake, yet
    static const int fMultiSig = false;

    if (!tx.IsCoinStake() || !IsFromMe(tx, fMultiSig))
    {
        return; // only disconnecting coinstake requires marking input unspent
    }

    LOCK(cs_wallet);
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        map<uint256, CWalletTx>::iterator mi = mapWallet.find(txin.prevout.hash);
        if (mi != mapWallet.end())
        {
            CWalletTx& prev = (*mi).second;
            if (txin.prevout.n < prev.vout.size() &&
                // coinstake must be spendable to sign it
                (IsMine(prev.vout[txin.prevout.n], fMultiSig) & ISMINE_SPENDABLE))
            {
                prev.MarkUnspent(txin.prevout.n);
                prev.WriteToDisk();
            }
        }
    }
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey, int nColor)
{
    if (nIndex == -1)
    {
        CKeyPool keypool;
        pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
        if (nIndex != -1)
            vchPubKey = keypool.vchPubKey;
        else {
            if (pwallet->vchDefaultKey.IsValid()) {
                printf("CReserveKey::GetReservedKey(): Warning: Using default key instead of a new key, top up your keypool!");
                vchPubKey = pwallet->vchDefaultKey;
            } else
                return false;
        }
    }
    assert(vchPubKey.IsValid());
    pubkey = vchPubKey;
    pubkey.nColor = nColor;
    return true;
}

void CReserveKey::KeepKey()
{
    if (nIndex != -1)
        pwallet->KeepKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey()
{
    if (nIndex != -1)
        pwallet->ReturnKey(nIndex);
    nIndex = -1;
    vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID>& setAddress) const
{
    setAddress.clear();

    CWalletDB walletdb(strWalletFile);

    LOCK2(cs_main, cs_wallet);
    BOOST_FOREACH(const int64_t& id, setKeyPool)
    {
        CKeyPool keypool;
        if (!walletdb.ReadPool(id, keypool))
            throw runtime_error("GetAllReserveKeyHashes() : read failed");
        assert(keypool.vchPubKey.IsValid());
        CKeyID keyID = keypool.vchPubKey.GetID();
        if (!HaveKey(keyID))
            throw runtime_error("GetAllReserveKeyHashes() : unknown key in key pool");
        setAddress.insert(keyID);
    }
}

void CWallet::UpdatedTransaction(const uint256 &hashTx)
{
    {
        LOCK(cs_wallet);
        // Only notify UI if this transaction is in this wallet
        map<uint256, CWalletTx>::const_iterator mi = mapWallet.find(hashTx);
        if (mi != mapWallet.end())
            NotifyTransactionChanged(this, hashTx, CT_UPDATED);
    }
}

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const {
    AssertLockHeld(cs_wallet); // mapKeyMetadata
    mapKeyBirth.clear();

    // get birth times for keys with metadata
    for (std::map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
        if (it->second.nCreateTime)
            mapKeyBirth[it->first] = it->second.nCreateTime;

    // map in which we'll infer heights of other keys
    CBlockIndex *pindexMax = FindBlockByHeight(std::max(0, nBestHeight - 144)); // the tip can be reorganised; use a 144-block safety margin
    std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
    std::set<CKeyID> setKeys;
    GetKeys(setKeys);
    BOOST_FOREACH(const CKeyID &keyid, setKeys) {
        if (mapKeyBirth.count(keyid) == 0)
            mapKeyFirstBlock[keyid] = pindexMax;
    }
    setKeys.clear();

    // if there are no such keys, we're done
    if (mapKeyFirstBlock.empty())
        return;

    // find first block that affects those keys, if there are any left
    std::vector<CKeyID> vAffected;
    for (std::map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); it++) {
        // iterate over all wallet transactions...
        const CWalletTx &wtx = (*it).second;
        std::map<uint256, CBlockIndex*>::const_iterator blit = mapBlockIndex.find(wtx.hashBlock);
        if (blit != mapBlockIndex.end() && blit->second->IsInMainChain()) {
            // ... which are already in a block
            int nHeight = blit->second->nHeight;
            BOOST_FOREACH(const CTxOut &txout, wtx.vout) {
                // iterate over all their outputs
                ::ExtractAffectedKeys(*this, txout.scriptPubKey, vAffected);
                BOOST_FOREACH(const CKeyID &keyid, vAffected) {
                    // ... and all their affected keys
                    std::map<CKeyID, CBlockIndex*>::iterator rit = mapKeyFirstBlock.find(keyid);
                    if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight)
                        rit->second = blit->second;
                }
                vAffected.clear();
            }
        }
    }

    // Extract block timestamps for those keys
    for (std::map<CKeyID, CBlockIndex*>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end(); it++)
        mapKeyBirth[it->first] = it->second->nTime - 7200; // block times can be 2h off
}
