// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2017 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_WALLET_H
#define BITCOIN_WALLET_H

#include <string>
#include <vector>

#include <stdlib.h>

#include "main.h"
#include "key.h"
#include "keystore.h"
#include "script.h"
#include "ui_interface.h"
#include "util.h"
#include "walletdb.h"
#include "stealth.h"
#include "smessage.h"
#include "crypter.h"

extern bool fWalletUnlockStakingOnly;
extern bool fConfChange;
class CAccountingEntry;
class CWalletTx;
class CReserveKey;
class COutput;
class CCoinControl;

typedef std::map<CKeyID, CStealthKeyMetadata> StealthKeyMetaMap;
typedef std::map<std::string, std::string> mapValue_t;

// (secret, value)
typedef std::pair<CBitcoinSecret, int64_t> pairAddressValue_t;
// { address: (secret, value), ... }
typedef std::map<CBitcoinAddress, pairAddressValue_t> mapSecretByAddress_t;
// { color: { address: (secret, value), ... }, ... }
typedef std::map<int, mapSecretByAddress_t> mapSecretByAddressByColor_t;

/** (client) version numbers for particular wallet features */
enum WalletFeature
{
    FEATURE_BASE = 10500, // the earliest version new wallets supports (only useful for getinfo's clientversion output)

    FEATURE_WALLETCRYPT = 40000, // wallet encryption
    FEATURE_COMPRPUBKEY = 60000, // compressed public keys

    FEATURE_LATEST = 60000
};



/** A key pool entry */
class CKeyPool
{
public:
    int64_t nTime;
    CPubKey vchPubKey;

    CKeyPool()
    {
        nTime = GetTime();
    }

    CKeyPool(const CPubKey& vchPubKeyIn)
    {
        nTime = GetTime();
        vchPubKey = vchPubKeyIn;
    }

    IMPLEMENT_SERIALIZE
    (
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(nTime);
        READWRITE(vchPubKey);
    )
};

/** A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CWallet : public CCryptoKeyStore
{
private:
    bool SelectCoinsSimple(int64_t nTargetValue, int nColor, unsigned int nSpendTime,
                           int nMinConf, std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet,
                           int64_t& nValueRet, bool fMultiSig) const;
    bool SelectCoins(int64_t nTargetValue, int nColor, unsigned int nSpendTime,
                     std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet,
                     int64_t& nValueRet, bool fMultiSig, const CCoinControl *coinControl=NULL) const;

    CWalletDB *pwalletdbEncryption;

    // the current wallet version: clients below this version are not able to load the wallet
    int nWalletVersion;

    // the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
    int nWalletMaxVersion;

    /**
     * Private version of AddWatchOnly method which does not accept a
     * timestamp, and which will reset the wallet's nTimeFirstKey value to 1 if
     * the watch key did not previously have a timestamp associated with it.
     * Because this is an inherited virtual method, it is accessible despite
     * being marked private, but it is marked private anyway to encourage use
     * of the other AddWatchOnly which accepts a timestamp and sets
     * nTimeFirstKey more intelligently for more efficient rescans.
     */
    bool AddWatchOnly(const CScript& dest);

public:
    mutable CCriticalSection cs_wallet;

    bool fFileBacked;
    std::string strWalletFile;

    std::set<int64_t> setKeyPool;
    std::map<CKeyID, CKeyMetadata> mapKeyMetadata;

    // Map from Script ID to key metadata (for watch-only keys).
    std::map<CScriptID, CKeyMetadata> m_script_metadata;

    std::set<CStealthAddress> stealthAddresses;
    StealthKeyMetaMap mapStealthKeyMeta;
    uint32_t nStealth, nFoundStealth;

    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID;

    CWallet()
    {
        nWalletVersion = FEATURE_BASE;
        nWalletMaxVersion = FEATURE_BASE;
        fFileBacked = false;
        nMasterKeyMaxID = 0;
        pwalletdbEncryption = NULL;
        nOrderPosNext = 0;
    }
    CWallet(std::string strWalletFileIn)
    {
        nWalletVersion = FEATURE_BASE;
        nWalletMaxVersion = FEATURE_BASE;
        strWalletFile = strWalletFileIn;
        fFileBacked = true;
        nMasterKeyMaxID = 0;
        pwalletdbEncryption = NULL;
        nOrderPosNext = 0;
    }

    std::map<uint256, CWalletTx> mapWallet;
    int64_t nOrderPosNext;
    std::map<uint256, int> mapRequestCount;

    std::map<CTxDestination, std::string> mapAddressBook;

    CPubKey vchDefaultKey;
    int64_t nTimeFirstKey;

    // check whether we are allowed to upgrade (or already support) to the named feature
    bool CanSupportFeature(enum WalletFeature wf) { AssertLockHeld(cs_wallet); return nWalletMaxVersion >= wf; }

    void AvailableCoinsMinConf(int nColor, std::vector<COutput>& vCoins, int nConf, bool fMultiSig) const;
    void AvailableCoins(int nColor, std::vector<COutput>& vCoins, bool fMultiSig,
                        bool fOnlyConfirmed=true, const CCoinControl *coinControl=NULL) const;
    // no color check because vCoins is already populated with coins of the same color!!
    bool SelectCoinsMinConf(int64_t nTargetValue, unsigned int nSpendTime, int nConfMine,
                            int nConfTheirs, std::vector<COutput> vCoins,
                            std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet,
                            int64_t& nValueRet, bool fMultiSig) const;
    // keystore implementation
    // Generate a new key
    CPubKey GenerateNewKey();
    // Adds a key to the store, and saves it to disk.
    // bool AddKey(const CKey& key);
    bool AddKeyPubKey(const CKey& key, const CPubKey &pubkey);
    // Adds a key to the store, without saving it to disk (used by LoadWallet)
    bool LoadKey(const CKey& key) { return CCryptoKeyStore::AddKey(key); }
    bool LoadKey(const CKey& key, const CPubKey &pubkey) { return CCryptoKeyStore::AddKeyPubKey(key, pubkey); }
    // Load metadata (used by LoadWallet)
    bool LoadKeyMetadata(const CPubKey &pubkey, const CKeyMetadata &metadata);

    bool LoadMinVersion(int nVersion) { AssertLockHeld(cs_wallet); nWalletVersion = nVersion; nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion); return true; }

    // Adds an encrypted key to the store, and saves it to disk.
    bool AddCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    // Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
    bool LoadCryptedKey(const CPubKey &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    bool AddCScript(const CScript& redeemScript);
    bool LoadCScript(const CScript& redeemScript);

    //! Adds a watch-only address to the store, and saves it to disk.
    bool AddWatchOnly(const CScript& dest, int64_t nCreateTime);
    bool RemoveWatchOnly(const CScript &dest);
    //! Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
    bool LoadWatchOnly(const CScript &dest);

    bool Lock();
    bool Unlock(const SecureString& strWalletPassphrase, bool lockedOK=false);
    bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
    bool EncryptWallet(const SecureString& strWalletPassphrase);

    void GetKeyBirthTimes(std::map<CKeyID, int64_t> &mapKeyBirth) const;


    /** Increment the next transaction order id
        @return next transaction order id
     */
    int64_t IncOrderPosNext(CWalletDB *pwalletdb = NULL);

    typedef std::pair<CWalletTx*, CAccountingEntry*> TxPair;
    typedef std::multimap<int64_t, TxPair > TxItems;

    /** Get the wallet's activity log
        @return multimap of ordered transactions and accounting entries
        @warning Returned pointers are *only* valid within the scope of passed acentries
     */
    TxItems OrderedTxItems(std::list<CAccountingEntry>& acentries, std::string strAccount = "");

    void MarkDirty();
    bool AddToWallet(const CWalletTx& wtxIn);
    bool AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate = false, bool fFindBlock = false);
    bool EraseFromWallet(uint256 hash);
    void WalletUpdateSpent(const CTransaction& prevout, bool fBlock = false);
    int ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate = false, void (*pProgress)(int)=NULL);
    int ScanForWalletTransaction(const uint256& hashTx);
    void ReacceptWalletTransactions();
    void ResendWalletTransactions(bool fForce = false);
    // gets balance for coin of nColor
    int64_t GetBalance(int nColor) const;
    void GetBalances(int nMinDepth, std::vector<int64_t> &vBalance) const;
    void GetPrivateKeys(std::set<int> setColors, bool fMultiSig,
                        mapSecretByAddressByColor_t &mapAddrs) const;
    void GetHand(int nMinDepth, std::vector<int> &vCards) const;
    int64_t GetUnconfirmedBalance(int nColor) const;
    int64_t GetImmatureBalance(int nColor) const;
    int64_t GetStake(int nColor) const;
    int64_t GetNewMint(int nColor) const;
    // nColor is parameter to CreateTransaction() and not packed in vecSend because
    // transactions are composed of inputs/outputs of only one color anyway.


    bool CreateTransaction(const std::vector<std::pair<CScript, int64_t> >& vecSend, int nColor,
                           CWalletTx& wtxNew, CReserveKey& reservekey, CReserveKey &reservefeekey,
                           int64_t& nFeeRet, int32_t& nChangePos, int32_t& nFeeChangePos,
                           std::string strTxComment, unsigned int nServiceTypeID,
                           const CCoinControl* coinControl=NULL);
    bool CreateTransaction(const std::vector<std::pair<CScript, int64_t> >& vecSend, int nColor,
                           CWalletTx& wtxNew, CReserveKey& reservekey, CReserveKey &reservefeekey,
                           int64_t& nFeeRet, const CCoinControl* coinControl=NULL);
    bool CreateTransaction(CScript scriptPubKey, int64_t nValue, int nColor, std::string& sNarr,
                           CWalletTx& wtxNew, CReserveKey& reservekey, CReserveKey& reservefeekey,
                           int64_t& nFeeRet, std::string strTxComment, unsigned int nServiceTypeID,
                           const CCoinControl* coinControl=NULL);
    bool CreateTransaction(CScript scriptPubKey, int64_t nValue, int nColor,
                           CWalletTx& wtxNew, CReserveKey& reservekey, CReserveKey& reservefeekey,
                           int64_t& nFeeRet, const CCoinControl*  coinControl=NULL);

//     bool CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey);

    bool CommitTransaction(CWalletTx& wtxNew,
                           CReserveKey& reservekey, CReserveKey &reservefeekey);

    // TODO: why do the following need a keystore when they are members of a keystore?
    // this is used for reporting only
    // get current stake weight for currency of nColor
    bool GetStakeWeightByColor(int nColor, const CKeyStore& keystore, uint64_t& nWeight);
    // bool GetStakeWeight(int nColor, const CKeyStore& keystore, uint64_t& nWeight);
    // get the weighted sum of stake weights for all staking currencies
    bool GetStakeWeight(const CKeyStore& keystore, uint64_t& nWeight);

    bool CreateCoinStake(int nColor, const CKeyStore& keystore,
                          unsigned int nBits, int64_t nSearchInterval,
                            int64_t nFees[], CTransaction& txMint, CTransaction& txStake, CKey& key);

    std::string SendMoney(CScript scriptPubKey, int64_t nValue, int nColor,
                          std::string& sNarr, CWalletTx& wtxNew, bool fAskFee=false,
                          std::string strTxComment = "", int nServiceTypeID = SERVICE_NONE);

    std::string SendMoneyToDestination(const CTxDestination &address, int64_t nValue,
                   int nColor, std::string& sNarr, CWalletTx& wtxNew, bool fAskFee=false,
                   std::string strTxComment = "", int nServiceTypeID = SERVICE_NONE, bool burn = false);

    bool NewStealthAddress(int nColor, std::string& sError, std::string& sLabel, CStealthAddress& sxAddr);
    bool AddStealthAddress(CStealthAddress& sxAddr);
    bool UnlockStealthAddresses(const CKeyingMaterial& vMasterKeyIn);
    bool UpdateStealthAddress(std::string & addr, std::string &label, bool addIfNotExist);
//     bool CreateStealthTransaction(CScript scriptPubKey, int64_t nValue, int nColor, std::vector<uint8_t>& P, std::vector<uint8_t>& narr, std::string& sNarr, CWalletTx& wtxNew, CReserveKey& reservekey, int64_t& nFeeRet, const CCoinControl* coinControl=NULL);

    bool CreateStealthTransaction(CScript scriptPubKey, int64_t nValue, int nColor,
                                  std::vector<uint8_t>& P, std::vector<uint8_t>& narr,
                                  std::string& sNarr, CWalletTx& wtxNew,
                                  CReserveKey& reservekey, CReserveKey& reservefeekey,
                                  int64_t& nFeeRet, const CCoinControl* coinControl=NULL);


    std::string SendStealthMoney(CScript scriptPubKey, int64_t nValue, int nColor, std::vector<uint8_t>& P, std::vector<uint8_t>& narr, std::string& sNarr, CWalletTx& wtxNew, bool fAskFee=false);

    // stealth addresses have color information
    bool SendStealthMoneyToDestination(CStealthAddress& sxAddress, int64_t nValue, std::string& sNarr, CWalletTx& wtxNew, std::string& sError, bool fAskFee=false);
    bool FindStealthTransactions(const CTransaction& tx, mapValue_t& mapNarr);

    bool NewKeyPool();
    bool TopUpKeyPool(unsigned int nSize = 0);
    int64_t AddReserveKey(const CKeyPool& keypool);
    void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool);
    void KeepKey(int64_t nIndex);
    void ReturnKey(int64_t nIndex);
    bool GetKeyFromPool(CPubKey &key, bool fAllowReuse=true);
    int64_t GetOldestKeyPoolTime();
    void GetAllReserveKeys(std::set<CKeyID>& setAddress) const;

    // GetAddressGroupings() &  GetAddressBalances() are currently defunct
    std::set< std::set<CTxDestination> > GetAddressGroupings();
    std::map<CTxDestination, int64_t> GetAddressBalances();

    isminetype IsMine(const CTxIn& txin, bool fMultiSig) const;
    //       color   value
    std::pair<int, int64_t> GetDebit(const CTxIn &txin, bool fMultiSig) const;
    int64_t GetDebit(const CTxIn& txin, int nColor, bool fMultiSig) const;
    isminetype IsMine(const CTxOut& txout, bool fMultiSig) const
    {
        return ::IsMine(*this, txout.scriptPubKey, fMultiSig);
    }
    int64_t GetCredit(const CTxOut& txout, int nColor, bool fMultiSig) const
    {
        if (!MoneyRange(txout.nValue, txout.nColor))
            throw std::runtime_error("CWallet::GetCredit() : value out of range");
        return (((IsMine(txout, fMultiSig) & ISMINE_ALL) && (txout.nColor == nColor)) ? txout.nValue : 0);
    }

    std::pair<int, int64_t> GetCredit(const CTxOut &txout, bool fMultiSig) const
    {
        int64_t cred = GetCredit(txout, txout.nColor, fMultiSig);
        return std::make_pair(txout.nColor, cred);
    }

    bool IsChange(const CTxOut& txout) const;

    int64_t GetChange(const CTxOut& txout) const
    {
        if (!MoneyRange(txout.nValue, txout.nColor))
            throw std::runtime_error("CWallet::GetChange() : value out of range");
        return (IsChange(txout) ? txout.nValue : 0);
    }


    isminetype IsMine(const CTransaction& tx, bool fMultiSig) const
    {
        isminetype isMineTx = ISMINE_NO;
        BOOST_FOREACH(const CTxOut& txout, tx.vout)
        {
            isminetype isMineOut = IsMine(txout, fMultiSig);
            if ((isMineOut & ISMINE_ALL) && txout.nValue >= vMinimumInputValue[txout.nColor])
            {
                isMineTx = isMineTx | isMineOut;
            }
        }
        return isMineTx;
    }

    bool IsFromMe(const CTransaction& tx, bool fMultiSig) const
    {
        std::map<int, int64_t> mapDebit;
        FillDebits(tx, mapDebit, fMultiSig);
        std::map<int, int64_t>::const_iterator it;
        for (it = mapDebit.begin(); it != mapDebit.end(); ++it)
        {
            if (it->second > 0)
            {
                 return true;
            }
        }
        return false;
    }

    bool FillDebits(const CTransaction& tx, std::map<int, int64_t> &mapDebit, bool fMultiSig) const
    {
        mapDebit.clear();
        BOOST_FOREACH(const CTxIn& txin, tx.vin)
        {
            std::pair<int, int64_t> pairDebit = GetDebit(txin, fMultiSig);
            int64_t nDebit = pairDebit.second;
            if (nDebit <= 0)
            {
                continue;
            }
            int nColor = pairDebit.first;
            if (!MoneyRange(nDebit, nColor))
            {
                throw std::runtime_error("CWallet::GetDebit() : value out of range");
            }
            mapDebit[nColor] += nDebit;
        }
        return (mapDebit.size() > 0);
    }

    // TODO: this is going to be inefficient for many currencies
    int64_t GetDebit(const CTransaction& tx, int nColor, bool fMultiSig) const
    {
        if (!CheckColor(nColor))
        {
            throw std::runtime_error("CWallet::GetDebit() : invalid currency");
        }
        std::map<int, int64_t> mapDebit;
        FillDebits(tx, mapDebit, fMultiSig);
        return mapDebit[nColor];
    }


    bool FillCredits(const CTransaction& tx, std::map<int, int64_t> &mapCredit, bool fMultiSig) const
    {
        mapCredit.clear();
        BOOST_FOREACH(const CTxOut& txout, tx.vout)
        {
            int nColor = txout.nColor;
            int64_t nCredit = GetCredit(txout, nColor, fMultiSig);  // checks money range
            if (nCredit <= 0)
            {
                continue;
            }
            mapCredit[nColor] += nCredit;
            if (!MoneyRange(mapCredit[nColor], nColor))
            {
                throw std::runtime_error("CWallet::GetCredit() : value out of range");
            }
        }
        return (mapCredit.size() > 0);
    }

    // TODO: this is going to be inefficient for many currencies
    int64_t GetCredit(const CTransaction& tx, int nColor, bool fMultiSig) const
    {
        if (!CheckColor(nColor))
        {
            throw std::runtime_error("CWallet::GetCredit() : invalid currency");
        }
        std::map<int, int64_t> mapCredit;
        FillCredits(tx, mapCredit, fMultiSig);
        return mapCredit[nColor];
    }

    bool FillMatures(const CMerkleTx& tx, std::map<int, int64_t> &mapCredit, bool fMultiSig) const
    {
        mapCredit.clear();
        if ((tx.IsCoinBase() || tx.IsCoinStake()) && tx.GetBlocksToMaturity() > 0)
        {
            return false;
        }
        return FillCredits(tx, mapCredit, fMultiSig);
    }

    // this is completely not dependable--and will probably never be
    //    because change obfuscation is part of the bitcoin protocol
    bool FillChange(const CTransaction & tx, std::map<int, int64_t> &mapChange) const
    {
        mapChange.clear();
        BOOST_FOREACH(const CTxOut& txout, tx.vout)
        {
            mapChange[txout.nColor] += GetChange(txout); // checks money range
            if (!MoneyRange(mapChange[txout.nColor], txout.nColor))
            {
                throw std::runtime_error("CWallet::FillChange() : value out of range");
            }
        }
        return (mapChange.size() > 0);
    }

    // TODO: this is going to be inefficient for many currencies
    int64_t GetChange(const CTransaction& tx, int nColor) const
    {
        if (!CheckColor(nColor))
        {
            throw std::runtime_error("CWallet::GetChange() : invalid currency");
        }
        std::map<int, int64_t> mapChange;
        FillChange(tx, mapChange);
        return mapChange[nColor];
    }

    void SetBestChain(const CBlockLocator& loc);

    DBErrors LoadWallet(bool& fFirstRunRet);

    bool SetAddressBookName(const CTxDestination& address, int nColor, const std::string& strName);

    bool DelAddressBookName(const CTxDestination& address);

    void UpdatedTransaction(const uint256 &hashTx);

    void PrintWallet(const CBlock& block);

    void Inventory(const uint256 &hash)
    {
        {
            LOCK(cs_wallet);
            std::map<uint256, int>::iterator mi = mapRequestCount.find(hash);
            if (mi != mapRequestCount.end())
                (*mi).second++;
        }
    }

    unsigned int GetKeyPoolSize()
    {
        AssertLockHeld(cs_wallet); // setKeyPool
        return setKeyPool.size();
    }

    bool GetTransaction(const uint256 &hashTx, CWalletTx& wtx);

    bool SetDefaultKey(const CPubKey &vchPubKey);

    // signify that a particular wallet feature is now used. this may change nWalletVersion and nWalletMaxVersion if those are lower
    bool SetMinVersion(enum WalletFeature, CWalletDB* pwalletdbIn = NULL, bool fExplicit = false);

    // change which version we're allowed to upgrade to (note that this does not immediately imply upgrading to that format)
    bool SetMaxVersion(int nVersion);

    // get the current wallet format (the oldest client version guaranteed to understand this wallet)
    int GetVersion() { return nWalletVersion; }

    void FixSpentCoins(std::vector<int>& vMismatchFound,
                       std::vector<int64_t>& vBalanceInQuestion, bool fCheckOnly = false);
    void DisableTransaction(const CTransaction &tx);

    /** Address book entry changed.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const CTxDestination &address, int nColor, const std::string &label, bool isMine, ChangeType status)> NotifyAddressBookChanged;

    /** Wallet transaction added, removed or updated.
     * @note called with lock cs_wallet held.
     */
    boost::signals2::signal<void (CWallet *wallet, const uint256 &hashTx, ChangeType status)> NotifyTransactionChanged;

    /** Watch-only address added */
    boost::signals2::signal<void (bool fHaveWatchOnly)> NotifyWatchonlyChanged;
};

/** A key allocated from the key pool. */
class CReserveKey
{
protected:
    CWallet* pwallet;
    int64_t nIndex;
    CPubKey vchPubKey;
public:
    CReserveKey(CWallet* pwalletIn)
    {
        nIndex = -1;
        pwallet = pwalletIn;
    }

    ~CReserveKey()
    {
        if (!fShutdown)
            ReturnKey();
    }

    void ReturnKey();
    bool GetReservedKey(CPubKey &pubkey, int nColor);
    void KeepKey();
};


typedef std::map<std::string, std::string> mapValue_t;


static void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (!mapValue.count("n"))
    {
        nOrderPos = -1; // TODO: calculate elsewhere
        return;
    }
    nOrderPos = atoi64(mapValue["n"].c_str());
}


static void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue)
{
    if (nOrderPos == -1)
        return;
    mapValue["n"] = i64tostr(nOrderPos);
}


/** A transaction with a bunch of additional info that only the owner cares about.
 *  It includes any unrecorded transactions needed to link it back to the block chain.
 *  These do not track color in any member functions because they already know
 *  their color.
 */
class CWalletTx : public CMerkleTx
{
private:
    const CWallet* pwallet;

public:
    std::vector<CMerkleTx> vtxPrev;
    mapValue_t mapValue;
    std::vector<std::pair<std::string, std::string> > vOrderForm;
    unsigned int fTimeReceivedIsTxTime;
    unsigned int nTimeReceived;  // time received by this node
    unsigned int nTimeSmart;
    char fFromMe;
    std::string strFromAccount;
    std::vector<char> vfSpent; // which outputs are already spent
    int64_t nOrderPos;  // position in ordered transaction list

    // memory only
    mutable bool fDebitCached;
    mutable bool fCreditCached;
    mutable bool fAvailableCreditCached;
    mutable bool fChangeCached;
    mutable std::map<int, int64_t> mapDebitCached;
    mutable std::map<int, int64_t> mapCreditCached;
    mutable std::map<int, int64_t> mapAvailableCreditCached;
    mutable std::map<int, int64_t> mapChangeCached;

    CWalletTx()
    {
        Init(NULL);
    }

    CWalletTx(const CWallet* pwalletIn)
    {
        Init(pwalletIn);
    }

    CWalletTx(const CWallet* pwalletIn, const CMerkleTx& txIn) : CMerkleTx(txIn)
    {
        Init(pwalletIn);
    }

    CWalletTx(const CWallet* pwalletIn, const CTransaction& txIn) : CMerkleTx(txIn)
    {
        Init(pwalletIn);
    }

    void Init(const CWallet* pwalletIn)
    {
        pwallet = pwalletIn;
        vtxPrev.clear();
        mapValue.clear();
        vOrderForm.clear();
        fTimeReceivedIsTxTime = false;
        nTimeReceived = 0;
        nTimeSmart = 0;
        fFromMe = false;
        strFromAccount.clear();
        vfSpent.clear();
        fDebitCached = false;
        fCreditCached = false;
        fAvailableCreditCached = false;
        fChangeCached = false;
        mapDebitCached.clear();
        mapCreditCached.clear();
        mapAvailableCreditCached.clear();
        mapChangeCached.clear();
        nOrderPos = -1;
    }

    IMPLEMENT_SERIALIZE
    (
        CWalletTx* pthis = const_cast<CWalletTx*>(this);
        if (fRead)
            pthis->Init(NULL);
        char fSpent = false;

        if (!fRead)
        {
            pthis->mapValue["fromaccount"] = pthis->strFromAccount;

            std::string str;
            BOOST_FOREACH(char f, vfSpent)
            {
                str += (f ? '1' : '0');
                if (f)
                    fSpent = true;
            }
            pthis->mapValue["spent"] = str;

            WriteOrderPos(pthis->nOrderPos, pthis->mapValue);

            if (nTimeSmart)
                pthis->mapValue["timesmart"] = strprintf("%u", nTimeSmart);
        }

        nSerSize += SerReadWrite(s, *(CMerkleTx*)this, nType, nVersion,ser_action);
        READWRITE(vtxPrev);
        READWRITE(mapValue);
        READWRITE(vOrderForm);
        READWRITE(fTimeReceivedIsTxTime);
        READWRITE(nTimeReceived);
        READWRITE(fFromMe);
        READWRITE(fSpent);

        if (fRead)
        {
            pthis->strFromAccount = pthis->mapValue["fromaccount"];

            if (mapValue.count("spent"))
                BOOST_FOREACH(char c, pthis->mapValue["spent"])
                    pthis->vfSpent.push_back(c != '0');
            else
                pthis->vfSpent.assign(vout.size(), fSpent);

            ReadOrderPos(pthis->nOrderPos, pthis->mapValue);

            pthis->nTimeSmart = mapValue.count("timesmart") ? (unsigned int)atoi64(pthis->mapValue["timesmart"]) : 0;
        }

        pthis->mapValue.erase("fromaccount");
        pthis->mapValue.erase("version");
        pthis->mapValue.erase("spent");
        pthis->mapValue.erase("n");
        pthis->mapValue.erase("timesmart");
    )

    // marks certain txout's as spent
    // returns true if any update took place
    bool UpdateSpent(const std::vector<char>& vfNewSpent)
    {
        bool fReturn = false;
        for (unsigned int i = 0; i < vfNewSpent.size(); i++)
        {
            if (i == vfSpent.size())
                break;

            if (vfNewSpent[i] && !vfSpent[i])
            {
                vfSpent[i] = true;
                fReturn = true;
                fAvailableCreditCached = false;
            }
        }
        return fReturn;
    }

    // make sure balances are recalculated
    void MarkDirty()
    {
        fCreditCached = false;
        fAvailableCreditCached = false;
        fDebitCached = false;
        fChangeCached = false;
    }

    void BindWallet(CWallet *pwalletIn)
    {
        pwallet = pwalletIn;
        MarkDirty();
    }

    void MarkSpent(unsigned int nOut)
    {
        if (nOut >= vout.size())
            throw std::runtime_error("CWalletTx::MarkSpent() : nOut out of range");
        vfSpent.resize(vout.size());
        if (!vfSpent[nOut])
        {
            vfSpent[nOut] = true;
            fAvailableCreditCached = false;
        }
    }

    void MarkUnspent(unsigned int nOut)
    {
        if (nOut >= vout.size())
            throw std::runtime_error("CWalletTx::MarkUnspent() : nOut out of range");
        vfSpent.resize(vout.size());
        if (vfSpent[nOut])
        {
            vfSpent[nOut] = false;
            fAvailableCreditCached = false;
        }
    }

    bool IsSpent(unsigned int nOut) const
    {
        if (nOut >= vout.size())
            throw std::runtime_error("CWalletTx::IsSpent() : nOut out of range");
        if (nOut >= vfSpent.size())
            return false;
        return (!!vfSpent[nOut]);
    }

    int64_t GetDebit(int nColor, bool fMultiSig) const
    {
        if (vin.empty())
            return 0;
        if (fDebitCached)
            return mapDebitCached[nColor];
        return pwallet->GetDebit(*this, nColor, fMultiSig);
    }

    int64_t GetCredit(int nColor, bool fMultiSig, bool fUseCache=false) const
    {
        // Must wait until coinbase is safely deep enough in the chain before valuing it
        if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
            return 0;

        // GetBalance can assume transactions in mapWallet won't change
        if (fUseCache && fCreditCached)
        {
            return mapCreditCached[nColor];  // color is validated in caching
        }
        return pwallet->GetCredit(*this, nColor, fMultiSig);
    }

    // fills the map cache if necessary
    int64_t GetAvailableCredit(int nColor, bool fMultiSig, bool fUseCache=false) const
    {
        // Must wait until coinbase is safely deep enough in the chain before valuing it
        if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0)
            return 0;

        if (fUseCache && fAvailableCreditCached)
        {
            return mapAvailableCreditCached[nColor];
        }

        mapAvailableCreditCached.clear();
        for (unsigned int i = 0; i < vout.size(); i++)
        {
            if (!IsSpent(i))
            {
                const CTxOut &txout = vout[i];
                mapAvailableCreditCached[txout.nColor] += pwallet->GetCredit(txout, nColor, fMultiSig);
                if (!MoneyRange(mapAvailableCreditCached[txout.nColor], txout.nColor))
                    throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
            }
        }
        fAvailableCreditCached = true;

        return mapAvailableCreditCached[nColor];
    }

    int64_t GetChange(int nColor) const
    {
        if (fChangeCached)
        {
            return mapChangeCached[nColor];
        }
        return pwallet->GetChange(*this, nColor);
    }

    void GetColorsOut(std::set<int> &setColorsOut) const
    {
        for (int i = 0; i < (int) vout.size(); ++i)
        {
           setColorsOut.insert(vout[i].nColor);
        }
    }

    void GetAmounts(int nColor, std::list<std::pair<CTxDestination,
                    int64_t> >& listReceived,
                    std::list<std::pair<CTxDestination, int64_t> >& listSent,
                    int64_t& nFee, std::string& strSentAccount, bool fMultiSig) const;

    void GetAccountAmounts(int nColor, const std::string& strAccount,
                           int64_t& nReceived, int64_t& nSent, int64_t& nFee, bool fMultiSig) const;

    bool IsFromMe(bool fMultiSig) const
    {
        for (int nColor = 1; nColor < N_COLORS; ++nColor)
        {
              if (GetDebit(nColor, fMultiSig) > 0)
              {
                    return true;
              }
        } 
        return false;
    }

    bool IsTrusted() const
    {
        // can trust multisig involving one's self (?)
        static const bool fMultiSig = true;

        // Quick answer in most cases
        if (!IsFinal())
            return false;
        int nDepth = GetDepthInMainChain();
        if (nDepth >= 1)
            return true;
        if (nDepth < 0)
            return false;
        if (fConfChange || !IsFromMe(fMultiSig)) // using wtx's cached debit
            return false;

        // If no confirmations but it's from us, we can still
        // consider it confirmed if all dependencies are confirmed
        std::map<uint256, const CMerkleTx*> mapPrev;
        std::vector<const CMerkleTx*> vWorkQueue;
        vWorkQueue.reserve(vtxPrev.size()+1);
        vWorkQueue.push_back(this);
        for (unsigned int i = 0; i < vWorkQueue.size(); i++)
        {
            const CMerkleTx* ptx = vWorkQueue[i];

            if (!ptx->IsFinal())
                return false;
            int nPDepth = ptx->GetDepthInMainChain();
            if (nPDepth >= 1)
                continue;
            if (nPDepth < 0)
                return false;
            if (!pwallet->IsFromMe(*ptx, fMultiSig))
                return false;

            if (mapPrev.empty())
            {
                BOOST_FOREACH(const CMerkleTx& tx, vtxPrev)
                    mapPrev[tx.GetHash()] = &tx;
            }

            BOOST_FOREACH(const CTxIn& txin, ptx->vin)
            {
                if (!mapPrev.count(txin.prevout.hash))
                    return false;
                vWorkQueue.push_back(mapPrev[txin.prevout.hash]);
            }
        }

        return true;
    }

    bool WriteToDisk();

    int64_t GetTxTime() const;
    int GetRequestCount() const;

    void AddSupportingTransactions(CTxDB& txdb);

    bool AcceptWalletTransaction(CTxDB& txdb, bool fCheckInputs=true);
    bool AcceptWalletTransaction();

    void RelayWalletTransaction(CTxDB& txdb);
    void RelayWalletTransaction();
};




class COutput
{
public:
    const CWalletTx *tx;
    int i;
    int nDepth;

    COutput(const CWalletTx *txIn, int iIn, int nDepthIn)
    {
        tx = txIn; i = iIn; nDepth = nDepthIn;
    }

    std::string ToString() const
    {
        return strprintf("COutput(%s, %d, %d) [%s %s]",
                 tx->GetHash().ToString().substr(0,10).c_str(), i, nDepth,
                 FormatMoney(tx->vout[i].nValue, tx->vout[i].nColor).c_str(),
                 COLOR_TICKER[tx->vout[i].nColor]);
    }

    void print() const
    {
        printf("%s\n", ToString().c_str());
    }
};




/** Private key that includes an expiration date in case it never gets used. */
class CWalletKey
{
public:
    CPrivKey vchPrivKey;
    int64_t nTimeCreated;
    int64_t nTimeExpires;
    std::string strComment;
    //// todo: add something to note what created it (user, getnewaddress, change)
    ////   maybe should have a map<string, string> property map

    CWalletKey(int64_t nExpires=0)
    {
        nTimeCreated = (nExpires ? GetTime() : 0);
        nTimeExpires = nExpires;
    }

    IMPLEMENT_SERIALIZE
    (
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPrivKey);
        READWRITE(nTimeCreated);
        READWRITE(nTimeExpires);
        READWRITE(strComment);
    )
};






/** Account information.
 * Stored in wallet with key "acc"+string account name.
 */
class CAccount
{
public:
    CPubKey vchPubKey;

    CAccount()
    {
        SetNull();
    }

    void SetNull()
    {
        vchPubKey = CPubKey();
    }

    IMPLEMENT_SERIALIZE
    (
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPubKey);
    )
};



/** Internal transfers.
 * Database key is acentry<account><counter>.
 */
class CAccountingEntry
{
public:
    std::string strAccount;
    int64_t nCreditDebit;
    int nColor;
    int64_t nTime;
    std::string strOtherAccount;
    std::string strComment;
    mapValue_t mapValue;
    int64_t nOrderPos;  // position in ordered transaction list
    uint64_t nEntryNo;

    CAccountingEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        nCreditDebit = 0;
        nColor = (int) BREAKOUT_COLOR_NONE;
        nTime = 0;
        strAccount.clear();
        strOtherAccount.clear();
        strComment.clear();
        nOrderPos = -1;
    }

    IMPLEMENT_SERIALIZE
    (
        CAccountingEntry& me = *const_cast<CAccountingEntry*>(this);
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        // Note: strAccount is serialized as part of the key, not here.
        READWRITE(nCreditDebit);
        READWRITE(nColor);
        READWRITE(nTime);
        READWRITE(strOtherAccount);

        if (!fRead)
        {
            WriteOrderPos(nOrderPos, me.mapValue);

            if (!(mapValue.empty() && _ssExtra.empty()))
            {
                CDataStream ss(nType, nVersion);
                ss.insert(ss.begin(), '\0');
                ss << mapValue;
                ss.insert(ss.end(), _ssExtra.begin(), _ssExtra.end());
                me.strComment.append(ss.str());
            }
        }

        READWRITE(strComment);

        size_t nSepPos = strComment.find("\0", 0, 1);
        if (fRead)
        {
            me.mapValue.clear();
            if (std::string::npos != nSepPos)
            {
                CDataStream ss(std::vector<char>(strComment.begin() + nSepPos + 1, strComment.end()), nType, nVersion);
                ss >> me.mapValue;
                me._ssExtra = std::vector<char>(ss.begin(), ss.end());
            }
            ReadOrderPos(me.nOrderPos, me.mapValue);
        }
        if (std::string::npos != nSepPos)
            me.strComment.erase(nSepPos);

        me.mapValue.erase("n");
    )

private:
    std::vector<char> _ssExtra;
};

bool GetWalletFile(CWallet* pwallet, std::string &strWalletFileOut);

#endif
