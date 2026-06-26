// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2013 The NovaCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb-leveldb.h"
#include "miner.h"
#include "kernel.h"

#include "ethash/helpers.hpp"


// from main.cpp
extern unsigned int nLaunchTime;

// Global mining state for KAWPoW
CCriticalSection cs_kawpow_mining;
std::atomic<bool> fKawpowMiningActive(false);
uint64_t nKawpowHashesDone = 0;
uint64_t nKawpowHashesPerSec = 0;
int64_t nKawpowMiningTimeStart = 0;
std::atomic<int> nKawpowThreadCount(0);

std::map<std::string, CBlock> mapKawpowBlockTemplates;
std::mutex kawpowTemplateMutex;

//  static bool fSHA256dMiningActive = false;

// Epoch context cache for mining
static std::map<int, std::shared_ptr<ethash_epoch_context>> mining_epoch_cache;
static CCriticalSection cs_mining_epoch_cache;

using namespace std;
using namespace ethash;



//////////////////////////////////////////////////////////////////////////////
//
// BitcoinMiner
//

extern unsigned int nMinerSleep;

// Helper function to get a cached KAWPoW block template
CBlock* GetKawpowBlockTemplate(const string& headerHash)
{
    lock_guard<mutex> lock(kawpowTemplateMutex);

    auto it = mapKawpowBlockTemplates.find(headerHash);
    if (it != mapKawpowBlockTemplates.end())
    {
        return new CBlock(it->second);
    }

    return NULL;
}

// Helper function to clean expired KAWPoW templates
void CleanKawpowTemplates(int64_t nExpiryTime)
{
    lock_guard<mutex> lock(kawpowTemplateMutex);

    int64_t nNow = GetTime();
    auto it = mapKawpowBlockTemplates.begin();
    while (it != mapKawpowBlockTemplates.end())
    {
        if (it->second.nTime < (nNow - nExpiryTime))
        {
            it = mapKawpowBlockTemplates.erase(it);
        }
        else
        {
            ++it;
        }
    }
}


int static FormatHashBlocks(void* pbuffer, unsigned int len)
{
    unsigned char* pdata = (unsigned char*)pbuffer;
    unsigned int blocks = 1 + ((len + 8) / 64);
    unsigned char* pend = pdata + 64 * blocks;
    memset(pdata + len, 0, 64 * blocks - len);
    pdata[len] = 0x80;
    unsigned int bits = len * 8;
    pend[-1] = (bits >> 0) & 0xff;
    pend[-2] = (bits >> 8) & 0xff;
    pend[-3] = (bits >> 16) & 0xff;
    pend[-4] = (bits >> 24) & 0xff;
    return blocks;
}


// ---------------------------------------------------------------------------
// SHA-256 compression function constants
// ---------------------------------------------------------------------------
static const uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

// Rotate-right helper
static inline uint32_t rotr32(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

// ---------------------------------------------------------------------------
// SHA256Transform
//
// Applies one SHA-256 compression-function round to a single 512-bit (64-byte)
// block.
//
//   pstate  [out] 8 x uint32_t  — resulting chaining value
//   pinput  [in]  16 x uint32_t — the 512-bit input block (big-endian words)
//   pinit   [in]  8 x uint32_t  — initial chaining value to use instead of
//                                  the standard IV
//
// The original code accessed SHA256_CTX::h[] directly, which is an opaque
// internal field in OpenSSL 3.x.  We therefore implement the compression
// function in software, which is both portable and future-proof.
// ---------------------------------------------------------------------------
static const uint32_t pSHA256InitState[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
};

void SHA256Transform(void* pstate, void* pinput, const void* pinit)
{
    // --- 1. Build the message schedule W[0..63] ----------------------------
    //
    // pinput is 16 big-endian uint32_t words.  ByteReverse converts from the
    // on-disk / network byte order that the caller supplies into the native
    // uint32_t values that the SHA-256 spec expects.
    uint32_t W[64];
    const uint32_t* in = reinterpret_cast<const uint32_t*>(pinput);
    for (int i = 0; i < 16; ++i)
        W[i] = ByteReverse(in[i]);

    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(W[i-15], 7) ^ rotr32(W[i-15], 18) ^ (W[i-15] >> 3);
        uint32_t s1 = rotr32(W[i-2],  17) ^ rotr32(W[i-2],  19) ^ (W[i-2]  >> 10);
        W[i] = W[i-16] + s0 + W[i-7] + s1;
    }

    // --- 2. Initialise working variables from the caller-supplied IV -------
    const uint32_t* init = reinterpret_cast<const uint32_t*>(pinit);
    uint32_t a = init[0], b = init[1], c = init[2], d = init[3];
    uint32_t e = init[4], f = init[5], g = init[6], h = init[7];

    // --- 3. 64 rounds of the compression function --------------------------
    for (int i = 0; i < 64; ++i) {
        uint32_t S1    = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch    = (e & f) ^ (~e & g);
        uint32_t temp1 = h + S1 + ch + K[i] + W[i];

        uint32_t S0    = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj   = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    // --- 4. Add the compressed chunk to the current hash value -------------
    uint32_t* out = reinterpret_cast<uint32_t*>(pstate);
    out[0] = init[0] + a;
    out[1] = init[1] + b;
    out[2] = init[2] + c;
    out[3] = init[3] + d;
    out[4] = init[4] + e;
    out[5] = init[5] + f;
    out[6] = init[6] + g;
    out[7] = init[7] + h;
}

// Some explaining would be appreciated
class COrphan
{
public:
    CTransaction* ptx;
    set<uint256> setDependsOn;
    double dPriority;
    double dFeePerKb;

    COrphan(CTransaction* ptxIn)
    {
        ptx = ptxIn;
        dPriority = dFeePerKb = 0;
    }

    void print() const
    {
        printf("COrphan(hash=%s, dPriority=%.1f, dFeePerKb=%.1f)\n",
               ptx->GetHash().ToString().substr(0, 10).c_str(),
               dPriority,
               dFeePerKb);
        BOOST_FOREACH (uint256 hash, setDependsOn)
        {
            printf("   setDependsOn %s\n",
                   hash.ToString().substr(0, 10).c_str());
        }
    }
};


uint64_t nLastBlockTx = 0;
uint64_t nLastBlockSize = 0;
int64_t nLastCoinStakeSearchInterval = 0;

// We want to sort transactions by priority and fee, so:
typedef boost::tuple<double, double, CTransaction*> TxPriority;
class TxPriorityCompare
{
    bool byFee;

public:
    TxPriorityCompare(bool _byFee)
    : byFee(_byFee)
    {
    }
    bool operator()(const TxPriority& a, const TxPriority& b)
    {
        if (byFee)
        {
            if (a.get<1>() == b.get<1>())
            {
                return a.get<0>() < b.get<0>();
            }
            return a.get<1>() < b.get<1>();
        }
        else
        {
            if (a.get<0>() == b.get<0>())
            {
                return a.get<1>() < b.get<1>();
            }
            return a.get<0>() < b.get<0>();
        }
    }
};

// CreateNewBlock: create new block (without proof-of-work/proof-of-stake)
CBlock* CreateNewBlock(CWallet* pwallet,
                       CBlockIndex* pindexPrev,
                       bool fProofOfStake,
                       int64_t pFees[])
{
    // Create new block
    AUTO_PTR<CBlock> pblock(new CBlock(fProofOfStake
                                           ? CBlock::CURRENT_POS_VERSION
                                           : CBlock::CURRENT_POW_VERSION));

    if (!pblock.get())
    {
        return NULL;
    }

    // Create coinbase tx
    CTransaction txNew;
    const unsigned int nTime = txNew.nTime;
    txNew.vin.resize(1);
    txNew.vin[0].prevout.SetNull();
    txNew.vout.resize(1);

    int nHeight = pindexPrev->nHeight+1; // height of new block

    if (fProofOfStake)
    {
        // Height first in coinbase required
        txNew.vin[0].scriptSig = (CScript() << nHeight) + COINBASE_FLAGS;
        assert(txNew.vin[0].scriptSig.size() <= 100);

        txNew.vout[0].SetEmpty();
    }
    else
    {
        CReserveKey reservekey(pwallet);
        CPubKey pubkey;
        struct AMOUNT reward = GetProofOfWorkReward(pindexPrev);
        if (!reservekey.GetReservedKey(pubkey, reward.nColor))
        {
            error("CreateNewBlock: Failed to get reserved key for mining");
            return NULL;
        }

        txNew.vout[0].scriptPubKey.SetDestination(pubkey.GetID());
    }

    // Add our coinbase tx as first transaction
    pblock->vtx.push_back(txNew);

    // Largest block you're willing to create:
    unsigned int nBlockMaxSize = GetArg("-blockmaxsize",
                                        DEFAULT_NEW_BLOCK_SIZE);
    // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
    nBlockMaxSize = max((unsigned int) 1000,
                             min((unsigned int) (MAX_BLOCK_SIZE - 1000),
                                      nBlockMaxSize));

    // How much of the block should be dedicated to high-priority transactions,
    // included regardless of the fees they pay
    unsigned int nBlockPrioritySize = GetArg("-blockprioritysize",
                                             DEFAULT_BLOCK_PRIORITY_SIZE);
    nBlockPrioritySize = min(nBlockMaxSize, nBlockPrioritySize);

    // Minimum block size you want to create; block will be filled with free
    // transactions until there are no more or the block reaches this size:
    unsigned int nBlockMinSize = GetArg("-blockminsize", 0);
    nBlockMinSize = min(nBlockMaxSize, nBlockMinSize);

    // Fee-per-kilobyte amount considered the same as "free"
    // Be careful setting this: if you set it to zero then
    // a transaction spammer can cheaply fill blocks using
    // 1-satoshi-fee transactions. It should be set above the real
    // cost to you of processing a transaction.
    int64_t vMinTxFee[N_COLORS];

    // miners are going to have to specify this by currency number not ticker
    // TODO: handle this in init and via RPC call
    //       not every time a block is constructed
    for (int i = 0; i < N_COLORS; ++i)
    {
        int64_t nMinTxFee;
        char cArg[30];
        snprintf(cArg, sizeof(cArg), "-mintxfee_%d", i);
        if (mapArgs.count(cArg))
        {
            ParseMoney(mapArgs[cArg], i, nMinTxFee);
            vMinTxFee[i] = nMinTxFee;
        }
        else
        {
            vMinTxFee[i] = MIN_TX_FEE[i];
        }
    }

    pblock->nBits = GetNextTargetRequired(nTime, pindexPrev, fProofOfStake);

    // fees burned for PoS, but kept for PoW
    int64_t nFees[N_COLORS];
    for (int i = 0; i < N_COLORS; ++i)
    {
        nFees[i] = 0;
    }

    // Collect memory pool transactions into the block
    {
        LOCK2(cs_main, mempool.cs);
        CTxDB txdb("r");

        // Priority order to process transactions
        list<COrphan> vOrphan; // list memory doesn't move
        map<uint256, vector<COrphan*> > mapDependers;

        // This vector will be sorted into a priority queue:
        vector<TxPriority> vecPriority;
        vecPriority.reserve(mempool.mapTx.size());
        map<uint256, CTransaction>::iterator mi;
        for (mi = mempool.mapTx.begin(); mi != mempool.mapTx.end(); ++mi)
        {
            CTransaction& tx = (*mi).second;
            int nColor = tx.GetColor();
            if (tx.DoesMature() || !tx.IsFinal() || !CheckColor(nColor))
            {
                continue;
            }

            int nFeeColor = FEE_COLOR[nColor];

            COrphan* porphan = NULL;
            double dPriority = 0;
            int64_t nTotalFeeIn = 0;
            bool fMissingInputs = false;
            BOOST_FOREACH(const CTxIn& txin, tx.vin)
            {
                const uint256& prevhash = txin.prevout.hash;
                const unsigned int prevn = txin.prevout.n;
                // Read prev transaction
                CTransaction txPrev;
                CTxIndex txindex;
                if (!txPrev.ReadFromDisk(txdb, txin.prevout, txindex))
                {
                    // This should never happen; all transactions in the memory
                    // pool should connect to either transactions in the chain
                    // or other transactions in the memory pool.
                    if (!mempool.mapTx.count(prevhash))
                    {
                        printf("ERROR: mempool transaction missing input\n");
                        if (fDebugMiner)
						{
                             assert("mempool transaction missing input" == 0);
                        }
                        fMissingInputs = true;
                        if (porphan)
                        {
                            vOrphan.pop_back();
                        }
                        break;
                    }

                    // Has to wait for dependencies
                    if (!porphan)
                    {
                        // Use list for automatic deletion
                        vOrphan.push_back(COrphan(&tx));
                        porphan = &vOrphan.back();
                    }
                    mapDependers[prevhash].push_back(porphan);
                    porphan->setDependsOn.insert(prevhash);
                    const CTxOut& prevtxout =
                             mempool.mapTx[prevhash].vout[prevn];
                    if (nFeeColor == prevtxout.nColor)
                    {
                        nTotalFeeIn += prevtxout.nValue;
                    }
                    continue;
                }
                int64_t nValueIn = txPrev.vout[prevn].nValue;
                int nInputColor = txPrev.vout[prevn].nColor;
                if (nFeeColor == nInputColor)
                {
                    nTotalFeeIn += nValueIn;
                }

                int nConf = txindex.GetDepthInMainChain();
                // priorities need to be weighted per currency
                dPriority += PRIORITY_MULTIPLIER[nInputColor] *
                             (double) nValueIn * nConf;
            }
            if (fMissingInputs)
            {
                continue;
            }

            // Priority is multiplier * sum(valuein * age) / txsize
            unsigned int nTxSize = ::GetSerializeSize(tx,
                                                      SER_NETWORK,
                                                      PROTOCOL_VERSION);
            dPriority /= nTxSize;

            // This is a more accurate fee-per-kilobyte than is used by the
            // client code, because the client code rounds up the size to the
            // nearest 1K. That's good, because it gives an incentive to create
            // smaller transactions. For better or worse, there is no
            // adjustment for fee change.
            double dFeePerKb = double(nTotalFeeIn -
                                      tx.GetValueOut(nFeeColor)) /
                               (double(nTxSize) / 1000.0);
            // dFeePerKb is weighted by the priority multiplier for the fee currency
            dFeePerKb *= PRIORITY_MULTIPLIER[nFeeColor];

            if (porphan)
            {
                porphan->dPriority = dPriority;
                porphan->dFeePerKb = dFeePerKb;
            }
            else
            {
                vecPriority.push_back(
                    TxPriority(dPriority, dFeePerKb, &(*mi).second));
            }
        }

        // Collect transactions into block
        map<uint256, CTxIndex> mapTestPool;
        uint64_t nBlockSize = 1000;
        uint64_t nBlockTx = 0;
        int nBlockSigOps = 100;
        bool fSortedByFee = (nBlockPrioritySize <= 0);

        TxPriorityCompare comparer(fSortedByFee);
        make_heap(vecPriority.begin(), vecPriority.end(), comparer);

        while (!vecPriority.empty())
        {
            // Take highest priority transaction from the priority queue:
            double dPriority = vecPriority.front().get<0>();
            double dFeePerKb = vecPriority.front().get<1>();
            CTransaction& tx = *(vecPriority.front().get<2>());

            int nColor = tx.GetColor();
            int nFeeColor = FEE_COLOR[nColor];

            pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
            vecPriority.pop_back();

            // Size limits
            unsigned int nTxSize = ::GetSerializeSize(tx,
                                                      SER_NETWORK,
                                                      PROTOCOL_VERSION);
            if (nBlockSize + nTxSize >= nBlockMaxSize)
            {
                continue;
            }

            // Legacy sigOps limit
            unsigned int nTxSigOps = tx.GetLegacySigOpCount();
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
            {
                continue;
            }

            // Timestamp limit
            if ((fProofOfStake && (tx.nTime > nTime)) ||
                (tx.nTime > GetAdjustedTime()))
            {
                continue;
            }


            // Skip free transactions if we're past the minimum block size:
            if (fSortedByFee && (dFeePerKb < vMinTxFee[nFeeColor]) &&
                (nBlockSize + nTxSize >= nBlockMinSize))
            {
                continue;
            }

            // Prioritize by fee once past the priority size or
            //    we run out of high-priority transactions:
            if (!fSortedByFee &&
                ((nBlockSize + nTxSize >= nBlockPrioritySize) ||
                 (dPriority < (PRIORITY_MULTIPLIER[nFeeColor] *
							   COIN[nFeeColor] *
                               144 / 250))))
            {
                fSortedByFee = true;
                comparer = TxPriorityCompare(fSortedByFee);
                make_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }

            // Connecting shouldn't fail due to dependency on other memory pool
            // transactions because we're already processing them in order of
            // dependency
            map<uint256, CTxIndex> mapTestPoolTmp(mapTestPool);
            MapPrevTx mapInputs;
            bool fInvalid;
            if (!tx.FetchInputs(txdb,
                                mapTestPoolTmp,
                                false,
                                true,
                                mapInputs,
                                fInvalid))
            {
                continue;
            }

            // take all the scavengable fees
            // TODO: Maybe there is a more efficient way to do this? Need FillValuesIn/Out.
            int64_t nTxFees = 0;
            for (int i = 1; i < N_COLORS; ++i)
            {
                int64_t txfee = tx.GetValueIn(mapInputs, i) - tx.GetValueOut(i);
                if (i == nFeeColor)
                {
                    nTxFees = txfee;
                }

                // only collect scavengable fees
                if (SCAVENGABLE[i])
                {
                    nFees[i] += txfee;
                }
            }

            // for each transaction, fees are asessed in the fee color
            if (nTxFees < vMinTxFee[nFeeColor])
            {
                continue;
            }

            nTxSigOps += tx.GetP2SHSigOpCount(mapInputs);
            if (nBlockSigOps + nTxSigOps >= MAX_BLOCK_SIGOPS)
            {
                continue;
            }

            if (!tx.ConnectInputs(txdb,
                                  mapInputs,
                                  mapTestPoolTmp,
                                  CDiskTxPos(1, 1, 1),
                                  pindexPrev,
                                  false,
                                  true,
                                  STANDARD_SCRIPT_VERIFY_FLAGS))
            {
                continue;
            }

            mapTestPoolTmp[tx.GetHash()] = CTxIndex(CDiskTxPos(1, 1, 1),
                                                    tx.vout.size());
            swap(mapTestPool, mapTestPoolTmp);

            // Added
            pblock->vtx.push_back(tx);
            nBlockSize += nTxSize;
            ++nBlockTx;
            nBlockSigOps += nTxSigOps;

            if (fDebugMiner && GetBoolArg("-printpriority"))
            {
                printf("priority %.1f feeperkb %.1f txid %s\n",
                       dPriority,
                       dFeePerKb,
                       tx.GetHash().ToString().c_str());
            }

            // Add transactions that depend on this one to the priority queue
            uint256 hash = tx.GetHash();
            if (mapDependers.count(hash))
            {
                BOOST_FOREACH (COrphan* porphan, mapDependers[hash])
                {
                    if (!porphan->setDependsOn.empty())
                    {
                        porphan->setDependsOn.erase(hash);
                        if (porphan->setDependsOn.empty())
                        {
                            vecPriority.push_back(
                                TxPriority(porphan->dPriority,
                                           porphan->dFeePerKb,
                                           porphan->ptx));
                            push_heap(vecPriority.begin(),
                                           vecPriority.end(),
                                           comparer);
                        }
                    }
                }
            }
        }

        nLastBlockTx = nBlockTx;
        nLastBlockSize = nBlockSize;

        if (fDebugMiner && GetBoolArg("-printpriority"))
        {
            printf("CreateNewBlock(): total size %" PRIu64 "\n", nBlockSize);
        }

        // add scavengable fees
        for (int j = 1; j < N_COLORS; ++j)
        {
            if (SCAVENGABLE[j])
            {
                int64_t nTotalMintPrev = (pindexPrev
                                              ? pindexPrev->vTotalMint[j]
                                              : 0);
                int64_t nMoneySupplyPrev = (pindexPrev
                                                ? pindexPrev->vMoneySupply[j]
                                                : 0);
                nFees[j] += (nTotalMintPrev - nMoneySupplyPrev);
            }
        }

        // Proof of Work
        if (!fProofOfStake)
        {
            // fees must be handled outside of the subsidy calculation
            struct AMOUNT reward = GetProofOfWorkReward(pindexPrev);
            pblock->vtx[0].vout[0].nValue = reward.nValue +
                                            nFees[reward.nColor];
            pblock->vtx[0].vout[0].nColor = reward.nColor;
            // make outputs for the non-coinbase currency fees
            for (int i = 1; i < N_COLORS; ++i)
            {
                if ((nFees[i] > 0) && (i != reward.nColor))
                {
                    // send all fees to the coinbase pubkey
                    CTxOut feeOut(nFees[i],
                                  i,
                                  pblock->vtx[0].vout[0].scriptPubKey);
                    pblock->vtx[0].vout.push_back(feeOut);
                }
            }
        }

        if (pFees != NULL)
        {
            for (int i = 1; i < N_COLORS; ++i)
            {
                pFees[i] = nFees[i];
            }
        }

        // Fill in header
        pblock->hashPrevBlock = pindexPrev->GetBlockHash();
        pblock->nTime = max(pindexPrev->GetPastTimeLimit() + 1,
                            pblock->GetMaxTransactionTime());
        pblock->nTime = max(pblock->GetBlockTime(),
                            pindexPrev->GetBlockTime());

        if (!fProofOfStake)
        {
            pblock->UpdateTime(pindexPrev);
        }

        int nFork = GetFork(pblock->nTime);
        bool fIsPostSCrypt = (nFork >= BRK_FORK007);

        pblock->nNonce = 0;

        // KAWPoW
        if (!fProofOfStake && fIsPostSCrypt)
        {
            pblock->nHeight = nHeight;
            pblock->nNonce64 = 0;
            pblock->mix_hash = uint256();

            pblock->hashMerkleRoot = pblock->BuildMerkleTree();

            // KAWPoW header hash for template caching
            uint256 headerHash = pblock->GetKAWPOWHeaderHash();

            // Cache the block template for stratum-like mining
            {
                lock_guard<mutex> lock(kawpowTemplateMutex);
                mapKawpowBlockTemplates[headerHash.GetHex()] = *pblock;

                // Prune all but last 10 templates
                if (mapKawpowBlockTemplates.size() > 10)
                {
                    auto it = mapKawpowBlockTemplates.begin();
                    advance(it, mapKawpowBlockTemplates.size() - 10);
                    mapKawpowBlockTemplates
                        .erase(mapKawpowBlockTemplates.begin(), it);
                }
            }

            if (fDebugMiner)
            {
                printf("CreateNewBlock(): KAWPOW BLOCK\n");
                printf("  Height: %d\n", pblock->nHeight);
                printf("  Time: %d\n", pblock->nTime);
                printf("  Version: 0x%08x\n", pblock->nVersion);
                printf("  Transactions: %d\n", (int) pblock->vtx.size());
                printf("  Difficulty: %.6f\n", GetDifficulty(pblock->nBits));
                printf("  Header Hash: %s\n", headerHash.GetHex().c_str());
                printf("  Hash Prev: %s\n", pblock->hashPrevBlock.GetHex().c_str());
                printf("  Epoch: %d\n", get_epoch_number(nHeight));

                // Log multi-currency rewards
                printf("  Rewards:\n");
                for (size_t i = 0; i < pblock->vtx[0].vout.size(); i++)
                {
                    const CTxOut& out = pblock->vtx[0].vout[i];
                    if (out.nValue > 0)
                    {
                        printf("    %s: %s\n",
                               COLOR_TICKER[out.nColor],
                               FormatMoney(out.nValue,
                                           out.nColor,
                                           false,
                                           false).c_str());
                    }
                }
            }
        }
    }

    return pblock.release();
}


void IncrementExtraNonce(CBlock* pblock,
                         CBlockIndex* pindexPrev,
                         unsigned int& nExtraNonce)
{
    // Update nExtraNonce
    static uint256 hashPrevBlock;
    if (hashPrevBlock != pblock->hashPrevBlock)
    {
        nExtraNonce = 0;
        hashPrevBlock = pblock->hashPrevBlock;
    }
    ++nExtraNonce;

    // Height first in coinbase required for block.version=2
    unsigned int nHeight = pindexPrev->nHeight + 1;
    pblock->vtx[0].vin[0].scriptSig = (CScript()
                                       << nHeight << CBigNum(nExtraNonce)) +
                                      COINBASE_FLAGS;
    assert(pblock->vtx[0].vin[0].scriptSig.size() <= 100);

    pblock->hashMerkleRoot = pblock->BuildMerkleTree();

    int nFork = GetFork(pblock->nTime);
    if (nFork >= BRK_FORK007)
    {
        // Update cached template if it exists
        uint256 headerHash = pblock->GetKAWPOWHeaderHash();
        {
            lock_guard<mutex> lock(kawpowTemplateMutex);
            mapKawpowBlockTemplates[headerHash.GetHex()] = *pblock;
        }
    }
}


void FormatHashBuffers(CBlock* pblock,
                       char* pmidstate,
                       char* pdata,
                       char* phash1)
{
    // Pre-build hash buffers
    struct
    {
        struct unnamed2
        {
            int nVersion;
            uint256 hashPrevBlock;
            uint256 hashMerkleRoot;
            unsigned int nTime;
            unsigned int nBits;
            unsigned int nNonce;
        } block;
        unsigned char pchPadding0[64];
        uint256 hash1;
        unsigned char pchPadding1[64];
    } tmp = {};

    tmp.block.nVersion = pblock->nVersion;
    tmp.block.hashPrevBlock = pblock->hashPrevBlock;
    tmp.block.hashMerkleRoot = pblock->hashMerkleRoot;
    tmp.block.nTime = pblock->nTime;
    tmp.block.nBits = pblock->nBits;
    tmp.block.nNonce = pblock->nNonce;

    FormatHashBlocks(&tmp.block, sizeof(tmp.block));
    FormatHashBlocks(&tmp.hash1, sizeof(tmp.hash1));

    // Byte swap all the input buffer
    for (unsigned int i = 0; i < sizeof(tmp) / 4; i++)
    {
        ((unsigned int*) &tmp)[i] = ByteReverse(((unsigned int*) &tmp)[i]);
    }

    // Precalc the first half of the first hash, which stays constant
    SHA256Transform(pmidstate, &tmp.block, pSHA256InitState);

    memcpy(pdata, &tmp.block, 128);
    memcpy(phash1, &tmp.hash1, 64);
}


bool CheckWork(CBlock* pblock,
               CWallet& wallet,
               CReserveKey& reservekey,
               uint256& hashBlock,
               uint256& mix_hash,
               const uint256& hashTarget)
{
    hashBlock = pblock->GetHashFull(mix_hash);

    if(!pblock->IsProofOfWork())
    {
        return error("CheckWork() : %s is not a proof-of-work block",
                     hashBlock.GetHex().c_str());
    }

    if (hashBlock > hashTarget)
    {
        if (fDebugMiner)
        {
            printf("CheckWork() :\n   hashBlock: %s\n   target: %s\n",
                   hashBlock.GetHex().c_str(),
                   hashTarget.GetHex().c_str());
        }
        return error("CheckWork() : proof-of-work not meeting target");
    }

    if (pblock->nTime < nLaunchTime)
    {
         return error("CheckWork() : time stamp before launch");
    }

    pblock->mix_hash = mix_hash;

    //// debug print
    printf("======================== CheckWork() =========================\n");
    printf(("CheckWork() : new proof-of-work block found\n  hash:  %s\n"
            "  target: %s\n"),
           hashBlock.GetHex().c_str(),
           hashTarget.GetHex().c_str());
    pblock->print();
    printf("  generated %s\n",
           FormatMoney(pblock->vtx[0].vout[0].nValue,
                       pblock->vtx[0].vout[0].nColor).c_str());

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != hashBestChain)
        {
            return error("CheckWork() : generated block is stale");
        }

        // Remove key from key pool
        reservekey.KeepKey();

        // Track how many getdata requests this block gets
        {
            LOCK(wallet.cs_wallet);
            wallet.mapRequestCount[hashBlock] = 0;
        }

        // Process this block the same as if we had
        // received it from another node
        bool fOrphan;
        if (!ProcessBlock(NULL, pblock, fOrphan))
        {
            return error("CheckWork() : ProcessBlock, block not accepted");
        }
    }

    return true;
}


bool CheckWorkKawpow(CBlock* pblock,
                     CWallet& wallet,
                     CReserveKey& reservekey,
                     uint256& hashBlock,
                     uint256& mix_hash,
                     const uint256& hashTarget)
{
    if (!pblock->IsKawpowBlock())
    {
        return error(
            "CheckWorkKawpow() : block is not configured for KAWPoW mining");
    }

    if (pblock->mix_hash == 0)
    {
        return error(
            "CheckWorkKawpow() : mix_hash is null");
    }

    // mix_hash is already set by the miner; use it directly to derive
    // the final hash without recomputing the full KAWPoW DAG traversal.
    mix_hash = pblock->mix_hash;
    hashBlock = KAWPOWHash_OnlyMix(*pblock);

    // Verify the hash meets the target
    if (hashBlock > hashTarget)
    {
        if (fDebugMiner)
        {
            printf("CheckWorkKawpow() : KAWPoW hash does not meet target\n");
            printf("   hashBlock: %s\n", hashBlock.GetHex().c_str());
            printf("   mix_hash:  %s\n", mix_hash.GetHex().c_str());
            printf("   target:    %s\n", hashTarget.GetHex().c_str());
        }
        return false;
    }

    // Validate timestamp
    if (pblock->nTime < nLaunchTime)
    {
        return error("CheckWorkKawpow() : block timestamp before launch time");
    }

    // Additional KAWPoW-specific validations
    if (!KawpowIsActive())
    {
        return error(
            "CheckWorkKawpow() : KAWPoW block before activation time");
    }

    // Additional KAWPoW-specific validations
    if (!KawpowIsActive(pblock->nTime))
    {
        return error(
            "CheckWorkKawpow() : KAWPoW block time before activation time");
    }

    const auto epoch_number = get_epoch_number(pblock->nHeight);

    printf("====================== CheckWorkKawpow() =====================\n");
    printf("CheckWorkKawpow() : new KAWPoW block found!\n");
    printf("  hash:      %s\n", hashBlock.GetHex().c_str());
    printf("  mix_hash:  %s\n", pblock->mix_hash.GetHex().c_str());
    printf("  target:    %s\n", hashTarget.GetHex().c_str());
    printf("  nonce64:   %016" PRIx64 "\n", pblock->nNonce64);
    printf("  height:    %d\n", pblock->nHeight);
    printf("  time:    %d\n", pblock->nTime);
    printf("  epoch:     %d\n", epoch_number);

    // Print multi-currency rewards (Breakout specific)
    printf("Block rewards:\n");
    for (size_t i = 0; i < pblock->vtx[0].vout.size(); i++)
    {
        const CTxOut& out = pblock->vtx[0].vout[i];
        if (out.nValue > 0)
        {
            printf("  %s: %s\n",
                   COLOR_TICKER[out.nColor],
                   FormatMoney(out.nValue,
                   out.nColor,
                   false,
                   false).c_str());
        }
    }

    // Found a valid solution
    {
        LOCK(cs_main);

        // Check if the block is still valid (not stale)
        if (pblock->hashPrevBlock != hashBestChain)
        {
            return error("CheckWorkKawpow() : generated block is stale\n  %s\n",
                         hashBlock.GetHex().c_str());
        }

        // For multi-currency system: ensure all currencies are properly
        // handled

        // Check that coinbase outputs are valid for all currencies
        for (const CTxOut& out : pblock->vtx[0].vout)
        {
            if (!CheckColor(out.nColor))
            {
                return error(
                    "CheckWorkKawpow() : invalid currency color (%d) in coinbase\n  %s\n",
                    out.nColor,
                    hashBlock.GetHex().c_str());
            }
        }

        // Remove key from key pool
        reservekey.KeepKey();

        // Track how many getdata requests this block gets
        {
            LOCK(wallet.cs_wallet);
            wallet.mapRequestCount[hashBlock] = 0;
        }

        // Process this block the same as if we had received it from another
        // node
        bool fOrphan;
        if (!ProcessBlock(NULL, pblock, fOrphan))
        {
            return error(
                "CheckWorkKawpow() : ProcessBlock failed, block not accepted");
        }

        // Update mining statistics for KAWPoW
        {
            static CCriticalSection cs_kawpow_stats;
            LOCK(cs_kawpow_stats);

            static uint64_t nKawpowBlocksFound = 0;
            static uint64_t nKawpowHashesComputed = 0;

            nKawpowBlocksFound++;

            // Log statistics
            if (fDebugMiner)
            {
                printf("KAWPoW Statistics:\n");
                printf("  Blocks found: %" PRIu64 "\n", nKawpowBlocksFound);
                printf("  Total hashes: %" PRIu64 "\n", nKawpowHashesComputed);
            }
        }
    }

    return true;
}


bool CheckStake(CBlock* pblock, CWallet& wallet)
{
    uint256 proofHash = 0, hashTarget = 0;
    uint256 hashBlock = pblock->GetHash();

    if (!pblock->IsProofOfStake())
    {
        return error("CheckStake() : %s is not a proof-of-stake block",
                     hashBlock.GetHex().c_str());
    }

    // verify hash target and signature of coinstake tx
    if (!CheckProofOfStake(mapBlockIndex[pblock->hashPrevBlock],
                           pblock->vtx[1],
                           pblock->nBits,
                           proofHash,
                           hashTarget))
    {
        return error("CheckStake() : proof-of-stake checking failed");
    }

    //// debug print
    printf("======================== CheckStake() =========================\n");
    printf(("CheckStake() : new proof-of-stake block found\n  hash: %s\n"
            "  proofhash: %s\n  target: %s\n"),
           hashBlock.GetHex().c_str(),
           proofHash.GetHex().c_str(),
           hashTarget.GetHex().c_str());
    pblock->print();
    printf("  out %s\n",
           FormatMoney(pblock->vtx[1].GetValueOut(pblock->vtx[1].GetColor()),
                       pblock->vtx[1].GetColor()).c_str());

    // Found a solution
    {
        LOCK(cs_main);
        if (pblock->hashPrevBlock != hashBestChain)
        {
            return error("CheckStake() : generated block is stale");
        }

        // Track how many getdata requests this block gets
        {
            LOCK(wallet.cs_wallet);
            wallet.mapRequestCount[hashBlock] = 0;
        }

        // Process this block the same as if we had
        //    received it from another node
        bool fOrphan;
        if (!ProcessBlock(NULL, pblock, fOrphan))
        {
            return error("CheckStake() : ProcessBlock, block not accepted");
        }
    }

    return true;
}

void StakeMiner(CWallet* pwallet)
{
    SetThreadPriority(THREAD_PRIORITY_LOWEST);

    // Make this thread recognisable as the mining thread
    RenameThread("breakout-stake-miner");

#if PROOF_MODEL == PURE_POS
    //
    static const int nFirstPoSBlock = GetFirstPoSBlock();
    int nTargetSpacing = GetTargetSpacing(true, pindexBest->nTime);
    unsigned int nMilliWaitForPoS = nTargetSpacing * 1000 / 2;
#endif

    bool fTryToSync = true;

    while (true)
    {
        if ((nMaxHeight > 0) && (nBestHeight >= nMaxHeight))
        {
            printf("StakeMiner : reached max height of %d.\n", nMaxHeight);
            return;
        }

        if (fShutdown)
        {
            return;
        }

        while (pwallet->IsLocked())
        {
            nLastCoinStakeSearchInterval = 0;
            GranularMilliSleep(1000, 500);
            if (fShutdown)
            {
                return;
            }
        }

        while (vNodes.empty() || IsInitialBlockDownload())
        {
            nLastCoinStakeSearchInterval = 0;
            fTryToSync = true;
            GranularMilliSleep(1000, 500);
            if (fShutdown)
            {
                return;
            }
        }

        if (fTryToSync)
        {
            fTryToSync = false;
            if ((GetConnectionCount() < (fTestNet ? 1u : 3u)) ||
                (nBestHeight < GetNumBlocksOfPeers()))
            {
                for (int i = 0; i < 60; ++i)
                {
                    GranularMilliSleep(1000, 500);
                    if (fShutdown)
                    {
                        return;
                    }
                }
                continue;
            }
        }

#if PROOF_MODEL == PURE_POS
        if (pindexBest->nHeight < (nFirstPoSBlock - 1))
        {
            GranularMilliSleep(nMilliWaitForPoS, 500);
            continue;
        }
#endif

        //
        // Create new block
        //
        int64_t nFees[N_COLORS];

        CBlockIndex* pindexPrev = pindexBest;
        AUTO_PTR<CBlock> pblock(
            CreateNewBlock(pwallet, pindexPrev, true, nFees));
        if (!pblock.get())
        {
            return;
        }

        // Trying to sign a block
        if (pblock->SignBlock(*pwallet, nFees))
        {
            SetThreadPriority(THREAD_PRIORITY_NORMAL);
            CheckStake(pblock.get(), *pwallet);
            SetThreadPriority(THREAD_PRIORITY_LOWEST);
            int nTargetSpacing = GetTargetSpacing(true, pindexPrev->nTime);
            GranularMilliSleep(nTargetSpacing * 1000, 500);
        }
        else
        {
            GranularMilliSleep(nMinerSleep, 500);
        }
    }
}

bool ValidateKawpowSubmission(const string& header_hash_hex,
                              const string& mix_hash_hex,
                              uint64_t nonce64,
                              int height,
                              const uint256& hashTarget)
{
    try
    {
        // Calculate KAWPoW hash
        const hash256 header_hash = to_hash256(header_hash_hex);
        const hash256 mix_hash = to_hash256(mix_hash_hex);
        const hash256 block_hash = progpow::hash_no_verify(height,
                                                           header_hash,
                                                           mix_hash,
                                                           nonce64);

        const uint256 hashBlock = to_uint256(block_hash);

        if (hashBlock > hashTarget)
        {
            if (fDebugMiner)
            {
                printf("ValidateKawpowSubmission: hash doesn't meet target\n");
            }
            return false;
        }
        return true;
    }
    catch (const exception& e)
    {
        error("ValidateKawpowSubmission exception: %s", e.what());
        return false;
    }
}

void KawpowMinerMilliSleep(int length, int step_size = 250)
{
    if (fKawpowMiningActive)
    {
        int elapsed = 0;
        while ((elapsed < length) && fKawpowMiningActive)
        {
            int chunk = min(step_size, length - elapsed);
            GranularMilliSleep(chunk, 500);
            if (fShutdown)
            {
                return;
            }
            elapsed += chunk;
        }
    }
    else
    {
        GranularMilliSleep(length, 500);
    }
}


void MineWithKawpow(CWallet* pwallet)
{
    printf("KAWPoW miner starting\n");
    SetThreadPriority(THREAD_PRIORITY_LOWEST);
    RenameThread("breakout-kawpow-miner");

    // Check if wallet is available
    if (!pwallet)
    {
        printf("KAWPoW miner: No wallet available\n");
        return;
    }

    // Set mining active flag
    {
        LOCK(cs_kawpow_mining);
        fKawpowMiningActive = true;
        nKawpowThreadCount++;
        if (nKawpowThreadCount == 1)
        {
            nKawpowMiningTimeStart = GetTimeMicros();
            nKawpowHashesDone = 0;
            nKawpowHashesPerSec = 0;
        }
    }

    unsigned int nExtraNonce = 0;
    CReserveKey reservekey(pwallet);

    try
    {
        while (fKawpowMiningActive)
        {
            if (fShutdown)
            {
                break;
            }

            // Check if we have peers (unless bypassed)
            if (!GetBoolArg("-bypassdownload", false))
            {
                if (vNodes.empty())
                {
                    if (fDebugMiner)
                    {
                        printf("KAWPoW miner: No peers, waiting...\n");
                    }
                    KawpowMinerMilliSleep(5000);
                    continue;
                }

                if (IsInitialBlockDownload())
                {
                    if (fDebugMiner)
                    {
                        printf("KAWPoW miner: Initial block download, "
                               "waiting...\n");
                    }
                    KawpowMinerMilliSleep(5000);
                    continue;
                }
            }

            // Get current tip
            CBlockIndex* pindexPrev = pindexBest;
            if (!pindexPrev)
            {
                if (fDebugMiner)
                {
                    printf("KAWPoW miner: No blockchain tip available\n");
                }
                KawpowMinerMilliSleep(1000);
                continue;
            }

            // Check if KAWPoW is active
            int64_t nCurrentTime = GetAdjustedTime();
            if (!KawpowIsActive(nCurrentTime))
            {
                if (fDebugMiner)
                {
                    printf(
                        "KAWPoW miner: Waiting for activation (current: %" PRId64 ", "
                        "activation: %" PRId64 ")\n",
                        nCurrentTime,
                        nKAWPOWActivationTime);
                }
                KawpowMinerMilliSleep(10000);
                continue;
            }

            // Create new block
            unsigned int nTransactionsUpdatedLast = nTransactionsUpdated;

            AUTO_PTR<CBlock> pblock(
                CreateNewBlock(pwallet, pindexPrev, false));

            if (!pblock.get())
            {
                printf("KAWPoW miner: Failed to create new block\n");
                KawpowMinerMilliSleep(1000);
                continue;
            }

            IncrementExtraNonce(pblock.get(), pindexPrev, nExtraNonce);

            int nHeight = pblock->nHeight;
            if (fDebugMiner)
            {
                printf(
                    "KAWPoW miner: Mining block %d with %zu transactions (%u "
                    "bytes)\n",
                    nHeight,
                    pblock->vtx.size(),
                    ::GetSerializeSize(*pblock,
                                       SER_NETWORK,
                                       PROTOCOL_VERSION));
            }

            // Get target
            uint256 hashTarget = CompactToUint256(pblock->nBits);

            // Get epoch context
            const int epoch_number = get_epoch_number(nHeight);
            shared_ptr<ethash_epoch_context> context;

            {
                LOCK(cs_mining_epoch_cache);
                auto it = mining_epoch_cache.find(epoch_number);
                if (it != mining_epoch_cache.end())
                {
                    context = it->second;
                }
                else
                {
                    printf("KAWPoW miner: Creating DAG for epoch %d "
                              "(height %d)...\n",
                              epoch_number,
                              nHeight);

                    auto start_dag = GetTimeMicros();
                    // Wrap the unique_ptr's released pointer in a shared_ptr,
                    // preserving the custom deleter.
                    auto unique_ctx = create_epoch_context(epoch_number);
                    context = shared_ptr<ethash_epoch_context>(
                        unique_ctx.release(),
                        ethash_destroy_epoch_context
                    );
                    auto end_dag = GetTimeMicros();

                    printf("KAWPoW miner: DAG creation took %.2f seconds\n",
                              (end_dag - start_dag) / 1000000.0);

                    // Cache the epoch (keep 2 latest epochs)
                    mining_epoch_cache[epoch_number] = context;
                    if (mining_epoch_cache.size() > 2)
                    {
                        mining_epoch_cache.erase(mining_epoch_cache.begin());
                    }

                    if (fDebugMiner)
                    {
                        printf(
                            "getkawpowhash(): Created and cached epoch %d\n",
                            epoch_number);
                    }
                }
            }

            // Get KAWPoW header hash
            uint256 headerHash = pblock->GetKAWPOWHeaderHash();
            hash256 header_hash_ethash;
            memcpy(header_hash_ethash.bytes, headerHash.begin(), 32);

            // Mining loop
            int64_t nStart = GetTime();
            pblock->nNonce64 = GetRand(numeric_limits<uint64_t>::max());

            uint64_t nHashCount = 0;
            // Check for interruption every 65535 hashes
            constexpr uint64_t HASH_INTERRUPT = 0xFFFF;
            // Check for new block every 255 hashes
            constexpr uint64_t HASH_INTERRUPT_NEWBLOCK = 0xFF;

            printf("KAWPoW miner: Starting search, target: %s\n",
                   hashTarget.GetHex().c_str());

            uint256 hashLowest("0xffffffffffffffffffffffffffffffff"
                                 "ffffffffffffffffffffffffffffffff");
            while (fKawpowMiningActive)
            {
                if ((nMaxHeight > 0) && (nBestHeight >= nMaxHeight))
                {
                    printf("KAWPoW miner: reached max height of %d\n", nMaxHeight);
                    return;
                }
                // Check for stop conditions periodically
                if (((nHashCount & HASH_INTERRUPT) == 0) ||
                    ((nHashCount % HASH_INTERRUPT_NEWBLOCK == 0) && (pindexPrev != pindexBest)))
                {
                    if (fDebugMiner)
                    {
                        printf("KAWPoW miner: checking for stop conditions\n"
                               "  Hash Count: %" PRIu64 "\n",
                               nHashCount);
                    }
                    // Check if we should stop mining
                    if (fShutdown)
                    {
                        break;
                    }

                    // Check if block chain tip changed
                    if (pindexPrev != pindexBest)
                    {
                        printf(
                            "KAWPoW miner: Chain tip changed, restarting\n");
                        break;
                    }

                    // Check if transactions updated and enough time passed
                    if ((nTransactionsUpdated != nTransactionsUpdatedLast) &&
                        ((GetTime() - nStart) > 60))
                    {
                        printf("KAWPoW miner: Mempool updated, restarting\n");
                        break;
                    }

                    // Update timestamp
                    int64_t nNewTime = max(
                        pindexPrev->GetMedianTimePast() + 1,
                        GetAdjustedTime());
                    if (pblock->nTime != nNewTime)
                    {
                        pblock->nTime = nNewTime;
                        // Recalculate header hash with new timestamp
                        headerHash = pblock->GetKAWPOWHeaderHash();
                        memcpy(header_hash_ethash.bytes,
                               headerHash.begin(),
                               32);

                        // Update difficulty if needed
                        unsigned int nNewBits = GetNextTargetRequired(
                            pblock->nTime,
                            pindexPrev,
                            false);
                        if (pblock->nBits != nNewBits)
                        {
                            pblock->nBits = nNewBits;
                            hashTarget = CompactToUint256(nNewBits);
                            printf(
                                "KAWPoW miner: Difficulty adjusted to %08x\n",
                                nNewBits);
                        }
                    }

                    // Update mining statistics
                    {
                        LOCK(cs_kawpow_mining);
                        nKawpowHashesDone += nHashCount;
                        int64_t nTimeDiff = GetTimeMicros() -
                                            nKawpowMiningTimeStart;
                        if (nTimeDiff > 0)
                        {
                            nKawpowHashesPerSec = (nKawpowHashesDone *
                                                   1000000) /
                                                  nTimeDiff;
                        }
                    }

                    // Log progress
                    static const uint64_t UPDATE_EVERY = fDebugMiner ? 1000
                                                                     : 1000000;
                    if (nKawpowHashesDone > 0 &&
                        (nKawpowHashesDone % UPDATE_EVERY) == 0)
                    {
                        printf("KAWPoW miner: %" PRIu64 " hashes, %" PRIu64 " H/s\n",
                               nKawpowHashesDone,
                               nKawpowHashesPerSec);
                    }

                    nHashCount = 0;
                }

                // Calculate KAWPoW hash
                const auto result = progpow::hash(*context,
                                                  nHeight,
                                                  header_hash_ethash,
                                                  pblock->nNonce64);

                // Convert result to uint256
                uint256 mix_hash;
                uint256 final_hash;
                memcpy(mix_hash.begin(), result.mix_hash.bytes, 32);
                memcpy(final_hash.begin(), result.final_hash.bytes, 32);
                if (final_hash < hashLowest)
                {
                    hashLowest = final_hash;
                }

                nHashCount++;

                if (fDebugMiner)
                {
                    if (nHashCount % 1000 == 0)
                    {
                        printf("KAWPoW miner: %" PRIu64 " Hashes\n  lowest: %s\n",
                               nHashCount,
                               hashLowest.GetHex().c_str());
                    }
                }


                // Check if we found a valid solution
                if (final_hash <= hashTarget)
                {
                    // Found a solution!
                    SetThreadPriority(THREAD_PRIORITY_NORMAL);

                    // Set the mix hash in the block
                    pblock->mix_hash = mix_hash;

                    printf("KAWPoW miner: Solution found!\n");
                    printf("  Height: %d\n", pblock->nHeight);
                    printf("  Time: %d\n", pblock->nTime);
                    printf("  Hash: %s\n", final_hash.GetHex().c_str());
                    printf("  Mix hash: %s\n", pblock->mix_hash.GetHex().c_str());
                    printf("  Target: %s\n", hashTarget.GetHex().c_str());
                    printf("  Nonce: %016" PRIx64 "\n", pblock->nNonce64);
                    printf("  Epoch: %d\n", epoch_number);


                    // Validate the solution
                    uint256 checkHash, checkMixHash;
                    if (!CheckWorkKawpow(pblock.get(),
                                         *pwallet,
                                         reservekey,
                                         checkHash,
                                         checkMixHash,
                                         hashTarget))
                    {
                        printf(
                            "KAWPoW miner: Solution validation failed!\n");
                        SetThreadPriority(THREAD_PRIORITY_LOWEST);
                        break;
                    }

                    // Solution validated successfully
                    printf("KAWPoW miner: Block %d accepted by network\n",
                              nHeight);

                    // Log multi-currency rewards
                    for (size_t i = 0; i < pblock->vtx[0].vout.size(); i++)
                    {
                        const CTxOut& out = pblock->vtx[0].vout[i];
                        if (out.nValue > 0)
                        {
                            printf(
                                "KAWPoW reward: %s\n",
                                FormatMoney(out.nValue, out.nColor).c_str());
                        }
                    }

                    // Update statistics
                    {
                        LOCK(cs_kawpow_mining);
                        nKawpowHashesDone += nHashCount;
                    }

                    SetThreadPriority(THREAD_PRIORITY_LOWEST);

                    // Continue mining (start new block)
                    break;
                }

                // Increment nonce
                pblock->nNonce64++;

                // Check for nonce overflow (very unlikely with 64-bit nonce)
                if (pblock->nNonce64 == 0)
                {
                    printf("KAWPoW miner: Nonce overflow, restarting\n");
                    break;
                }
            }

            // Small delay before next iteration
            if (fKawpowMiningActive)
            {
                MilliSleep(100);
            }
        }
    }
    catch (const boost::thread_interrupted&)
    {
        printf("KAWPoW miner: Thread interrupted\n");
    }
    catch (const exception& e)
    {
        printf("KAWPoW miner: Exception: %s\n", e.what());
    }

    // Cleanup on exit
    {
        LOCK(cs_kawpow_mining);
        nKawpowThreadCount--;
        if (nKawpowThreadCount == 0)
        {
            fKawpowMiningActive = false;
            printf("KAWPoW miner: All threads stopped\n");
        }
    }

    printf("KAWPoW miner: Thread terminated\n");
}

// Helper function to start KAWPoW mining
int StartKawpowMining(bool fGenerate, int nThreads, CWallet* pwallet)
{
    static boost::thread_group* kawpowMinerThreads = nullptr;

    static CCriticalSection cs_kawpow_start;   // serialize start/stop transitions
    LOCK(cs_kawpow_start);

    int numCores = GetNumCores();
    if (nThreads < 0)
    {
        nThreads = numCores;
    }

    // Stop existing mining threads
    if (kawpowMinerThreads != nullptr)
    {
        printf("Stopping existing KAWPoW mining threads\n");
        StopKawpowMining();
        kawpowMinerThreads->interrupt_all();
        kawpowMinerThreads->join_all();
        delete kawpowMinerThreads;
        kawpowMinerThreads = nullptr;

        // Clear epoch cache
        {
            LOCK(cs_mining_epoch_cache);
            mining_epoch_cache.clear();
        }
    }

    if (nThreads == 0 || !fGenerate)
    {
        printf("KAWPoW mining disabled\n");
        return numCores;
    }

    if (!pwallet)
    {
        printf("Cannot start KAWPoW mining: No wallet available\n");
        return 0;
    }

    // Check if KAWPoW is activated
    int64_t nTimeNow = GetAdjustedTime();
    if (!KawpowIsActive(nTimeNow))
    {
        printf("KAWPoW mining: Waiting for activation time %" PRId64
               " (current: %" PRId64 ")\n",
               nKAWPOWActivationTime,
               nTimeNow);
    }

    printf("Starting KAWPoW mining with %d threads\n", nThreads);

    kawpowMinerThreads = new boost::thread_group();

    // Reset mining statistics
    {
        LOCK(cs_kawpow_mining);
        nKawpowMiningTimeStart = GetTimeMicros();
        nKawpowHashesDone = 0;
        nKawpowHashesPerSec = 0;
        nKawpowThreadCount = 0;
        fKawpowMiningActive = true;
    }

    // Start mining threads
    for (int i = 0; i < nThreads; i++)
    {
        kawpowMinerThreads->create_thread(
            boost::bind(&MineWithKawpow, pwallet));
    }

    return numCores;
}

// void StopSHA256dMining()
// {
//     LOCK(cs_kawpow_mining);
//     fSHA256dMiningActive = false;
//     printf("SHA256d mining stop signal sent\n");
// }

// Function to stop KAWPoW mining
void StopKawpowMining()
{
    LOCK(cs_kawpow_mining);
    fKawpowMiningActive = false;
    printf("KAWPoW mining stop signal sent\n");
}

void StopAllMining()
{
    // StopSHA256dMining();
    StopKawpowMining();
}
