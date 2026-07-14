// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2014-2015 The BlackCoin developers
// Copyright (c) 2015-2016 James C. Stroud
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "alert.h"
#include "checkpoints.h"
#include "db.h"
#include "txdb-leveldb.h"
#include "exploredb-leveldb.h"
#include "explore/explore.hpp"
#include "net.h"
#include "init.h"
#include "ui_interface.h"
#include "kernel.h"
#include "stealth.h"

#include <boost/algorithm/string/replace.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

using namespace std;
using namespace boost;

const int GETBLOCKS_LIMIT = 500;

unsigned int nLaunchTime = LAUNCH_TIME;

//
// Global state
//

CCriticalSection cs_setpwalletRegistered;
set<CWallet*> setpwalletRegistered;

CCriticalSection cs_main;

CTxMemPool mempool;
unsigned int nTransactionsUpdated = 0;

map<uint256, CBlockIndex*> mapBlockIndex;
map<int, CBlockIndex*> mapBlockLookup;

set<pair<COutPoint, unsigned int> > setStakeSeen;


CBlockIndex* pindexGenesisBlock = NULL;
int nBestHeight = -1;

uint256 nBestChainTrust = 0;
uint256 nBestInvalidTrust = 0;

uint256 hashBestChain = 0;
CBlockIndex* pindexBest = NULL;
int64_t nTimeBestReceived = 0;
bool fImporting = false;

// Amount of blocks that other nodes claim to have
CMedianFilter<int> cPeerBlockCounts(5, 0);

struct COrphanBlock {
    uint256 hashBlock;
    uint256 hashPrev;
    pair<COutPoint, unsigned int> stake;
    valtype vchBlock;
};

map<uint256, COrphanBlock*> mapOrphanBlocks;
multimap<uint256, COrphanBlock*> mapOrphanBlocksByPrev;
set<pair<COutPoint, unsigned int> > setStakeSeenOrphan;

map<uint256, CTransaction> mapOrphanTransactions;
map<uint256, set<uint256> > mapOrphanTransactionsByPrev;

// Constant stuff for coinbase transactions we create:
CScript COINBASE_FLAGS;

const string strMessageMagic = "Breakout Signed Message:\n";

// Settings

// nDefaultCurrency and nDefaultStake are for sideways compatibility.
int nDefaultCurrency = (int) BREAKOUT_COLOR_NONE;
// This is not the color that stakes, all staking colors stake.
// This is the color that certain settings and commands apply
// to by default, like reservebalance.
int nDefaultStake = (int) BREAKOUT_COLOR_NONE;

// Do this dynamically here for maximum flexibility later
// Need this for consistency checking in case someone
//        makes a PoW only multicurrency
int nNumberOfStakingCurrencies = 0;

vector<int64_t> vTransactionFee(MIN_TX_FEE, MIN_TX_FEE + N_COLORS);
vector<int64_t> vReserveBalance(N_COLORS, 0);
vector<int64_t> vMinimumInputValue(MIN_INPUT_VALUE,
                                   MIN_INPUT_VALUE + N_COLORS);
// Minimum amount stake can be split into, to avoid overwhelming UTXOs
// Example: If minimum stake split is 4, then <8 won't be split.
vector<int64_t> vMinimumStakeSplit(MIN_INPUT_VALUE,
                                   MIN_INPUT_VALUE + N_COLORS);

//////////////////////////////////////////////////////////////////////////////
//
// dispatching functions
//

// These functions dispatch to one or all registered wallets


void RegisterWallet(CWallet* pwalletIn)
{
    {
        LOCK(cs_setpwalletRegistered);
        setpwalletRegistered.insert(pwalletIn);
    }
}

void UnregisterWallet(CWallet* pwalletIn)
{
    {
        LOCK(cs_setpwalletRegistered);
        setpwalletRegistered.erase(pwalletIn);
    }
}

// check whether the passed transaction is from us
bool static IsFromMe(CTransaction& tx, bool fMultiSig)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        if (pwallet->IsFromMe(tx, fMultiSig))
            return true;
    return false;
}

// get the wallet transaction with the given hash (if it exists)
bool static GetTransaction(const uint256& hashTx, CWalletTx& wtx)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        if (pwallet->GetTransaction(hashTx,wtx))
            return true;
    return false;
}

// erases transaction with the given hash from all wallets
void static EraseFromWallets(uint256 hash)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
        pwallet->EraseFromWallet(hash);
}

// make sure all wallets know about the given transaction, in the given block
void SyncWithWallets(const CTransaction& tx,
                     const CBlock* pblock,
                     bool fUpdate,
                     bool fConnect)
{
    // no reason why multisig should prevent a tx from syncing with all wallets
    static const bool fMultiSig = true;

    if (!fConnect)
    {
        // ppcoin: wallets need to refund inputs when disconnecting coinstake
        if (tx.IsCoinStake())
        {
            BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
            {
                if (pwallet->IsFromMe(tx, fMultiSig))
                {
                    pwallet->DisableTransaction(tx);
                }
            }
        }
        return;
    }

    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
    {
        pwallet->AddToWalletIfInvolvingMe(tx, pblock, fUpdate);
    }
}

// notify wallets about a new best chain
void static SetBestChain(const CBlockLocator& loc)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
    {
        pwallet->SetBestChain(loc);
    }
}

// notify wallets about an updated transaction
void static UpdatedTransaction(const uint256& hashTx)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
    {
        pwallet->UpdatedTransaction(hashTx);
    }
}

// dump all wallets
void static PrintWallets(const CBlock& block)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
    {
        pwallet->PrintWallet(block);
    }
}

// notify wallets about an incoming inventory (for request counts)
void static Inventory(const uint256& hash)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
    {
        pwallet->Inventory(hash);
    }
}

// ask wallets to resend their transactions
void ResendWalletTransactions(bool fForce)
{
    BOOST_FOREACH(CWallet* pwallet, setpwalletRegistered)
    {
        pwallet->ResendWalletTransactions(fForce);
    }
}

// Check that all of the input and output scripts of a transaction contains
// valid opcodes
bool CheckTxScriptsSanity(const CTransaction& tx)
{
    // Check input scripts for non-coinbase txs
    if (!tx.IsCoinBase())
    {
        for (unsigned int i = 0; i < tx.vin.size(); i++)
        {
            if (!tx.vin[i].scriptSig.HasValidOps() ||
                (tx.vin[i].scriptSig.size() > MAX_SCRIPT_SIZE))
            {
                return false;
            }
        }
    }
    // Check output scripts
    for (unsigned int i = 0; i < tx.vout.size(); i++)
    {
        if (!tx.vout[i].scriptPubKey.HasValidOps() ||
            (tx.vout[i].scriptPubKey.size() > MAX_SCRIPT_SIZE))
        {
            return false;
        }
    }
    return true;
}

bool DecodeHexTx(CTransaction& tx, const string& strHexTx)
{
    if (!IsHex(strHexTx)) {
        return false;
    }

    valtype txData(ParseHex(strHexTx));

    CDataStream ssData(txData, SER_NETWORK, PROTOCOL_VERSION);
    try {
        ssData >> tx;
        if (ssData.eof() && CheckTxScriptsSanity(tx)) {
            return true;
        }
    }
    catch (const std::exception&) {
        return false;
    }

    return true;
}

bool DecodeHexBlk(CBlock& block, const std::string& strHexBlk)
{
    if (!IsHex(strHexBlk))
    {
        return false;
    }

    valtype blockData(ParseHex(strHexBlk));
    CDataStream ssBlock(blockData, SER_NETWORK, PROTOCOL_VERSION);
    try
    {
        ssBlock >> block;
    }
    catch (const std::exception&)
    {
        return false;
    }

    return true;
}


//////////////////////////////////////////////////////////////////////////////
//
// mapOrphanTransactions
//

bool AddOrphanTx(const CTransaction& tx)
{
    uint256 hash = tx.GetHash();
    if (mapOrphanTransactions.count(hash))
        return false;

    // Ignore big transactions, to avoid a
    // send-big-orphans memory exhaustion attack. If a peer has a legitimate
    // large transaction with a missing parent then we assume
    // it will rebroadcast it later, after the parent transaction(s)
    // have been mined or received.
    // 10,000 orphans, each of which is at most 5,000 bytes big is
    // at most 500 megabytes of orphans:

    size_t nSize = tx.GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);

    if (nSize > 5000)
    {
        printf("ignoring large orphan tx (size: %" PRIszu ", hash: %s)\n", nSize, hash.ToString().substr(0,10).c_str());
        return false;
    }

    mapOrphanTransactions[hash] = tx;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
        mapOrphanTransactionsByPrev[txin.prevout.hash].insert(hash);

    printf("stored orphan tx %s (mapsz %" PRIszu ")\n", hash.ToString().substr(0,10).c_str(),
        mapOrphanTransactions.size());
    return true;
}

void static EraseOrphanTx(uint256 hash)
{
    if (!mapOrphanTransactions.count(hash))
        return;
    const CTransaction& tx = mapOrphanTransactions[hash];
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
    {
        mapOrphanTransactionsByPrev[txin.prevout.hash].erase(hash);
        if (mapOrphanTransactionsByPrev[txin.prevout.hash].empty())
            mapOrphanTransactionsByPrev.erase(txin.prevout.hash);
    }
    mapOrphanTransactions.erase(hash);
}

unsigned int LimitOrphanTxSize(unsigned int nMaxOrphans)
{
    unsigned int nEvicted = 0;
    while (mapOrphanTransactions.size() > nMaxOrphans)
    {
        // Evict a random orphan:
        uint256 randomhash = GetRandHash();
        map<uint256, CTransaction>::iterator it = mapOrphanTransactions.lower_bound(randomhash);
        if (it == mapOrphanTransactions.end())
            it = mapOrphanTransactions.begin();
        EraseOrphanTx(it->first);
        ++nEvicted;
    }
    return nEvicted;
}







//////////////////////////////////////////////////////////////////////////////
//
// CTransaction and CTxIndex
//

bool CTransaction::ReadFromDisk(CTxDB& txdb,
                                COutPoint prevout,
                                CTxIndex& txindexRet)
{
    SetNull();
    if (!txdb.ReadTxIndex(prevout.hash, txindexRet))
    {
        return false;
    }
    if (!ReadFromDisk(txindexRet.pos))
    {
        return false;
    }
    if (prevout.n >= vout.size())
    {
        SetNull();
        return false;
    }
    return true;
}

bool CTransaction::ReadFromDisk(CTxDB& txdb, COutPoint prevout)
{
    CTxIndex txindex;
    return ReadFromDisk(txdb, prevout, txindex);
}

bool CTransaction::ReadFromDisk(COutPoint prevout)
{
    CTxDB txdb("r");
    CTxIndex txindex;
    return ReadFromDisk(txdb, prevout, txindex);
}

bool CTransaction::IsStandard() const
{
    if (nVersion > CTransaction::CURRENT_VERSION)
        return false;


    if (vout.size() < 1)
    {
         return false;
    }

    // make sure vout is color consistent and valid
    // Non coinbase can have fee delegate outputs, so color consistency
    //     means all outputs have colors of vout[0] or
    //     the color of the fee delegate.
    // Coinbase may have transaction fee outputs of varying colors.
    int nColor = GetColor();
    for (unsigned int i = 0; i < vout.size(); ++i)
    {
        int c = vout[i].nColor;
        if (!CheckColor(c))
        {
             return false;
        }
    }

    int nFeeColor = FEE_COLOR[nColor];

    if (!IsCoinBase())
    {
        vector<CTxOut>::const_iterator it;
        for (it = vout.begin() + 1; it != vout.end(); ++it)
        {
            if ((it->nColor != nColor) && (it->nColor != nFeeColor))
            {
                  return false;
            }
        }
    }

    // Disallow large transaction comments
    if ((unsigned int) strTxComment.length() > MAX_TX_COMMENT_LEN)
    {
          return false;
    }


    // Treat non-final transactions as non-standard to prevent a specific type
    // of double-spend attack, as well as DoS attacks. (if the transaction
    // can't be mined, the attacker isn't expending resources broadcasting it)
    // Basically we don't want to propagate transactions that can't be included in
    // the next block.
    //
    // However, IsFinalTx() is confusing... Without arguments, it uses
    // chainActive.Height() to evaluate nLockTime; when a block is accepted, chainActive.Height()
    // is set to the value of nHeight in the block. However, when IsFinalTx()
    // is called within CBlock::AcceptBlock(), the height of the block *being*
    // evaluated is what is used. Thus if we want to know if a transaction can
    // be part of the *next* block, we need to call IsFinalTx() with one more
    // than chainActive.Height().
    //
    // Timestamps on the other hand don't get any special treatment, because we
    // can't know what timestamp the next block will have, and there aren't
    // timestamp applications where it matters.
    if (!IsFinal(nBestHeight + 1)) {
        return false;
    }
    // nTime has different purpose from nLockTime but can be used in similar attacks
    if (nTime > FutureDrift(GetAdjustedTime())) {
        return false;
    }

    // Extremely large transactions with lots of inputs can cost the network
    // almost as much to process as they cost the sender in fees, because
    // computing signature hashes is O(ninputs*txsize). Limiting transactions
    // to MAX_STANDARD_TX_SIZE mitigates CPU exhaustion attacks.
    unsigned int sz = GetSerializeSize(SER_NETWORK, CTransaction::CURRENT_VERSION);
    if (sz >= MAX_STANDARD_TX_SIZE) {
        return false;
    }



    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        // Biggest 'standard' txin is a 15-of-15 P2SH multisig with compressed
        // keys. (remember the 520 byte limit on redeemScript size) That works
        // out to a (15*(33+1))+3=513 byte redeemScript, 513+1+15*(73+1)+3=1627
        // bytes of scriptSig, which we round off to 1650 bytes for some minor
        // future-proofing. That's also enough to spend a 20-of-20
        // CHECKMULTISIG scriptPubKey, though such a scriptPubKey is not
        // considered standard)
        if (txin.scriptSig.size() > 1650)
            return false;
        if (!txin.scriptSig.IsPushOnly())
            return false;
        if (fEnforceCanonical && !txin.scriptSig.HasCanonicalPushes()) {
            return false;
        }
    }
    unsigned int nDataOut = 0;
    unsigned int nTxnOut = 0;
    txnouttype whichType;
    BOOST_FOREACH(const CTxOut& txout, vout) {
        if (!::IsStandard(txout.scriptPubKey, whichType))
            return false;

        if (whichType == TX_NULL_DATA)  {
            nDataOut++;
        } else {
            if (txout.nValue == 0)
                  return false;
            nTxnOut++;
        }
            if (fEnforceCanonical && !txout.scriptPubKey.HasCanonicalPushes())
                  return false;
    }

    if (nDataOut > nTxnOut) {
        return false;
    }

    return true;
}

// TODO: move up or add to CTransaction class or something
bool IsFinalTx(const CTransaction &tx, int nBlockHeight, int64_t nBlockTime)
{
    AssertLockHeld(cs_main);
    // Time based nLockTime implemented in 0.1.6
    if (tx.nLockTime == 0)
        return true;
    if (nBlockHeight == 0)
        nBlockHeight = nBestHeight;
    if (nBlockTime == 0)
        nBlockTime = GetAdjustedTime();
    if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
        return true;
    BOOST_FOREACH(const CTxIn& txin, tx.vin)
        if (!txin.IsFinal())
            return false;
    return true;
}


//
// Check transaction inputs, and make sure any
// pay-to-script-hash transactions are evaluating IsStandard scripts
//
// Why bother? To avoid denial-of-service attacks; an attacker
// can submit a standard HASH... OP_EQUAL transaction,
// which will get accepted into blocks. The redemption
// script can be anything; an attacker could use a very
// expensive-to-check-upon-redemption script like:
//   DUP CHECKSIG DROP ... repeated 100 times... OP_1
//
bool CTransaction::AreInputsStandard(const MapPrevTx& mapInputs) const
{
    if (IsCoinBase())
        return true; // Coinbases don't use vin normally

    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const CTxOut& prev = GetOutputFor(vin[i], mapInputs);

        vector<valtype > vSolutions;
        txnouttype whichType;
        // get the scriptPubKey corresponding to this input:
        const CScript& prevScript = prev.scriptPubKey;
        if (!Solver(prevScript, whichType, vSolutions))
            return false;
        int nArgsExpected = ScriptSigArgsExpected(whichType, vSolutions);
        if (nArgsExpected < 0)
            return false;

        // Transactions with extra stuff in their scriptSigs are
        // non-standard. Note that this EvalScript() call will
        // be quick, because if there are any operations
        // beside "push data" in the scriptSig the
        // IsStandard() call returns false
        vector<valtype > stack;
        if (!EvalScript(stack, vin[i].scriptSig, *this, i, SCRIPT_VERIFY_NONE, 0))
            return false;

        if (whichType == TX_SCRIPTHASH)
        {
            if (stack.empty())
                return false;
            CScript subscript(stack.back().begin(), stack.back().end());
            vector<valtype > vSolutions2;
            txnouttype whichType2;
            if (Solver(subscript, whichType2, vSolutions2))
            {
                int tmpExpected = ScriptSigArgsExpected(whichType2, vSolutions2);
                if (tmpExpected < 0)
                    return false;
                nArgsExpected += tmpExpected;
            }
            else
            {
                // Any other Script with less than 15 sigops OK:
                unsigned int sigops = subscript.GetSigOpCount(true);
                // ... extra data left on the stack after execution is OK, too:
                return (sigops <= MAX_P2SH_SIGOPS);
            }
        }

        if (stack.size() != (unsigned int)nArgsExpected)
            return false;
    }

    return true;
}

unsigned int
CTransaction::GetLegacySigOpCount() const
{
    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        nSigOps += txin.scriptSig.GetSigOpCount(false);
    }
    BOOST_FOREACH(const CTxOut& txout, vout)
    {
        nSigOps += txout.scriptPubKey.GetSigOpCount(false);
    }
    return nSigOps;
}

int CMerkleTx::SetMerkleBranch(const CBlock* pblock)
{
    AssertLockHeld(cs_main);
    if (fClient)
    {
        if (hashBlock == 0)
            return 0;
    }
    else
    {
        CBlock blockTmp;
        if (pblock == NULL)
        {
            // Load the block this tx is in
            CTxIndex txindex;
            if (!CTxDB("r").ReadTxIndex(GetHash(), txindex))
                return 0;
            if (!blockTmp.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos))
                return 0;
            pblock = &blockTmp;
        }

        // Update the tx's hashBlock
        hashBlock = pblock->GetHash();

        // Locate the transaction
        for (nIndex = 0; nIndex < (int)pblock->vtx.size(); nIndex++)
            if (pblock->vtx[nIndex] == *(CTransaction*)this)
                break;
        if (nIndex == (int)pblock->vtx.size())
        {
            vMerkleBranch.clear();
            nIndex = -1;
            printf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
            return 0;
        }

        // Fill in merkle branch
        vMerkleBranch = pblock->GetMerkleBranch(nIndex);
    }

    // Is the tx in a block that's in the main chain
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;

    return pindexBest->nHeight - pindex->nHeight + 1;
}


bool CTransaction::CheckTransaction() const
{
    // Basic checks that don't depend on any context
    if (vin.empty())
        return DoS(10, error("CTransaction::CheckTransaction() : vin empty"));
    if (vout.empty())
        return DoS(10, error("CTransaction::CheckTransaction() : vout empty"));
    // flying Delorean: https://github.com/ppcoin/ppcoin/pull/104
    if (nTime > FutureDrift(GetAdjustedTime()))
        return DoS(10, error("CTransaction::CheckTransaction() : timestamp is too far into the future"));
    // Size limits
    if (::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return DoS(100, error("CTransaction::CheckTransaction() : size limits failed"));

    // Check for output color consistency (input checking is done for accept)
    // coinbase vout can be many colored because of transaction fees
    if (!IsCoinBase())
    {
        int nColor = GetColor();
        int nFeeColor = FEE_COLOR[nColor];
        vector<CTxOut>::const_iterator oit;
        for (oit = vout.begin() + 1; oit != vout.end(); ++oit)
        {
              if ((oit->nColor != nColor) && (oit->nColor != nFeeColor))
              {
                   return DoS(100, error("CTransaction::CheckTransaction() : "
                                           "all txout are not tx currency or fee delegate"));
              }
        }
    }

    // Check for negative or overflow output values
    vector<int64_t> vValueOut(N_COLORS, 0);
    for (unsigned int i = 0; i < vout.size(); i++)
    {
        const CTxOut& txout = vout[i];
        int nColorOut = txout.nColor;

        if (txout.IsEmpty() && !DoesMature())
        {
            return DoS(100,
                       error("CTransaction::CheckTransaction() : txout empty "
                             "for user transaction"));
        }
        if (txout.nValue < 0)
        {
            return DoS(100,
                       error("CTransaction::CheckTransaction() : txout.nValue "
                             "negative"));
        }
        if (txout.nValue > MAX_MONEY[nColorOut])
        {
            return DoS(100,
                       error("CTransaction::CheckTransaction() : txout.nValue "
                             "too high"));
        }
        vValueOut[nColorOut] += txout.nValue;
        if (!MoneyRange(vValueOut[nColorOut], nColorOut))
        {
            return DoS(100,
                       error("CTransaction::CheckTransaction() : txout total "
                             "out of range"));
        }
    }

    // Check for duplicate inputs
    set<COutPoint> vInOutPoints;
    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        if (vInOutPoints.count(txin.prevout))
            return false;
        vInOutPoints.insert(txin.prevout);
    }

    if (IsCoinBase())
    {
        if (vin[0].scriptSig.size() < 2 || vin[0].scriptSig.size() > 100)
            return DoS(100, error("CTransaction::CheckTransaction() : coinbase script size is invalid (%d)", (int) vin[0].scriptSig.size()));
    }
    else
    {
        BOOST_FOREACH(const CTxIn& txin, vin)
            if (txin.prevout.IsNull())
                return DoS(10, error("CTransaction::CheckTransaction() : prevout is null"));
    }

    // special stratum required
    if ((unsigned int) strTxComment.length() > MAX_TX_COMMENT_LEN)
    {
        return DoS(10, error("CTransaction::CheckTransaction() : comment too big"));
    }

    return true;
}

int64_t CTransaction::GetMinFee(unsigned int nBlockSize,
                                enum GetMinFee_mode mode, unsigned int nBytes) const
{
    if (DoesMature())
    {
         return 0;
    }
    int nColor = this->GetColor();
    int nFeeColor = FEE_COLOR[nColor];

    // Base fee is either MIN_TX_FEE or MIN_RELAY_TX_FEE
    int64_t nBaseFee = (mode == GMF_RELAY) ? MIN_RELAY_TX_FEE[nFeeColor] : MIN_TX_FEE[nFeeColor];

    unsigned int nNewBlockSize = nBlockSize + nBytes;
    int64_t nMinFee = (1 + (int64_t)nBytes / 1000) * nBaseFee;

    // To limit dust spam, require MIN_TX_FEE/MIN_RELAY_TX_FEE if any output is less than 0.01
    if (nMinFee < nBaseFee)
    {
        BOOST_FOREACH(const CTxOut& txout, vout)
            if (txout.nValue < CENT[nColor])
                nMinFee = nBaseFee;
    }

    // Raise the price as the block approaches full
    if (nBlockSize != 1 && nNewBlockSize >= MAX_BLOCK_SIZE_GEN/2)
    {
        if (nNewBlockSize >= MAX_BLOCK_SIZE_GEN)
        {
            return MAX_MONEY[nFeeColor];
        }
        nMinFee *= MAX_BLOCK_SIZE_GEN / (MAX_BLOCK_SIZE_GEN - nNewBlockSize);
    }

    // extra fees were never included
    if (GetFork(this->nTime) >= BRK_FORK003) {
       // ensure they pay their service fees, which are tacked on flat
       nMinFee += this->GetServiceFee();
       // ensure they pay their OP_RETURN fees, which are also tacked on flat
       nMinFee += this->GetOpRetFee();
    }

    if (!MoneyRange(nMinFee, nFeeColor))
    {
        nMinFee = MAX_MONEY[nFeeColor];
    }
    return nMinFee;
}

// Service Fee
int64_t CTransaction::GetServiceFee() const {
    int nFeeColor = FEE_COLOR[this->GetColor()];
    int64_t nServiceFee = 0;
    nServiceFee = COMMENT_FEE_PER_CHAR[nFeeColor] * strTxComment.size();
    return nServiceFee;
}


// OP_RETURN Fees: Encourage use of services
// You get 1 stealth secret free for each non OP_RETURN Output
// But you pay for non OP_RETURN outputs
int64_t CTransaction::GetOpRetFee() const {
    int nFeeColor = FEE_COLOR[this->GetColor()];
    vector<uint8_t> vchR;
    opcodetype opCode;

    vector<int64_t> vOpRetFees;

    int nFreeSSecrets = 0;

    BOOST_FOREACH(const CTxOut& txout, vout) {
        CScript scriptPK = txout.scriptPubKey;
        CScript::const_iterator pc = scriptPK.begin();

        // checked in GetOp2, but be safe
        if (!vchR.empty()) {
              vchR.clear();
        }

        if (!(scriptPK.GetOp(pc, opCode, vchR) && (opCode == OP_RETURN))) {
                nFreeSSecrets += 1;
        }
    }

    BOOST_FOREACH(const CTxOut& txout, vout) {
        CScript scriptPK = txout.scriptPubKey;
        CScript::const_iterator pc = scriptPK.begin();

        if (!vchR.empty()) {
              vchR.clear();
        }

        if (scriptPK.GetOp(pc, opCode, vchR) && (opCode == OP_RETURN)) {
              int64_t nFee;
              unsigned int scriptSize = scriptPK.size();
              if (scriptSize > MAX_OP_RET_LEN)
              {
                  // TODO: reject somewhere where it makes more sense
                  nFee = MAX_MONEY[nFeeColor] + 1;
              }
              else
              {
                  nFee = (OP_RET_FEE_PER_CHAR[nFeeColor] * scriptPK.size());
              }
              vOpRetFees.push_back(nFee);
        }
    }

    // assume the smallest fees belong to secrets
    sort(vOpRetFees.begin(), vOpRetFees.end(), greater<int>());

    int nOpRetFee = 0;
    if ((int) vOpRetFees.size() > nFreeSSecrets)
    {
          vOpRetFees.resize(vOpRetFees.size() - nFreeSSecrets);
          vector<int64_t>::const_iterator it;
          for (it = vOpRetFees.begin(); it != vOpRetFees.end(); ++it)
          {
               nOpRetFee += (int64_t) *it;
          }
    }
    return nOpRetFee;
}


bool CTxMemPool::accept(CTxDB& txdb, CTransaction &tx, bool fCheckInputs,
                        bool* pfMissingInputs)
{
    // no reason why multisig should be treated differently for accept to mempool
    static const bool fMultiSig = true;

    AssertLockHeld(cs_main);
    if (pfMissingInputs)
        *pfMissingInputs = false;

    if (!tx.CheckTransaction())
        return error("CTxMemPool::accept() : CheckTransaction failed");

    // Coinbase is only valid in a block, not as a loose transaction
    if (tx.IsCoinBase())
        return tx.DoS(100, error("CTxMemPool::accept() : coinbase as individual tx"));

    // ppcoin: coinstake is also only valid in a block, not as a loose transaction
    if (tx.IsCoinStake())
        return tx.DoS(100, error("CTxMemPool::accept() : coinstake as individual tx"));

    // Rather not work on nonstandard transactions (unless -testnet)
    if (!fTestNet && !tx.IsStandard())
        return error("CTxMemPool::accept() : nonstandard transaction type");

    // Do we already have it?
    uint256 hash = tx.GetHash();
    {
        LOCK(cs);
        if (mapTx.count(hash))
            return false;
    }
    if (fCheckInputs)
        if (txdb.ContainsTx(hash))
            return false;

    // Check for conflicts with in-memory transactions
    CTransaction* ptxOld = NULL;
    for (unsigned int i = 0; i < tx.vin.size(); i++)
    {
        COutPoint outpoint = tx.vin[i].prevout;
        if (mapNextTx.count(outpoint))
        {
            // Disable replacement feature for now
            return false;

            // Allow replacing with a newer version of the same transaction
            if (i != 0)
                return false;
            ptxOld = mapNextTx[outpoint].ptx;
            if (ptxOld->IsFinal())
                return false;
            if (!tx.IsNewerThan(*ptxOld))
                return false;
            for (unsigned int i = 0; i < tx.vin.size(); i++)
            {
                COutPoint outpoint = tx.vin[i].prevout;
                if (!mapNextTx.count(outpoint) || mapNextTx[outpoint].ptx != ptxOld)
                    return false;
            }
            break;
        }
    }

    int nColor = tx.GetColor();
    int nFeeColor = FEE_COLOR[nColor];

    if (fCheckInputs)
    {
        MapPrevTx mapInputs;
        map<uint256, CTxIndex> mapUnused;
        bool fInvalid = false;

        if (!tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        {
            if (fInvalid)
            {
                return error("CTxMemPool::accept() : FetchInputs found invalid tx %s",
                                                     hash.ToString().substr(0,10).c_str());
            }
            if (pfMissingInputs)
                *pfMissingInputs = true;
            return false;
        }

        // Check for non-standard pay-to-script-hash in inputs
        if (!tx.AreInputsStandard(mapInputs) && !fTestNet)
        {
            return error("CTxMemPool::accept() : nonstandard transaction input");
        }

        // Note: if you modify this code to accept non-standard transactions, then
        // you should add code here to check that the transaction does a
        // reasonable number of ECDSA signature verifications.

        // Check that the transaction doesn't have an excessive number of
        // sigops, making it impossible to mine. Since the coinbase transaction
        // itself can contain sigops MAX_TX_SIGOPS is less than
        // MAX_BLOCK_SIGOPS; we still consider this an invalid rather than
        // merely non-standard transaction.
        unsigned int nSigOps = tx.GetLegacySigOpCount();
        nSigOps += tx.GetP2SHSigOpCount(mapInputs);
        if (nSigOps > MAX_TX_SIGOPS)
            return tx.DoS(0,
                          error("CTxMemPool::accept() : too many sigops %s, %d > %d",
                                hash.ToString().c_str(), nSigOps, MAX_TX_SIGOPS));


        // fees are only assessed in the fee color (nFeeColor)
        int64_t nFees;
        if (nFeeColor == nColor)
        {
              nFees = tx.GetValueIn(mapInputs, nColor) - tx.GetValueOut(nColor);
        }
        else
        {
              ColorsMap mapValuesOut;
              tx.FillValuesOut(mapValuesOut);
              nFees = tx.GetValueIn(mapInputs, nFeeColor) - mapValuesOut.Get(nFeeColor);
        }

        unsigned int nSize = ::GetSerializeSize(tx, SER_NETWORK, PROTOCOL_VERSION);

        // Don't accept it if it can't get into a block (assume block is 1% full?)
        int64_t txMinFee = tx.GetMinFee(MAX_BLOCK_SIZE/100, GMF_RELAY, nSize);
        if (nFees < txMinFee)
            return error("CTxMemPool::accept() : not enough fees %s, %" PRId64 " < %" PRId64,
                         hash.ToString().c_str(),
                         nFees, txMinFee);

        // Continuously rate-limit free transactions
        // This mitigates 'penny-flooding' -- sending thousands of free transactions just to
        // be annoying or make others' transactions take longer to confirm.
        if (nFees < MIN_RELAY_TX_FEE[nFeeColor])
        {
            static CCriticalSection cs;
            static double dFreeCount;
            static int64_t nLastTime;
            int64_t nNow = GetTime();

            {
                LOCK(cs);
                // Use an exponentially decaying ~10-minute window:
                dFreeCount *= pow(1.0 - 1.0/600.0, (double)(nNow - nLastTime));
                nLastTime = nNow;
                // -limitfreerelay unit is thousand-bytes-per-minute
                // At default rate it would take over a month to fill 1GB
                if (dFreeCount > GetArg("-limitfreerelay", 15)*10*1000 && !IsFromMe(tx, fMultiSig))
                    return error("CTxMemPool::accept() : free transaction rejected by rate limiter");
                if (fDebug)
                    printf("Rate limit dFreeCount: %g => %g\n", dFreeCount, dFreeCount+nSize);
                dFreeCount += nSize;
            }
        }

        // Check against previous transactions
        // This is done last to help prevent CPU exhaustion denial-of-service attacks.
        if (!tx.ConnectInputs(txdb, mapInputs, mapUnused, CDiskTxPos(1,1,1),
                                                 pindexBest, false, false, STANDARD_SCRIPT_VERIFY_FLAGS))
        {
            return error("CTxMemPool::accept() : ConnectInputs failed %s", hash.ToString().substr(0,10).c_str());
        }
    }

    // Store transaction in memory
    {
        LOCK(cs);
        if (ptxOld)
        {
            printf("CTxMemPool::accept() : replacing tx %s with new version\n", ptxOld->GetHash().ToString().c_str());
            remove(*ptxOld);
        }
        addUnchecked(hash, tx);
    }

    ///// are we sure this is ok when loading transactions or restoring block txes
    // If updated, erase old tx from wallet
    if (ptxOld)
        EraseFromWallets(ptxOld->GetHash());

    printf("CTxMemPool::accept() : accepted %s (poolsz %" PRIszu ")\n",
           hash.ToString().substr(0,10).c_str(),
           mapTx.size());
    return true;
}

bool CTransaction::AcceptToMemoryPool(CTxDB& txdb, bool fCheckInputs, bool* pfMissingInputs)
{
    return mempool.accept(txdb, *this, fCheckInputs, pfMissingInputs);
}

bool CTxMemPool::addUnchecked(const uint256& hash, CTransaction &tx)
{
    // Add to memory pool without checking anything.  Don't call this directly,
    // call CTxMemPool::accept to properly check the transaction first.
    {
        mapTx[hash] = tx;
        for (unsigned int i = 0; i < tx.vin.size(); i++)
            mapNextTx[tx.vin[i].prevout] = CInPoint(&mapTx[hash], i);
        nTransactionsUpdated++;
    }
    return true;
}


bool CTxMemPool::remove(const CTransaction &tx, bool fRecursive)
{
    // Remove transaction from memory pool
    {
        LOCK(cs);
        uint256 hash = tx.GetHash();
        if (mapTx.count(hash))
        {
            if (fRecursive) {
                for (unsigned int i = 0; i < tx.vout.size(); i++) {
                    map<COutPoint, CInPoint>::iterator it = mapNextTx.find(COutPoint(hash, i));
                    if (it != mapNextTx.end())
                        remove(*it->second.ptx, true);
                }
            }
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
                mapNextTx.erase(txin.prevout);
            mapTx.erase(hash);
            nTransactionsUpdated++;
        }
    }
    return true;
}

bool CTxMemPool::removeConflicts(const CTransaction &tx)
{
    // Remove transactions which depend on inputs of tx, recursively
    LOCK(cs);
    BOOST_FOREACH(const CTxIn &txin, tx.vin) {
        map<COutPoint, CInPoint>::iterator it = mapNextTx.find(txin.prevout);
        if (it != mapNextTx.end()) {
            const CTransaction &txConflict = *it->second.ptx;
            if (txConflict != tx)
                remove(txConflict, true);
        }
    }
    return true;
}

void CTxMemPool::clear()
{
    LOCK(cs);
    mapTx.clear();
    mapNextTx.clear();
    ++nTransactionsUpdated;
}

void CTxMemPool::queryHashes(vector<uint256>& vtxid)
{
    vtxid.clear();

    LOCK(cs);
    vtxid.reserve(mapTx.size());
    for (map<uint256, CTransaction>::iterator mi = mapTx.begin(); mi != mapTx.end(); ++mi)
        vtxid.push_back((*mi).first);
}




int CMerkleTx::GetDepthInMainChainINTERNAL(CBlockIndex* &pindexRet) const
{
    if (hashBlock == 0 || nIndex == -1)
        return 0;
    AssertLockHeld(cs_main);

    // Find the block it claims to be in
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashBlock);
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;

    // Make sure the merkle branch connects to this block
    if (!fMerkleVerified)
    {
        if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot)
            return 0;
        fMerkleVerified = true;
    }

    pindexRet = pindex;
    return pindexBest->nHeight - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(CBlockIndex* &pindexRet) const
{
    AssertLockHeld(cs_main);
    int nResult = GetDepthInMainChainINTERNAL(pindexRet);
    if (nResult == 0 && !mempool.exists(GetHash()))
        return -1; // Not in chain, not in mempool

    return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const
{
    // ADVISORY: static is an optimization, may not be suitable for forks
    static int nCoinbaseMaturity = GetCoinbaseMaturity();

    if (!DoesMature())
    {
        return 0;
    }
    return max(0, (nCoinbaseMaturity + 1) - GetDepthInMainChain());
}


bool CMerkleTx::AcceptToMemoryPool(CTxDB& txdb, bool fCheckInputs)
{
    if (fClient)
    {
        if (!IsInMainChain() && !ClientConnectInputs())
            return false;
        return CTransaction::AcceptToMemoryPool(txdb, fCheckInputs);
    }
    else
    {
        return CTransaction::AcceptToMemoryPool(txdb, fCheckInputs);
    }
}

bool CMerkleTx::AcceptToMemoryPool()
{
    CTxDB txdb("r");
    return AcceptToMemoryPool(txdb);
}



bool CWalletTx::AcceptWalletTransaction(CTxDB& txdb, bool fCheckInputs)
{

    {
        LOCK(mempool.cs);
        // Add previous supporting transactions first
        BOOST_FOREACH(CMerkleTx& tx, vtxPrev)
        {
            if (!tx.DoesMature())
            {
                uint256 hash = tx.GetHash();
                if (!mempool.exists(hash) && !txdb.ContainsTx(hash))
                    tx.AcceptToMemoryPool(txdb, fCheckInputs);
            }
        }
        return AcceptToMemoryPool(txdb, fCheckInputs);
    }
    return false;
}

bool CWalletTx::AcceptWalletTransaction()
{
    CTxDB txdb("r");
    return AcceptWalletTransaction(txdb);
}

int CTxIndex::GetDepthInMainChain() const
{
    // Read block header
    CBlock block;
    if (!block.ReadFromDisk(pos.nFile, pos.nBlockPos, false))
        return 0;
    // Find the block in the index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(block.GetHash());
    if (mi == mapBlockIndex.end())
        return 0;
    CBlockIndex* pindex = (*mi).second;
    if (!pindex || !pindex->IsInMainChain())
        return 0;
    return 1 + nBestHeight - pindex->nHeight;
}

// Return transaction in tx, and if it was found inside a block, its hash is placed in hashBlock
bool GetTransaction(const uint256 &hash, CTransaction &tx, uint256 &hashBlock)
{
    {
        LOCK(cs_main);
        {
            LOCK(mempool.cs);
            if (mempool.lookup(hash, tx))
            {
                return true;
            }
        }
        CTxDB txdb("r");
        CTxIndex txindex;
        if (tx.ReadFromDisk(txdb, COutPoint(hash, 0), txindex))
        {
            CBlock block;
            if (block.ReadFromDisk(txindex.pos.nFile, txindex.pos.nBlockPos, false))
                hashBlock = block.GetHash();
            return true;
        }
    }
    return false;
}








//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

static CBlockIndex* pblockindexFBBHLast;
CBlockIndex* FindBlockByHeight(int nHeight)
{
    CBlockIndex *pblockindex;
    if (nHeight < nBestHeight / 2)
        pblockindex = pindexGenesisBlock;
    else
        pblockindex = pindexBest;
    if (pblockindexFBBHLast && abs(nHeight - pblockindex->nHeight) > abs(nHeight - pblockindexFBBHLast->nHeight))
        pblockindex = pblockindexFBBHLast;
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;
    while (pblockindex->nHeight < nHeight)
        pblockindex = pblockindex->pnext;
    pblockindexFBBHLast = pblockindex;
    return pblockindex;
}

bool CBlock::ReadFromDisk(const CBlockIndex* pindex, bool fReadTransactions)
{
    if (!fReadTransactions)
    {
        *this = pindex->GetBlockHeader();
        return true;
    }
    if (!ReadFromDisk(pindex->nFile, pindex->nBlockPos, fReadTransactions))
        return false;
    if (GetHash() != pindex->GetBlockHash())
        return error("CBlock::ReadFromDisk() : GetHash() doesn't match index");
    return true;
}

uint256 static GetOrphanRoot(const uint256& hash)
{
    map<uint256, COrphanBlock*>::iterator it = mapOrphanBlocks.find(hash);
    if (it == mapOrphanBlocks.end())
        return hash;

    // Work back to the first block in the orphan chain
    do {
        map<uint256, COrphanBlock*>::iterator it2 = mapOrphanBlocks.find(it->second->hashPrev);
        if (it2 == mapOrphanBlocks.end())
            return it->first;
        it = it2;
    } while(true);
}

// ppcoin: find block wanted by given orphan block
uint256 WantedByOrphan(const COrphanBlock* pblockOrphan)
{
    // Work back to the first block in the orphan chain
    while (mapOrphanBlocks.count(pblockOrphan->hashPrev))
        pblockOrphan = mapOrphanBlocks[pblockOrphan->hashPrev];
    return pblockOrphan->hashPrev;
}

// Remove a random orphan block (which does not have any dependent orphans).
void static PruneOrphanBlocks()
{
    if (mapOrphanBlocksByPrev.size() <= (size_t)max((int64_t)0,
													GetArg("-maxorphanblocks",
                                                           DEFAULT_MAX_ORPHAN_BLOCKS)))
        return;

    // Pick a random orphan block.
    int pos = insecure_rand() % mapOrphanBlocksByPrev.size();
    multimap<uint256, COrphanBlock*>::iterator it = mapOrphanBlocksByPrev.begin();
    while (pos--) it++;

    // As long as this block has other orphans depending on it, move to one of those successors.
    do {
        multimap<uint256, COrphanBlock*>::iterator it2 =
                             mapOrphanBlocksByPrev.find(it->second->hashBlock);
        if (it2 == mapOrphanBlocksByPrev.end())
            break;
        it = it2;
    } while(1);

    setStakeSeenOrphan.erase(it->second->stake);
    uint256 hash = it->second->hashBlock;
    delete it->second;
    mapOrphanBlocksByPrev.erase(it);
    mapOrphanBlocks.erase(hash);
}


// miner's coin base reward
// new coins: carefully author this function, it is specific to breakout
struct AMOUNT GetPoWSubsidy(CBlockIndex* pindexPrev)
{
    struct AMOUNT stSubsidy;
    int nHeight = pindexPrev->nHeight + 1;
    stSubsidy.nValue = 0;
    // the default value is Breakout Coin so that
    //    the chain can move during distribution
    stSubsidy.nColor = (int) BREAKOUT_COLOR_BRK;

    // premines
    if (nHeight < N_COLORS)
    {
        stSubsidy.nValue = POW_SUBSIDY[nHeight];
        stSubsidy.nColor = nHeight;
    }
    else
    {
        stSubsidy.nColor = (int) BREAKOUT_COLOR_SIS;
        // Siscoin emission characteristics same as BTC
        // PoW blocks are expected every 2 min (half are PoW)
        int halvings = nHeight / 2103840;
        if (halvings >= 64)
        {
             stSubsidy.nValue = 0;
        }
        else
        {
            if (GetFork(pindexPrev->nTime) < BRK_FORK005)
            {
                stSubsidy.nValue = 50 * COIN[BREAKOUT_COLOR_SIS];
            }
            else
            {
                // new block times are 5 times shorter
                stSubsidy.nValue = 10 * COIN[BREAKOUT_COLOR_SIS];
            }
            stSubsidy.nValue >>= halvings;
        }
    }

    return stSubsidy;
}


// miner's coin base reward
// fee outputs are created elsewhere, so even though they may be collected, don't
//    add them to the subsidy here
// TODO: the ability for miners to collect fees for any currency is
//       good argument for multicolored transactions, or to mark vtx[2+] as fee transactions
struct AMOUNT GetProofOfWorkReward(CBlockIndex* pindexPrev)
{
    struct AMOUNT stSubsidy = GetPoWSubsidy(pindexPrev);

    if (fDebug && GetBoolArg("-printcreation"))
    {
        printf("GetProofOfWorkReward() : create=%s nSubsidy=%" PRId64 "\n",
               FormatMoney(stSubsidy.nValue, stSubsidy.nColor).c_str(),
               stSubsidy.nValue);
    }
    return stSubsidy;
}


// miner's coin stake depends on the block height and mint currency (mintColor)
// fees get eaten by network for PoS
// coin authors: carefully rewrite this code if you intend to change emission
struct AMOUNT GetProofOfStakeReward(CBlockIndex* pindexPrev, int nStakeColor)
{
    int mintColor = (int) MINT_COLOR[nStakeColor];

    int64_t nSubsidy = BASE_POS_REWARD[nStakeColor];

    if (nSubsidy > 0)
    {
        // deck staked
        if (nSubsidy < BASE_COIN)
        {
            if (GetFork(pindexPrev->nTime) < BRK_FORK003)
            {
                nSubsidy = 0;
            }
            else if (GetFork(pindexPrev->nTime) < BRK_FORK004)
            {
                struct AMOUNT stSubsidy = GetPoWSubsidy(pindexPrev);
                nSubsidy = 2 * ((stSubsidy.nValue * COIN[mintColor]) +
                                (stSubsidy.nValue * CENT[mintColor] * nSubsidy));
            }
            else
            {
                struct AMOUNT stSubsidy = GetPoWSubsidy(pindexPrev);
                nSubsidy = (10 * stSubsidy.nValue) + (nSubsidy * (stSubsidy.nValue / 10));
            }
        }
        // BRX staked
        else
        {
            // calculate the reward based on instantaneous inflation rates
            // this may be more complicated for multiple staking currencies
            int64_t supply = pindexPrev->vMoneySupply[mintColor];
            int64_t minReward;
            if (GetFork(pindexPrev->nTime) < BRK_FORK002)
            {
                // the original emission was too low by 1/2
                minReward = (5 * (supply / 100)) / 105192;
            }
            else if (GetFork(pindexPrev->nTime) < BRK_FORK005)
            {
                // the original subsidy was 1/2 the correct amount for ~5% interest
                nSubsidy += nSubsidy;
                // multiply by 5% then divide by the number of blocks in a year (52596)
                minReward = (5 * (supply / 100)) / 52596;
            }
            else if (GetFork(pindexPrev->nTime) < BRK_FORK006)
            {
                nSubsidy += nSubsidy;
                // block times will be sped 5x, i.e.
                //   (5 * (supply / 100)) / (5 * 52596) = supply / 5259600
                minReward = supply / 5259600;
            }
            else
            {
                // adjust nSubsidy for block time reduction
                nSubsidy = (2 * nSubsidy) / 5;
                //   (5 * (supply / 100)) / (5 * 52596) = supply / 5259600
                minReward = supply / 5259600;
            }
            if (nSubsidy < minReward)
            {
                nSubsidy = minReward;
            }
        }
    }
    else
    {
        nSubsidy = 0;
    }

    if (fDebug && GetBoolArg("-printcreation"))
    {
        printf("GetProofOfStakeReward(): stake=%s; create=%s\n",
               COLOR_TICKER[nStakeColor],
               FormatMoney(nSubsidy, mintColor).c_str());
    }

    struct AMOUNT stSubsidy;
    stSubsidy.nValue = nSubsidy;
    stSubsidy.nColor = mintColor;
    return stSubsidy;
}

//
// maximum nBits value could possible be required nTime after
//
unsigned int ComputeMaxBits(unsigned int nBase,
                            unsigned int nDeltaTime,
                            bool fProofOfStake,
                            unsigned int nTime = BRK_GENESIS_TIME)
{
    CBigNum bnTargetLimit = GetTargetLimit(fProofOfStake, nTime);
    CBigNum bnResult;
    bnResult.SetCompact(nBase);
    bnResult *= 2;
    while ((nDeltaTime > 0) && (bnResult < bnTargetLimit))
    {
        // Maximum 200% adjustment per 96 min
        bnResult *= 2;
        nDeltaTime -= 96 * 60;
    }
    if (bnResult > bnTargetLimit)
    {
        bnResult = bnTargetLimit;
    }
    return bnResult.GetCompact();
}

//
// minimum amount of work that could possibly be required nTime after
// minimum proof-of-work required was nBase
//
unsigned int ComputeMinWork(unsigned int nBase,
                            int64_t nDeltaTime,
                            unsigned int nTime)
{
    return ComputeMaxBits(nBase, nDeltaTime, false, nTime);
}

//
// minimum amount of stake that could possibly be required nTime after
// minimum proof-of-stake required was nBase
//
unsigned int ComputeMinStake(unsigned int nBase,
                             int64_t nDeltaTime)
{
    return ComputeMaxBits(nBase, nDeltaTime, true);
}

// ppcoin: find last block index up to pindex
const CBlockIndex* GetLastBlockIndex(const CBlockIndex* pindex, bool fProofOfStake)
{
    while (pindex && pindex->pprev && (pindex->IsProofOfStake() != fProofOfStake))
    {
        pindex = pindex->pprev;
    }
    return pindex;
}

double GetDifficulty(unsigned int nBits)
{
    int nShift = (nBits >> 24) & 0xff;

    double dDiff =
        (double)0x0000ffff / (double)(nBits & 0x00ffffff);

    while (nShift < 29)
    {
        dDiff *= 256.0;
        nShift++;
    }
    while (nShift > 29)
    {
        dDiff /= 256.0;
        nShift--;
    }

    return dDiff;
}

double GetDifficulty(const CBlockIndex* blockindex)
{
    // Floating point number that is a multiple of the minimum difficulty,
    // minimum difficulty = 1.0.
    if (blockindex == NULL)
    {
        if (pindexBest == NULL)
            return 1.0;
        else
            blockindex = GetLastBlockIndex(pindexBest, false);
    }
    return GetDifficulty(blockindex->nBits);
}

// Adapted from DASH (C) Evan Duffield, DGW v3, evan@dashpay.io
unsigned int BreakoutGravityWave(unsigned int nTime,
                                 const CBlockIndex* pindexLast,
                                 bool fProofOfStake)
{
    // how many blocks of proof type to use for the diff calculation
    constexpr unsigned int BLOCKSPAN = 24;

    // seconds in a day (86400 = 60 * 60 * 24)
    constexpr int64_t DAY = 86400;

    CBigNum bnTargetLimit = GetTargetLimit(fProofOfStake, nTime);

    // reset target if this is the first kawpow block
    if ((!fProofOfStake) && (GetFork(pindexLast->nTime) <= BRK_FORK006) &&
        (GetFork(nTime) == BRK_FORK007))
    {
        return bnTargetLimit.GetCompact();
    }

    const CBlockIndex* pindexPrev = GetLastBlockIndex(pindexLast,
                                                      fProofOfStake);

    if (pindexPrev == NULL)
    {
        return bnTargetLimit.GetCompact();  // genesis block
    }

    // post-scrypt: decay diff if long time since last PoW
    const int64_t nLastTime = pindexLast->nTime;
    const int64_t nPrevTime = pindexPrev->nTime;

    const bool fKawpow = (!fProofOfStake) && KawpowIsActive(nLastTime);

    const CBlockIndex *BlockReading = pindexPrev;
    int64_t nActualTimespan = 0;
    int64_t LastBlockTime = 0;
    int64_t PastBlocksMin = BLOCKSPAN;
    int64_t CountBlocks = 0;
    CBigNum PastDifficultyAverage;
    CBigNum PastDifficultyAveragePrev;

    // Grab the last BLOCKSPAN blocks of this type.
    // vpIndex will be in reverse order by height.
    unsigned int count;
    vector<const CBlockIndex *>vpIndex(BLOCKSPAN);
    const CBlockIndex *pindex = pindexPrev;
    for (count = 0; count < BLOCKSPAN; ++count)
    {
        vpIndex[count] = pindex;
        if (pindex->pprev == NULL)
        {
            break;
        }
        else
        {
            pindex = GetLastBlockIndex(pindex->pprev, fProofOfStake);
        }
        if (pindex == NULL)
        {
           break;
        }
    }
    if (count < BLOCKSPAN)
    {
        return bnTargetLimit.GetCompact();
    }

    for (unsigned int i = 0; i < BLOCKSPAN; i++)
    {
        BlockReading = vpIndex[i];

        CountBlocks = i + 1;

        if(CountBlocks <= PastBlocksMin)
        {
            if (CountBlocks == 1)
            {
                PastDifficultyAverage.SetCompact(BlockReading->nBits);
            }
            else
            {
                PastDifficultyAverage =
                       ((PastDifficultyAveragePrev * CountBlocks) +
                        (CBigNum().SetCompact(BlockReading->nBits))) /
                       (CountBlocks + 1);
            }
            PastDifficultyAveragePrev = PastDifficultyAverage;
        }

        if (LastBlockTime > 0)
        {
            int64_t delta = (LastBlockTime - BlockReading->GetBlockTime());
            nActualTimespan += delta;
        }
        LastBlockTime = BlockReading->GetBlockTime();

    }

    CBigNum bnNew(PastDifficultyAverage);

    const int64_t nTargetSpacing = GetTargetSpacing(fProofOfStake,
                                                    pindexLast->nTime);
    int64_t _nTargetTimespan = CountBlocks * nTargetSpacing;

    if (nActualTimespan < _nTargetTimespan/3)
    {
        nActualTimespan = _nTargetTimespan/3;
    }
    if (nActualTimespan > _nTargetTimespan*3)
    {
        nActualTimespan = _nTargetTimespan*3;
    }

    // A long span between the pindexLast and the most recent PoW block
    // suggests miners have shut off their rigs.
    // This diff adjustment recalculates as if the hashes from the last
    // blockspan were distributed to a set of virtual blocks mined
    // between the most recent PoW block and pindexLast, where these
    // virtual blocks increase with the square of the number of days.
    if (fKawpow)
    {
        int64_t delta = max(INT64_C(0), nLastTime - nPrevTime);
        if (delta > 0)
        {
            int64_t interval = fTestNet ? (nTargetSpacing * BLOCKSPAN) : DAY;
            int64_t nVirtualDays = delta / interval;
            int64_t k = nVirtualDays * nVirtualDays;
            nActualTimespan += (k * delta);
        }
    }

    // Retarget
    bnNew *= nActualTimespan;
    bnNew /= _nTargetTimespan;

    if (bnNew > bnTargetLimit)
    {
        bnNew = bnTargetLimit;
    }

    return bnNew.GetCompact();
}


// killed the legacy retarget
unsigned int GetNextTargetRequired(unsigned int nTime,
                                   const CBlockIndex* pindexLast,
                                   bool fProofOfStake)
{
    return BreakoutGravityWave(nTime, pindexLast, fProofOfStake);
}


bool CheckSHA256ProofOfWork(uint256 hash, unsigned int nBits)
{
    CBigNum bnTarget;
    bnTarget.SetCompact(nBits);

    // ADVISORY: static is an optimization, may not be desired for forks
    static CBigNum bnTargetLimit = GetTargetLimit(false, BRK_GENESIS_TIME);

    // Check range
    if (bnTarget <= 0 || bnTarget > bnTargetLimit)
        return error("ChecSHA256kProofOfWork() : nBits below minimum work");

    // Check proof of work matches claimed amount
    if (hash > bnTarget.getuint256())
        return error("CheckSHA256ProofOfWork() : hash doesn't match nBits");

    return true;
}


bool CheckKawpowProofOfWork(const CBlock* pblock)
{
    if (!pblock)
    {
        return false;
    }

    CBigNum bnTarget;
    bnTarget.SetCompact(pblock->nBits);

    // Check range
    if ((bnTarget <= 0) ||
        (bnTarget > GetTargetLimit(false, nKAWPOWActivationTime)))
    {
        return error("CheckKawpowProofOfWork() : nBits below minimum work");
    }

    if (pblock->mix_hash == 0)
    {
        // this should never happen:
        //    unverified kawpow with null mix_hash
        throw std::runtime_error(
                     "CBlock::GetHash(): TSNH mix_hash is null");
    }

    // mix_hash is set by miner or deserialized from network.
    // KAWPOWHash_OnlyMix() incorporates mix_hash into the final hash,
    // so we only need to verify it meets the target.
    uint256 calculatedHash = KAWPOWHash_OnlyMix(*pblock);
    if (calculatedHash > bnTarget.getuint256())
    {
        return error("CheckKawpowProofOfWork() : hash doesn't meet target");
    }

    return true;
}

bool CheckProofOfWork(uint256 hash, unsigned int nBits, const CBlock* pblock)
{
    if (pblock && pblock->IsKawpowBlock())
    {
        // Validate KAWPoW
        return CheckKawpowProofOfWork(pblock);
    }
    else
    {
        // Use existing SHA256 validation
        return CheckSHA256ProofOfWork(hash, nBits);
    }
}


// Return maximum amount of blocks that other nodes claim to have
int GetNumBlocksOfPeers()
{
    return max(cPeerBlockCounts.median(), Checkpoints::GetTotalBlocksEstimate());
}

bool IsInitialBlockDownload()
{
    LOCK(cs_main);
    if (pindexBest == NULL || nBestHeight < Checkpoints::GetTotalBlocksEstimate())
    {
        return true;
    }
    static int64_t nLastUpdate;
    static CBlockIndex* pindexLastBest;
    if (pindexBest != pindexLastBest)
    {
        pindexLastBest = pindexBest;
        nLastUpdate = GetTime();
    }
    return (GetTime() - nLastUpdate < 15 &&
        // 15 min
        pindexBest->GetBlockTime() < GetTime() - 15 * 60);
}

void static InvalidChainFound(CBlockIndex* pindexNew)
{
    if (pindexNew->nChainTrust > nBestInvalidTrust)
    {
        nBestInvalidTrust = pindexNew->nChainTrust;
        CTxDB().WriteBestInvalidTrust(CBigNum(nBestInvalidTrust));
        uiInterface.NotifyBlocksChanged();
    }

    uint256 nBestInvalidBlockTrust = pindexNew->nChainTrust - pindexNew->pprev->nChainTrust;
    uint256 nBestBlockTrust = pindexBest->nHeight != 0 ? (pindexBest->nChainTrust - pindexBest->pprev->nChainTrust) : pindexBest->nChainTrust;

    printf("InvalidChainFound: invalid block=%s  height=%d  trust=%s  blocktrust=%" PRId64 "  date=%s\n",
      pindexNew->GetBlockHash().ToString().substr(0,20).c_str(), pindexNew->nHeight,
      CBigNum(pindexNew->nChainTrust).ToString().c_str(), nBestInvalidBlockTrust.Get64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexNew->GetBlockTime()).c_str());
    printf("InvalidChainFound:  current best=%s  height=%d  trust=%s  blocktrust=%" PRId64 "  date=%s\n",
      hashBestChain.ToString().substr(0,20).c_str(), nBestHeight,
      CBigNum(pindexBest->nChainTrust).ToString().c_str(),
      nBestBlockTrust.Get64(),
      DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()).c_str());
}

/**
 * @brief This takes a block, removes the nNonce64 and the mixHash.
 #    Then performs a serialized hash of it SHA256D.
 *    This will be used as the input to the KAAAWWWPOW hashing function
 * @note Only to be called and used on KAAAWWWPOW block headers
 */
uint256 CBlock::GetKAWPOWHeaderHash() const
{
    CKAWPOWInput input{*this};

    return SerializeHash(input);
}

uint256 KAWPOWHash(const CBlock& block, uint256& mix_hash)
{
    // context is used to create the mix_hash, and is created
    //    with the epoch_number from the block height:
    //               nHeight -> context -> mix_hash
    // if the mix_hash is known, then context is not necessary,
    //    using hash_no_verify() in KAWPOWHash_OnlyMix(), however
    //    nHeight is still used in the hash for the mix_hash
    static ethash::epoch_context_ptr pcontext{nullptr, nullptr};

    int nHeight = block.nHeight;

    const int nEpoch = ethash::get_epoch_number(nHeight);

    if (!pcontext || (pcontext->epoch_number != nEpoch))
    {
        pcontext = ethash::create_epoch_context(nEpoch);
    }

    uint256 nHeaderHash = block.GetKAWPOWHeaderHash();
    const ethash::hash256 hashHeader = to_hash256(nHeaderHash);

    const ethash::result resultProgPoW = progpow::hash(*pcontext,
                                                       nHeight,
                                                       hashHeader,
                                                       block.nNonce64);

    mix_hash = to_uint256(resultProgPoW.mix_hash);
    return to_uint256(resultProgPoW.final_hash);
}


uint256 KAWPOWHash_OnlyMix(const CBlock& block)
{
    uint256 nHeaderHash = block.GetKAWPOWHeaderHash();
    const ethash::hash256 hashHeader = to_hash256(nHeaderHash);
    const ethash::hash256 hashMix = to_hash256(block.mix_hash);

    const ethash::hash256 hashProgPoW = progpow::hash_no_verify(
                                                       block.nHeight,
                                                       hashHeader,
                                                       hashMix,
                                                       block.nNonce64);

    return to_uint256(hashProgPoW);
}

bool KawpowIsActive(const int64_t nTime)
{
    return (GetFork(nTime) >= BRK_FORK007);
}

bool KawpowIsActive()
{
    return KawpowIsActive(GetAdjustedTime());
}


void CBlock::UpdateTime(const CBlockIndex* pindexPrev)
{
    nTime = max(GetBlockTime(), GetAdjustedTime());
}

bool IsConfirmedInNPrevBlocks(const CTxIndex& txindex, const CBlockIndex* pindexFrom, int nMaxDepth, int& nActualDepth)
{
    for (const CBlockIndex* pindex = pindexFrom; pindex && pindexFrom->nHeight - pindex->nHeight < nMaxDepth; pindex = pindex->pprev)
    {
        if (pindex->nBlockPos == txindex.pos.nBlockPos && pindex->nFile == txindex.pos.nFile)
        {
            nActualDepth = pindexFrom->nHeight - pindex->nHeight;
            return true;
        }
    }

    return false;
}


bool CTransaction::DisconnectInputs(CTxDB& txdb)
{
    // Relinquish previous transactions' spent pointers
    if (!IsCoinBase())
    {
        BOOST_FOREACH(const CTxIn& txin, vin)
        {
            COutPoint prevout = txin.prevout;

            // Get prev txindex from disk
            CTxIndex txindex;
            if (!txdb.ReadTxIndex(prevout.hash, txindex))
                return error("DisconnectInputs() : ReadTxIndex failed");

            if (prevout.n >= txindex.vSpent.size())
                return error("DisconnectInputs() : prevout.n out of range");

            // Mark outpoint as not spent
            txindex.vSpent[prevout.n].SetNull();

            // Write back
            if (!txdb.UpdateTxIndex(prevout.hash, txindex))
                return error("DisconnectInputs() : UpdateTxIndex failed");
        }
    }

    // Remove transaction from index
    // This can fail if a duplicate of this transaction was in a chain that got
    // reorganized away. This is only possible if this transaction was completely
    // spent, so erasing it would be a no-op anyway.
    txdb.EraseTxIndex(*this);

    return true;
}


bool CTransaction::FetchInputs(CTxDB& txdb, const map<uint256, CTxIndex>& mapTestPool,
                               bool fBlock, bool fMiner, MapPrevTx& inputsRet, bool& fInvalid)
{
    // FetchInputs can return false either because we just haven't seen some inputs
    // (in which case the transaction should be stored as an orphan)
    // or because the transaction is malformed (in which case the transaction should
    // be dropped).  If tx is definitely invalid, fInvalid will be set to true.
    fInvalid = false;

    if (IsCoinBase())
        return true; // Coinbase transactions have no inputs to fetch.

    for (unsigned int i = 0; i < vin.size(); i++)
    {
        COutPoint prevout = vin[i].prevout;
        if (inputsRet.count(prevout.hash))
            continue; // Got it already

        // Read txindex
        CTxIndex& txindex = inputsRet[prevout.hash].first;
        bool fFound = true;
        if ((fBlock || fMiner) && mapTestPool.count(prevout.hash))
        {
            // Get txindex from current proposed changes
            txindex = mapTestPool.find(prevout.hash)->second;
        }
        else
        {
            // Read txindex from txdb
            fFound = txdb.ReadTxIndex(prevout.hash, txindex);
        }
        if (!fFound && (fBlock || fMiner))
            return fMiner ? false : error("FetchInputs() : %s prev tx %s index entry not found", GetHash().ToString().substr(0,10).c_str(),  prevout.hash.ToString().substr(0,10).c_str());

        // Read txPrev
        CTransaction& txPrev = inputsRet[prevout.hash].second;
        if (!fFound || txindex.pos == CDiskTxPos(1,1,1))
        {
            // Get prev tx from single transactions in memory
            {
                LOCK(mempool.cs);

                if (!mempool.lookup(prevout.hash, txPrev))
                    return error("FetchInputs() : %s mempool Tx prev not found %s",
                                 GetHash().ToString().substr(0,10).c_str(),
                                 prevout.hash.ToString().substr(0,10).c_str());

            }
            if (!fFound)
                txindex.vSpent.resize(txPrev.vout.size());
        }
        else
        {
            // Get prev tx from disk
            if (!txPrev.ReadFromDisk(txindex.pos))
                return error("FetchInputs() : %s ReadFromDisk prev tx %s failed", GetHash().ToString().substr(0,10).c_str(),  prevout.hash.ToString().substr(0,10).c_str());
        }
    }

    // Make sure all prevout.n indexes are valid:
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const COutPoint prevout = vin[i].prevout;
        assert(inputsRet.count(prevout.hash) != 0);
        const CTxIndex& txindex = inputsRet[prevout.hash].first;
        const CTransaction& txPrev = inputsRet[prevout.hash].second;
        if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
        {
            // Revisit this if/when transaction replacement is implemented and allows
            // adding inputs:
            fInvalid = true;
            return DoS(100, error("FetchInputs() : %s prevout.n out of range %d %" PRIszu " %" PRIszu " prev tx %s\n%s", GetHash().ToString().substr(0,10).c_str(), prevout.n, txPrev.vout.size(), txindex.vSpent.size(), prevout.hash.ToString().substr(0,10).c_str(), txPrev.ToString().c_str()));
        }
    }

    return true;
}

const CTxOut& CTransaction::GetOutputFor(const CTxIn& input, const MapPrevTx& inputs) const
{
    MapPrevTx::const_iterator mi = inputs.find(input.prevout.hash);
    if (mi == inputs.end())
        throw runtime_error("CTransaction::GetOutputFor() : prevout.hash not found");

    const CTransaction& txPrev = (mi->second).second;
    if (input.prevout.n >= txPrev.vout.size())
        throw runtime_error("CTransaction::GetOutputFor() : prevout.n out of range");

    return txPrev.vout[input.prevout.n];
}

int64_t CTransaction::GetValueIn(const MapPrevTx& inputs, int nColorIn) const
{
    if (IsCoinBase())
        return 0;

    int64_t nResult = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        CTxOut txOut = GetOutputFor(vin[i], inputs);
        if (txOut.nColor == nColorIn)
        {
          nResult += GetOutputFor(vin[i], inputs).nValue;
        }
    }
    return nResult;

}

void CTransaction::FillValuesIn(const MapPrevTx& inputs, ColorsMap& mapValuesIn) const
{
    mapValuesIn.Clear();
    if (IsCoinBase())
    {
        return;
    }
    for (unsigned int i = 0; i < vin.size(); ++i)
    {
        CTxOut txOut = GetOutputFor(vin[i], inputs);
        mapValuesIn.Add(txOut.nColor, txOut.nValue);
    }
}


unsigned int CTransaction::GetP2SHSigOpCount(const MapPrevTx& inputs) const
{
    if (IsCoinBase())
        return 0;

    unsigned int nSigOps = 0;
    for (unsigned int i = 0; i < vin.size(); i++)
    {
        const CTxOut& prevout = GetOutputFor(vin[i], inputs);
        if (prevout.scriptPubKey.IsPayToScriptHash())
            nSigOps += prevout.scriptPubKey.GetSigOpCount(vin[i].scriptSig);
    }
    return nSigOps;
}

bool CTransaction::ConnectInputs(CTxDB& txdb, MapPrevTx inputs, map<uint256,
                                 CTxIndex>& mapTestPool, const CDiskTxPos& posThisTx,
                                 const CBlockIndex* pindexBlock, bool fBlock, bool fMiner, unsigned int flags)
{
    // Take over previous transactions' spent pointers
    // fBlock is true when this is called from AcceptBlock when a new best-block is added to the blockchain
    // fMiner is true when called from the internal bitcoin miner
    // ... both are false when called from CTransaction::AcceptToMemoryPool

    // ADVISORY: static is an optimization, may not be suitable for forks
    static int nCoinbaseMaturity = GetCoinbaseMaturity();

    if (!IsCoinBase())
    {
        vector<int64_t> vValueIn(N_COLORS, 0);
        int64_t nFees = 0;
        int64_t nChangeFees = 0;
        for (unsigned int i = 0; i < vin.size(); i++)
        {
            COutPoint prevout = vin[i].prevout;
            assert(inputs.count(prevout.hash) > 0);
            CTxIndex& txindex = inputs[prevout.hash].first;
            CTransaction& txPrev = inputs[prevout.hash].second;

            if (prevout.n >= txPrev.vout.size() || prevout.n >= txindex.vSpent.size())
                return DoS(100, error("ConnectInputs() : %s prevout.n out of range %d %" PRIszu " %" PRIszu " prev tx %s\n%s", GetHash().ToString().substr(0,10).c_str(), prevout.n, txPrev.vout.size(), txindex.vSpent.size(), prevout.hash.ToString().substr(0,10).c_str(), txPrev.ToString().c_str()));

            // If prev is coinbase or coinstake, check that it's matured
            if (txPrev.DoesMature())
            {
                for (const CBlockIndex* pindex = pindexBlock;
                     pindex && pindexBlock->nHeight - pindex->nHeight <
                                   nCoinbaseMaturity;
                     pindex = pindex->pprev)
                {
                    if (pindex->nBlockPos == txindex.pos.nBlockPos &&
                        pindex->nFile == txindex.pos.nFile)
                    {
                        return error(
                            "ConnectInputs() : tried to spend %s at depth %d",
                            txPrev.IsCoinBase() ? "coinbase" : "coinstake",
                            pindexBlock->nHeight - pindex->nHeight);
                    }
                }
            }

            // ppcoin: check transaction timestamp
            if (txPrev.nTime > nTime)
            {
                return DoS(100,
                           error("ConnectInputs() : transaction timestamp "
                                 "earlier than input transaction"));
            }

            if (txPrev.vout[prevout.n].IsEmpty())
            {
                return DoS(
                    1,
                    error(
                        "ConnectInputs() : special marker is not spendable"));
            }

            // Check for negative or overflow input values
            int nInputColor = txPrev.vout[prevout.n].nColor;
            vValueIn[nInputColor] += txPrev.vout[prevout.n].nValue;
            if (!MoneyRange(txPrev.vout[prevout.n].nValue, nInputColor) ||
                !MoneyRange(vValueIn[nInputColor], nInputColor))
                return DoS(100, error("ConnectInputs() : txin values out of range"));

        }

        int nColor = this->GetColor();
        int nFeeColor = FEE_COLOR[nColor];

        // The first loop above does all the inexpensive checks.
        // Only if ALL inputs pass do we perform expensive ECDSA signature checks.
        // Helps prevent CPU exhaustion attacks.
        for (unsigned int i = 0; i < vin.size(); i++)
        {
            COutPoint prevout = vin[i].prevout;
            assert(inputs.count(prevout.hash) > 0);
            CTxIndex& txindex = inputs[prevout.hash].first;
            CTransaction& txPrev = inputs[prevout.hash].second;

            // Check for conflicts (double-spend)
            // This doesn't trigger the DoS code on purpose; if it did, it would make it easier
            // for an attacker to attempt to split the network.
            if (!txindex.vSpent[prevout.n].IsNull())
            {
                return fMiner ? false : error("ConnectInputs() : %s prev tx already used at %s",
                                              GetHash().ToString().substr(0,10).c_str(),
                                              txindex.vSpent[prevout.n].ToString().c_str());
            }

            // Skip ECDSA signature verification when connecting blocks (fBlock=true)
            // before the last blockchain checkpoint. This is safe because block merkle hashes are
            // still computed and checked, and any change will be caught at the next checkpoint.
            if (!(fBlock && (nBestHeight < Checkpoints::GetTotalBlocksEstimate())))
            {
                // Verify signature
                if (!VerifySignature(txPrev, *this, i, flags, 0))
                {
                    return DoS(100,error("ConnectInputs() : %s VerifySignature failed",
                                         GetHash().ToString().substr(0,10).c_str()));
                }
            }

            // Mark outpoints as spent
            txindex.vSpent[prevout.n] = posThisTx;

            // Write back
            if (fBlock || fMiner)
            {
                mapTestPool[prevout.hash] = txindex;
            }
        }

        // multicurrency: coinstake is zero sum except for lost maturity
        //                new mint goes in coinbase tx, even when PoS
        // if (!IsCoinStake())
        {
            // txs are fully multicurrency, check all colors
            for (int i = 1; i < N_COLORS; ++i)
            {
                if (vValueIn[i] < GetValueOut(i))
                {
                    return DoS(
                        100,
                        error("ConnectInputs() : for currency %d: %s value in "
                              "(%" PRId64 ") < value out (%" PRId64 ")",
                              i,
                              GetHash().ToString().substr(0, 10).c_str(),
                              vValueIn[i],
                              GetValueOut(i)));
                }
            }

            // Tally transaction fees -- coinstake has no tx fees
            if (!IsCoinStake())
            {
                int64_t nTxFee;
                int64_t nTxChangeFee; // why? delegate fees: rounded change --> fee
                if (nColor == nFeeColor)
                {
                    nTxFee = vValueIn[nColor] - GetValueOut(nColor);
                    nTxChangeFee = 0;
                }
                else
                {
                    ColorsMap mapValuesOut;
                    FillValuesOut(mapValuesOut);
                    // in case of no fee change, int64_t default init will be 0
                    nTxFee = vValueIn[nFeeColor] - mapValuesOut.Get(nFeeColor);
                    nTxChangeFee = vValueIn[nColor] - mapValuesOut.Get(nColor);
                }

                if (nTxFee < 0)
                {
                    return DoS(100, error("ConnectInputs() : %s nTxFee < 0",
                                               GetHash().ToString().substr(0,10).c_str()));
                }
                if (nTxChangeFee < 0)
                {
                    return DoS(100, error("ConnectInputs() : %s nTxChangeFee < 0",
                                               GetHash().ToString().substr(0,10).c_str()));
                }

                // enforce transaction fees for every block
                if (nTxFee < GetMinFee())
                {
                    return fBlock? DoS(100, error("ConnectInputs() : %s not paying required fee=%s, paid=%s",
                              GetHash().ToString().substr(0,10).c_str(),
                              FormatMoney(GetMinFee(), nFeeColor).c_str(),
                              FormatMoney(nTxFee, nFeeColor).c_str())) : false;
                }

                // reason for adding to 0 here instead of just checking nTxFee is unknown
                nFees += nTxFee;
                if (!MoneyRange(nFees, nFeeColor))
                {
                    return DoS(100, error("ConnectInputs() : nFees out of range"));
                }
                // blindly following lead of ancients
                nChangeFees += nTxChangeFee;
                if (!MoneyRange(nChangeFees, nColor))
                {
                    return DoS(100, error("ConnectInputs() : nChangeFees out of range"));
                }
            }
        }
    }

    return true;
}


bool CTransaction::ClientConnectInputs()
{
    if (IsCoinBase())
        return false;

    // Take over previous transactions' spent pointers
    {
        LOCK(mempool.cs);
        // inputs can be any color
        vector<int64_t> vValueIn(N_COLORS, 0);
        for (unsigned int i = 0; i < vin.size(); i++)
        {
            // Get prev tx from single transactions in memory
            COutPoint prevout = vin[i].prevout;
            if (!mempool.exists(prevout.hash))
                return false;
            CTransaction& txPrev = mempool.lookup(prevout.hash);

            if (prevout.n >= txPrev.vout.size())
                return false;

            // Verify signature
            if (!VerifySignature(txPrev, *this, i, STANDARD_SCRIPT_VERIFY_FLAGS, 0))
                return error("ConnectInputs() : VerifySignature failed");

            ///// this is redundant with the mempool.mapNextTx stuff,
            ///// not sure which I want to get rid of
            ///// this has to go away now that posNext is gone
            // // Check for conflicts
            // if (!txPrev.vout[prevout.n].posNext.IsNull())
            //     return error("ConnectInputs() : prev tx already used");
            //
            // // Flag outpoints as used
            // txPrev.vout[prevout.n].posNext = posThisTx;

            int nInputColor = txPrev.vout[prevout.n].nColor;
            vValueIn[nInputColor] += txPrev.vout[prevout.n].nValue;

            if (!MoneyRange(txPrev.vout[prevout.n].nValue, nInputColor) ||
                                            !MoneyRange(vValueIn[nInputColor], nInputColor))
                return error("ClientConnectInputs() : txin values out of range");
        }

        // because fee change color may not be the same as tx color
        ColorsMap mapValuesOut;
        FillValuesOut(mapValuesOut);
        ColorsMapConstIter oit;
        for (oit = mapValuesOut.Begin(); oit != mapValuesOut.End(); ++oit)
        {
              if (oit->second > vValueIn[oit->first])
              {
                  // output exceeds input -> can't make new money
                  return false;
              }
        }
    }

    return true;
}


enum ConfirmationStatus
{
    CONF_UNCHECKED           = 0,
    CONF_ALWAYS_UNCONFIRMED  = 0,
    CONF_UNCONFIRMED_BEFORE  = 0,
    CONF_CONFIRMED_BEFORE    = 1,
    CONF_BECAME_UNCONFIRMED  = 1,
    CONF_CONFIRMED_AFTER     = 2,
    CONF_BECAME_CONFIRMED    = 2,
    CONF_ALWAYS_CONFIRMED    = 3
};

// disconnecting pindex
bool CBlock::DisconnectBlock(CTxDB& txdb, CBlockIndex* pindex)
{
    // Disconnect in reverse order
    for (int i = vtx.size()-1; i >= 0; i--)
    {
        if (!vtx[i].DisconnectInputs(txdb))
        {
            return false;
        }
    }

    // Update block index on disk without changing it in memory.
    // The memory index structure will be changed after the db commits.
    if (pindex->pprev)
    {
        CDiskBlockIndex blockindexPrev(pindex->pprev);
        blockindexPrev.hashNext = 0;
        if (!txdb.WriteBlockIndex(blockindexPrev))
        {
            return error("DisconnectBlock() : WriteBlockIndex failed");
        }
    }

    // ppcoin: clean up wallet after disconnecting coinstake
    BOOST_FOREACH(CTransaction& tx, vtx)
    {
        SyncWithWallets(tx, this, false, false);
    }

    if (fWithExploreAPI)
    {
        CExploreDB exploredb;
        if (!ExploreDisconnectBlock(txdb, exploredb, this))
        {
            return error("DisconnectBlock() : ExploreDisconnectBlock failed");
        }
    }

    return true;
}

// connecting pindex
bool CBlock::ConnectBlock(CTxDB& txdb, CBlockIndex* pindex, bool fJustCheck)
{
    // Check it again in case a previous version let a bad block in, but skip
    // BlockSig checking
    if (!CheckBlock(!fJustCheck, !fJustCheck, false))
    {
        return false;
    }

    unsigned int flags = SCRIPT_VERIFY_NOCACHE | STANDARD_SCRIPT_VERIFY_FLAGS;

    //// issue here: it doesn't know the version
    unsigned int nTxPos;
    if (fJustCheck)
    {
        // FetchInputs treats CDiskTxPos(1,1,1) as a special "refer to
        // memorypool" indicator Since we're just checking the block and not
        // actually connecting it, it might not (and probably shouldn't) be on
        // the disk to get the transaction from
        nTxPos = 1;
    }
    else
    {
        nTxPos = pindex->nBlockPos +
                 ::GetSerializeSize(CBlock(pindex->nVersion), SER_DISK, CLIENT_VERSION) -
                 (2 * GetSizeOfCompactSize(0)) +
                 GetSizeOfCompactSize(vtx.size());
    }

    map<uint256, CTxIndex> mapQueuedChanges;

    // TODO: these may be ok for hundreds of currencies, but will be a waste
    // for 100k, etc. fees are in - out for non-coinbase transactions.
    // The solution is to use ColorsMap.
    vector<int64_t>vFees(N_COLORS, 0);
    vector<int64_t>vValueIn(N_COLORS, 0);
    vector<int64_t>vValueOut(N_COLORS, 0);
    vector<int64_t>vScavengedFees(N_COLORS, 0);

    int64_t nStakeReward = 0;
    int nStakeColor = (int) BREAKOUT_COLOR_NONE;

    // *claimed* subsidy from GetProofOfStakeReward()
    int64_t nMintValue = 0;
    int nMintColor = (int) BREAKOUT_COLOR_NONE;

    // initializes filled with zeros for genesis block index
    if (pindex->pprev != NULL)
    {
        pindex->vTotalMint = pindex->pprev->vTotalMint;
        // some clients had negative vTotalMint so do a "hard reset"
        bool fReset = ((GetFork(pindex->pprev->nTime) == BRK_FORK004) &&
                       (GetFork(pindex->nTime) == BRK_FORK005));
        if (fReset)
        {
            pindex->vTotalMint[BREAKOUT_COLOR_SIS] =
                        pindex->pprev->vMoneySupply[BREAKOUT_COLOR_SIS];
        }
    }

    unsigned int nSigOps = 0;
    BOOST_FOREACH(CTransaction& tx, vtx)
    {
        uint256 hashTx = tx.GetHash();

        int nColor = tx.GetColor();

        // Do not allow blocks that contain transactions that 'overwrite' older transactions,
        // unless those are already completely spent.
        // If such overwrites are allowed, coinbases and transactions depending upon those
        // can be duplicated to remove the ability to spend the first instance -- even after
        // being sent to another address.
        // See BIP30 and http://r6.ca/blog/20120206T005236Z.html for more information.
        // This logic is not necessary for memory pool transactions, as AcceptToMemoryPool
        // already refuses previously-known transaction ids entirely.
        // This rule was originally applied all blocks whose timestamp was after March 15, 2012, 0:00 UTC.
        // Now that the whole chain is irreversibly beyond that time it is applied to all blocks except the
        // two in the chain that violate it. This prevents exploiting the issue against nodes in their
        // initial block download.
        CTxIndex txindexOld;
        if (txdb.ReadTxIndex(hashTx, txindexOld)) {
            BOOST_FOREACH(CDiskTxPos &pos, txindexOld.vSpent)
                if (pos.IsNull())
                    return DoS(100, error("ConnectBlock() : tried to overwrite transaction"));
        }

        nSigOps += tx.GetLegacySigOpCount();
        if (nSigOps > MAX_BLOCK_SIGOPS)
        {
            return DoS(100, error("ConnectBlock() : too many sigops"));
        }

        CDiskTxPos posThisTx(pindex->nFile, pindex->nBlockPos, nTxPos);
        if (!fJustCheck)
        {
            nTxPos += ::GetSerializeSize(tx, SER_DISK, CLIENT_VERSION);
        }

        // make sure all outputs have valid colors
        for (int idx = 0; idx < (int) tx.vout.size(); ++idx)
        {
            if (!CheckColor(tx.vout[idx].nColor))
            {
                return DoS(100, error(
                   "ConnectBlock() : invalid color for output %d: %d",
                                                idx, tx.vout[idx].nColor));
            }
        }

        // tally vValueOut
        // all tx can have multiple output colors
        //   coinbase: because of different colored fees
        //   others: because of fee delegate change
        //      color  amount
        ColorsMap mapValuesOut;
        tx.FillValuesOut(mapValuesOut);
        ColorsMapConstIter oit;
        for (oit = mapValuesOut.Begin(); oit != mapValuesOut.End(); ++oit)
        {
            vValueOut[oit->first] += oit->second;
        }

        MapPrevTx mapInputs;
        if (tx.IsCoinBase())
        {
            nMintValue = tx.vout[0].nValue;
            nMintColor = tx.vout[0].nColor;
            // check that the pos coinbase tx has no non-mint fees
            // TODO: generalize for systems where pos can collect fees?
            // TODO: this forces all of mint to be in vout[0]
            if (IsProofOfStake())
            {
                if (tx.vout.size() > 1)
                {
                    return DoS(100,
                       error("ConnectBlock() : PoS tx %s has >1 outputs (%d)",
                                   tx.GetHash().ToString().c_str(), (int) tx.vout.size()));
                }
            }
        }
        else
        {
            bool fInvalid;
            if (!tx.FetchInputs(txdb, mapQueuedChanges, true, false, mapInputs, fInvalid))
            {
                return false;
            }

            // Add in sigops done by pay-to-script-hash inputs;
            // this is to prevent a "rogue miner" from creating
            // an incredibly-expensive-to-validate block.
            nSigOps += tx.GetP2SHSigOpCount(mapInputs);
            if (nSigOps > MAX_BLOCK_SIGOPS)
            {
                return DoS(100, error("ConnectBlock() : too many sigops"));
            }

            // color:value pairs for the tx inputs
            ColorsMap mapValuesIn;
            tx.FillValuesIn(mapInputs, mapValuesIn);
            ColorsMapConstIter iit;
            // tally vValueIn[], which are the values in for the whole block
            for (iit = mapValuesIn.Begin(); iit != mapValuesIn.End(); ++iit)
            {
                  vValueIn[iit->first] += iit->second;
            }

            // multicurrencies: coinstake tx is different from the coinmint tx
            if (tx.IsCoinStake())
            {
                // staking burns coin age, so in should equal out
                // pos rewards are in the coinbase tx not coinstake
                nStakeReward = mapValuesOut.Get(nColor) - mapValuesIn.Get(nColor);
                if (nStakeReward > 0)
                {
                    return DoS(100, error("ConnectBlock() : CoinStake reward is greater than 0\n"));
                }
                // limit size of stake outputs to 100 (vout[0] doesn't count)
                if (tx.vout.size() > 101)
                {
                    return DoS(100, error("ConnectBlock() : Too many CoinStake outputs\n"));
                }
                // prevent stake zero outputs past vout[0]
                for (int i = 1; i < (int) tx.vout.size(); ++i)
                {
                    if (tx.vout[i].nValue == 0)
                    {
                        return DoS(100, error("ConnectBlock() : CoinStake output is 0\n"));
                    }
                }

                // This is only relevant to PoS systems where minters can collect fees
                // // Scavenging calculation will not catch movement of stake to fees
                // if ((nStakeReward < 0) && (!SCAVENGABLE[nColor]))
                // {
                //     // subtracting a negative
                //     vFees[nColor] -= nStakeReward;
                // }
            }

            // It is possible to move coinstake to fees.
            // Movement of stake to fees is caught above for non-scavengable stake.
            for (int nIOColor = 1; nIOColor < N_COLORS; ++nIOColor)
            {
                // If fees are not scavengable, it makes no sense to collect
                // them even
                //      when not being scavenged.
                if (!SCAVENGABLE[nIOColor])
                {
                    continue;
                }
                int64_t nFeeIO = mapValuesIn.Get(nIOColor) -
                                 mapValuesOut.Get(nIOColor);
                if (nFeeIO < 0)
                {
                    return DoS(100,
                               error("ConnectBlock() : outputs exceeds inputs "
                                     "(currency %s, tx: %s\n",
                                     COLOR_TICKER[nIOColor],
                                     tx.GetHash().ToString().c_str()));
                }
                vFees[nIOColor] += nFeeIO;
            }

            if (!tx.ConnectInputs(txdb,
                                  mapInputs,
                                  mapQueuedChanges,
                                  posThisTx,
                                  pindex,
                                  true,
                                  false,
                                  flags))
            {
                return false;
            }
        }

        mapQueuedChanges[hashTx] = CTxIndex(posThisTx, tx.vout.size());
    }

    if (IsProofOfWork())
    {
        // PoW blocks can scavenge money supply losses of select currencies
        for (int nColor = 1; nColor < N_COLORS; ++nColor)
        {
            if (SCAVENGABLE[nColor])
            {
                int64_t nTotalMintPrev = 0;
                if (pindex->pprev)
                {
                    bool fReset = ((nColor == BREAKOUT_COLOR_SIS) &&
                                   (GetFork(pindex->pprev->nTime) == BRK_FORK004) &&
                                   (GetFork(pindex->nTime) == BRK_FORK005));
                    if (fReset)
                    {
                        nTotalMintPrev = pindex->pprev->vMoneySupply[nColor];
                    }
                    else
                    {
                        nTotalMintPrev = pindex->pprev->vTotalMint[nColor];
                    }
                }
                int64_t nMoneySupplyPrev = (pindex->pprev ? pindex->pprev->vMoneySupply[nColor] : 0);
                vScavengedFees[nColor] += (nTotalMintPrev - nMoneySupplyPrev);
                vFees[nColor] += (nTotalMintPrev - nMoneySupplyPrev);
            }
        }

        if (fDebug && GetBoolArg("-printcreation"))
        {
            for (int nColor = 1; nColor < N_COLORS; ++nColor)
            {
                if (vFees[nColor] != 0)
                {
                     printf("ConnectBlock(): Fee for %s is %" PRId64 "\n",
                                COLOR_TICKER[nColor], vFees[nColor]);
                }
                if (vScavengedFees[nColor] != 0)
                {
                     printf("ConnectBlock(): Scavenged fee for %s is %" PRId64 "\n",
                                COLOR_TICKER[nColor], vScavengedFees[nColor]);
                }

            }
        }

        // check the pow coinbase transaction
        CTransaction txCoinBase = vtx[0];

        // fees can be many different colors
        // don't include them in the reward calc
        struct AMOUNT reward = GetProofOfWorkReward(pindex->pprev);

        int64_t nReward = reward.nValue + vFees[reward.nColor];

        // ensure coinbase reward is correct color
        if (txCoinBase.GetColor() != reward.nColor)
        {
            return DoS(100, error("ConnectBlock() : coinbase currency is %s, should be %s\n",
                   COLOR_TICKER[txCoinBase.GetColor()], COLOR_TICKER[reward.nColor]));
        }

        // Check coinbase reward
        // fees are in - out for non-coinbase transactions
        // fees for the subsidy color should be added to vtx[0].vout[0]
        // this mirrors single currency coins (e.g. bitcoin)
        if (txCoinBase.vout[0].nValue > nReward)
        {
            return DoS(100, error("ConnectBlock() : coinbase reward exceeded "
                                     "([currency=%s] actual=%" PRId64
                                     " vs calculated=%" PRId64 ")",
                                   COLOR_TICKER[txCoinBase.vout[0].nColor],
                                   txCoinBase.vout[0].nValue, nReward));
        }

        // check rest of fees, outputs in no particular order
        //      color  amount
        ColorsMap mapCoinBaseValuesOut;
        // fFees=true looks at vout[1] and later (vout[0] already checked)
        txCoinBase.FillValuesOut(mapCoinBaseValuesOut, true);
        ColorsMapConstIter oit;
        for (oit = mapCoinBaseValuesOut.Begin();
             oit != mapCoinBaseValuesOut.End();
             ++oit)
        {
            // reject any coinbase reward currency in second and later outputs
            // this makes accounting simpler elswhere (may change
            // this) for PoS, stake is in vtx[1]
            if (oit->first == reward.nColor)
            {
                return DoS(100,
                           error("ConnectBlock() : reward currency in "
                                 "non-coinbase output\n"));
            }
            else if (oit->second > vFees[oit->first])
            {
                 // already checked coinbase currency
                 return DoS(100, error("ConnectBlock() : fees exceeded for currency %s "
                                         "(actual=%" PRId64 " vs calc=%" PRId64 ")",
                                       COLOR_TICKER[oit->first],
                                       oit->second,
                                       vFees[oit->first]));
            }
        }

        // not a PoS block, set the pindex stake color to none
        pindex->nStakeColor = (int) BREAKOUT_COLOR_NONE;
        // make unclaimed mint reward available for scavenging if allowed


        int64_t nTotalMintRewardPrev = 0;
        if (pindex->pprev)
        {
            bool fReset = ((reward.nColor == BREAKOUT_COLOR_SIS) &&
                           (GetFork(pindex->pprev->nTime) == BRK_FORK004) &&
                           (GetFork(pindex->nTime) == BRK_FORK005));
            if (fReset)
            {
                nTotalMintRewardPrev = pindex->pprev->vMoneySupply[reward.nColor];
            }
            else
            {
                nTotalMintRewardPrev = pindex->pprev->vTotalMint[reward.nColor];
            }
        }
        pindex->vTotalMint[reward.nColor] = nTotalMintRewardPrev + reward.nValue;
    }

    if (IsProofOfStake())
    {
        // TODO: put in preprocessor conditional for coin age dependent PoS
        // pos reward does not rely on coin age
        // ppcoin: coin stake tx earns reward instead of paying fee
        // uint64_t nCoinAge;
        // if (!vtx[1].GetCoinAge(txdb, nCoinAge))
        //     return error("ConnectBlock() : %s unable to get coin age for
        //     coinstake", vtx[1].GetHash().ToString().substr(0,10).c_str());

        nStakeColor = vtx[1].GetColor();
        if (!CheckColor(nStakeColor))
        {
            return DoS(100, error("ConnectBlock() : stake currency is not valid (%d)", nStakeColor));
        }
        if (nMintColor != MINT_COLOR[nStakeColor])
        {
            return DoS(100, error("ConnectBlock() : mint (%s) is wrong color for coinstake (%s)",
                              COLOR_TICKER[nMintColor], COLOR_TICKER[MINT_COLOR[nStakeColor]]));
        }

        // breakout: reward based nHeight and color
        struct AMOUNT reward = GetProofOfStakeReward(pindex->pprev, nStakeColor);

        // breakout: reward is earned in the mint transaction, not the stake transaction
        if (nMintValue > reward.nValue)
        {
            return DoS(100, error("ConnectBlock() : coinstake pays too much(actual=%" PRId64
                                          " vs calculated=%" PRId64 ")", nMintValue, reward.nValue));
        }

        pindex->nStakeColor = nStakeColor;
        // make unclaimed mint reward available for scavenging if allowed
        int64_t nTotalMintRewardPrev = (pindex->pprev ? pindex->pprev->vTotalMint[reward.nColor] : 0);
        pindex->vTotalMint[reward.nColor] = nTotalMintRewardPrev + reward.nValue;
    }

    // ppcoin: track money supply and mint amount info
    pindex->nCoinbaseColor = vtx[0].GetColor();
    for (int i = 1; i < N_COLORS; ++i)
    {
        // confusing equation, but think of it as
        //     subtracting the noncoinbase (fees) from all (noncoinbase & coinbase)
        //     yielding just coinbase
        pindex->vCoinbase[i] = vValueOut[i] - vValueIn[i] + vFees[i] - vScavengedFees[i];
        pindex->vMoneySupply[i] = (pindex->pprev? pindex->pprev->vMoneySupply[i] : 0) +
                                                                   vValueOut[i] - vValueIn[i];
    }

    // check for unspendable outputs to adjust money supply and total mint
    if (pindex->pprev->nTime >= BURN_PROTOCOL_START_TIME)
    {
        BOOST_FOREACH(CTransaction& tx, vtx)
        {
            vector<valtype> vSolutions;
            txnouttype whichType;
            BOOST_FOREACH(const CTxOut& txout, tx.vout)
            {
                 if (!vSolutions.empty())
                 {
                     vSolutions.clear();
                 }
                 if (!Solver(txout.scriptPubKey, whichType, vSolutions))
                 {
                     if (GetFork(tx.nTime) < BRK_FORK001)
                     {
                         return error("Connect() : cannot connect tx with nonstandard output");
                     }
                     else
                     {
                         continue;
                     }
                 }
                 // subtract burned coins from both money supply and total mint
                 if (whichType == TX_NULL_DATA)
                 {
                     pindex->vTotalMint[txout.nColor] -= txout.nValue;
                     pindex->vMoneySupply[txout.nColor] -= txout.nValue;
                 }
            }
        }
    }

    if (!txdb.WriteBlockIndex(CDiskBlockIndex(pindex)))
    {
        return error("Connect() : WriteBlockIndex for pindex failed");
    }

    if (fJustCheck)
    {
        return true;
    }

    // Write queued txindex changes
    for (map<uint256, CTxIndex>::iterator mi = mapQueuedChanges.begin(); mi != mapQueuedChanges.end(); ++mi)
    {
        if (!txdb.UpdateTxIndex((*mi).first, (*mi).second))
        {
            return error("ConnectBlock() : UpdateTxIndex failed");
        }
    }

    // Update block index on disk without changing it in memory.
    // The memory index structure will be changed after the db commits.
    if (pindex->pprev)
    {
        CDiskBlockIndex blockindexPrev(pindex->pprev);
        blockindexPrev.hashNext = pindex->GetBlockHash();
        if (!txdb.WriteBlockIndex(blockindexPrev))
        {
            return error("ConnectBlock() : WriteBlockIndex failed");
        }
    }

    // Watch for transactions paying to me
    BOOST_FOREACH(CTransaction& tx, vtx)
    {
        SyncWithWallets(tx, this, true);
    }

    // fJustCheck is a validation-only pass, so it must not write the index.
    if (fWithExploreAPI && !fJustCheck)
    {
        CExploreDB exploredb;
        if (!ExploreConnectBlock(txdb, exploredb, this))
        {
            return error("ConnectBlock() : ExploreConnectBlock failed");
        }
    }

    return true;
}

bool static Reorganize(CTxDB& txdb, CBlockIndex* pindexNew)
{
    printf("REORGANIZE\n");

    // Find the fork
    CBlockIndex* pfork = pindexBest;
    CBlockIndex* plonger = pindexNew;
    while (pfork != plonger)
    {
        while (plonger->nHeight > pfork->nHeight)
            if (!(plonger = plonger->pprev))
                return error("Reorganize() : plonger->pprev is null");
        if (pfork == plonger)
            break;
        if (!(pfork = pfork->pprev))
            return error("Reorganize() : pfork->pprev is null");
    }


    // List of what to disconnect
    vector<CBlockIndex*> vDisconnect;
    for (CBlockIndex* pindex = pindexBest; pindex != pfork; pindex = pindex->pprev)
    {
        vDisconnect.push_back(pindex);
    }

    // List of what to connect
    vector<CBlockIndex*> vConnect;
    for (CBlockIndex* pindex = pindexNew; pindex != pfork; pindex = pindex->pprev)
    {
        vConnect.push_back(pindex);
    }
    reverse(vConnect.begin(), vConnect.end());

    printf("REORGANIZE: Disconnect %" PRIszu " blocks:\n  %s\n   ->\n  %s\n",
           vDisconnect.size(),
           pfork->GetBlockHash().ToString().c_str(),
           pindexBest->GetBlockHash().ToString().c_str());
    printf("REORGANIZE: Connect %" PRIszu " blocks:\n  %s\n   ->\n  %s\n",
           vConnect.size(),
           pfork->GetBlockHash().ToString().c_str(),
           pindexNew->GetBlockHash().ToString().c_str());

    static const int MATURITY_DEPTH = GetCoinbaseMaturity() + 1;

    // tracks tx confirmation status before and after reorganization
    map<uint256, int> mapConfStatus;
    ColorsMap mapConfirmedChanges;
    ColorsMap mapStakeChanges;
    ColorsMap mapCoinbaseChanges;
    ColorsMap mapReceivedChanges;
    ColorsMap mapSentChanges;

    // Disconnect shorter branch
    list<CTransaction> vResurrect;
    BOOST_FOREACH(CBlockIndex* pindex, vDisconnect)
    {
        CBlock block;
        if (!block.ReadFromDisk(pindex))
        {
            return error("Reorganize() : ReadFromDisk for disconnect failed");
        }

        ///////////////////////////////////////////////////////////////////
        // Calculate balance changes for main wallet before the block is
        //    fully disconnected.
        ///////////////////////////////////////////////////////////////////
        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            // temporary wallet transaction for calculations
            CWalletTx wtx;
            {
                LOCK(pwalletMain->cs_wallet);
                wtx = CWalletTx(pwalletMain, tx);
            }
            bool fDoesMature = wtx.DoesMature();
            bool fImmature = (fDoesMature &&
                              (wtx.GetBlocksToMaturity() > 0));
            bool fUnconfirmed = ((!fDoesMature) && (wtx.GetDepthInMainChain() <= 0));
            mapConfStatus[wtx.GetHash()] = fUnconfirmed
                                               ? CONF_UNCONFIRMED_BEFORE
                                               : CONF_CONFIRMED_BEFORE;
            ColorsMap mapReceived;
            ColorsMap mapSent;
            wtx.GetAmounts(false, mapReceived, mapSent);
            // DISCONNECTION: Subtract Received, add back Sent.
            mapConfirmedChanges.Subtract(mapReceived);
            mapConfirmedChanges.Add(mapSent);
            if (fImmature)
            {
                // DISCONNECTION: Subtract Received
                if (wtx.IsCoinStake())
                {
                    // inputs = outputs, so keep track of only one side
                    mapStakeChanges.Subtract(mapReceived);
                }
                else if (wtx.IsCoinBase())
                {
                    // there are no inputs for coinbase
                    mapCoinbaseChanges.Subtract(mapReceived);
                }
                else
                {
                     throw runtime_error(
                                "Reorganize(): TSNH upon DISCONNECTION - "
                                "matures but not coinstake or coinbase");
                }
            }
        }
        // Subtract coins that would become immature upon disconnecting.
        CBlockIndex* pindexImm = pindex;
        // confs:     1        2      3      4     ...     MATURITY_DEPTH
        //        pindexImm->pprev->pprev->pprev-> ... -> [becomes immature]
        int i = 1;
        while (pindexImm->pprev)
        {
            pindexImm = pindexImm->pprev;
            i += 1;
            if (i == MATURITY_DEPTH)
            {
                break;
            }
        }
        if ((i != MATURITY_DEPTH) && (pindexImm != pindexGenesisBlock))
        {
            // This should never happen: can't step back through
            //    pprevs to get to block that would become immature
            throw std::runtime_error(strprintf("Reorganize(): i=%d "
                         "TSNH can't step back from disconnect\n  %s",
                         i,
                         pindex->GetBlockHash().ToString().c_str()));
        }
        // ensure it stepped through enough blocks
        if (pindexImm && (i == MATURITY_DEPTH))
        {
            CBlock blockImm;
            if (!blockImm.ReadFromDisk(pindexImm))
            {
                return error("Reorganize(): Read from disk failed \n  %s",
                             pindexImm->GetBlockHash().ToString().c_str());
            }
            BOOST_FOREACH(CTransaction& tx, blockImm.vtx)
            {
                if (tx.DoesMature())
                {
                    // temporary wallet transaction for calculations
                    CWalletTx wtx;
                    {
                        LOCK(pwalletMain->cs_wallet);
                        wtx = CWalletTx(pwalletMain, tx);
                    }
                    ColorsMap mapReceived;
                    ColorsMap mapSentUnused;
                    wtx.GetAmounts(false, mapReceived, mapSentUnused);

                    // BECAME-IMMATURE (Became stake/coinbase) ==> Add
                    if (wtx.IsCoinStake())
                    {
                        // inputs = outputs, so keep track of only one side
                        mapStakeChanges.Add(mapReceived);
                    }
                    else if (wtx.IsCoinBase())
                    {
                        // there are no inputs for coinbase
                        mapCoinbaseChanges.Add(mapReceived);
                    }
                    else
                    {
                         throw runtime_error(
                                 "Reorganize(): TSNH upon BECAME-IMMATURE - "
                                 "matures but not coinstake or coinbase");
                    }
                }
            }
        }
        ///////////////////////////////////////////////////////////////////
        ///////////////////////////////////////////////////////////////////

        if (!block.DisconnectBlock(txdb, pindex))
        {
            return error("Reorganize() : DisconnectBlock failed\n  %s",
                         pindex->GetBlockHash().ToString().c_str());
        }

        // Queue memory transactions to resurrect
        BOOST_REVERSE_FOREACH(const CTransaction& tx, block.vtx)
        {
             if (!tx.DoesMature() &&
                 (pindex->nHeight > Checkpoints::GetTotalBlocksEstimate()))
             {
                 vResurrect.push_front(tx);
             }
        }
    }

    // Connect longer branch
    vector<CTransaction> vDelete;
    for (unsigned int i = 0; i < vConnect.size(); i++)
    {
        CBlockIndex* pindex = vConnect[i];
        CBlock block;
        if (!block.ReadFromDisk(pindex))
        {
            return error("Reorganize() : ReadFromDisk for connect failed");
        }

        ///////////////////////////////////////////////////////////////////
        // Calculate balance changes for main wallet before the block is
        //    fully connected.
        ///////////////////////////////////////////////////////////////////
        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            // temporary wallet transaction for calculations
            CWalletTx wtx;
            {
                LOCK(pwalletMain->cs_wallet);
                wtx = CWalletTx(pwalletMain, tx);
            }
            bool fDoesMature = wtx.DoesMature();
            bool fImmature = (fDoesMature &&
                              (wtx.GetBlocksToMaturity() > 0));
            bool fUnconfirmed = ((!fDoesMature) && (wtx.GetDepthInMainChain() <= 0));
            mapConfStatus[wtx.GetHash()] = fUnconfirmed ? CONF_UNCONFIRMED_BEFORE
                                                        : CONF_CONFIRMED_BEFORE;
            ColorsMap mapReceived;
            ColorsMap mapSent;
            wtx.GetAmounts(false, mapReceived, mapSent);
            // CONNECTION: Add Received, subtract Sent.
            mapConfirmedChanges.Add(mapReceived);
            mapConfirmedChanges.Subtract(mapSent);
            if (fImmature)
            {
                // CONNECTION: Add Received
                if (wtx.IsCoinStake())
                {
                    // inputs = outputs, so keep track of only one side
                    mapStakeChanges.Add(mapReceived);
                }
                else if (wtx.IsCoinBase())
                {
                    // there are no inputs for coinbase
                    mapCoinbaseChanges.Add(mapReceived);
                }
                else
                {
                     throw runtime_error(
                              "Reorganize(): TSNH upon CONNECTION - "
                              "matures but not coinstake or coinbase");
                }
            }
        }
        // Add coins that would become mature upon connecting.
        CBlockIndex* pindexMat = pindex;
        // new confs:     1        2      3      4     ...     MATURITY_DEPTH
        //            pindexMat->pprev->pprev->pprev-> ... -> [becomes mature]
        int j = 1;
        while (pindexMat->pprev)
        {
            pindexMat = pindexMat->pprev;
            j += 1;
            if (j == MATURITY_DEPTH)
            {
                break;
            }
        }
        if ((j != MATURITY_DEPTH) && (pindexMat != pindexGenesisBlock))
        {
            // This should never happen: can't step back through
            //    pprevs to get to block that would become mature
            throw std::runtime_error(strprintf("Reorganize(): i=%d "
                         "TSNH can't step back from connect\n  %s",
                         j,
                         pindex->GetBlockHash().ToString().c_str()));
        }
        // ensure it stepped through enough blocks
        if (pindexMat && (j == MATURITY_DEPTH))
        {
            CBlock blockMat;
            blockMat.ReadFromDisk(pindexMat);
            BOOST_FOREACH(CTransaction& tx, blockMat.vtx)
            {
                if (tx.DoesMature())
                {
                    // temporary wallet transaction for calculations
                    CWalletTx wtx;
                    {
                        LOCK(pwalletMain->cs_wallet);
                        wtx = CWalletTx(pwalletMain, tx);
                    }
                    ColorsMap mapReceived;
                    ColorsMap mapSentUnused;
                    wtx.GetAmounts(false, mapReceived, mapSentUnused);

                    // BECAME-MATURE (stake/coinbase matured) ==> Subtract
                    if (wtx.IsCoinStake())
                    {
                        // inputs = outputs, so keep track of only one side
                        mapStakeChanges.Subtract(mapReceived);
                    }
                    else if (wtx.IsCoinBase())
                    {
                        // there are no inputs for coinbase
                        mapCoinbaseChanges.Subtract(mapReceived);
                    }
                    else
                    {
                         throw runtime_error(
                                   "Reorganize(): TSNH upon BECAME-MATURE - "
                                   "matures but not coinstake or coinbase");
                    }
                }
            }
        }
        ///////////////////////////////////////////////////////////////////

        if (!block.ConnectBlock(txdb, pindex))
        {
            // Invalid block
            return error("Reorganize() : ConnectBlock failed\n  %s",
                         pindex->GetBlockHash().ToString().c_str());
        }

        // Queue memory transactions to delete
        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            vDelete.push_back(tx);
        }
    }

    if (!txdb.WriteHashBestChain(pindexNew->GetBlockHash()))
    {
        return error("Reorganize() : WriteHashBestChain failed");
    }

    // Make sure it's successfully written to disk before changing memory structure
    if (!txdb.TxnCommit())
    {
        return error("Reorganize() : TxnCommit failed");
    }

    // Disconnect shorter branch from top down
    BOOST_FOREACH(CBlockIndex* pindex, vDisconnect)
    {
        if (pindex->pprev)
        {
            pindex->pprev->pnext = NULL;
        }
        mapBlockLookup.erase(pindex->nHeight);
    }

    // Ensure that block previous to the first is itself properly connected
    // before connecting to this.
    const CBlockIndex* pindexTop = vConnect.empty() ? NULL : vConnect[0];
    if (pindexTop &&
        pindexTop->pprev &&
        pindexTop->pprev->pprev &&
        pindexTop->pprev->pprev->pnext != pindexTop->pprev)
    {
        return error("Reorganize(): "
                         "previous block is not properly connected\n  %s",
                     pindexTop->pprev->pprev->phashBlock->ToString().c_str());
    }

    // Connect longer branch from bottom up
    BOOST_FOREACH(CBlockIndex* pindex, vConnect)
    {
        if (pindex->pprev)
        {
            pindex->pprev->pnext = pindex;
        }
        mapBlockLookup[pindex->nHeight] = pindex;
    }

    // Resurrect memory transactions that were in the disconnected branch
    BOOST_FOREACH(CTransaction& tx, vResurrect)
    {
        tx.AcceptToMemoryPool(txdb, false);
    }

    // Delete redundant memory transactions that are in the connected branch
    BOOST_FOREACH(CTransaction& tx, vDelete)
    {
        mempool.remove(tx);
        mempool.removeConflicts(tx);
    }

    ///////////////////////////////////////////////////////////////////
    // Calculate any changes to unconfirmed balances resulting from
    //    the disconnections (vDisconnect).
    ///////////////////////////////////////////////////////////////////
    BOOST_FOREACH(CBlockIndex* pindex, vDisconnect)
    {
        CBlock block;
        if (!block.ReadFromDisk(pindex))
        {
            return error("Reorganize(): ReadFromDisk after disconnect failed");
        }
        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            // temporary wallet transaction for calculations
            CWalletTx wtx;
            {
                LOCK(pwalletMain->cs_wallet);
                wtx = CWalletTx(pwalletMain, tx);
            }
            uint256 hash = wtx.GetHash();
            // Coinstake/coinbase txs can never be mempool-unconfirmed; they
            // are always block-bound. Disconnecting them does not move them to
            // mapReceived/mapSent, so exclude DoesMature() txs here.
            if ((mapConfStatus[hash] == CONF_BECAME_UNCONFIRMED) &&
                !tx.DoesMature())
            {
                ColorsMap mapReceived;
                ColorsMap mapSent;
                wtx.GetAmounts(false, mapReceived, mapSent);
                // BECAME-UNCONFIRMED: Add
                mapReceivedChanges.Add(mapReceived);
                mapSentChanges.Add(mapSent);
            }
        }
    }
    ///////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////
    // Calculate any changes to unconfirmed balances resulting from
    //    the connections (vConnect).
    ///////////////////////////////////////////////////////////////////
    BOOST_FOREACH(CBlockIndex* pindex, vConnect)
    {
        CBlock block;
        if (!block.ReadFromDisk(pindex))
        {
            return error("Reorganize(): ReadFromDisk after connect failed");
        }
        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            // temporary wallet transaction for calculations
            CWalletTx wtx;
            {
                LOCK(pwalletMain->cs_wallet);
                wtx = CWalletTx(pwalletMain, tx);
            }
            uint256 hash = wtx.GetHash();
            mapConfStatus[hash] |= CONF_CONFIRMED_AFTER;
            // Symmetric guard: coinstake/coinbase txs are never in the unconfirmed
            // maps, so connecting them should never subtract from mapReceived/mapSent.
            if ((mapConfStatus[hash] == CONF_BECAME_CONFIRMED) && !tx.DoesMature())
            {
                ColorsMap mapReceived;
                ColorsMap mapSent;
                wtx.GetAmounts(false, mapReceived, mapSent);
                // BECAME-CONFIRMED: Subtract.
                mapReceivedChanges.Subtract(mapReceived);
                mapSentChanges.Subtract(mapSent);
            }
        }
    }
    ///////////////////////////////////////////////////////////////////

    ///////////////////////////////////////////////////////////////////
    // Add balance changes to pwalletMain now that chain is
    //    fully reorganized
    ///////////////////////////////////////////////////////////////////
    {
        LOCK(pwalletMain->cs_wallet);
        pwalletMain->mapConfirmed.Add(mapConfirmedChanges);
        pwalletMain->mapStake.Add(mapStakeChanges);
        pwalletMain->mapCoinbase.Add(mapCoinbaseChanges);
        pwalletMain->mapReceived.Add(mapReceivedChanges);
        pwalletMain->mapSent.Add(mapSentChanges);
    }
    if (fDebugMiner)
    {
        LOCK(pwalletMain->cs_wallet);
        printf("====================\n");
        printf("Reorganize() : mapConfirmedChanges: %s\n",
               mapConfirmedChanges.ToString().c_str());
        printf("Reorganize() : mapStakeChanges: %s\n",
               mapStakeChanges.ToString().c_str());
        printf("Reorganize() : mapCoinbaseChanges: %s\n",
               mapCoinbaseChanges.ToString().c_str());
        printf("Reorganize() : mapReceivedChanges: %s\n",
               mapReceivedChanges.ToString().c_str());
        printf("Reorganize() : mapSentChanges: %s\n",
               mapSentChanges.ToString().c_str());
        printf("====================\n");
        printf("Reorganize() : mapConfirmed: %s\n",
               pwalletMain->mapConfirmed.ToString().c_str());
        printf("Reorganize() : mapStake: %s\n",
               pwalletMain->mapStake.ToString().c_str());
        printf("Reorganize() : mapCoinbase: %s\n",
               pwalletMain->mapCoinbase.ToString().c_str());
        printf("Reorganize() : mapReceived: %s\n",
               pwalletMain->mapReceived.ToString().c_str());
        printf("Reorganize() : mapSent: %s\n",
               pwalletMain->mapSent.ToString().c_str());
        printf("====================\n");
    }
    ///////////////////////////////////////////////////////////////////


    printf("REORGANIZE: done\n");

    return true;
}


// Called from inside SetBestChain: attaches a block to the new best chain being built
bool CBlock::SetBestChainInner(CTxDB& txdb, CBlockIndex* pindexNew)
{
    uint256 hash = GetHash();

    // Adding to current best branch
    if (!ConnectBlock(txdb, pindexNew) || !txdb.WriteHashBestChain(hash))
    {
        txdb.TxnAbort();
        InvalidChainFound(pindexNew);
        return false;
    }

    if (!txdb.TxnCommit())
    {
        return error("SetBestChainInner() : TxnCommit failed");
    }

    // ensure that previous block is itself properly connected before
    // connecting to this
    if (pindexNew->pprev && pindexNew->pprev->pprev &&
        (pindexNew->pprev->pprev->pnext != pindexNew->pprev))
    {
        return error("SetBestChainInner(): "
                     "previous block is not properly connected\n  %s",
                     pindexNew->pprev->pprev->phashBlock->ToString().c_str());
    }

    // Add to current best branch
    pindexNew->pprev->pnext = pindexNew;
    mapBlockLookup[pindexNew->nHeight] = pindexNew;

    // Delete redundant memory transactions
    BOOST_FOREACH (CTransaction& tx, vtx)
    {
        mempool.remove(tx);
    }

    return true;
}

bool CBlock::SetBestChain(CTxDB& txdb, CBlockIndex* pindexNew)
{
    uint256 hash = GetHash();

    if (!txdb.TxnBegin())
    {
        return error("SetBestChain() : TxnBegin failed");
    }

    if (pindexGenesisBlock == NULL &&
        hash == (!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet))
    {
        txdb.WriteHashBestChain(hash);
        if (!txdb.TxnCommit())
        {
            return error("SetBestChain() : TxnCommit failed");
        }
        pindexGenesisBlock = pindexNew;
    }
    else if (hashPrevBlock == hashBestChain)
    {
        static const int MATURITY_DEPTH = GetCoinbaseMaturity() + 1;

        // tracks tx confirmation status before and after connection
        map<uint256, int> mapConfStatus;
        ColorsMap mapConfirmedChanges;
        ColorsMap mapStakeChanges;
        ColorsMap mapCoinbaseChanges;
        ColorsMap mapReceivedChanges;
        ColorsMap mapSentChanges;

        CBlock block;
        if (!block.ReadFromDisk(pindexNew))
        {
            return error(
                "SetBestChain() : ReadFromDisk new block failed\n  %s\n",
                pindexNew->GetBlockHash().GetHex().c_str());
        }

        ///////////////////////////////////////////////////////////////////
        // Calculate balance changes for main wallet before the block is
        //    fully connected.
        ///////////////////////////////////////////////////////////////////
        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            // temporary wallet transaction for calculations
            CWalletTx wtx;
            {
                LOCK(pwalletMain->cs_wallet);
                wtx = CWalletTx(pwalletMain, tx);
            }
            bool fDoesMature = wtx.DoesMature();
            bool fImmature = (fDoesMature &&
                              (wtx.GetBlocksToMaturity() > 0));
            bool fUnconfirmed = ((!fDoesMature) && (wtx.GetDepthInMainChain() <= 0));
            mapConfStatus[wtx.GetHash()] = fUnconfirmed
                                               ? CONF_UNCONFIRMED_BEFORE
                                               : CONF_CONFIRMED_BEFORE;
            ColorsMap mapReceived;
            ColorsMap mapSent;
            wtx.GetAmounts(false, mapReceived, mapSent);
            // CONNECTION: Add Received, subtract Sent.
            mapConfirmedChanges.Add(mapReceived);
            mapConfirmedChanges.Subtract(mapSent);
            if (fImmature)
            {
                // CONNECTION: Add.

                if (wtx.IsCoinStake())
                {
                    // inputs = outputs, so keep track of only one side
                    mapStakeChanges.Add(mapReceived);
                }
                else if (wtx.IsCoinBase())
                {
                    // there are no inputs for coinbase
                    mapCoinbaseChanges.Add(mapReceived);
                }
                else
                {
                     throw runtime_error(
                              "SetBestChain(): TSNH upon CONNECTION to best - "
                              "matures but not coinstake or coinbase");
                }
            }
        }
        // Add coins that would become mature upon connecting.
        CBlockIndex* pindexMat = pindexNew;
        // new confs:     1        2      3      4     ...     MATURITY_DEPTH
        //            pindexMat->pprev->pprev->pprev-> ... -> [becomes mature]
        int i = 1;
        while (pindexMat->pprev)
        {
            pindexMat = pindexMat->pprev;
            i += 1;
            if (i == MATURITY_DEPTH)
            {
                break;
            }
        }
        if ((i != MATURITY_DEPTH) && (pindexMat != pindexGenesisBlock))
        {
            // This should never happen: can't step back through
            //    pprevs to get to block that would become mature
            throw std::runtime_error(strprintf("SetBestChain(): i=%d "
                         "TSNH can't step back from new block\n  %s",
                         i,
                         pindexNew->GetBlockHash().ToString().c_str()));
        }
        // ensure it stepped through enough blocks
        if (pindexMat && (i == MATURITY_DEPTH))
        {
            CBlock blockMat;
            blockMat.ReadFromDisk(pindexMat);
            BOOST_FOREACH(CTransaction& tx, blockMat.vtx)
            {
                if (tx.DoesMature())
                {
                    // temporary wallet transaction for calculations
                    CWalletTx wtx;
                    {
                        LOCK(pwalletMain->cs_wallet);
                        wtx = CWalletTx(pwalletMain, tx);
                    }
                    ColorsMap mapReceived;
                    ColorsMap mapSentUnused;
                    wtx.GetAmounts(false, mapReceived, mapSentUnused);

                    // BECAME-MATURE (stake/coinbase matured) ==> Subtract
                    if (wtx.IsCoinStake())
                    {
                        // inputs = outputs, so keep track of only one side
                        mapStakeChanges.Subtract(mapReceived);
                    }
                    else if (wtx.IsCoinBase())
                    {
                        // there are no inputs for coinbase
                        mapCoinbaseChanges.Subtract(mapReceived);
                    }
                    else
                    {
                         throw runtime_error(
                              "SetBestChain(): TSNH upon BECAME-MATURE "
                              "(CONNECTION to best) - "
                              "matures but not coinstake or coinbase");
                    }
                }
            }
        }
        ///////////////////////////////////////////////////////////////////

        if (!SetBestChainInner(txdb, pindexNew))
        {
            return error("SetBestChain() : SetBestChainInner failed");
        }

        ///////////////////////////////////////////////////////////////////
        // Calculate any changes to unconfirmed balances resulting from
        //    connecting the block.
        ///////////////////////////////////////////////////////////////////
        BOOST_FOREACH(const CTransaction& tx, block.vtx)
        {
            // temporary wallet transaction for calculations
            CWalletTx wtx;
            {
                LOCK(pwalletMain->cs_wallet);
                wtx = CWalletTx(pwalletMain, tx);
            }
            uint256 hash = wtx.GetHash();
            mapConfStatus[hash] |= CONF_CONFIRMED_AFTER;
            if (mapConfStatus[hash] == CONF_BECAME_CONFIRMED)
            {
                ColorsMap mapReceived;
                ColorsMap mapSent;
                wtx.GetAmounts(false, mapReceived, mapSent);
                // BECAME-CONFIRMED: Subtract.
                mapReceivedChanges.Subtract(mapReceived);
                mapSentChanges.Subtract(mapSent);
            }
        }
        ///////////////////////////////////////////////////////////////////

        ///////////////////////////////////////////////////////////////////
        // Add balance changes to pwalletMain now that chain is
        //    fully reorganized
        ///////////////////////////////////////////////////////////////////
        {
            LOCK(pwalletMain->cs_wallet);
            pwalletMain->mapConfirmed.Add(mapConfirmedChanges);
            pwalletMain->mapStake.Add(mapStakeChanges);
            pwalletMain->mapCoinbase.Add(mapCoinbaseChanges);
            pwalletMain->mapReceived.Add(mapReceivedChanges);
            pwalletMain->mapSent.Add(mapSentChanges);
        }
        if (fDebugMiner)
        {
            LOCK(pwalletMain->cs_wallet);
            printf("====================\n");
            printf("SetBestChain() : mapConfirmedChanges: %s\n",
                   mapConfirmedChanges.ToString().c_str());
            printf("SetBestChain() : mapStakeChanges: %s\n",
                   mapStakeChanges.ToString().c_str());
            printf("SetBestChain() : mapCoinbaseChanges: %s\n",
                   mapCoinbaseChanges.ToString().c_str());
            printf("SetBestChain() : mapReceivedChanges: %s\n",
                   mapReceivedChanges.ToString().c_str());
            printf("SetBestChain() : mapSentChanges: %s\n",
                   mapSentChanges.ToString().c_str());
            printf("====================\n");
            printf("SetBestChain() : mapConfirmed: %s\n",
                   pwalletMain->mapConfirmed.ToString().c_str());
            printf("SetBestChain() : mapStake: %s\n",
                   pwalletMain->mapStake.ToString().c_str());
            printf("SetBestChain() : mapCoinbase: %s\n",
                   pwalletMain->mapCoinbase.ToString().c_str());
            printf("SetBestChain() : mapReceived: %s\n",
                   pwalletMain->mapReceived.ToString().c_str());
            printf("SetBestChain() : mapSent: %s\n",
                   pwalletMain->mapSent.ToString().c_str());
            printf("====================\n");
        }
        ///////////////////////////////////////////////////////////////////
    }
    else
    {
        // the first block in the new chain that will cause it to become the
        // new best chain
        CBlockIndex* pindexIntermediate = pindexNew;

        // list of blocks that need to be connected afterwards
        vector<CBlockIndex*> vpindexSecondary;

        // Reorganize is costly in terms of db load, as it works in a single db
        // transaction. Try to limit how much needs to be done inside
        while (pindexIntermediate->pprev &&
               pindexIntermediate->pprev->nChainTrust >
                   pindexBest->nChainTrust)
        {
            vpindexSecondary.push_back(pindexIntermediate);
            pindexIntermediate = pindexIntermediate->pprev;
        }

        if (!vpindexSecondary.empty())
        {
            printf("Postponing %" PRIszu " reconnects\n",
                   vpindexSecondary.size());
        }

        // Switch to new best branch
        if (!Reorganize(txdb, pindexIntermediate))
        {
            txdb.TxnAbort();
            InvalidChainFound(pindexNew);
            return error("SetBestChain() : Reorganize failed");
        }

        static const int MATURITY_DEPTH = GetCoinbaseMaturity() + 1;

        // tracks tx confirmation status before and after connections
        map<uint256, int> mapConfStatus;
        ColorsMap mapConfirmedChanges;
        ColorsMap mapStakeChanges;
        ColorsMap mapCoinbaseChanges;
        ColorsMap mapReceivedChanges;
        ColorsMap mapSentChanges;

        // Connect further blocks
        BOOST_REVERSE_FOREACH(CBlockIndex * pindex, vpindexSecondary)
        {
            CBlock block;
            if (!block.ReadFromDisk(pindex))
            {
                printf("SetBestChain() : ReadFromDisk failed\n");
                break;
            }

            ///////////////////////////////////////////////////////////////////
            // Calculate balance changes for main wallet before the block is
            //    fully connected.
            ///////////////////////////////////////////////////////////////////
            BOOST_FOREACH(const CTransaction& tx, block.vtx)
            {
                // temporary wallet transaction for calculations
                CWalletTx wtx;
                {
                    LOCK(pwalletMain->cs_wallet);
                    wtx = CWalletTx(pwalletMain, tx);
                }
                bool fDoesMature = wtx.DoesMature();
                bool fImmature = (fDoesMature &&
                                  (wtx.GetBlocksToMaturity() > 0));
                bool fUnconfirmed = (!fDoesMature && (wtx.GetDepthInMainChain() <= 0));
                mapConfStatus[wtx.GetHash()] = fUnconfirmed
                                                   ? CONF_UNCONFIRMED_BEFORE
                                                   : CONF_CONFIRMED_BEFORE;
                ColorsMap mapReceived;
                ColorsMap mapSent;
                wtx.GetAmounts(false, mapReceived, mapSent);
                // CONNECTION: Add Received, subtract Sent.
                mapConfirmedChanges.Add(mapReceived);
                mapConfirmedChanges.Subtract(mapSent);
                if (fImmature)
                {
                    // CONNECTION: Add.
                    if (wtx.IsCoinStake())
                    {
                        // inputs = outputs, so keep track of only one side
                        mapStakeChanges.Add(mapReceived);
                    }
                    else if (wtx.IsCoinBase())
                    {
                        // there are no inputs for coinbase
                        mapCoinbaseChanges.Add(mapReceived);
                    }
                    else
                    {
                         throw runtime_error(
                              "SetBestChain(): TSNH upon CONNECTION "
                              "(after reorganize) - "
                              "matures but not coinstake or coinbase");
                    }
                }
            }
            // Add coins that would become mature upon connecting.
            CBlockIndex* pindexMat = pindex;
            // new confs:     1        2      3      4     ...     MATURITY_DEPTH
            //            pindexMat->pprev->pprev->pprev-> ... -> [becomes mature]
            int i = 1;
            while (pindexMat->pprev)
            {
                pindexMat = pindexMat->pprev;
                i += 1;
                if (i == MATURITY_DEPTH)
                {
                    break;
                }
            }
            if ((i != MATURITY_DEPTH) && (pindexMat != pindexGenesisBlock))
            {
                // This should never happen: can't step back through
                //    pprevs to get to block that would become mature
                throw std::runtime_error(strprintf("SetBestChain(): i=%d "
                             "TSNH can't step back from connect\n  %s",
                             i,
                             pindex->GetBlockHash().ToString().c_str()));
            }
            // ensure it stepped through enough blocks
            if (pindexMat && (i == MATURITY_DEPTH))
            {
                CBlock blockMat;
                blockMat.ReadFromDisk(pindexMat);
                BOOST_FOREACH(CTransaction& tx, blockMat.vtx)
                {
                    if (tx.DoesMature())
                    {
                        // temporary wallet transaction for calculations
                        CWalletTx wtx;
                        {
                            LOCK(pwalletMain->cs_wallet);
                            wtx = CWalletTx(pwalletMain, tx);
                        }
                        ColorsMap mapReceived;
                        ColorsMap mapSentUnused;
                        wtx.GetAmounts(false, mapReceived, mapSentUnused);
                        // BECAME-MATURE (stake/coinbase matured) ==> Subtract
                        if (wtx.IsCoinStake())
                        {
                            // inputs = outputs, so keep track of only one side
                            mapStakeChanges.Subtract(mapReceived);
                        }
                        else if (wtx.IsCoinBase())
                        {
                            // there are no inputs for coinbase
                            mapCoinbaseChanges.Subtract(mapReceived);
                        }
                        else
                        {
                             throw runtime_error(
                                          "SetBestChain(): TSNH upon BECAME-MATURE "
                                          "(after reorganize) - "
                                          "matures but not coinstake or coinbase");
                        }
                    }
                }
            }
            ///////////////////////////////////////////////////////////////////

            if (!txdb.TxnBegin())
            {
                printf("SetBestChain() : TxnBegin 2 failed\n");
                break;
            }
            // errors now are not fatal, we still did a reorganisation to a new
            // chain in a valid way
            if (!block.SetBestChainInner(txdb, pindex))
            {
                break;
            }

            ///////////////////////////////////////////////////////////////////
            // Calculate any changes to unconfirmed balances resulting from
            //    connecting the block.
            ///////////////////////////////////////////////////////////////////
            BOOST_FOREACH(const CTransaction& tx, block.vtx)
            {
                // temporary wallet transaction for calculations
                CWalletTx wtx;
                {
                    LOCK(pwalletMain->cs_wallet);
                    wtx = CWalletTx(pwalletMain, tx);
                }
                uint256 hash = wtx.GetHash();
                mapConfStatus[hash] |= CONF_CONFIRMED_AFTER;
                if (mapConfStatus[hash] == CONF_BECAME_CONFIRMED)
                {
                    ColorsMap mapReceived;
                    ColorsMap mapSent;
                    wtx.GetAmounts(false, mapReceived, mapSent);
                    // BECAME-CONFIRMED: Subtract.
                    mapReceivedChanges.Subtract(mapReceived);
                    mapSentChanges.Subtract(mapSent);
                }
            }
            ///////////////////////////////////////////////////////////////////

            ///////////////////////////////////////////////////////////////////
            // Add balance changes to pwalletMain now that chain is
            //    fully reorganized
            ///////////////////////////////////////////////////////////////////
            {
                LOCK(pwalletMain->cs_wallet);
                pwalletMain->mapConfirmed.Add(mapConfirmedChanges);
                pwalletMain->mapStake.Add(mapStakeChanges);
                pwalletMain->mapCoinbase.Add(mapCoinbaseChanges);
                pwalletMain->mapReceived.Add(mapReceivedChanges);
                pwalletMain->mapSent.Add(mapSentChanges);
            }
            if (fDebugMiner)
            {
                LOCK(pwalletMain->cs_wallet);
                printf("====================\n");
                printf("SetBestChain() : mapConfirmedChanges: %s\n",
                       mapConfirmedChanges.ToString().c_str());
                printf("SetBestChain() : mapStakeChanges: %s\n",
                       mapStakeChanges.ToString().c_str());
                printf("SetBestChain() : mapCoinbaseChanges: %s\n",
                       mapCoinbaseChanges.ToString().c_str());
                printf("SetBestChain() : mapReceivedChanges: %s\n",
                       mapReceivedChanges.ToString().c_str());
                printf("SetBestChain() : mapSentChanges: %s\n",
                       mapSentChanges.ToString().c_str());
                printf("====================\n");
                printf("SetBestChain() : mapConfirmed: %s\n",
                       pwalletMain->mapConfirmed.ToString().c_str());
                printf("SetBestChain() : mapStake: %s\n",
                       pwalletMain->mapStake.ToString().c_str());
                printf("SetBestChain() : mapCoinbase: %s\n",
                       pwalletMain->mapCoinbase.ToString().c_str());
                printf("SetBestChain() : mapReceived: %s\n",
                       pwalletMain->mapReceived.ToString().c_str());
                printf("SetBestChain() : mapSent: %s\n",
                       pwalletMain->mapSent.ToString().c_str());
                printf("====================\n");
            }
            ///////////////////////////////////////////////////////////////////
        }
    }

    // Update best block in wallet (so we can detect restored wallets)
    bool fIsInitialDownload = IsInitialBlockDownload();
    if (!fIsInitialDownload)
    {
        const CBlockLocator locator(pindexNew);
        ::SetBestChain(locator);
    }

    // New best block
    hashBestChain = hash;
    pindexBest = pindexNew;
    pblockindexFBBHLast = NULL;
    nBestHeight = pindexBest->nHeight;
    nBestChainTrust = pindexNew->nChainTrust;
    nTimeBestReceived = GetTime();
    nTransactionsUpdated++;

    uint256 nBestBlockTrust = pindexBest->nHeight != 0
                                  ? (pindexBest->nChainTrust -
                                     pindexBest->pprev->nChainTrust)
                                  : pindexBest->nChainTrust;

    printf(
        "SetBestChain: new best=%s  height=%d  trust=%s  blocktrust=%" PRId64
        "  date=%s\n",
        hashBestChain.ToString().c_str(),
        nBestHeight,
        CBigNum(nBestChainTrust).ToString().c_str(),
        nBestBlockTrust.Get64(),
        DateTimeStrFormat("%x %H:%M:%S", pindexBest->GetBlockTime()).c_str());

    // Check the version of the last 100 blocks to see if we need to upgrade:
    if (!fIsInitialDownload)
    {
        int nUpgraded = 0;
        const CBlockIndex* pindex = pindexBest;
        for (int i = 0; i < 100 && pindex != NULL; i++)
        {
            if (pindex->nVersion > CBlock::CURRENT_VERSION)
            {
                ++nUpgraded;
            }
            pindex = pindex->pprev;
        }
        if (nUpgraded > 0)
        {
            printf("SetBestChain: %d of last 100 blocks above version %d\n",
                   nUpgraded,
                   CBlock::CURRENT_VERSION);
        }
        if (nUpgraded > 100 / 2)
        {
            // strMiscWarning is read by GetWarnings(), called by Qt and the
            // JSON-RPC code to warn the user:
            strMiscWarning = _(
                "Warning: This version is obsolete, upgrade required!");
        }
    }

    string strCmd = GetArg("-blocknotify", "");

    if (!fIsInitialDownload && !strCmd.empty())
    {
        boost::replace_all(strCmd, "%s", hashBestChain.GetHex());
        boost::thread t(runCommand, strCmd);  // thread runs free
    }

    return true;
}

// TODO: put in preprocessor conditional for coinage dependent PoS
// ppcoin: total coin age spent in transaction, in the unit of coin-days.
// A coin-day is really a BASE_COIN-day (see colors.h).
// Only those coins meeting minimum age requirement counts. As those
// transactions not in main chain are not currently indexed so we
// might not find out about their coin age. Older transactions are
// guaranteed to be in main chain by sync-checkpoint. This rule is
// introduced to help nodes establish a consistent view of the coin
// age (trust score) of competing branches.
bool CTransaction::GetCoinAge(CTxDB& txdb, const CBlockIndex* pindexPrev, uint64_t& nCoinAge) const
{
    CBigNum bnCentSecond = 0;  // coin age in the unit of cent-seconds
    nCoinAge = 0;

    CBigNum bnSeconds ; // age of coins (not coin age)
    if (IsCoinBase())
        return true;

    BOOST_FOREACH(const CTxIn& txin, vin)
    {
        // First try finding the previous transaction in database
        CTransaction txPrev;
        CTxIndex txindex;
        if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
            continue;  // previous transaction not in main chain
        if (nTime < txPrev.nTime)
            return false;  // Transaction timestamp violation

        int nCoinColor = txPrev.vout[txin.prevout.n].nColor;
        int nStakeMinConfs = GetStakeMinConfirmations(nCoinColor);

        int nSpendDepth;
        if (IsConfirmedInNPrevBlocks(txindex, pindexPrev, nStakeMinConfs - 1, nSpendDepth))
        {
            if (fDebug)
            {
               printf("GetCoinAge(): coin age skip nSpendDepth=%d\n", nSpendDepth + 1);
            }
            continue; // only count coins meeting min confirmations requirement
        }

        int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
        int nColorIn = txPrev.vout[txin.prevout.n].nColor;
        bnSeconds = CBigNum(nTime-txPrev.nTime);

        // The concept of cents in atomic currencies is strained,
        // but weight_not_atomic * COIN[color_not_atomic] -->
        //                                weight_atomic * 100 * CENT[color_atomic]
	    // where weight is applied elsewhere (e.g. GetStakeWeightByColor).
        if (COIN[nColorIn] == CENT[nColorIn])
        {
            bnCentSecond += 100 * CBigNum(nValueIn) * bnSeconds / COIN[nColorIn];
        }
        else
        {
            bnCentSecond += CBigNum(nValueIn) * bnSeconds / CENT[nColorIn];
        }

        if (fDebug && GetBoolArg("-printcoinage"))
            printf("coin age nValueIn=%" PRId64 " nTimeDiff=%d bnCentSecond=%s\n", nValueIn, nTime - txPrev.nTime, bnCentSecond.ToString().c_str());
    }

    CBigNum bnCoinDay = ((bnCentSecond * BASE_CENT) / BASE_COIN) / (24 * 60 * 60);
    if (fDebug && GetBoolArg("-printcoinage"))
        printf("coin age bnCoinDay=%s\n", bnCoinDay.ToString().c_str());
    nCoinAge = bnCoinDay.getuint64();
    return true;
}

// TODO: put in preporcessor conditional for coinage dependent PoS
// ppcoin: total coin age spent in block, in the unit of coin-days.
bool CBlock::GetCoinAge(uint64_t& nCoinAge) const
{
    nCoinAge = 0;

    CBlockIndex *pindexPrev = CBlockLocator(this->GetHash()).GetBlockIndex();

    CTxDB txdb("r");
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        uint64_t nTxCoinAge;
        if (tx.GetCoinAge(txdb, pindexPrev, nTxCoinAge))
            nCoinAge += nTxCoinAge;
        else
            return false;
    }

    if (nCoinAge == 0) // block coin age minimum 1 coin-day
        nCoinAge = 1;
    if (fDebug && GetBoolArg("-printcoinage"))
        printf("block coin age total nCoinDays=%" PRId64 "\n", nCoinAge);
    return true;
}

bool CBlock::AddToBlockIndex(unsigned int nFile,
                             unsigned int nBlockPos,
                             const uint256& hashProof)
{
    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = GetHash();
    if (mapBlockIndex.count(hash))
    {
        return error("AddToBlockIndex() : %s already exists",
                     hash.ToString().substr(0, 20).c_str());
    }

    // Construct new block index object
    CBlockIndex* pindexNew = new CBlockIndex(nFile, nBlockPos, *this);
    if (!pindexNew)
    {
        return error("AddToBlockIndex() : new CBlockIndex failed");
    }
    if (fDebugMiner && IsKawpowBlock())
    {
        pindexNew->IsKawpow();
        printf("%s\n", pindexNew->ToString().c_str());
    }
    pindexNew->phashBlock = &hash;
    map<uint256, CBlockIndex*>::iterator miPrev = mapBlockIndex.find(
        hashPrevBlock);
    if (miPrev != mapBlockIndex.end())
    {
        pindexNew->pprev = (*miPrev).second;
        pindexNew->nHeight = pindexNew->pprev->nHeight + 1;
    }
    else if (fDebugMiner && IsKawpowBlock())
    {
        printf("WARNING: Could not find previous block!\n  this:%s\n  prev:%s\n",
               hash.GetHex().c_str(),
               hashPrevBlock.GetHex().c_str());
    }

    // ppcoin: compute chain trust score
    pindexNew->nChainTrust = (pindexNew->pprev ? pindexNew->pprev->nChainTrust
                                               : 0) +
                             pindexNew->GetBlockTrust();

    // ppcoin: compute stake entropy bit for stake modifier
    if (!pindexNew->SetStakeEntropyBit(GetStakeEntropyBit()))
    {
        return error("AddToBlockIndex() : SetStakeEntropyBit() failed");
    }

    // Record proof hash value
    pindexNew->hashProof = hashProof;

    // ppcoin: compute stake modifier
    uint256 bnStakeModifier = 0;
    bool fGeneratedStakeModifier = false;
    if (!ComputeNextStakeModifier(pindexNew->pprev,
                                  bnStakeModifier,
                                  fGeneratedStakeModifier))
    {
        return error("AddToBlockIndex() : ComputeNextStakeModifier() failed");
    }
    pindexNew->SetStakeModifier(bnStakeModifier, fGeneratedStakeModifier);
    pindexNew->nStakeModifierChecksum = GetStakeModifierChecksum(pindexNew);
    if (!CheckStakeModifierCheckpoints(pindexNew->nHeight,
                                       pindexNew->nStakeModifierChecksum))
    {
        return error("AddToBlockIndex() : Rejected by stake modifier "
                     "checkpoint height=%d, modifier=0x%08" PRIx32,
                     pindexNew->nHeight,
                     pindexNew->nStakeModifierChecksum);
    }

    // Add to mapBlockIndex
    map<uint256, CBlockIndex*>::iterator
        mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;
    if (pindexNew->IsProofOfStake())
    {
        setStakeSeen.insert(
            make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));
    }
    pindexNew->phashBlock = &((*mi).first);

    // Write to disk block index
    CTxDB txdb;
    if (!txdb.TxnBegin())
    {
        return false;
    }
    CDiskBlockIndex diskIndexNew(pindexNew);
    if (fDebugMiner && IsKawpowBlock())
    {
        printf("%s\n", diskIndexNew.ToString().c_str());
    }
    txdb.WriteBlockIndex(diskIndexNew);
    if (!txdb.TxnCommit())
    {
        return false;
    }

    LOCK(cs_main);

    // New best
    if (pindexNew->nChainTrust > nBestChainTrust)
    {
        if (!SetBestChain(txdb, pindexNew))
        {
            return false;
        }
    }

    if (pindexNew == pindexBest)
    {
        // Notify UI to display prev block's coinbase if it was ours
        static uint256 hashPrevBestCoinBase;
        UpdatedTransaction(hashPrevBestCoinBase);
        hashPrevBestCoinBase = vtx[0].GetHash();
    }

    uiInterface.NotifyBlocksChanged();
    return true;
}


bool CBlock::CheckBlock(bool fCheckPOW, bool fCheckMerkleRoot, bool fCheckSig) const
{
    // These are checks that are independent of context
    // that can be verified before saving an orphan block.

    // Size limits
    if (vtx.empty() ||
        vtx.size() > MAX_BLOCK_SIZE ||
        ::GetSerializeSize(*this, SER_NETWORK, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
    {
        return DoS(100, error("CheckBlock() : size limits failed"));
    }

    // Check proof of work matches claimed amount
    if (fCheckPOW && IsProofOfWork() && !CheckProofOfWork(GetHash(), nBits, this))
    {
        return DoS(50, error("CheckBlock() : proof of work failed"));
    }

    // Check timestamp
    if (GetBlockTime() > FutureDrift(GetAdjustedTime()))
    {
        return error("CheckBlock() : block timestamp too far in the future");
    }

    // First transaction must be coinbase, the rest must not be
    if (vtx.empty() || !vtx[0].IsCoinBase())
    {
        return DoS(100, error("CheckBlock() : first tx is not coinbase"));
    }
    for (unsigned int i = 1; i < vtx.size(); i++)
    {
        if (vtx[i].IsCoinBase())
        {
            return DoS(100, error("CheckBlock() : more than one coinbase"));
        }
    }

    // Check coinbase timestamp
    if (GetBlockTime() > FutureDrift((int64_t)vtx[0].nTime))
    {
        return DoS(50, error("CheckBlock() : coinbase timestamp: %" PRId64 " + 15 sec, "
                               "is too early for block: %" PRId64,
                             (int64_t)vtx[0].nTime,
                             (int64_t)GetBlockTime()));
    }

    if (IsProofOfStake())
    {
        // Second transaction must be coinstake, the rest must not be
        if (vtx.empty() || !vtx[1].IsCoinStake())
        {
            return DoS(100, error("CheckBlock() : second tx is not coinstake"));
        }
        for (unsigned int i = 2; i < vtx.size(); i++)
        {
            if (vtx[i].IsCoinStake())
            {
                return DoS(100, error("CheckBlock() : more than one coinstake"));
            }
        }

        // Check coinmint timestamp
        if (!CheckCoinStakeTimestamp(GetBlockTime(), (int64_t)vtx[0].nTime))
        {
            return DoS(50,
                       error("CheckBlock() : coinmint timestamp violation "
                               "nTimeBlock=%" PRId64 " nTimeTx=%u",
                             GetBlockTime(),
                             vtx[1].nTime));
        }

        // Check coinstake timestamp
        if (!CheckCoinStakeTimestamp(GetBlockTime(), (int64_t)vtx[1].nTime))
        {
            return DoS(50,
                       error("CheckBlock() : coinstake timestamp violation "
                               "nTimeBlock=%" PRId64 " nTimeTx=%u",
                             GetBlockTime(),
                             vtx[1].nTime));
        }

        // NovaCoin: check proof-of-stake block signature
        if (fCheckSig)
        {
            ReturnCode codeBlockSig = CheckBlockSignature();
            if (!codeBlockSig)
            {
                // don't hard dos a peer just because our we temporarily can't
                // access our own disk 
                int n = (codeBlockSig.code ==
                         (int) BlockSigStatus::TX_UNREADABLE)
                            ? 5
                            : 100;
                return DoS(
                    n,
                    error(
                        "CheckBlock() : bad proof-of-stake block signature"));
            }
        }
    }

    // Check transactions
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        // color consistency of outputs checked here, among other things
        if (!tx.CheckTransaction())
        {
            return DoS(tx.nDoS, error("CheckBlock() : CheckTransaction failed"));
        }

        // ppcoin: check transaction timestamp
        if (GetBlockTime() < (int64_t)tx.nTime)
        {
            return DoS(50, error("CheckBlock() : block timestamp earlier than transaction timestamp"));
        }
    }

    // Check for duplicate txids. This is caught by ConnectInputs(),
    // but catching it earlier avoids a potential DoS attack:
    set<uint256> uniqueTx;
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        uniqueTx.insert(tx.GetHash());
    }
    if (uniqueTx.size() != vtx.size())
        return DoS(100, error("CheckBlock() : duplicate transaction"));

    unsigned int nSigOps = 0;
    BOOST_FOREACH(const CTransaction& tx, vtx)
    {
        nSigOps += tx.GetLegacySigOpCount();
    }
    if (nSigOps > MAX_BLOCK_SIGOPS)
    {
        return DoS(100, error("CheckBlock() : out-of-bounds SigOpCount"));
    }

    // Check merkle root
    if (fCheckMerkleRoot && hashMerkleRoot != BuildMerkleTree())
    {
        return DoS(100, error("CheckBlock() : hashMerkleRoot mismatch"));
    }

    return true;
}

bool CBlock::AcceptBlock()
{
#if PROOF_MODEL == PURE_POS
    static const int nLastPoWBlock = GetLastPoWBlock();
#endif

    AssertLockHeld(cs_main);
    // Check for duplicate
    uint256 hash = GetHash();
    if (mapBlockIndex.count(hash))
    {
        return error("AcceptBlock() : block already in mapBlockIndex");
    }

    // Get prev block index
    map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(
        hashPrevBlock);
    if (mi == mapBlockIndex.end())
    {
        return DoS(10, error("AcceptBlock() : prev block not found"));
    }
    CBlockIndex* pindexPrev = (*mi).second;
    int nHeight = pindexPrev->nHeight + 1;

#if PROOF_MODEL == PURE_POS
    if (IsProofOfWork() && (nHeight > nLastPoWBlock))
    {
        return DoS(
            100,
            error(
                "AcceptBlock() : reject proof-of-work (too late) at height %d",
                nHeight));
    }
#endif

    // Check coinbase timestamp
    if (GetBlockTime() > FutureDrift((int64_t) vtx[0].nTime))
    {
        return DoS(50,
                   error("AcceptBlock() : coinbase timestamp is too early"));
    }

    // Check coinstake timestamp
    if (IsProofOfStake() &&
        !CheckCoinStakeTimestamp(GetBlockTime(), (int64_t) vtx[1].nTime))
    {
        return DoS(50,
                   error("AcceptBlock() : coinstake timestamp violation "
                         "nTimeBlock=%" PRId64 " nTimeTx=%u",
                         GetBlockTime(),
                         vtx[1].nTime));
    }

    // Check proof-of-work or proof-of-stake
    if (nBits !=
        GetNextTargetRequired(this->nTime, pindexPrev, IsProofOfStake()))
    {
        printf("AcceptBlock()): Proof calculated: %d,   Proof received in "
               "block: %d\n",
               GetNextTargetRequired(this->nTime,
                                     pindexPrev,
                                     IsProofOfStake()),
               nBits);
        return DoS(100,
                   error("AcceptBlock() : incorrect %s",
                         IsProofOfWork() ? "proof-of-work"
                                         : "proof-of-stake"));
    }

    // Check timestamp against prev
    if ((GetBlockTime() <= pindexPrev->GetPastTimeLimit()) ||
        (FutureDrift(GetBlockTime()) < pindexPrev->GetBlockTime()))
    {
        return error("AcceptBlock() : block's timestamp is too early");
    }

    // Check timestamp against launch time
    if (GetBlockTime() < nLaunchTime)
    {
        return DoS(100, error("AcceptBlock(): timestamp is prior to launch"));
    }

    // Check that all transactions are finalized
    BOOST_FOREACH (const CTransaction& tx, vtx)
    {
        if (!tx.IsFinal(nHeight, GetBlockTime()))
        {
            return DoS(
                10,
                error("AcceptBlock() : contains a non-final transaction"));
        }
    }

    // Check that the block chain matches the known block chain up to a
    // checkpoint
    if (!Checkpoints::CheckHardened(nHeight, hash))
    {
        return DoS(100,
                   error("AcceptBlock() : rejected by hardened checkpoint "
                         "lock-in at %d",
                         nHeight));
    }

    uint256 hashProof;
    // Verify hash target and signature of coinstake tx
    if (IsProofOfStake())
    {
        uint256 targetProofOfStake;
        if (!CheckProofOfStake(pindexPrev,
                               vtx[1],
                               nBits,
                               hashProof,
                               targetProofOfStake))
        {
            // do not error here as we expect this during initial block
            // download
            printf("WARNING: AcceptBlock(): check proof-of-stake failed for "
                   "block %s\n",
                   hash.ToString().c_str());
            return false;
        }
    }
    // PoW is checked in CheckBlock()
    if (IsProofOfWork())
    {
        hashProof = GetHash();
    }

    // Enforce rule that the coinbase starts with serialized block height
    CScript expect = CScript() << nHeight;
    if (vtx[0].vin[0].scriptSig.size() < expect.size() ||
        !equal(expect.begin(), expect.end(), vtx[0].vin[0].scriptSig.begin()))
    {
        return DoS(100,
                   error("AcceptBlock() : block height mismatch in coinbase"));
    }

    // Write block to history file
    if (!CheckDiskSpace(::GetSerializeSize(*this, SER_DISK, CLIENT_VERSION)))
    {
        return error("AcceptBlock() : out of disk space");
    }
    unsigned int nFile = -1;
    unsigned int nBlockPos = 0;
    if (!WriteToDisk(nFile, nBlockPos))
    {
        return error("AcceptBlock() : WriteToDisk failed");
    }
    if (!AddToBlockIndex(nFile, nBlockPos, hashProof))
    {
        return error("AcceptBlock() : AddToBlockIndex failed");
    }
    if (fDebugMiner && IsKawpowBlock())
    {
        printf("Block added and written: nFile=%u, nBlockPos=%u\n  %s\n",
               nFile, nBlockPos, GetHash().GetHex().c_str());
    }

    // Relay inventory, but don't relay old inventory during initial block
    // download
    int nBlockEstimate = Checkpoints::GetTotalBlocksEstimate();
    if (hashBestChain == hash)
    {
        LOCK(cs_vNodes);
        BOOST_FOREACH (CNode* pnode, vNodes)
        {
            if (nBestHeight > (pnode->nStartingHeight != -1
                                   ? pnode->nStartingHeight - 2000
                                   : nBlockEstimate))
            {
                pnode->PushInventory(CInv(MSG_BLOCK, hash));
            }
        }
    }

    return true;
}

uint256 CBlockIndex::GetBlockTrust() const
{
    CBigNum bnTarget;
    bnTarget.SetCompact(nBits);

    if (bnTarget <= 0)
        return 0;

    return ((CBigNum(1)<<256) / (bnTarget+1)).getuint256();
}

bool CBlockIndex::IsSuperMajority(int minVersion,
                                  const CBlockIndex* pstart,
                                  unsigned int nRequired,
                                  unsigned int nToCheck)
{
    unsigned int nFound = 0;
    for (unsigned int i = 0; i < nToCheck && nFound < nRequired && pstart != NULL; i++)
    {
        if (pstart->nVersion >= minVersion)
        {
            ++nFound;
        }
        pstart = pstart->pprev;
    }
    return (nFound >= nRequired);
}

void PushGetBlocks(CNode* pnode, CBlockIndex* pindexBegin, uint256 hashEnd)
{
    // Filter out duplicate requests
    if ((pindexBegin == pnode->pindexLastGetBlocksBegin) &&
        (hashEnd == pnode->hashLastGetBlocksEnd))
    {
        return;
    }
    pnode->pindexLastGetBlocksBegin = pindexBegin;
    pnode->hashLastGetBlocksEnd = hashEnd;

    pnode->PushMessage("getblocks", CBlockLocator(pindexBegin), hashEnd);
}

#if 0
// bool static ReserealizeBlockSignature(CBlock* pblock)
// {
//     if (pblock->IsProofOfWork()) {
//         pblock->vchBlockSig.clear();
//         return true;
//     }

//     return CKey::ReserealizeSignature(pblock->vchBlockSig);
// }
#endif

bool static IsCanonicalBlockSignature(CBlock* pblock)
{
    if (pblock->IsProofOfWork()) {
        return pblock->vchBlockSig.empty();
    }

    return IsDERSignature(pblock->vchBlockSig, false);
}

// TODO: use flags in return value instead of fOrphan, etc
bool ProcessBlock(CNode* pfrom, CBlock* pblock, bool& fOrphan, bool fIsBootstrap)
{
    AssertLockHeld(cs_main);

    bool fAllowDuplicateStake = (fIsBootstrap && GetBoolArg("-permitdirtybootstrap", false));

    fOrphan = false;

    // Check for duplicate
    uint256 hash = pblock->GetHash();
    if (mapBlockIndex.count(hash))
    {
        return error("ProcessBlock() : already have block %d %s",
                     mapBlockIndex[hash]->nHeight,
                     hash.ToString().substr(0, 20).c_str());
    }
    if (mapOrphanBlocks.count(hash))
    {
        return error("ProcessBlock() : already have block (orphan) %s",
                     hash.ToString().c_str());
    }

    // enforce 0 nonce for proof-of-stake to prevent degradation to PoW
    if (pblock->IsProofOfStake() && (pblock->nNonce != 0))
    {
        if (pfrom)
        {
              pfrom->Misbehaving(100);
        }
        return error("ProcessBlock() : PoS block has non-zero nonce: %s",
                     hash.ToString().c_str());
    }

    // ppcoin: check proof-of-stake
    // Limited duplicity on stake: prevents block flood attack
    // Duplicate stake allowed only when there is orphan child block
    if (pblock->IsProofOfStake() &&
        setStakeSeen.count(pblock->GetProofOfStake()) &&
        !mapOrphanBlocksByPrev.count(hash) &&
        !fAllowDuplicateStake)
    {
        return error(
            "ProcessBlock() : duplicate proof-of-stake (%s, %d) for block %s",
            pblock->GetProofOfStake().first.ToString().c_str(),
            pblock->GetProofOfStake().second,
            hash.ToString().c_str());
    }

    // Preliminary checks
    if (!pblock->CheckBlock())
    {
        return error("ProcessBlock() : CheckBlock FAILED");
    }

    CBlockIndex* pcheckpoint = Checkpoints::GetLastCheckpoint();
    if (pcheckpoint && pblock->hashPrevBlock != hashBestChain)
    {
        // Extra checks to prevent "fill up memory by spamming with bogus blocks"
        unsigned int nBlockTime = pblock->GetBlockTime();
        int64_t deltaTime = nBlockTime - pcheckpoint->nTime;

        CBigNum bnNewBlock;
        bnNewBlock.SetCompact(pblock->nBits);
        CBigNum bnRequired;

        if (pblock->IsProofOfStake())
        {
            bnRequired.SetCompact(
                           ComputeMinStake(
                               GetLastBlockIndex(pcheckpoint, true)->nBits,
                               deltaTime));
        }
        else
        {
            bnRequired.SetCompact(
                           ComputeMinWork(
                               GetLastBlockIndex(pcheckpoint, false)->nBits,
                               deltaTime,
                               pblock->nTime));
        }

        if (bnNewBlock > bnRequired)
        {
            if (pfrom)
            {
                pfrom->Misbehaving(100);
            }
            return error("ProcessBlock() : block with too little %s",
                         pblock->IsProofOfStake() ? "proof-of-stake" : "proof-of-work");
        }
    }

    // Block signature can be malleated in such a way that it increases block
    // size up to maximum allowed by protocol
    if (!IsCanonicalBlockSignature(pblock))
    {
        pfrom->Misbehaving(100);
        return error("ProcessBlock(): bad block signature encoding");
    }

    // If don't already have its previous block, shunt it off to holding area
    // until we get it
    if (!mapBlockIndex.count(pblock->hashPrevBlock))
    {
        printf("ProcessBlock: ORPHAN BLOCK, prev=%s\n",
               pblock->hashPrevBlock.ToString().c_str());
        fOrphan = true;
        // Accept orphans as long as there is a node from which to request its
        // parents
        if (pfrom)
        {
            // ppcoin: check proof-of-stake
            if (pblock->IsProofOfStake())
            {
                // Limited duplicity on stake: prevents block flood attack
                // Duplicate stake allowed only when there is orphan child
                // block
                if (setStakeSeenOrphan.count(pblock->GetProofOfStake()) &&
                    !mapOrphanBlocksByPrev.count(hash))
                {
                    return error(
                        "ProcessBlock() : "
                        "duplicate proof-of-stake (%s, %d) for orphan block "
                        "%s",
                        pblock->GetProofOfStake().first.ToString().c_str(),
                        pblock->GetProofOfStake().second,
                        hash.ToString().c_str());
                }
            }
            PruneOrphanBlocks();
            COrphanBlock* pblock2 = new COrphanBlock();
            {
                CDataStream ss(SER_DISK, CLIENT_VERSION);
                ss << *pblock;
                pblock2->vchBlock = valtype(ss.begin(), ss.end());
            }
            pblock2->hashBlock = hash;
            pblock2->hashPrev = pblock->hashPrevBlock;
            pblock2->stake = pblock->GetProofOfStake();
            mapOrphanBlocks.insert(make_pair(hash, pblock2));
            mapOrphanBlocksByPrev.insert(
                make_pair(pblock2->hashPrev, pblock2));
            if (pblock->IsProofOfStake())
            {
                setStakeSeenOrphan.insert(pblock->GetProofOfStake());
            }

            // Ask this guy to fill in what we're missing
            PushGetBlocks(pfrom, pindexBest, GetOrphanRoot(hash));
            // ppcoin: getblocks may not obtain the ancestor block rejected
            // earlier by duplicate-stake check so we ask for it again directly
            if (!IsInitialBlockDownload())
            {
                pfrom->AskFor(CInv(MSG_BLOCK, WantedByOrphan(pblock2)));
            }
        }
        return true;
    }

    // Store to disk
    if (!pblock->AcceptBlock())
    {
        return error("ProcessBlock() : AcceptBlock FAILED");
    }

    // Recursively process any orphan blocks that depended on this one
    vector<uint256> vWorkQueue;
    vWorkQueue.push_back(hash);
    for (unsigned int i = 0; i < vWorkQueue.size(); i++)
    {
        uint256 hashPrev = vWorkQueue[i];
        for (multimap<uint256, COrphanBlock*>::iterator mi =
                 mapOrphanBlocksByPrev.lower_bound(hashPrev);
             mi != mapOrphanBlocksByPrev.upper_bound(hashPrev);
             ++mi)
        {
            CBlock block;
            {
                CDataStream ss(mi->second->vchBlock, SER_DISK, CLIENT_VERSION);
                ss >> block;
            }
            block.BuildMerkleTree();
            if (block.AcceptBlock())
            {
                vWorkQueue.push_back(mi->second->hashBlock);
            }
            mapOrphanBlocks.erase(mi->second->hashBlock);
            setStakeSeenOrphan.erase(block.GetProofOfStake());
            delete mi->second;
        }
        mapOrphanBlocksByPrev.erase(hashPrev);
    }

    printf("ProcessBlock: ACCEPTED\n");

    return true;
}

// novacoin: attempt to generate suitable proof-of-stake
bool CBlock::SignBlock(CWallet& wallet, int64_t nFees[])
{
    // if we are trying to sign
    //    something except proof-of-stake block template
    // it's a template so it won't stay this way
    if (!((vtx[0].vout.size() == 1) && vtx[0].vout[0].IsEmpty()))
    {
        return false;
    }

    // if we are trying to sign
    //    a complete proof-of-stake block
    if (IsProofOfStake())
    {
        return true;
    }

    static int64_t
        nLastCoinStakeSearchTime = GetAdjustedTime();  // startup timestamp

    CKey key;
    // mint and stake could be 2 different colors, so require 2 different
    // transactions mint comes before stake in vtx CTransaction txCoinMint =
    // vtx[0];
    CTransaction txCoinStake;

    // ADVISORY: note static
    static int nNotStakeTimestampMask = ~GetStakeTimestampMask();
    txCoinStake.nTime &= nNotStakeTimestampMask;

    int64_t nSearchTime = vtx[0].nTime = txCoinStake
                                             .nTime;  // search to current time

    if (nSearchTime > nLastCoinStakeSearchTime)
    {
        // randomly pick a color from all staking currencies with weight
        vector<int> vColors;
        for (int i = 1; i < N_COLORS; ++i)
        {
            if (!CanStake(i))
            {
                continue;
            }

            uint64_t nWeight = 0;
            if (!wallet.GetStakeWeightByColor(i, wallet, nWeight))
            {
                continue;
            }
            if (nWeight == 0)
            {
                continue;
            }
            vColors.push_back(i);
        }
        if (vColors.size() == 0)
        {
            return false;
        }
        int idx = rand() % vColors.size();
        int nColor = vColors[idx];

        // TODO: get rid of nSearchInterval
        int64_t nSearchInterval = 1;
        if (wallet.CreateCoinStake(nColor,
                                   wallet,
                                   nBits,
                                   nSearchInterval,
                                   nFees,
                                   vtx[0],
                                   txCoinStake,
                                   key))
        {
            if (txCoinStake.nTime >= pindexBest->GetPastTimeLimit() + 1)
            {
                // make sure coinstake would meet timestamp protocol
                //    as it would be the same as the block timestamp
                vtx[0].nTime = nTime = txCoinStake.nTime;

                // we have to make sure that we have no future timestamps in
                //    our transactions set
                for (vector<CTransaction>::iterator it = vtx.begin();
                     it != vtx.end();)
                {
                    if (it->nTime > nTime)
                    {
                        it = vtx.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }

                // vtx[0] is coinmint tx
                vtx.insert(vtx.begin() + 1, txCoinStake);
                hashMerkleRoot = BuildMerkleTree();

                // append a signature to our block
                return key.Sign(GetHash(), vchBlockSig);
            }
        }
        nLastCoinStakeSearchInterval = nSearchTime - nLastCoinStakeSearchTime;
        nLastCoinStakeSearchTime = nSearchTime;
    }

    return false;
}

ReturnCode CBlock::CheckBlockSignature() const
{
    if (IsProofOfWork())
    {
        if (vchBlockSig.empty())
        {
            return ReturnCode();
        }
        else
        {
            return ReturnCode((int)BlockSigStatus::POW_SIG_NOT_EMPTY,
                              "CheckBlockSignature(): PoW sig not empty");
        }
    }

    if (vchBlockSig.empty())
    {
        return ReturnCode((int)BlockSigStatus::POS_SIG_IS_EMPTY,
                          "CheckBlockSignature(): PoS sig is empty");
    }

    vector<valtype> vSolutions;
    txnouttype whichType;

    if (GetFork(nTime) >= BRK_FORK007)
    {
        const CTxIn& txin = vtx[1].vin[0];
        ScriptSigType scriptSigType = GetScriptSigType(txin.scriptSig);

        if (scriptSigType == ScriptSigType::P2PK)
        {
            CTxDB txdb("r");
            CTransaction txPrev;
            CTxIndex txindex;

            if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
            {
                return ReturnCode(
                            (int)BlockSigStatus::TX_UNREADABLE,
                            strprintf(
                                "CheckBlockSignature(): TSNH "
                                "can't read tx:\n  %s",
                                txin.prevout.hash.GetHex().c_str()));
            }

            if (txPrev.vout.size() < (txin.prevout.n + 1))
            {
                return ReturnCode(
                            (int)BlockSigStatus::TX_PREV_VOUT_TOO_SMALL,
                            strprintf(
                                "CheckBlockSignature(): TSNH tx vout "
                                "smaller than %u:\n  %s",
                                txin.prevout.n + 1,
                                txin.prevout.hash.GetHex().c_str()));
            }

            const CTxOut& prevout = txPrev.vout[txin.prevout.n];

            if (!Solver(prevout.scriptPubKey, whichType, vSolutions))
            {
                return ReturnCode(
                            (int)BlockSigStatus::SOLVER_FAILED,
                            strprintf(
                                "CheckBlockSignature(): TSNH prevout scriptSig "
                                "is P2PK but can't determine "
                                "scriptPubkey\n  %d-%s",
                                txin.prevout.n,
                                txPrev.GetHash().ToString().c_str()));
            }

            if (whichType != TX_PUBKEY)
            {
                return ReturnCode(
                            (int)BlockSigStatus::PREVOUT_NOT_TX_PUBKEY,
                            strprintf(
                                "CheckBlockSignature(): TSNH prevout scriptSig "
                                "is P2PK but prevout isn't TX_PUBKEY\n  %d-%s",
                                txin.prevout.n,
                                txPrev.GetHash().ToString().c_str()));
            }

            const valtype& vchPubKey = vSolutions[0];
            return ReturnCode(
                        CPubKey(vchPubKey).Verify(GetHash(), vchBlockSig)
                            ? (int)BlockSigStatus::SIG_OK
                            : (int)BlockSigStatus::VERIFY_FAILED,
                        CPubKey(vchPubKey).Verify(GetHash(), vchBlockSig)
                            ? ""
                            : "CheckBlockSignature(): P2PK verify failed");
        }
        else if (scriptSigType == ScriptSigType::P2PKH)
        {
            valtype vchPubKey;
            if (GetPubKeyFromP2PKH(txin.scriptSig, vchPubKey))
            {
                bool fOK = CPubKey(vchPubKey).Verify(GetHash(), vchBlockSig);
                return ReturnCode(
                            fOK ? (int)BlockSigStatus::SIG_OK
                                : (int)BlockSigStatus::VERIFY_FAILED,
                            fOK ? ""
                                : "CheckBlockSignature(): P2PKH verify failed");
            }
            else
            {
                return ReturnCode(
                            (int)BlockSigStatus::PUBKEY_EXTRACT_FAILED,
                            strprintf(
                                "CheckBlockSignature(): TSNH "
                                "could not extract pubkey from "
                                "coinstake scriptSig\n  vtx[1].vin[0]-%s",
                                GetHash().ToString().c_str()));
            }
        }
        // fall through — maybe the pubkey is encoded in the unspendable output
    }

    const CScript& script = vtx[1].vout[1].scriptPubKey;

    if (!Solver(script, whichType, vSolutions))
    {
        return ReturnCode((int)BlockSigStatus::SOLVER_FAILED,
                          "CheckBlockSignature(): Solver failed on vout[1]");
    }

    if (whichType == TX_PUBKEY)
    {
        const valtype& vchPubKey = vSolutions[0];
        bool fOK = CPubKey(vchPubKey).Verify(GetHash(), vchBlockSig);
        return ReturnCode(
                    fOK ? (int)BlockSigStatus::SIG_OK
                        : (int)BlockSigStatus::VERIFY_FAILED,
                    fOK ? ""
                        : "CheckBlockSignature(): TX_PUBKEY verify failed");
    }

    // Block signing key encoded in OP_RETURN nonspendable output
    CScript::const_iterator pc = script.begin();
    opcodetype opcode;
    valtype vchPushValue;

    if (!script.GetOp(pc, opcode, vchPushValue))
    {
        return ReturnCode((int)BlockSigStatus::SCRIPT_GETOP_FAILED,
                          "CheckBlockSignature(): GetOp failed (1)");
    }
    if (opcode != OP_RETURN)
    {
        return ReturnCode((int)BlockSigStatus::NOT_OP_RETURN,
                          "CheckBlockSignature(): expected OP_RETURN");
    }
    if (!script.GetOp(pc, opcode, vchPushValue))
    {
        return ReturnCode((int)BlockSigStatus::SCRIPT_GETOP_FAILED,
                          "CheckBlockSignature(): GetOp failed (2)");
    }
    if (!IsCompressedOrUncompressedPubKey(vchPushValue))
    {
        return ReturnCode((int)BlockSigStatus::NOT_COMPRESSED_PUBKEY,
                          "CheckBlockSignature(): not a valid pubkey in OP_RETURN");
    }

    bool fOK = CPubKey(vchPushValue).Verify(GetHash(), vchBlockSig);
    return ReturnCode(
                fOK ? (int)BlockSigStatus::SIG_OK
                    : (int)BlockSigStatus::VERIFY_FAILED,
                fOK ? ""
                    : "CheckBlockSignature(): OP_RETURN pubkey verify failed");
}

bool CheckDiskSpace(uint64_t nAdditionalBytes)
{
    uint64_t nFreeBytesAvailable = boost::filesystem::space(GetDataDir())
                                       .available;

    // Check for nMinDiskSpace bytes (currently 50MB)
    if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
    {
        fShutdown = true;
        string strMessage = _("Warning: Disk space is low!");
        strMiscWarning = strMessage;
        printf("*** %s\n", strMessage.c_str());
        uiInterface.ThreadSafeMessageBox(
            strMessage,
            "Breakout",
            CClientUIInterface::OK | CClientUIInterface::ICON_EXCLAMATION |
                CClientUIInterface::MODAL);
        StartShutdown();
        return false;
    }
    return true;
}

static boost::filesystem::path BlockFilePath(unsigned int nFile)
{
    string strBlockFn = strprintf("blk%04u.dat", nFile);
    return GetDataDir() / strBlockFn;
}

FILE* OpenBlockFile(unsigned int nFile, unsigned int nBlockPos, const char* pszMode)
{
    if ((nFile < 1) || (nFile == (unsigned int) -1))
        return NULL;
    FILE* file = fopen(BlockFilePath(nFile).string().c_str(), pszMode);
    if (!file)
        return NULL;
    if (nBlockPos != 0 && !strchr(pszMode, 'a') && !strchr(pszMode, 'w'))
    {
        if (fseek(file, nBlockPos, SEEK_SET) != 0)
        {
            fclose(file);
            return NULL;
        }
    }
    return file;
}

static unsigned int nCurrentBlockFile = 1;

FILE* AppendBlockFile(unsigned int& nFileRet)
{
    nFileRet = 0;
    while (true)
    {
        FILE* file = OpenBlockFile(nCurrentBlockFile, 0, "ab");
        if (!file)
            return NULL;
        if (fseek(file, 0, SEEK_END) != 0)
            return NULL;
        // FAT32 file size max 4GB, fseek and ftell max 2GB, so we must stay under 2GB
        if (ftell(file) < (long)(0x7F000000 - MAX_SIZE))
        {
            nFileRet = nCurrentBlockFile;
            return file;
        }
        fclose(file);
        nCurrentBlockFile++;
    }
}

bool LoadBlockIndex(bool fAllowNew)
{
    LOCK(cs_main);


    if (fTestNet)
    {
        pchMessageStart[0] = pchMessageStartTestnet[0];
        pchMessageStart[1] = pchMessageStartTestnet[1];
        pchMessageStart[2] = pchMessageStartTestnet[2];
        pchMessageStart[3] = pchMessageStartTestnet[3];
    }

    //
    // Load block index
    //
    CTxDB txdb("cr+");
    if (!txdb.LoadBlockIndex())
    {
        return false;
    }


    // The hash of the currency names is stored in the genesis block
    //       to ensure that the identity of every currency is unambiguous.
    // This way, no one can argue they have the Ace of Spades when in actuality
    //       they have, say, the Three of Clubs.
    // >>> # python
    // >>> import hashlib
    // >>> names = ", ".join(["<none>", "Brostake", "Brocoin", "Atomic",
    //              "Joker",
    //              "Ace of Spades", "Two of Spades", "Three of Spades", "Four of Spades",
    //              "Five of Spades", "Six of Spades", "Seven of Spades", "Eight of Spades",
    //              "Nine of Spades", "Ten of Spades", "Jack of Spades", "Queen of Spades", "King of Spades",
    //              "Ace of Diamonds", "Two of Diamonds", "Three of Diamonds", "Four of Diamonds",
    //              "Five of Diamonds", "Six of Diamonds", "Seven of Diamonds", "Eight of Diamonds",
    //              "Nine of Diamonds", "Ten of Diamonds", "Jack of Diamonds", "Queen of Diamonds", "King of Diamonds",
    //              "Ace of Clubs", "Two of Clubs", "Three of Clubs", "Four of Clubs",
    //              "Five of Clubs", "Six of Clubs", "Seven of Clubs", "Eight of Clubs",
    //              "Nine of Clubs", "Ten of Clubs", "Jack of Clubs", "Queen of Clubs", "King of Clubs",
    //              "Ace of Hearts", "Two of Hearts", "Three of Hearts", "Four of Hearts",
    //              "Five of Hearts", "Six of Hearts", "Seven of Hearts", "Eight of Hearts",
    //              "Nine of Hearts", "Ten of Hearts", "Jack of Hearts", "Queen of Hearts", "King of Hearts"])
    // >>> hashlib.sha256(names).hexdigest()
    // 'f4be9677f3caaa8a9a1f9e58a0f9a80dd9fd9f224455714c414f4963848e0b9b"


    //
    // Init with genesis block
    //
    if (mapBlockIndex.empty())
    {
        printf("Block index empty: creating genesis.\n");
        if (!fAllowNew)
        {
            return false;
        }

        uint256 hashNames("0xf4be9677f3caaa8a9a1f9e58a0f9a80dd9fd9f224455714c414f4963848e0b9b");

        const char* pszTimestamp = "Obama Backs Clinton and Urges Party to Come Together";
        CTransaction txNew;
        txNew.nVersion = 1;
        txNew.nTime = BRK_GENESIS_TIME;
        txNew.vin.resize(1);
        txNew.vout.resize(1);
        txNew.strTxComment = hashNames.ToString();
        txNew.vin[0].scriptSig = CScript() << 0 << hashNames << CBigNum(nLaunchTime) << valtype((const unsigned char*)pszTimestamp, (const unsigned char*)pszTimestamp + strlen(pszTimestamp));
        txNew.vout[0].SetEmpty();
        txNew.SetColor((int) BREAKOUT_COLOR_NONE);
        CBlock block;
        block.vtx.push_back(txNew);
        block.hashPrevBlock = 0;
        block.hashMerkleRoot = block.BuildMerkleTree();
        block.nVersion = 1;
        block.nTime    = BRK_GENESIS_TIME;
        block.nBits    = GetTargetLimit(false, BRK_GENESIS_TIME).GetCompact();
        block.nNonce   = 64912865;
        if(fTestNet)
        {
            block.nNonce   = 14069277;
        }
        uint256 hashTarget;
        if (false && (block.GetHash() !=
                             (fTestNet ? hashGenesisBlockTestNet : hashGenesisBlock)))
        {
            printf("========== Finding Genesis Nonce ==========\n");
            hashTarget = CBigNum().SetCompact(block.nBits).getuint256();
            printf("hashTarget == %s\n", hashTarget.ToString().c_str());
            uint256 hashLowest = ~uint256(0);
            uint64_t nHashes = 0;
            uint256 hashBlock = block.GetHash();
            while (hashBlock > hashTarget)
            {
                ++block.nNonce;
                if (block.nNonce == 0)
                {
                    printf("NONCE WRAPPED, incrementing time");
                    ++block.nTime;
                }
                hashBlock = block.GetHash();
                if (hashBlock < hashLowest)
                {
                    hashLowest = hashBlock;
                }
                ++nHashes;
                if ((nHashes > 0) && (nHashes % 1000000 == 0))
                {
                    printf("%" PRIu64 " M Hashes Tried\n  Lowest: %s\n",
                           nHashes / 1000000,
                           hashLowest.ToString().c_str());
                }
            }
        }
        block.print();
        printf("========== Genesis Block Details ==========\n");
        printf("hashTarget == %s\n", hashTarget.ToString().c_str());
        printf("block.GetHash() == %s\n", block.GetHash().ToString().c_str());
        printf("block.hashMerkleRoot == %s\n", block.hashMerkleRoot.ToString().c_str());
        printf("block.nTime = %u \n", block.nTime);
        printf("block.nNonce = %u \n", block.nNonce);
        printf("===========================================\n");

        //// debug print
        assert(block.hashMerkleRoot ==
          uint256("0xc85aebf34987332a800386aa169a761b1e4042bfdf2554e6633dbb35a8d4a9f9"));
        assert(block.GetHash() == (!fTestNet ? hashGenesisBlock : hashGenesisBlockTestNet));
        assert(block.CheckBlock());

        // Start new block file
        unsigned int nFile;
        unsigned int nBlockPos;
        if (!block.WriteToDisk(nFile, nBlockPos))
        {
            return error("LoadBlockIndex() : writing genesis block to disk failed");
        }
        if (!block.AddToBlockIndex(nFile, nBlockPos, hashGenesisBlock))
        {
            return error("LoadBlockIndex() : genesis block not accepted");
        }
    mapBlockLookup[0] = mapBlockIndex[block.GetHash()];
    }

    string strPubKey = "";

    return true;
}

void PrintBlockTree()
{
    AssertLockHeld(cs_main);
    // pre-compute tree structure
    map<CBlockIndex*, vector<CBlockIndex*> > mapNext;
    for (map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.begin(); mi != mapBlockIndex.end(); ++mi)
    {
        CBlockIndex* pindex = (*mi).second;
        mapNext[pindex->pprev].push_back(pindex);
        // test
        //while (rand() % 3 == 0)
        //    mapNext[pindex->pprev].push_back(pindex);
    }

    vector<pair<int, CBlockIndex*> > vStack;
    vStack.push_back(make_pair(0, pindexGenesisBlock));

    int nPrevCol = 0;
    while (!vStack.empty())
    {
        int nCol = vStack.back().first;
        CBlockIndex* pindex = vStack.back().second;
        vStack.pop_back();

        // print split or gap
        if (nCol > nPrevCol)
        {
            for (int i = 0; i < nCol-1; i++)
                printf("| ");
            printf("|\\\n");
        }
        else if (nCol < nPrevCol)
        {
            for (int i = 0; i < nCol; i++)
                printf("| ");
            printf("|\n");
       }
        nPrevCol = nCol;

        // print columns
        for (int i = 0; i < nCol; i++)
            printf("| ");

        // print item
        CBlock block;
        block.ReadFromDisk(pindex);
        // does not print all potential fees
        printf("%d (%u,%u) %s  %08x  %s  coinbase %s  tx %" PRIszu "",
               pindex->nHeight,
               pindex->nFile,
               pindex->nBlockPos,
               block.GetHash().ToString().c_str(),
               block.nBits,
               DateTimeStrFormat("%x %H:%M:%S", block.GetBlockTime()).c_str(),
               FormatMoney(pindex->vCoinbase[pindex->nCoinbaseColor],
                           pindex->nCoinbaseColor).c_str(),
               block.vtx.size());

        PrintWallets(block);

        // put the main time-chain first
        vector<CBlockIndex*>& vNext = mapNext[pindex];
        for (unsigned int i = 0; i < vNext.size(); i++)
        {
            if (vNext[i]->pnext)
            {
                swap(vNext[0], vNext[i]);
                break;
            }
        }

        // iterate children
        for (unsigned int i = 0; i < vNext.size(); i++)
            vStack.push_back(make_pair(nCol+i, vNext[i]));
    }
}

bool LoadExternalBlockFile(FILE* fileIn)
{
    int64_t nStart = GetTimeMillis();

    int nLoaded = 0;
    {
        LOCK(cs_main);
        try {
            CAutoFile blkdat(fileIn, SER_DISK, CLIENT_VERSION);
            unsigned int nPos = 0;
            while (nPos != (unsigned int)-1 && blkdat.good() && !fRequestShutdown)
            {
                unsigned char pchData[65536];
                do {
                    fseek(blkdat, nPos, SEEK_SET);
                    int nRead = fread(pchData, 1, sizeof(pchData), blkdat);
                    if (nRead <= 8)
                    {
                        nPos = (unsigned int)-1;
                        break;
                    }
                    void* nFind = memchr(pchData, pchMessageStart[0], nRead+1-sizeof(pchMessageStart));
                    if (nFind)
                    {
                        if (memcmp(nFind, pchMessageStart, sizeof(pchMessageStart))==0)
                        {
                            nPos += ((unsigned char*)nFind - pchData) + sizeof(pchMessageStart);
                            break;
                        }
                        nPos += ((unsigned char*)nFind - pchData) + 1;
                    }
                    else
                        nPos += sizeof(pchData) - sizeof(pchMessageStart) + 1;
                } while(!fRequestShutdown);
                if (nPos == (unsigned int)-1)
                    break;
                fseek(blkdat, nPos, SEEK_SET);
                unsigned int nSize;
                blkdat >> nSize;
                if (nSize > 0 && nSize <= MAX_BLOCK_SIZE)
                {
                    CBlock block;
                    blkdat >> block;
                    bool fOrphan;
                    if (ProcessBlock(NULL, &block, fOrphan, true))
                    {
                        nLoaded++;
                        nPos += 4 + nSize;
                    }
                    if ((nMaxHeight > 0) && (nBestHeight >= nMaxHeight))
                    {
                        break;
                    }
                }
            }
        }
        catch (std::exception &e) {
            printf("%s() : Deserialize or I/O error caught during load\n",
                   __PRETTY_FUNCTION__);
        }
    }
    printf("Loaded %i blocks from external file in %" PRId64 "ms\n", nLoaded, GetTimeMillis() - nStart);
    return nLoaded > 0;
}

struct CImportingNow
{
    CImportingNow() {
        assert(fImporting == false);
        fImporting = true;
    }

    ~CImportingNow() {
        assert(fImporting == true);
        fImporting = false;
    }
};

void ThreadImport(vector<boost::filesystem::path> vImportFiles)
{
    RenameThread("blackcoin-loadblk");

    CImportingNow imp;

    // -loadblock=
    BOOST_FOREACH(boost::filesystem::path &path, vImportFiles) {
        FILE *file = fopen(path.string().c_str(), "rb");
        if (file)
            LoadExternalBlockFile(file);
    }

    // hardcoded $DATADIR/bootstrap.dat
    boost::filesystem::path pathBootstrap = GetDataDir() / "bootstrap.dat";
    if (boost::filesystem::exists(pathBootstrap)) {
        FILE *file = fopen(pathBootstrap.string().c_str(), "rb");
        if (file) {
            boost::filesystem::path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
            LoadExternalBlockFile(file);
            RenameOver(pathBootstrap, pathBootstrapOld);
        }
    }
}


//////////////////////////////////////////////////////////////////////////////
//
// CAlert
//

extern map<uint256, CAlert> mapAlerts;
extern CCriticalSection cs_mapAlerts;

string GetWarnings(string strFor, int nAlertType)
{
    int nPriority = 0;
    string strStatusBar;
    string strRPC;

    if (GetBoolArg("-testsafemode"))
        strRPC = "test";

    // Misc warnings like out of disk space and clock is wrong
    if (strMiscWarning != "")
    {
        nPriority = 1000;
        strStatusBar = strMiscWarning;
    }

    // Alerts
    {
        LOCK(cs_mapAlerts);
        BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
        {
            const CAlert& alert = item.second;
            if (alert.AppliesToMe() && (alert.nPriority > nPriority) && (alert.nAlertType == nAlertType))
            {
                nPriority = alert.nPriority;
                strStatusBar = alert.strStatusBar;
                if (nPriority > 1000)
                    strRPC = strStatusBar;
            }
        }
    }

    if (strFor == "statusbar")
        return strStatusBar;
    else if (strFor == "rpc")
        return strRPC;
    assert(!"GetWarnings() : invalid parameter");
    return "error";
}








//////////////////////////////////////////////////////////////////////////////
//
// Messages
//


bool static AlreadyHave(CTxDB& txdb, const CInv& inv)
{
    switch (inv.type)
    {
    case MSG_TX:
        {
        bool txInMap = false;
            {
            LOCK(mempool.cs);
            txInMap = (mempool.exists(inv.hash));
            }
        return txInMap ||
               mapOrphanTransactions.count(inv.hash) ||
               txdb.ContainsTx(inv.hash);
        }

    case MSG_BLOCK:
        return mapBlockIndex.count(inv.hash) ||
               mapOrphanBlocks.count(inv.hash);
    }
    // Don't know what it is, just say we already got one
    return true;
}


bool static ProcessMessage(CNode* pfrom, string strCommand, CDataStream& vRecv)
{
    unsigned int nStakeMinAge = GetStakeMinAge(pindexBest->nTime);
    static map<CService, CPubKey> mapReuseKey;
    RandAddSeedPerfmon();
    if (fDebug)
        printf("received: %s (%" PRIszu " bytes)\n", strCommand.c_str(), vRecv.size());
    if (mapArgs.count("-dropmessagestest") && GetRand(atoi(mapArgs["-dropmessagestest"])) == 0)
    {
        printf("dropmessagestest DROPPING RECV MESSAGE\n");
        return true;
    }

    if (strCommand == "version")
    {
        // Each connection can only send one version message
        if (pfrom->nVersion != 0)
        {
            pfrom->Misbehaving(1);
            return false;
        }

        int64_t nTime;
        CAddress addrMe;
        CAddress addrFrom;
        uint64_t nNonce = 1;
        vRecv >> pfrom->nVersion >> pfrom->nServices >> nTime >> addrMe;
        if (pfrom->nVersion < GetMinPeerProtoVersion(GetAdjustedTime()))
        {
            // Since February 20, 2012, the protocol is initiated at version 209,
            // and earlier versions are no longer supported
            printf("partner %s using obsolete version %i; disconnecting\n", pfrom->addr.ToString().c_str(), pfrom->nVersion);
            pfrom->fDisconnect = true;
            return false;
        }

        if (pfrom->nVersion == 10300)
            pfrom->nVersion = 300;
        if (!vRecv.empty())
            vRecv >> addrFrom >> nNonce;
        if (!vRecv.empty())
            vRecv >> pfrom->strSubVer;
        if (!vRecv.empty())
            vRecv >> pfrom->nStartingHeight;

        if (pfrom->fInbound && addrMe.IsRoutable())
        {
            pfrom->addrLocal = addrMe;
            SeenLocal(addrMe);
        }

        // Disconnect if we connected to ourself
        if (nNonce == nLocalHostNonce && nNonce > 1)
        {
            printf("connected to self at %s, disconnecting\n", pfrom->addr.ToString().c_str());
            pfrom->fDisconnect = true;
            return true;
        }

        // record my external IP reported by peer
        if (addrFrom.IsRoutable() && addrMe.IsRoutable())
            addrSeenByPeer = addrMe;

        // Be shy and don't send version until we hear
        if (pfrom->fInbound)
            pfrom->PushVersion();

        pfrom->fClient = !(pfrom->nServices & NODE_NETWORK);

        if (GetBoolArg("-synctime", true))
            AddTimeData(pfrom->addr, nTime);

        // Change version
        pfrom->PushMessage("verack");
        pfrom->vSend.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));

        if (!pfrom->fInbound)
        {
            // Advertise our address
            if (!fNoListen && !IsInitialBlockDownload())
            {
                CAddress addr = GetLocalAddress(&pfrom->addr);
                if (addr.IsRoutable())
                {
                    pfrom->PushAddress(addr);
                } else if (IsPeerAddrLocalGood(pfrom)) {
                    addr.SetIP(pfrom->addrLocal);
                    pfrom->PushAddress(addr);
                }
            }

            // Get recent addresses
            if (pfrom->fOneShot || pfrom->nVersion >= CADDR_TIME_VERSION || addrman.size() < 1000)
            {
                pfrom->PushMessage("getaddr");
                pfrom->fGetAddr = true;
            }
            addrman.Good(pfrom->addr);
        } else {
            if (((CNetAddr)pfrom->addr) == (CNetAddr)addrFrom)
            {
                addrman.Add(addrFrom, addrFrom);
                addrman.Good(addrFrom);
            }
        }

        // Ask the first connected node for block updates
        static int nAskedForBlocks = 0;
        if (!pfrom->fClient && !pfrom->fOneShot &&
            (pfrom->nStartingHeight > (nBestHeight - 144)) &&
            (pfrom->nVersion < NOBLKS_VERSION_START ||
             pfrom->nVersion >= NOBLKS_VERSION_END) &&
             (nAskedForBlocks < 1 || GetConnectionCount() <= 1u))
        {
            nAskedForBlocks++;
            pfrom->PushGetBlocks(pindexBest, uint256(0));
        }

        // Relay alerts
        {
            LOCK(cs_mapAlerts);
            BOOST_FOREACH(PAIRTYPE(const uint256, CAlert)& item, mapAlerts)
                item.second.RelayTo(pfrom);
        }

        pfrom->fSuccessfullyConnected = true;

        printf("receive version message: "
                 "version %d, blocks=%d, us=%s, them=%s, peer=%s\n",
               pfrom->nVersion,
               pfrom->nStartingHeight,
               addrMe.ToString().c_str(),
               addrFrom.ToString().c_str(),
               pfrom->addr.ToString().c_str());

        cPeerBlockCounts.input(pfrom->nStartingHeight);
    }


    else if (pfrom->nVersion == 0)
    {
        // Must have a version message before anything else
        pfrom->Misbehaving(1);
        return false;
    }


    else if (strCommand == "verack")
    {
        pfrom->vRecv.SetVersion(min(pfrom->nVersion, PROTOCOL_VERSION));
    }


    else if (strCommand == "addr")
    {
        vector<CAddress> vAddr;
        vRecv >> vAddr;

        // Don't want addr from older versions unless seeding
        if (pfrom->nVersion < CADDR_TIME_VERSION && addrman.size() > 1000)
            return true;
        if (vAddr.size() > 1000)
        {
            pfrom->Misbehaving(20);
            return error("message addr size() = %" PRIszu "", vAddr.size());
        }

        // Store the new addresses
        vector<CAddress> vAddrOk;
        int64_t nNow = GetAdjustedTime();
        int64_t nSince = nNow - 10 * 60;
        BOOST_FOREACH(CAddress& addr, vAddr)
        {
            if (fShutdown)
                return true;
            if (addr.nTime <= 100000000 || addr.nTime > nNow + 10 * 60)
                addr.nTime = nNow - 5 * 24 * 60 * 60;
            pfrom->AddAddressKnown(addr);
            bool fReachable = IsReachable(addr);
            if (addr.nTime > nSince && !pfrom->fGetAddr && vAddr.size() <= 10 && addr.IsRoutable())
            {
                // Relay to a limited number of other nodes
                {
                    LOCK(cs_vNodes);
                    // Use deterministic randomness to send to the same nodes for 24 hours
                    // at a time so the setAddrKnowns of the chosen nodes prevent repeats
                    static uint256 hashSalt;
                    if (hashSalt == 0)
                        hashSalt = GetRandHash();
                    uint64_t hashAddr = addr.GetHash();
                    uint256 hashRand = hashSalt ^ (hashAddr<<32) ^ ((GetTime()+hashAddr)/(24*60*60));
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    multimap<uint256, CNode*> mapMix;
                    BOOST_FOREACH(CNode* pnode, vNodes)
                    {
                        if (pnode->nVersion < CADDR_TIME_VERSION)
                            continue;
                        unsigned int nPointer;
                        memcpy(&nPointer, &pnode, sizeof(nPointer));
                        uint256 hashKey = hashRand ^ nPointer;
                        hashKey = Hash(BEGIN(hashKey), END(hashKey));
                        mapMix.insert(make_pair(hashKey, pnode));
                    }
                    int nRelayNodes = fReachable ? 2 : 1; // limited relaying of addresses outside our network(s)
                    for (multimap<uint256, CNode*>::iterator mi = mapMix.begin(); mi != mapMix.end() && nRelayNodes-- > 0; ++mi)
                        ((*mi).second)->PushAddress(addr);
                }
            }
            // Do not store addresses outside our network
            if (fReachable)
                vAddrOk.push_back(addr);
        }
        addrman.Add(vAddrOk, pfrom->addr, 2 * 60 * 60);
        if (vAddr.size() < 1000)
            pfrom->fGetAddr = false;
        if (pfrom->fOneShot)
            pfrom->fDisconnect = true;
    }

    else if (strCommand == "inv")
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            pfrom->Misbehaving(20);
            return error("message inv size() = %" PRIszu "", vInv.size());
        }

        // find last block in inv vector
        unsigned int nLastBlock = (unsigned int)(-1);
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++) {
            if (vInv[vInv.size() - 1 - nInv].type == MSG_BLOCK) {
                nLastBlock = vInv.size() - 1 - nInv;
                break;
            }
        }
        CTxDB txdb("r");
        for (unsigned int nInv = 0; nInv < vInv.size(); nInv++)
        {
            const CInv &inv = vInv[nInv];

            if (fShutdown)
                return true;
            pfrom->AddInventoryKnown(inv);

            bool fAlreadyHave = AlreadyHave(txdb, inv);
            if (fDebug)
                printf("  got inventory: %s  %s\n", inv.ToString().c_str(), fAlreadyHave ? "have" : "new");

            if (!fAlreadyHave)
                pfrom->AskFor(inv);
            else if (inv.type == MSG_BLOCK && mapOrphanBlocks.count(inv.hash)) {
                pfrom->PushGetBlocks(pindexBest, GetOrphanRoot(inv.hash));
            } else if (nInv == nLastBlock) {
                // In case we are on a very long side-chain, it is possible that we already have
                // the last block in an inv bundle sent in response to getblocks. Try to detect
                // this situation and push another getblocks to continue.
                pfrom->PushGetBlocks(mapBlockIndex[inv.hash], uint256(0));
                if (fDebug)
                    printf("force request: %s\n", inv.ToString().c_str());
            }

            // Track requests for our stuff
            Inventory(inv.hash);
        }
    }


    else if (strCommand == "getdata")
    {
        vector<CInv> vInv;
        vRecv >> vInv;
        if (vInv.size() > MAX_INV_SZ)
        {
            pfrom->Misbehaving(20);
            return error("message getdata size() = %" PRIszu "", vInv.size());
        }

        if (fDebugNet || (vInv.size() != 1))
            printf("received getdata (%" PRIszu " invsz)\n", vInv.size());

        BOOST_FOREACH(const CInv& inv, vInv)
        {
            if (fShutdown)
                return true;
            if (fDebugNet || (vInv.size() == 1))
                printf("received getdata for: %s\n", inv.ToString().c_str());

            if (inv.type == MSG_BLOCK)
            {
                // Send block from disk
                map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(inv.hash);
                if (mi != mapBlockIndex.end())
                {
                    CBlock block;
                    block.ReadFromDisk((*mi).second);
                    pfrom->PushMessage("block", block);

                    // Trigger them to send a getblocks request for the next batch of inventory
                    if (inv.hash == pfrom->hashContinue)
                    {
                        // ppcoin: send latest proof-of-work block to allow the
                        // download node to accept as orphan (proof-of-stake
                        // block might be rejected by stake connection check)
                        vector<CInv> vInv;
                        vInv.push_back(CInv(MSG_BLOCK, GetLastBlockIndex(pindexBest, false)->GetBlockHash()));
                        pfrom->PushMessage("inv", vInv);
                        pfrom->hashContinue = 0;
                    }
                }
            }
            else if (inv.IsKnownType())
            {
                // Send stream from relay memory
                bool pushed = false;
                {
                    LOCK(cs_mapRelay);
                    map<CInv, CDataStream>::iterator mi = mapRelay.find(inv);
                    if (mi != mapRelay.end()) {
                        pfrom->PushMessage(inv.GetCommand(), (*mi).second);
                        pushed = true;
                    }
                }
                if (!pushed && inv.type == MSG_TX) {
                    LOCK(mempool.cs);
                    if (mempool.exists(inv.hash)) {
                        CTransaction tx = mempool.lookup(inv.hash);
                        CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
                        ss.reserve(1000);
                        ss << tx;
                        pfrom->PushMessage("tx", ss);
                    }
                }
            }

            // Track requests for our stuff
            Inventory(inv.hash);
        }
    }


    else if (strCommand == "getblocks")
    {
        if (pfrom->nVersion < GetMinPeerProtoVersion(GetAdjustedTime()))
        {
            printf("partner %s using obsolete version %i; disconnecting\n", pfrom->addr.ToString().c_str(), pfrom->nVersion);
            pfrom->fDisconnect = true;
            return false;
        }

        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        // Find the last block the caller has in the main chain
        CBlockIndex* pindex = locator.GetBlockIndex();

        // Send the rest of the chain
        if (pindex)
            pindex = pindex->pnext;
        int nLimit = GETBLOCKS_LIMIT;
        printf("getblocks %d to %s limit %d, node: %s\n",
                  (pindex ? pindex->nHeight : -1), hashStop.ToString().substr(0,20).c_str(), nLimit,
                  pfrom->addr.ToString().c_str());
        for (; pindex; pindex = pindex->pnext)
        {
            if (pindex->GetBlockHash() == hashStop)
            {
                printf("  getblocks stopping at %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString().substr(0,20).c_str());
                // ppcoin: tell downloading node about the latest block if it's
                // without risk being rejected due to stake connection check
                if (hashStop != hashBestChain && pindex->GetBlockTime() + nStakeMinAge > pindexBest->GetBlockTime())
                    pfrom->PushInventory(CInv(MSG_BLOCK, hashBestChain));
                break;
            }
            pfrom->PushInventory(CInv(MSG_BLOCK, pindex->GetBlockHash()));
            if (--nLimit <= 0)
            {
                // When this block is requested, we'll send an inv that'll make them
                // getblocks the next batch of inventory.
                printf("  getblocks stopping at limit %d %s\n", pindex->nHeight, pindex->GetBlockHash().ToString().substr(0,20).c_str());
                pfrom->hashContinue = pindex->GetBlockHash();
                break;
            }
        }
    }

    else if (strCommand == "checkpoint")
    {
        printf("checkpoint command: no more sync checkpoints");
    }

    else if (strCommand == "getheaders")
    {
        CBlockLocator locator;
        uint256 hashStop;
        vRecv >> locator >> hashStop;

        CBlockIndex* pindex = NULL;
        if (locator.IsNull())
        {
            // If locator is null, return the hashStop block
            map<uint256, CBlockIndex*>::iterator mi = mapBlockIndex.find(hashStop);
            if (mi == mapBlockIndex.end())
                return true;
            pindex = (*mi).second;
        }
        else
        {
            // Find the last block the caller has in the main chain
            pindex = locator.GetBlockIndex();
            if (pindex)
                pindex = pindex->pnext;
        }

        vector<CBlock> vHeaders;
        int nLimit = 2000;
        printf("getheaders %d to %s\n", (pindex ? pindex->nHeight : -1), hashStop.ToString().c_str());
        for (; pindex; pindex = pindex->pnext)
        {
            vHeaders.push_back(pindex->GetBlockHeader());
            if (--nLimit <= 0 || pindex->GetBlockHash() == hashStop)
                break;
        }
        pfrom->PushMessage("headers", vHeaders);
    }

    else if (strCommand == "tx")
    {
        vector<uint256> vWorkQueue;
        vector<uint256> vEraseQueue;
        CDataStream vMsg(vRecv);
        CTxDB txdb("r");
        CTransaction tx;
        vRecv >> tx;

        CInv inv(MSG_TX, tx.GetHash());
        pfrom->AddInventoryKnown(inv);

        bool fMissingInputs = false;
        if (tx.AcceptToMemoryPool(txdb, true, &fMissingInputs))
        {
            SyncWithWallets(tx, NULL, true);

            CWalletTx wtx(pwalletMain, tx);
            bool fUnconfirmed = (wtx.GetDepthInMainChain() <= 0);
            if (fUnconfirmed)
            {
                ColorsMap mapReceived;
                ColorsMap mapSent;
                wtx.GetAmounts(false, mapReceived, mapSent);
                pwalletMain->mapReceived.Add(mapReceived);
                pwalletMain->mapSent.Add(mapSent);
            }

            RelayTransaction(tx, inv.hash);
            mapAlreadyAskedFor.erase(inv);
            vWorkQueue.push_back(inv.hash);
            vEraseQueue.push_back(inv.hash);

            // Recursively process any orphan transactions that depended on this one
            for (unsigned int i = 0; i < vWorkQueue.size(); i++)
            {
                uint256 hashPrev = vWorkQueue[i];
                for (set<uint256>::iterator mi = mapOrphanTransactionsByPrev[hashPrev].begin();
                     mi != mapOrphanTransactionsByPrev[hashPrev].end();
                     ++mi)
                {
                    const uint256& orphanTxHash = *mi;
                    CTransaction& orphanTx = mapOrphanTransactions[orphanTxHash];
                    bool fMissingInputs2 = false;

                    if (orphanTx.AcceptToMemoryPool(txdb, true, &fMissingInputs2))
                    {
                        printf("   accepted orphan tx %s\n", orphanTxHash.ToString().substr(0,10).c_str());
                        SyncWithWallets(tx, NULL, true);
                        RelayTransaction(orphanTx, orphanTxHash);
                        mapAlreadyAskedFor.erase(CInv(MSG_TX, orphanTxHash));
                        vWorkQueue.push_back(orphanTxHash);
                        vEraseQueue.push_back(orphanTxHash);
                    }
                    else if (!fMissingInputs2)
                    {
                        // invalid orphan
                        vEraseQueue.push_back(orphanTxHash);
                        printf("   removed invalid orphan tx %s\n", orphanTxHash.ToString().substr(0,10).c_str());
                    }
                }
            }

            BOOST_FOREACH(uint256 hash, vEraseQueue)
                EraseOrphanTx(hash);
        }
        else if (fMissingInputs)
        {
            AddOrphanTx(tx);

            // DoS prevention: do not allow mapOrphanTransactions to grow unbounded
            unsigned int nEvicted = LimitOrphanTxSize(MAX_ORPHAN_TRANSACTIONS);
            if (nEvicted > 0)
                printf("mapOrphan overflow, removed %u tx\n", nEvicted);
        }
        if (tx.nDoS) pfrom->Misbehaving(tx.nDoS);
    }


    else if ((strCommand == "block") &&
             ((nMaxHeight <= 0) || (nBestHeight < nMaxHeight)))
    {
        CBlock block;
        vRecv >> block;
        uint256 hashBlock = block.GetHash();

        printf("received block %s\n", hashBlock.ToString().c_str());
        // block.print();

        CInv inv(MSG_BLOCK, hashBlock);
        pfrom->AddInventoryKnown(inv);
        bool fOrphan;
        if (ProcessBlock(pfrom, &block, fOrphan))
        {
            mapAlreadyAskedFor.erase(inv);
            // orphaned blocks will trigger their own getblocks request
            if (!fOrphan &&
                (nBestHeight < pfrom->nStartingHeight) &&
                (nBestHeight >= (pfrom->pindexLastGetBlocksBegin->nHeight +
                                 GETBLOCKS_LIMIT)))
            {
                PushGetBlocks(pfrom, pindexBest, uint256(0));
            }
        }
        if (block.nDoS) pfrom->Misbehaving(block.nDoS);
    }


    // This asymmetric behavior for inbound and outbound connections was introduced
    // to prevent a fingerprinting attack: an attacker can send specific fake addresses
    // to users' AddrMan and later request them by sending getaddr messages.
    // Making users (which are behind NAT and can only make outgoing connections) ignore
    // getaddr message mitigates the attack.
    else if ((strCommand == "getaddr") && (pfrom->fInbound))
    {
        // Don't return addresses older than nCutOff timestamp
        int64_t nCutOff = GetTime() - (nNodeLifespan * 24 * 60 * 60);
        pfrom->vAddrToSend.clear();
        vector<CAddress> vAddr = addrman.GetAddr();
        BOOST_FOREACH(const CAddress &addr, vAddr)
            if(addr.nTime > nCutOff)
                pfrom->PushAddress(addr);
    }


    else if (strCommand == "mempool")
    {
        vector<uint256> vtxid;
        mempool.queryHashes(vtxid);
        vector<CInv> vInv;
        for (unsigned int i = 0; i < vtxid.size(); i++) {
            CInv inv(MSG_TX, vtxid[i]);
            vInv.push_back(inv);
            if (i == (MAX_INV_SZ - 1))
                    break;
        }
        if (vInv.size() > 0)
            pfrom->PushMessage("inv", vInv);
    }


    else if (strCommand == "checkorder")
    {
        uint256 hashReply;
        vRecv >> hashReply;

        if (!GetBoolArg("-allowreceivebyip"))
        {
            pfrom->PushMessage("reply", hashReply, (int)2, string(""));
            return true;
        }

        CWalletTx order;
        vRecv >> order;

        /// we have a chance to check the order here

        // Keep giving the same key to the same ip until they use it
        if (!mapReuseKey.count(pfrom->addr))
            pwalletMain->GetKeyFromPool(mapReuseKey[pfrom->addr], true);

        // Send back approval of order and pubkey to use
        CScript scriptPubKey;
        scriptPubKey << mapReuseKey[pfrom->addr] << OP_CHECKSIG;
        pfrom->PushMessage("reply", hashReply, (int)0, scriptPubKey);
    }


    else if (strCommand == "reply")
    {
        uint256 hashReply;
        vRecv >> hashReply;

        CRequestTracker tracker;
        {
            LOCK(pfrom->cs_mapRequests);
            map<uint256, CRequestTracker>::iterator mi = pfrom->mapRequests.find(hashReply);
            if (mi != pfrom->mapRequests.end())
            {
                tracker = (*mi).second;
                pfrom->mapRequests.erase(mi);
            }
        }
        if (!tracker.IsNull())
            tracker.fn(tracker.param1, vRecv);
    }


    else if (strCommand == "ping")
    {
        if (pfrom->nVersion > BIP0031_VERSION)
        {
            uint64_t nonce = 0;
            vRecv >> nonce;
            // Echo the message back with the nonce. This allows for two useful features:
            //
            // 1) A remote node can quickly check if the connection is operational
            // 2) Remote nodes can measure the latency of the network thread. If this node
            //    is overloaded it won't respond to pings quickly and the remote node can
            //    avoid sending us more work, like chain download requests.
            //
            // The nonce stops the remote getting confused between different pings: without
            // it, if the remote node sends a ping once per second and this node takes 5
            // seconds to respond to each, the 5th ping the remote sends would appear to
            // return very quickly.
            pfrom->PushMessage("pong", nonce);
        }
    }

    else if (strCommand == "alert")
    {
        CAlert alert;
        vRecv >> alert;

        // will not be necessary after ALERT2_TIME
        if (strCommand == "alert")
        {
             alert.nAlertType = (int) ALERT_CLASSIC;
        }

        uint256 alertHash = alert.GetHash();
        // okay to share setKnown because all hashes will be unique
        if (pfrom->setKnown.count(alertHash) == 0)
        {
            if (alert.ProcessAlert())
            {
                // Relay
                pfrom->setKnown.insert(alertHash);
                {
                    LOCK(cs_vNodes);
                    BOOST_FOREACH(CNode* pnode, vNodes)
                        alert.RelayTo(pnode);
                }
            }
            else {
                // Small DoS penalty so peers that send us lots of
                // duplicate/expired/invalid-signature/whatever alerts
                // eventually get banned.
                // This isn't a Misbehaving(100) (immediate ban) because the
                // peer might be an older or different implementation with
                // a different signature key, etc.
                pfrom->Misbehaving(10);
            }
        }
    }

    else
    {
        // Ignore unknown commands for extensibility
    }


    // Update the last seen time for this node's address
    if (pfrom->fNetworkNode)
        if (strCommand == "version" || strCommand == "addr" || strCommand == "inv" || strCommand == "getdata" || strCommand == "ping")
            AddressCurrentlyConnected(pfrom->addr);


    return true;
}

bool ProcessMessages(CNode* pfrom)
{
    CDataStream& vRecv = pfrom->vRecv;
    if (vRecv.empty())
        return true;
    //if (fDebug)
    //    printf("ProcessMessages(%u bytes)\n", vRecv.size());

    //
    // Message format
    //  (4) message start
    //  (12) command
    //  (4) size
    //  (4) checksum
    //  (x) data
    //

    while (true)
    {
        // Don't bother if send buffer is too full to respond anyway
        if (pfrom->vSend.size() >= SendBufferSize())
            break;

        // Scan for message start
        CDataStream::iterator pstart = search(vRecv.begin(), vRecv.end(), BEGIN(pchMessageStart), END(pchMessageStart));
        int nHeaderSize = vRecv.GetSerializeSize(CMessageHeader());
        if (vRecv.end() - pstart < nHeaderSize)
        {
            if ((int)vRecv.size() > nHeaderSize)
            {
                printf("\n\nPROCESSMESSAGE MESSAGESTART NOT FOUND\n\n");
                vRecv.erase(vRecv.begin(), vRecv.end() - nHeaderSize);
            }
            break;
        }
        if (pstart - vRecv.begin() > 0)
            printf("\n\nPROCESSMESSAGE SKIPPED %" PRIpdd " BYTES\n\n", pstart - vRecv.begin());
        vRecv.erase(vRecv.begin(), pstart);

        // Read header
        vector<char> vHeaderSave(vRecv.begin(), vRecv.begin() + nHeaderSize);
        CMessageHeader hdr;
        vRecv >> hdr;
        if (!hdr.IsValid())
        {
            printf("\n\nPROCESSMESSAGE: ERRORS IN HEADER %s\n\n\n", hdr.GetCommand().c_str());
            continue;
        }
        string strCommand = hdr.GetCommand();

        // Message size
        unsigned int nMessageSize = hdr.nMessageSize;
        if (nMessageSize > MAX_SIZE)
        {
            printf("ProcessMessages(%s, %u bytes) : nMessageSize > MAX_SIZE\n", strCommand.c_str(), nMessageSize);
            continue;
        }
        if (nMessageSize > vRecv.size())
        {
            // Rewind and wait for rest of message
            vRecv.insert(vRecv.begin(), vHeaderSave.begin(), vHeaderSave.end());
            break;
        }

        // Checksum
        uint256 hash = Hash(vRecv.begin(), vRecv.begin() + nMessageSize);
        unsigned int nChecksum = 0;
        memcpy(&nChecksum, &hash, sizeof(nChecksum));
        if (nChecksum != hdr.nChecksum)
        {
            printf("ProcessMessages(%s, %u bytes) : CHECKSUM ERROR nChecksum=%08x hdr.nChecksum=%08x\n",
               strCommand.c_str(), nMessageSize, nChecksum, hdr.nChecksum);
            continue;
        }

        // Copy message to its own buffer
        CDataStream vMsg(vRecv.begin(), vRecv.begin() + nMessageSize, vRecv.nType, vRecv.nVersion);
        vRecv.ignore(nMessageSize);

        // Process message
        bool fRet = false;
        try
        {
            {
                LOCK(cs_main);
                fRet = ProcessMessage(pfrom, strCommand, vMsg);
            }
            if (fShutdown)
                return true;
        }
        catch (ios_base::failure& e)
        {
            if (strstr(e.what(), "end of data"))
            {
                // Allow exceptions from under-length message on vRecv
                printf("ProcessMessages(%s, %u bytes) : Exception '%s' caught, normally caused by a message being shorter than its stated length\n", strCommand.c_str(), nMessageSize, e.what());
            }
            else if (strstr(e.what(), "size too large"))
            {
                // Allow exceptions from over-long size
                printf("ProcessMessages(%s, %u bytes) : Exception '%s' caught\n", strCommand.c_str(), nMessageSize, e.what());
            }
            else
            {
                PrintExceptionContinue(&e, "ProcessMessages()");
            }
        }
        catch (std::exception& e) {
            PrintExceptionContinue(&e, "ProcessMessages()");
        } catch (...) {
            PrintExceptionContinue(NULL, "ProcessMessages()");
        }

        if (!fRet)
            printf("ProcessMessage(%s, %u bytes) FAILED\n", strCommand.c_str(), nMessageSize);
    }

    vRecv.Compact();
    return true;
}


bool SendMessages(CNode* pto, bool fSendTrickle)
{
    TRY_LOCK(cs_main, lockMain);
    if (lockMain) {
        // Don't send anything until we get their version message
        if (pto->nVersion == 0)
            return true;

        // Keep-alive ping. We send a nonce of zero because we don't use it anywhere
        // right now.
        if (pto->nLastSend && GetTime() - pto->nLastSend > 30 * 60 && pto->vSend.empty()) {
            uint64_t nonce = 0;
            if (pto->nVersion > BIP0031_VERSION)
                pto->PushMessage("ping", nonce);
            else
                pto->PushMessage("ping");
        }

        // Resend wallet transactions that haven't gotten in a block yet
        ResendWalletTransactions();

        // Address refresh broadcast
        static int64_t nLastRebroadcast;
        if (!IsInitialBlockDownload() && (GetTime() - nLastRebroadcast > 24 * 60 * 60))
        {
            {
                LOCK(cs_vNodes);
                BOOST_FOREACH(CNode* pnode, vNodes)
                {
                    // Periodically clear setAddrKnown to allow refresh broadcasts
                    if (nLastRebroadcast)
                        pnode->setAddrKnown.clear();

                    // Rebroadcast our address
                    if (!fNoListen)
                    {
                        CAddress addr = GetLocalAddress(&pnode->addr);
                        if (addr.IsRoutable())
                            pnode->PushAddress(addr);
                    }
                }
            }
            nLastRebroadcast = GetTime();
        }

        //
        // Message: addr
        //
        if (fSendTrickle)
        {
            vector<CAddress> vAddr;
            vAddr.reserve(pto->vAddrToSend.size());
            BOOST_FOREACH(const CAddress& addr, pto->vAddrToSend)
            {
                // returns true if wasn't already contained in the set
                if (pto->setAddrKnown.insert(addr).second)
                {
                    vAddr.push_back(addr);
                    // receiver rejects addr messages larger than 1000
                    if (vAddr.size() >= 1000)
                    {
                        pto->PushMessage("addr", vAddr);
                        vAddr.clear();
                    }
                }
            }
            pto->vAddrToSend.clear();
            if (!vAddr.empty())
                pto->PushMessage("addr", vAddr);
        }


        //
        // Message: inventory
        //
        vector<CInv> vInv;
        vector<CInv> vInvWait;
        {
            LOCK(pto->cs_inventory);
            vInv.reserve(pto->vInventoryToSend.size());
            vInvWait.reserve(pto->vInventoryToSend.size());
            BOOST_FOREACH(const CInv& inv, pto->vInventoryToSend)
            {
                if (pto->setInventoryKnown.count(inv))
                    continue;

                // trickle out tx inv to protect privacy
                if (inv.type == MSG_TX && !fSendTrickle)
                {
                    // 1/4 of tx invs blast to all immediately
                    static uint256 hashSalt;
                    if (hashSalt == 0)
                        hashSalt = GetRandHash();
                    uint256 hashRand = inv.hash ^ hashSalt;
                    hashRand = Hash(BEGIN(hashRand), END(hashRand));
                    bool fTrickleWait = ((hashRand & 3) != 0);

                    // always trickle our own transactions
                    if (!fTrickleWait)
                    {
                        CWalletTx wtx;
                        if (GetTransaction(inv.hash, wtx))
                            if (wtx.fFromMe)
                                fTrickleWait = true;
                    }

                    if (fTrickleWait)
                    {
                        vInvWait.push_back(inv);
                        continue;
                    }
                }

                // returns true if wasn't already contained in the set
                if (pto->setInventoryKnown.insert(inv).second)
                {
                    vInv.push_back(inv);
                    if (vInv.size() >= 1000)
                    {
                        pto->PushMessage("inv", vInv);
                        vInv.clear();
                    }
                }
            }
            pto->vInventoryToSend = vInvWait;
        }
        if (!vInv.empty())
            pto->PushMessage("inv", vInv);


        //
        // Message: getdata
        //
        vector<CInv> vGetData;
        int64_t nNow = GetTime() * 1000000;
        CTxDB txdb("r");
        while (!pto->mapAskFor.empty() && (*pto->mapAskFor.begin()).first <= nNow)
        {
            const CInv& inv = (*pto->mapAskFor.begin()).second;
            if (!AlreadyHave(txdb, inv))
            {
                if (fDebugNet)
                    printf("sending getdata: %s\n", inv.ToString().c_str());
                vGetData.push_back(inv);
                if (vGetData.size() >= 1000)
                {
                    pto->PushMessage("getdata", vGetData);
                    vGetData.clear();
                }
                mapAlreadyAskedFor[inv] = nNow;
            }
            pto->mapAskFor.erase(pto->mapAskFor.begin());
        }
        if (!vGetData.empty())
            pto->PushMessage("getdata", vGetData);

    }
    return true;
}
