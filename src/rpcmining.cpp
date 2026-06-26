// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2017-2021 The Raven Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "db.h"
#include "txdb-leveldb.h"
#include "init.h"
#include "miner.h"
#include "bitcoinrpc.h"

#include "ethash/helpers.hpp"

#include <boost/lexical_cast.hpp>

using namespace json_spirit;
using namespace std;

// Epoch context cache for mining
static map<int, shared_ptr<ethash_epoch_context>> mining_epoch_cache;
static CCriticalSection cs_mining_epoch_cache;

static const unsigned int DEFAULT_GENERATE_THREADS = 1;


// Function to get KAWPoW mining statistics
void GetKawpowMiningInfo(Object& obj)
{
    LOCK(cs_kawpow_mining);

    obj.push_back(Pair("kawpow_active", (uint64_t)fKawpowMiningActive));
    obj.push_back(Pair("kawpow_threads", (uint64_t)nKawpowThreadCount));
    obj.push_back(Pair("kawpow_hashrate", (uint64_t)nKawpowHashesPerSec));
    obj.push_back(Pair("kawpow_total_hashes", nKawpowHashesDone));

    if (nKawpowMiningTimeStart > 0)
    {
        int64_t nRunTime = (GetTimeMicros() - nKawpowMiningTimeStart) / 1000000;
        obj.push_back(Pair("kawpow_runtime_seconds", nRunTime));

        if (nRunTime > 0)
        {
            double avgHashrate = (double)nKawpowHashesDone / nRunTime;
            obj.push_back(Pair("kawpow_avg_hashrate", avgHashrate));
        }
    }

    // Add epoch cache information
    {
        LOCK(cs_mining_epoch_cache);
        Array epochArray;
        for (const auto& pair : mining_epoch_cache)
        {
            Object epochObj;
            epochObj.push_back(Pair("epoch", pair.first));
            epochObj.push_back(Pair("dag_size", (int64_t)ethash::get_full_dataset_size(pair.first)));
            epochArray.push_back(epochObj);
        }
        obj.push_back(Pair("cached_epochs", epochArray));
    }
}


bool TestBlockValidityKawpow(const CBlock& block,
                             CBlockIndex* pindexPrev,
                             bool fCheckPOW,
                             bool fCheckMerkleRoot)
{
    CValidationState& state = validationStateMain;

    // Ensure this is a KAWPoW block
    if (!block.IsKawpowBlock())
    {
        return state.Invalid(1, "not-kawpow-block");
    }

    // Validate KAWPoW proof of work if requested
    if (fCheckPOW)
    {
        uint256 mix_hash;
        uint256 hashBlock = block.GetHashFull(mix_hash);
        uint256 hashTarget = CBigNum().SetCompact(block.nBits).getuint256();

        if (hashBlock > hashTarget)
        {
            return validationStateMain.Invalid(2, "high-hash");
        }

        if (mix_hash != block.mix_hash)
        {
            return state.Invalid(3, "bad-mix-hash");
        }
    }

    // Validate merkle root if requested
    if (fCheckMerkleRoot)
    {
        uint256 hashMerkleRoot = block.BuildMerkleTree();
        if (block.hashMerkleRoot != hashMerkleRoot)
        {
            return state.Invalid(4, "bad-txnmrklroot");
        }
    }

    // Additional KAWPoW-specific validations
    if (!KawpowIsActive(block.nTime))
    {
        return state.Invalid(5, "kawpow-too-early");
    }

    // TODO: Implement these checks
    // Validate against standard block checks
    // return TestBlockValidity(state,
    //                          chainparams,
    //                          block,
    //                          pindexPrev,
    //                          fCheckPOW,
    //                          fCheckMerkleRoot);
    return state.Valid();
}

Value generateblocks(shared_ptr<CReserveKey> coinbaseScript,
                     int nGenerate,
                     uint64_t nMaxTries,
                     bool fKeepScript)
{
    static const int nInnerLoopCount = 0x10000;
    int nHeightEnd = 0;
    int nHeight = 0;

    {   // Don't keep cs_main locked
        LOCK(cs_main);
        nHeight = nBestHeight;
        nHeightEnd = nHeight + nGenerate;
    }

    unsigned int nExtraNonce = 0;
    Array blockHashes;

    // Determine which algorithm to use
    bool useKawpow = KawpowIsActive();

    printf("generateblocks: Using %s algorithm for %d blocks\n",
              useKawpow ? "KAWPoW" : "SHA256", nGenerate);

    while (nHeight < nHeightEnd)
    {
        int64_t nFees[N_COLORS];
        CBlock* pblock = CreateNewBlock(pwalletMain, pindexBest, false, nFees);
        if (!pblock)
        {
            throw JSONRPCError(RPC_INTERNAL_ERROR,
                               "Couldn't create new %s block",
                               useKawpow ? "KAWPoW" : "SHA256");
        }

        {
            LOCK(cs_main);
            IncrementExtraNonce(pblock, pindexBest, nExtraNonce);
        }

        // Mining loop
        if (useKawpow)
        {
            // KAWPoW mining
            bool found = false;
            uint64_t nTries = 0;

            // Get epoch context
            const int epoch_number = ethash::get_epoch_number(pblock->nHeight);
            auto context = ethash::create_epoch_context(epoch_number);

            // Get header hash
            uint256 headerHash = pblock->GetKAWPOWHeaderHash();
            ethash::hash256 header_hash_ethash;
            memcpy(header_hash_ethash.bytes, headerHash.begin(), 32);

            // Get target
            uint256 hashTarget = CompactToUint256(pblock->nBits);

            pblock->nNonce64 = GetRand(numeric_limits<uint64_t>::max());

            while (nMaxTries > 0 && nTries < nMaxTries)
            {
                // Calculate KAWPoW hash
                const auto result = progpow::hash(*context, pblock->nHeight,
                                                header_hash_ethash, pblock->nNonce64);

                // Convert to uint256
                uint256 mix_hash, final_hash;
                memcpy(mix_hash.begin(), result.mix_hash.bytes, 32);
                memcpy(final_hash.begin(), result.final_hash.bytes, 32);

                if (final_hash <= hashTarget)
                {
                    pblock->mix_hash = mix_hash;
                    found = true;
                    printf("generateblocks: KAWPoW solution found at nonce "
                           "%016" PRIx64 "\n",
                           pblock->nNonce64);
                    break;
                }

                pblock->nNonce64++;
                nTries++;
            }

            if (!found)
            {
                printf("generateblocks: KAWPoW mining exhausted %" PRIu64
                       " tries\n",
                       nTries);
                break;
            }
        }
        else
        {
            // SHA256 mining (existing logic)
            uint256 mix_hash; // Not used for SHA256
            while (nMaxTries > 0 && pblock->nNonce < nInnerLoopCount &&
                   !CheckProofOfWork(pblock->GetHash(), pblock->nBits, pblock))
            {
                ++pblock->nNonce;
                --nMaxTries;
            }

            if (nMaxTries == 0)
            {
                break;
            }
            if (pblock->nNonce == nInnerLoopCount)
            {
                continue;
            }
        }

        // Process the block
        bool fOrphanUnused;
        if (!ProcessBlock(nullptr, pblock, fOrphanUnused))
        {
            throw JSONRPCError(RPC_INTERNAL_ERROR,
                               "ProcessNewBlock, block not accepted");
        }

        ++nHeight;
        blockHashes.push_back(pblock->GetHash().GetHex());

        // Log multi-currency rewards for successful blocks
        if (pblock->vtx.size() > 0)
        {
            printf("generateblocks: Block %d rewards:\n", nHeight);
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
        }

        //mark script as important because it was used at least for one coinbase output
        if (fKeepScript)
        {
            coinbaseScript->KeepKey();
        }
    }

    return blockHashes;
}

Value defaultcurrency(const Array &params, bool fHelp)
{
    if (fHelp || params.size() != 1) {
        throw runtime_error(
            "defaultcurrency ticker\n");
    }
    if (!GetColorFromTicker(params[0].get_str(), nDefaultCurrency))
    {
        throw runtime_error("Invalid ticker.\n");
    }
    return Value::null;
}


Value defaultstake(const Array &params, bool fHelp)
{
    if (fHelp || params.size() != 1) {
        throw runtime_error("defaultstake ticker\n");
    }
    int nColor;
    if (!GetColorFromTicker(params[0].get_str(), nColor))
    {
        throw runtime_error("Invalid ticker.\n");
    }
    if (!CanStake(nColor))
    {
        throw runtime_error("Currency can not stake.\n");
    }
    nDefaultStake = nColor;
    return Value::null;
}

CBlockIndex* GetBlockIndexByNumber(int nHeight)
{
    CBlock block;
    CBlockIndex* pblockindex = mapBlockIndex[hashBestChain];
    while (pblockindex->nHeight > nHeight)
        pblockindex = pblockindex->pprev;

    uint256 hash = *pblockindex->phashBlock;

    pblockindex = mapBlockIndex[hash];
    block.ReadFromDisk(pblockindex, true);

    return pblockindex;
}




// TODO: allow to query for future blocks
Value getsubsidy(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2) {
        throw runtime_error(
            "getsubsidy [height] [height]\n"
            "If no height is provided, return subsidy of current block.\n"
            "Returns the pow reward for block at height or from one to the other.\n"
            "Biggest height should be no more than the last pow block.");
    }

    Object ret;


#if PROOF_MODEL == PURE_POS
    static const int nLastPoWBlock = GetLastPoWBlock();
#endif

    if (params.size() == 0) {
            struct AMOUNT subsidy = GetPoWSubsidy(pindexBest);
            ret.push_back(Pair("blockvalue", subsidy.nValue));
            ret.push_back(Pair("currency", COLOR_TICKER[subsidy.nColor]));
    }
    else if (params.size() == 1) {

            int nHeight = (int) params[0].get_int();
#if PROOF_MODEL == PURE_POS
            if (nHeight > GetLastPoWBlock() || nHeight < 1)
#else
            if (nHeight > (int) nBestHeight + 1 || nHeight < 1)
#endif
            {
                  throw runtime_error(
                    "getsubsidy [height] [height]\n"
                    "If no height is provided, return subsidy of current block.\n"
                    "Returns the pow reward for block at height or from one to the other.\n"
                    "Biggest height should be no more than the last pow block.");
            }

            CBlockIndex *pindexPrev;

            if (nHeight == (int) nBestHeight + 1)
            {
                 pindexPrev = pindexBest;
            }
            else
            {
                pindexPrev = GetBlockIndexByNumber(nHeight)->pprev;
            }

            struct AMOUNT subsidy = GetPoWSubsidy(pindexPrev);
            ret.push_back(Pair("blockvalue", subsidy.nValue));
            ret.push_back(Pair("currency", COLOR_TICKER[subsidy.nColor]));
    }
    else {
            int p1, p2;
            p1 = params[0].get_int();
            p2 = params[1].get_int();
#if PROOF_MODEL == PURE_POS
            if ((p1 < 1) || (p2 < 1) || (p1 > nLastPoWBlock) || (p2 > nLastPoWBlock)) {
#else
            if ((p1 < 1) || (p2 < 1) || (p1 > (int) nBestHeight) || (p2 > (int) nBestHeight))
            {
#endif
                  throw runtime_error(
                    "getsubsidy [height] [height]\n"
                    "If no height is provided, return subsidy of current block.\n"
                    "Returns the pow reward for block at height or from one to the other.\n"
                    "Biggest height should be no more than the last pow block.");
            }
            int first, last;
            if (p1 > p2) {
                    first = p2;
                    last = p1;
            }
            else {
                    first = p1;
                    last = p2;
            }

           for (int i = first; i <= last; i++) {
              Object obj;
              CBlockIndex *pindex = GetBlockIndexByNumber(i);
              struct AMOUNT subsidy = GetPoWSubsidy(pindex->pprev);
              obj.push_back(Pair("blockvalue", subsidy.nValue));
              obj.push_back(Pair("currency", COLOR_TICKER[subsidy.nColor]));
              ret.push_back(Pair(boost::lexical_cast<string>(i), obj));
           }
    }
    return ret;
}


Value getmininginfo(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getmininginfo\n"
            "Returns an object containing mining-related information.");

    bool hasPoS = (nNumberOfStakingCurrencies > 0);

    uint64_t nWeight = 0;
    if (hasPoS)
    {
        pwalletMain->GetStakeWeight(*pwalletMain, nWeight);
    }

    Object obj, diff, weight;

    obj.push_back(Pair("blocks",        (int)nBestHeight));
    obj.push_back(Pair("currentblocksize",(uint64_t)nLastBlockSize));
    obj.push_back(Pair("currentblocktx",(uint64_t)nLastBlockTx));

    diff.push_back(Pair("proof-of-work",        GetDifficulty(pindexBest)));
    if (hasPoS)
    {
        diff.push_back(Pair("proof-of-stake",       GetDifficulty(GetLastBlockIndex(pindexBest, true))));
        diff.push_back(Pair("search-interval",      (int)nLastCoinStakeSearchInterval));
    }
    obj.push_back(Pair("difficulty",    diff));

    struct AMOUNT subsidy = GetPoWSubsidy(pindexBest);
    obj.push_back(Pair("blockvalue", subsidy.nValue));
    obj.push_back(Pair("currency", COLOR_TICKER[subsidy.nColor]));
    obj.push_back(Pair("netmhashps",     GetPoWMHashPS()));
    if (KawpowIsActive())
    {
        GetKawpowMiningInfo(obj);
    }
    if (hasPoS)
    {
         obj.push_back(Pair("netstakeweight", GetPoSKernelPS()));
    }
    obj.push_back(Pair("errors",        GetWarnings("statusbar")));
    obj.push_back(Pair("pooledtx",      (uint64_t)mempool.size()));

    if (hasPoS)
    {
        weight.push_back(Pair("combined",  (uint64_t)nWeight));
        obj.push_back(Pair("stakeweight", weight));

        // IMPORTANT: if you use interest stake, then fix this stuff
        Object stakeObj;
        for (int nStakeColor = 1; nStakeColor < N_COLORS; ++nStakeColor)
        {
              struct AMOUNT subsidy = GetProofOfStakeReward(pindexBest, nStakeColor);
              if (subsidy.nValue > 0)
              {
                     Object mintObj;
                     mintObj.push_back(Pair(COLOR_TICKER[subsidy.nColor], (uint64_t) subsidy.nValue));
                     stakeObj.push_back(Pair(COLOR_TICKER[nStakeColor], mintObj));
              }
        }
        obj.push_back(Pair("stakereward",   stakeObj));
    }
    obj.push_back(Pair("testnet",       fTestNet));
    return obj;
}

Value getstakinginfo(const Array& params, bool fHelp)
{
    if (nNumberOfStakingCurrencies == 0)
    {
        throw runtime_error("No staking currencies.\n");
    }

    if (fHelp || params.size() != 0)
        throw runtime_error(
            "getstakinginfo\n"
            "Returns an object containing staking-related information.");

    int nTargetSpacing = GetTargetSpacing(true, pindexBest->nTime);


    uint64_t nWeight = 0;
    pwalletMain->GetStakeWeight(*pwalletMain, nWeight);

    uint64_t nNetworkWeight = GetPoSKernelPS();
    bool staking = nLastCoinStakeSearchInterval && nWeight;
    int nExpectedTime = staking ? (nTargetSpacing * nNetworkWeight / nWeight) : -1;

    Object obj;

    obj.push_back(Pair("enabled", GetBoolArg("-staking", true)));
    obj.push_back(Pair("staking", staking));
    obj.push_back(Pair("errors", GetWarnings("statusbar")));

    obj.push_back(Pair("currentblocksize", (uint64_t)nLastBlockSize));
    obj.push_back(Pair("currentblocktx", (uint64_t)nLastBlockTx));
    obj.push_back(Pair("pooledtx", (uint64_t)mempool.size()));

    obj.push_back(Pair("difficulty", GetDifficulty(GetLastBlockIndex(pindexBest, true))));
    obj.push_back(Pair("search-interval", (int)nLastCoinStakeSearchInterval));

    obj.push_back(Pair("weight", (uint64_t)nWeight));
    obj.push_back(Pair("netstakeweight", (uint64_t)nNetworkWeight));

    obj.push_back(Pair("expectedtime", nExpectedTime));

    return obj;
}

Value getworkex(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 2)
    {
        throw runtime_error("getworkex [data, coinbase]\n"
                            "If [data, coinbase] is not specified, returns "
                            "extended work data.\n");
    }

    if (vNodes.empty())
    {
        throw JSONRPCError(-9, "breakout is not connected!");
    }

    if (IsInitialBlockDownload())
    {
        throw JSONRPCError(-10, "breakout is downloading blocks...");
    }

#if PROOF_MODEL == PURE_POS
    static const int nLastPoWBlock = GetLastPoWBlock();
    if (pindexBest->nHeight >= nLastPoWBlock)
    {
        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
    }
#endif

    typedef map<uint256, pair<CBlock*, CScript>> mapNewBlock_t;
    static mapNewBlock_t mapNewBlock;
    static vector<CBlock*> vNewBlock;
    static CReserveKey reservekey(pwalletMain);

    if (params.size() == 0)
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64_t nStart = 0;
        static CBlock* pblock;
        if (pindexPrev != pindexBest ||
            ((nTransactionsUpdated != nTransactionsUpdatedLast) &&
             GetTime() - nStart > 60))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                BOOST_FOREACH (CBlock* pblock, vNewBlock)
                {
                    delete pblock;
                }
                vNewBlock.clear();
            }
            nTransactionsUpdatedLast = nTransactionsUpdated;
            pindexPrev = pindexBest;
            nStart = GetTime();

            // Create new block
            pblock = CreateNewBlock(pwalletMain, pindexPrev, false);
            if (!pblock)
            {
                throw JSONRPCError(-7, "Out of memory");
            }
            vNewBlock.push_back(pblock);
        }

        // Update nTime
        pblock->nTime = max(pindexPrev->GetPastTimeLimit() + 1,
                            GetAdjustedTime());
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(
            pblock,
            pblock->vtx[0].vin[0].scriptSig);

        // Prebuild hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        CTransaction coinbaseTx = pblock->vtx[0];
        vector<uint256> merkle = pblock->GetMerkleBranch(0);

        Object result;
        result.push_back(Pair("data", HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(
            Pair("target", HexStr(BEGIN(hashTarget), END(hashTarget))));

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << coinbaseTx;
        result.push_back(Pair("coinbase", HexStr(ssTx.begin(), ssTx.end())));

        Array merkle_arr;

        BOOST_FOREACH (uint256 merkleh, merkle)
        {
            merkle_arr.push_back(HexStr(BEGIN(merkleh), END(merkleh)));
        }

        result.push_back(Pair("merkle", merkle_arr));


        return result;
    }
    else
    {
        // Parse parameters
        valtype vchData = ParseHex(params[0].get_str());
        valtype coinbase;

        if (params.size() == 2)
        {
            coinbase = ParseHex(params[1].get_str());
        }

        if (vchData.size() != 128)
        {
            throw JSONRPCError(-8, "Invalid parameter");
        }

        CBlock* pdata = (CBlock*) &vchData[0];

        // Byte reverse
        for (int i = 0; i < 128 / 4; i++)
        {
            ((unsigned int*) pdata)[i] = ByteReverse(
                ((unsigned int*) pdata)[i]);
        }

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
        {
            return false;
        }
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;

        if (coinbase.size() == 0)
        {
            pblock->vtx[0]
                .vin[0]
                .scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        }
        else
        {
            CDataStream(coinbase, SER_NETWORK, PROTOCOL_VERSION) >>
                pblock->vtx[0];  // FIXME - AGS!
        }

        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        uint256 hashBlock;
        uint256 mix_hash;
        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();
        if (pblock->IsKawpowBlock())
        {
            return CheckWorkKawpow(pblock,
                                   *pwalletMain,
                                   reservekey,
                                   hashBlock,
                                   mix_hash,
                                   hashTarget);
        }
        else
        {
            return CheckWork(pblock,
                             *pwalletMain,
                             reservekey,
                             hashBlock,
                             mix_hash,
                             hashTarget);
        }
    }
}


Value getwork(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getwork [data]\n"
            "If [data] is not specified, returns formatted hash data to "
            "work on:\n"
            "  \"midstate\" : precomputed hash state after hashing the "
            "first half of the data (DEPRECATED)\n"  // deprecated
            "  \"data\" : block data\n"
            "  \"hash1\" : formatted hash buffer for second hash "
            "(DEPRECATED)\n"  // deprecated
            "  \"target\" : little endian hash target\n"
            "If [data] is specified, tries to solve the block and returns "
            "true if it was successful.");
    }

    if (vNodes.empty())
    {
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED,
                           "breakout is not connected!");
    }

    if (IsInitialBlockDownload())
    {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "breakout is downloading blocks...");
    }

#if PROOF_MODEL == PURE_POS
    static const int nLastPoWBlock = GetLastPoWBlock();
    if (pindexBest->nHeight >= nLastPoWBlock)
    {
        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
    }
#endif

    typedef map<uint256, pair<CBlock*, CScript>> mapNewBlock_t;
    static mapNewBlock_t mapNewBlock;  // FIXME: thread safety
    static vector<CBlock*> vNewBlock;
    static CReserveKey reservekey(pwalletMain);

    if (params.size() == 0)
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64_t nStart = 0;
        static CBlock* pblock;
        if ((pindexPrev != pindexBest) ||
            ((nTransactionsUpdated != nTransactionsUpdatedLast) &&
             (GetTime() - nStart > 60)) ||
            // give it 12 seconds for a new getwork (because future drift is
            // only 15 s)
            (pblock->GetBlockTime() >=
             FutureDrift((int64_t) pblock->vtx[0].nTime) + 12))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                BOOST_FOREACH (CBlock* pblock, vNewBlock)
                {
                    delete pblock;
                }
                vNewBlock.clear();
            }

            // Clear pindexPrev so future getworks make a new block, despite
            // any failures from here on
            pindexPrev = NULL;

            // Store the pindexBest used before CreateNewBlock, to avoid races
            nTransactionsUpdatedLast = nTransactionsUpdated;
            CBlockIndex* pindexPrevNew = pindexBest;
            nStart = GetTime();

            // Create new block
            pblock = CreateNewBlock(pwalletMain, pindexPrev, false);
            if (!pblock)
            {
                throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");
            }
            vNewBlock.push_back(pblock);

            // Need to update only after we know CreateNewBlock succeeded
            pindexPrev = pindexPrevNew;
        }

        // Update nTime
        pblock->UpdateTime(pindexPrev);
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(
            pblock,
            pblock->vtx[0].vin[0].scriptSig);

        // Pre-build hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        Object result;
        result.push_back(
            Pair("midstate",
                 HexStr(BEGIN(pmidstate), END(pmidstate))));  // deprecated
        result.push_back(Pair("data", HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(
            Pair("hash1", HexStr(BEGIN(phash1), END(phash1))));  // deprecated
        result.push_back(
            Pair("target", HexStr(BEGIN(hashTarget), END(hashTarget))));
        return result;
    }
    else
    {
        // Parse parameters
        valtype vchData = ParseHex(params[0].get_str());
        if (vchData.size() != 128)
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
        }
        CBlock* pdata = (CBlock*) &vchData[0];

        // Byte reverse
        for (int i = 0; i < 128 / 4; i++)
        {
            ((unsigned int*) pdata)[i] = ByteReverse(
                ((unsigned int*) pdata)[i]);
        }

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
        {
            return false;
        }
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;
        pblock->vtx[0].vin[0].scriptSig =
                 mapNewBlock[pdata->hashMerkleRoot].second;
        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        uint256 hashBlock;
        uint256 mix_hash;
        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();
        return CheckWork(pblock,
                         *pwalletMain,
                         reservekey,
                         hashBlock,
                         mix_hash,
                         hashTarget);
    }
}


Value getblocktemplatekawpow(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
    {
        throw runtime_error(
            "getblocktemplatekawpow [params]\n"
            "Returns data needed to construct a KAWPoW block for mining:\n"
            "  \"version\" : block version\n"
            "  \"previousblockhash\" : hash of current highest block\n"
            "  \"transactions\" : contents of non-coinbase transactions\n"
            "  \"coinbaseaux\" : data that should be included in coinbase\n"
            "  \"coinbasevalue\" : maximum allowable input to coinbase "
            "transaction\n"
            "  \"coinbasecurrencies\" : multi-currency coinbase values\n"
            "  \"target\" : hash target\n"
            "  \"mintime\" : minimum timestamp appropriate for next block\n"
            "  \"curtime\" : current timestamp\n"
            "  \"mutable\" : list of ways the block template may be changed\n"
            "  \"noncerange\" : range of valid nonces for KAWPoW\n"
            "  \"bits\" : compressed target of next block\n"
            "  \"height\" : height of the next block\n"
            "  \"pprpcheader\" : header hash for KAWPoW mining\n"
            "  \"pprpcepoch\" : DAG epoch number for KAWPoW\n"
            "See BIP 22 with KAWPoW extensions for full specification.");
    }

    bool fIsKawpow = KawpowIsActive();

    string strMode = "template";
    set<string> setClientRules;
    Value lpval;

    if (params.size() > 0)
    {
        const Object& oparam = params[0].get_obj();
        const Value& modeval = find_value(oparam, "mode");
        if (modeval.type() == str_type)
        {
            strMode = modeval.get_str();
        }
        else if (modeval.type() == null_type)
        {
            /* Do nothing */
        }
        else
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
        }

        lpval = find_value(oparam, "longpollid");
        // Long polling support
        if (!lpval.is_null())
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
             "Long polling not supported");
        }

        // no support for proposal mode (BIP 23)
        if (strMode == "proposal")
        {
            throw JSONRPCError(RPC_INVALID_PARAMETER,
                               "Proposals not supported");
        }

        const Value& aClientRules = find_value(oparam, "rules");
        if (aClientRules.type() == array_type)
        {
            Array ary = aClientRules.get_array();
            for (unsigned int i = 0; i < ary.size(); ++i)
            {
                const Value& v = ary[i];
                setClientRules.insert(v.get_str());
            }
        }
    }

    if (strMode != "template")
    {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
    }

    if (GetConnectionCount() == 0u)
    {
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED,
                           "Breakout is not connected!");
    }

    if (IsInitialBlockDownload())
    {
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD,
                           "Breakout is downloading blocks...");
    }

#if PROOF_MODEL == PURE_POS
    static const int nLastPoWBlock = GetLastPoWBlock();
    if (pindexBest->nHeight >= nLastPoWBlock)
    {
        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
    }
#endif

    static CReserveKey reservekey(pwalletMain);

    // Update block
    static CBlockIndex* pindexPrev = nullptr;
    static unique_ptr<CBlock> pblock;
    static string lastheader = "";

    // Cache whether the last invocation was with KAWPoW support
    static bool fLastTemplateKawpow = true;

    if (pindexPrev != pindexBest)
    {
        // Clear pindexPrev so future calls make a new block
        pindexPrev = nullptr;

        // Clear template cache
        if (fIsKawpow && !fLastTemplateKawpow)
        {
            lock_guard<mutex> lock(kawpowTemplateMutex);
            mapKawpowBlockTemplates.clear();
            fLastTemplateKawpow = true;
        }

        lastheader = "";

        // Store the pindexBest used before CreateNewBlock
        CBlockIndex* pindexPrevNew = pindexBest;

        // Create new KAWPoW block
        pblock.reset(CreateNewBlock(pwalletMain, pindexPrevNew, false));
        if (!pblock)
        {
            throw JSONRPCError(RPC_OUT_OF_MEMORY,
                               "Out of memory or keypool exhausted");
        }

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }

    CBlock* pblockCurrent = pblock.get();

    // Update nTime
    pblockCurrent->UpdateTime(pindexPrev);
    pblockCurrent->nNonce = 0;
    pblockCurrent->nNonce64 = 0;

    // Build transaction list
    Array transactions;
    map<uint256, int64_t> setTxIndex;
    int i = 0;
    CTxDB txdb("r");

    BOOST_FOREACH(CTransaction& tx, pblockCurrent->vtx)
    {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.DoesMature())
        {
            continue;
        }

        Object entry;

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << tx;
        entry.push_back(Pair("data", HexStr(ssTx.begin(), ssTx.end())));
        entry.push_back(Pair("txid", txHash.GetHex()));
        entry.push_back(Pair("hash", txHash.GetHex()));

        Array deps;
        BOOST_FOREACH(const CTxIn &in, tx.vin)
        {
            if (setTxIndex.count(in.prevout.hash))
                deps.push_back(setTxIndex[in.prevout.hash]);
        }
        entry.push_back(Pair("depends", deps));

        // Multi-currency fee handling
        MapPrevTx mapInputs;
        map<uint256, CTxIndex> mapUnused;
        bool fInvalid = false;
        if (tx.FetchInputs(txdb, mapUnused, false, false, mapInputs, fInvalid))
        {
            // %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
            // POOL OPERATORS - you are going to need specialized stratum
            // %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
            // Fees are in the fee currency, which could be implicit.
            // However we need to cycle here and push back ticker-value for fees
            // because miner can also take rounding fees from tx currency
            // %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
            Object feeObj;
            for (int nColor = 1; nColor < N_COLORS; ++nColor)
            {
                // Fees in some currencies can't be collected.
                if (!SCAVENGABLE[nColor])
                {
                    continue;
                }

                // allows for PoW miners to scavenge fees unclaimed by PoS
                int64_t nTotalMintPrev = (pindexPrev ? pindexPrev->vTotalMint[nColor] : 0);
                int64_t nMoneySupplyPrev = (pindexPrev ? pindexPrev->vMoneySupply[nColor] : 0);

                int fee = (tx.GetValueIn(mapInputs, nColor) - tx.GetValueOut(nColor)) +
                                                       (nTotalMintPrev - nMoneySupplyPrev);
                if (fee > 0)
                {
                     feeObj.push_back(Pair(COLOR_TICKER[nColor], fee));
                }
            }
            entry.push_back(Pair("fee", feeObj));
        }

        int64_t nSigOps = tx.GetLegacySigOpCount();
        if (mapInputs.size() > 0)
        {
            nSigOps += tx.GetP2SHSigOpCount(mapInputs);
        }
        entry.push_back(Pair("sigops", nSigOps));


        transactions.push_back(entry);
    }

    // Coinbase auxiliary data
    Object aux;
    aux.push_back(Pair("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end())));

    uint256 hashTarget = CBigNum().SetCompact(pblockCurrent->nBits).getuint256();

    static Array aMutable;
    if (aMutable.empty())
    {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    Object result;
    result.push_back(Pair("version", pblockCurrent->nVersion));
    result.push_back(Pair("previousblockhash", pblockCurrent->hashPrevBlock.GetHex()));
    result.push_back(Pair("transactions", transactions));
    result.push_back(Pair("coinbaseaux", aux));
    result.push_back(Pair("coinbasevalue", (int64_t)pblock->vtx[0].vout[0].nValue));
    result.push_back(Pair("coinbasecolor", (int64_t)pblock->vtx[0].vout[0].nColor));
    result.push_back(Pair("target", hashTarget.GetHex()));
    result.push_back(Pair("mintime", (int64_t)pindexPrev->GetPastTimeLimit()+1));
    result.push_back(Pair("mutable", aMutable));

    if (fIsKawpow)
    {
        // KAWPoW uses 64-bit nonce range
        result.push_back(Pair("noncerange", "0000000000000000ffffffffffffffff"));
    }
    else
    {
        result.push_back(Pair("noncerange", "00000000ffffffff"));
    }

    result.push_back(Pair("sigoplimit", (int64_t)MAX_BLOCK_SIGOPS));
    result.push_back(Pair("sizelimit", (int64_t)MAX_BLOCK_SIZE));
    result.push_back(Pair("curtime", (int64_t)pblockCurrent->GetBlockTime()));
    result.push_back(Pair("bits", HexBits(pblock->nBits)));
    result.push_back(Pair("height", (int64_t)(pindexPrev->nHeight + 1)));

    // KAWPoW specific fields
    if (fIsKawpow)
    {
        // Check if we can reuse a recent template
        if (!lastheader.empty())
        {
            lock_guard<mutex> lock(kawpowTemplateMutex);
            auto it = mapKawpowBlockTemplates.find(lastheader);
            if (it != mapKawpowBlockTemplates.end())
            {
                // Reuse template if it's less than 30 seconds old
                if (pblockCurrent->nTime - 30 < it->second.nTime)
                {
                    result.push_back(Pair("pprpcheader", lastheader));
                    result.push_back(Pair("pprpcepoch",
                                          ethash::get_epoch_number(
                                              pblockCurrent->nHeight)));

                    // Add DAG information
                    Object dagInfo;
                    dagInfo.push_back(Pair("epoch",
                                           ethash::get_epoch_number(
                                               pblockCurrent->nHeight)));
                    dagInfo.push_back(Pair("dagsize",
                                           ethash::get_full_dataset_size(
                                               ethash::get_epoch_number(
                                                   pblockCurrent->nHeight))));
                    dagInfo.push_back(Pair("cachesize",
                                           static_cast<int64_t>(
                                           ethash::get_light_cache_size(
                                               ethash::get_epoch_number(
                                                   pblockCurrent->nHeight)))));
                    result.push_back(Pair("dag", dagInfo));

                    return result;
                }
            }
        }

        // Create new template
        pblockCurrent->hashMerkleRoot = pblockCurrent->BuildMerkleTree();
        uint256 headerHash = pblockCurrent->GetKAWPOWHeaderHash();

        // Cache the template
        if (fIsKawpow)
        {
            lock_guard<mutex> lock(kawpowTemplateMutex);
            mapKawpowBlockTemplates[headerHash.GetHex()] = *pblockCurrent;
            lastheader = headerHash.GetHex();

            // Keep only last 10 templates
            while (mapKawpowBlockTemplates.size() > 10)
            {
                mapKawpowBlockTemplates.erase(mapKawpowBlockTemplates.begin());
            }
        }

        result.push_back(Pair("pprpcheader", headerHash.GetHex()));
        result.push_back(
            Pair("pprpcepoch",
                 ethash::get_epoch_number(pblockCurrent->nHeight)));

        // Add DAG information for miners
        Object dagInfo;
        int epoch = ethash::get_epoch_number(pblockCurrent->nHeight);
        dagInfo.push_back(Pair("epoch", epoch));
        dagInfo.push_back(
            Pair("dagsize", (int64_t) ethash::get_full_dataset_size(epoch)));
        dagInfo.push_back(
            Pair("cachesize", (int64_t) ethash::get_light_cache_size(epoch)));
        dagInfo.push_back(
            Pair("nextepoch",
                 ethash::get_epoch_number(pblockCurrent->nHeight +
                                          ETHASH_EPOCH_LENGTH)));
        result.push_back(Pair("dag", dagInfo));

        // Add mining algorithm info
        result.push_back(Pair("algorithm", "kawpow"));
        result.push_back(Pair("kawpowactivated", true));
    }
    else
    {
        // KAWPoW not yet activated
        result.push_back(Pair("algorithm", "sha256d"));
        result.push_back(Pair("kawpowactivated", false));
        result.push_back(
            Pair("kawpowactivationtime", (int64_t) nKAWPOWActivationTime));
    }

    return result;
}


Value getkawpowhash(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 4 || params.size() > 5)
    {
        throw runtime_error(
            "getkawpowhash \"header_hash\" \"mix_hash\" nonce height "
            "[\"target\"]\n"
            "\nCalculate and verify the KAWPoW hash for given block "
            "parameters.\n"
            "This is useful for testing mining implementations and "
            "validating shares.\n"

            "\nArguments:\n"
            "1. \"header_hash\"     (string, required) The KAWPoW header "
            "hash from getblocktemplate\n"
            "2. \"mix_hash\"        (string, required) The mix hash to "
            "verify (use \"\" to calculate)\n"
            "3. nonce              (string, required) The nonce value "
            "(hex string or decimal)\n"
            "4. height             (number, required) The block height\n"
            "5. \"target\"         (string, optional) The target hash to "
            "check against\n"

            "\nResult:\n"
            "{\n"
            "  \"header_hash\" : \"hash\",      (string) The input header "
            "hash\n"
            "  \"mix_hash\" : \"hash\",         (string) The calculated "
            "mix hash\n"
            "  \"final_hash\" : \"hash\",       (string) The final KAWPoW "
            "hash\n"
            "  \"nonce\" : n,                   (number) The nonce used\n"
            "  \"nonce_hex\" : \"hex\",         (string) The nonce in "
            "hexadecimal\n"
            "  \"height\" : n,                  (number) The block "
            "height\n"
            "  \"epoch\" : n,                   (number) The DAG epoch "
            "number\n"
            "  \"valid\" : true|false,          (boolean) Whether the "
            "solution is valid\n"
            "  \"mix_hash_valid\" : true|false, (boolean) Whether "
            "mix_hash matches\n"
            "  \"meets_target\" : true|false,   (boolean) Whether hash "
            "meets target (if provided)\n"
            "  \"difficulty\" : n.nnn,          (number) Difficulty of "
            "the hash\n"
            "  \"target\" : \"hash\",           (string) The target hash "
            "(if provided)\n"
            "  \"info\" : \"details\"           (string) Additional "
            "information or errors\n"
            "}\n"

            "\nExamples:\n"
            "\nCalculate KAWPoW hash for a nonce:\n" +
            HelpExampleCli("getkawpowhash",
                           "\"0abc...\" \"\" \"0x1000000\" 500000") +
            "\nVerify a complete solution:\n" +
            HelpExampleCli("getkawpowhash",
                           "\"0abc...\" \"0def...\" \"0x1000000\" 500000 "
                           "\"00000000ffff...\""));
    }

    // Parse parameters
    string str_header_hash = params[0].get_str();
    string str_mix_hash = params[1].get_str();
    string str_nonce = params[2].get_str();
    if (params[3].get_int() < 0)
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Height can't be negative");
    }
    uint32_t nHeight = static_cast<uint32_t>(params[3].get_int());

    unsigned int nTime;
    if (mapBlockLookup.count(nHeight))
    {
        nTime = mapBlockLookup[nHeight]->nTime;
    }
    else
    {
        nTime = GetAdjustedTime();
    }

    // Parse nonce (support both hex and decimal)
    uint64_t nNonce = 0;
    if (str_nonce.substr(0, 2) == "0x" || str_nonce.substr(0, 2) == "0X")
    {
        // Hex format
        if (!ParseUInt64(str_nonce.substr(2), 16, &nNonce))
        {
            throw JSONRPCError(RPC_INVALID_PARAMS,
                               "Invalid nonce hex string");
        }
    }
    else if (str_nonce.find_first_not_of("0123456789") == string::npos)
    {
        // Decimal format
        nNonce = stoull(str_nonce);
    }
    else
    {
        // Try as hex without prefix
        if (!ParseUInt64(str_nonce, 16, &nNonce))
        {
            throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid nonce format");
        }
    }

    // Validate height
    if (nHeight > (uint32_t) nBestHeight + 1000)
    {
        throw JSONRPCError(
            RPC_INVALID_PARAMS,
            strprintf(
                "Block height %u is too far in the future (current: %d)",
                nHeight,
                nBestHeight));
    }

    // Convert header hash
    ethash::hash256 header_hash;
    try
    {
        header_hash = to_hash256(str_header_hash);
    }
    catch (const exception& e)
    {
        throw JSONRPCError(RPC_INVALID_PARAMS,
                           strprintf("Invalid header_hash: %s", e.what()));
    }

    // Get epoch context for the height
    const int epoch_number = ethash::get_epoch_number(nHeight);

    // Log epoch context creation (for debugging)
    if (fDebugMiner)
    {
        printf("getkawpowhash: Creating epoch context for epoch %d "
                  "(height %u)\n",
                  epoch_number,
                  nHeight);
    }

    // Create epoch context (this may take time for first use of an epoch)
    static map<int, shared_ptr<ethash_epoch_context>> epoch_cache;
    static CCriticalSection cs_epoch_cache;

    shared_ptr<ethash_epoch_context> context;
    {
        LOCK(cs_epoch_cache);
        auto it = epoch_cache.find(epoch_number);
        if (it != epoch_cache.end())
        {
            context = it->second;
        }
        else
        {
            printf("getkawpowhash(): Creating DAG for epoch %d "
                      "(height %d)...\n",
                      epoch_number,
                      nHeight);

            auto start_dag = GetTimeMicros();
            // Wrap the unique_ptr's released pointer in a shared_ptr,
            // preserving the custom deleter.
            auto unique_ctx = ethash::create_epoch_context(epoch_number);
            context = shared_ptr<ethash_epoch_context>(
                unique_ctx.release(),
                ethash_destroy_epoch_context
            );
            auto end_dag = GetTimeMicros();

            printf("getkawpowhash(): DAG creation took %.2f seconds\n",
                      (end_dag - start_dag) / 1000000.0);

            // Cache the epoch (keep 2 latest epochs)
            epoch_cache[epoch_number] = context;
            if (epoch_cache.size() > 2)
            {
                epoch_cache.erase(epoch_cache.begin());
            }

            if (fDebugMiner)
            {
                printf("getkawpowhash(): Created and cached epoch %d\n",
                          epoch_number);
            }
        }
    }

    // Calculate ProgPoW hash
    auto start_time = GetTimeMicros();
    const auto result = progpow::hash(*context,
                                      nHeight,
                                      header_hash,
                                      nNonce);
    auto hash_time = GetTimeMicros() - start_time;

    // Convert results
    // uint256 calculated_mix_hash = uint256S(to_hex(result.mix_hash));
    // uint256 calculated_final_hash = uint256S(to_hex(result.final_hash));
    uint256 calculated_mix_hash = to_uint256(result.mix_hash);
    uint256 calculated_final_hash = to_uint256(result.final_hash);

    // Check if provided mix_hash matches (if provided)
    bool mix_hash_valid = true;
    bool mix_hash_match = false;
    bool mix_hash_provided = !str_mix_hash.empty();
    if (mix_hash_provided)
    {
        uint256 provided_mix_hash;
        try
        {
            provided_mix_hash = uint256S(str_mix_hash);
            mix_hash_valid = (provided_mix_hash == calculated_mix_hash);
            if (mix_hash_valid)
            {
                mix_hash_match = true;
            }
        }
        catch (...)
        {
            mix_hash_valid = false;
        }
    }

    // Check against target if provided
    bool has_target = (params.size() >= 5);
    bool meets_target = false;
    uint256 target;
    double difficulty = 0.0;

    uint256 nTargetLimit = GetTargetLimit(false, nTime).getuint256();

    if (has_target)
    {
        try
        {
            target = uint256S(params[4].get_str());
            meets_target = (calculated_final_hash <= target);

            // Calculate difficulty
            if (target > 0)
            {
                difficulty = (nTargetLimit / target).getdouble();
            }
        }
        catch (const exception& e)
        {
            throw JSONRPCError(RPC_INVALID_PARAMS,
                               strprintf("Invalid target: %s", e.what()));
        }
    }
    else
    {
        // Calculate difficulty from hash
        if (calculated_final_hash > 0)
        {
            difficulty = (nTargetLimit / calculated_final_hash).getdouble();
        }
    }

    // Determine overall validity
    bool is_valid = mix_hash_valid && (!has_target || meets_target);

    // Build result
    Object result_obj;
    result_obj.push_back(
        Pair("result", mix_hash_match ? "true" : "false"));
    result_obj.push_back(Pair("digest", calculated_final_hash.GetHex()));
    result_obj.push_back(Pair("header_hash", str_header_hash));
    result_obj.push_back(Pair("mix_hash", calculated_mix_hash.GetHex()));
    result_obj.push_back(
        Pair("final_hash", calculated_final_hash.GetHex()));
    result_obj.push_back(Pair("nonce", (uint64_t) nNonce));
    result_obj.push_back(
        Pair("nonce_hex", strprintf("0x%016" PRIx64, nNonce)));
    result_obj.push_back(Pair("height", (int) nHeight));
    result_obj.push_back(Pair("epoch", epoch_number));
    result_obj.push_back(Pair("valid", is_valid));

    if (mix_hash_provided)
    {
        result_obj.push_back(Pair("mix_hash_valid", mix_hash_valid));
        if (!mix_hash_valid)
        {
            result_obj.push_back(
                Pair("expected_mix_hash", calculated_mix_hash.GetHex()));
            result_obj.push_back(Pair("provided_mix_hash", str_mix_hash));
        }
    }

    if (has_target)
    {
        result_obj.push_back(Pair("meets_target", meets_target));
        result_obj.push_back(Pair("target", target.GetHex()));
    }

    result_obj.push_back(Pair("difficulty", difficulty));

    // Add DAG information
    Object dag_info;
    dag_info.push_back(Pair("epoch", epoch_number));
    dag_info.push_back(
        Pair("dag_size",
             (int64_t) ethash::get_full_dataset_size(epoch_number)));
    dag_info.push_back(
        Pair("cache_size",
             (int64_t) ethash::get_light_cache_size(epoch_number)));
    dag_info.push_back(Pair("epoch_length", ETHASH_EPOCH_LENGTH));
    result_obj.push_back(Pair("dag_info", dag_info));

    // Add performance metrics
    Object perf_info;
    perf_info.push_back(Pair("hash_time_us", hash_time));
    perf_info.push_back(Pair("hashrate_mhs", 1000000.0 / hash_time));
    result_obj.push_back(Pair("performance", perf_info));

    // Add info message
    string info;
    if (is_valid)
    {
        if (has_target && meets_target)
        {
            info = strprintf("Valid solution found! Difficulty: %.6f",
                             difficulty);
        }
        else if (mix_hash_valid)
        {
            info = strprintf(
                "Hash calculation successful. Difficulty: %.6f",
                difficulty);
        }
        else
        {
            info = "Hash calculation completed";
        }
    }
    else
    {
        if (!mix_hash_valid)
        {
            info = "Mix hash mismatch - invalid solution";
        }
        else if (has_target && !meets_target)
        {
            info = strprintf(
                "Hash does not meet target difficulty (%.6f required)",
                difficulty);
        }
        else
        {
            info = "Invalid solution";
        }
    }
    result_obj.push_back(Pair("info", info));

    // Add algorithm details
    Object algo_info;
    algo_info.push_back(Pair("algorithm", "kawpow"));
    algo_info.push_back(Pair("progpow_version", "0.9.4"));
    // algo_info.push_back(Pair("period_length", PROGPOW_PERIOD));
    // algo_info.push_back(Pair("dag_loads", PROGPOW_DAG_LOADS));
    // algo_info.push_back(Pair("cache_bytes", PROGPOW_CACHE_BYTES));
    // algo_info.push_back(Pair("cnt_dags", PROGPOW_CNT_DAG));
    // algo_info.push_back(Pair("cnt_cache", PROGPOW_CNT_CACHE));
    // algo_info.push_back(Pair("cnt_math", PROGPOW_CNT_MATH));
    result_obj.push_back(Pair("algorithm_details", algo_info));

    // Log successful validations
    if (is_valid && has_target && meets_target)
    {
        printf("getkawpowhash: Valid solution found at height %u, "
                  "nonce %" PRIu64 ", difficulty %.6f\n",
                  nHeight,
                  nNonce,
                  difficulty);
    }

    return result_obj;
}

Value submitblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "submitblock <hex data> [optional-params-obj]\n"
            "[optional-params-obj] parameter is currently ignored.\n"
            "Attempts to submit new block to network.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.");

    valtype blockData(ParseHex(params[0].get_str()));
    CDataStream ssBlock(blockData, SER_NETWORK, PROTOCOL_VERSION);
    CBlock block;
    try {
        ssBlock >> block;
    }
    catch (exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    if (fDebugMiner) {
          printf("The rpc block is.\n");
          block.print();
    }
    bool fOrphan;
    bool fAccepted = ProcessBlock(NULL, &block, fOrphan);
    if (!fAccepted)
        return "rejected";

    return Value::null;
}

// ProgPoW RPC Submit Block
Value pprpcsb(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 3)
        throw runtime_error(
            "pprpcsb <header_hash> <mix_hash> <nonce>\n"
            "ProgPoW RPC Submit Block\n"
            "Submit a KAWPoW solution from stratum mining\n"
            "<nonce> is a 64 bit hex nonce (0x...)\n");

    string header_hash = params[0].get_str();
    string mix_hash = params[1].get_str();
    string str_nonce = params[2].get_str();

    uint64_t nonce;
    if (!ParseUInt64(str_nonce, 16, &nonce))
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Invalid hex nonce");
    }

    // Retrieve the cached block template
    CBlock* pblock = GetKawpowBlockTemplate(header_hash);
    if (!pblock)
    {
        throw JSONRPCError(RPC_INVALID_PARAMS, "Block template not found");
    }

    // Get the target for validation
    uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

    // VALIDATE THE SUBMISSION HERE
    if (!ValidateKawpowSubmission(header_hash, mix_hash, nonce,
                                  pblock->nHeight, hashTarget))
    {
        delete pblock;
        return "invalid-solution";
    }

    // If validation passed, update the block with the solution
    pblock->nNonce64 = nonce;
    pblock->mix_hash = uint256S(mix_hash);

    // Process the block
    bool fOrphan;
    if (!ProcessBlock(NULL, pblock, fOrphan))
    {
        delete pblock;
        return "rejected";
    }

    delete pblock;
    return "accepted";
}

// Additional helper RPC for testing KAWPoW mining
Value testkawpow(const Array& params, bool fHelp)
{
    if (fHelp || params.size() != 2)
        throw runtime_error(
            "testkawpow height iterations\n"
            "\nTest KAWPoW hashing performance.\n"

            "\nArguments:\n"
            "1. height      (number, required) The block height to test\n"
            "2. iterations  (number, required) Number of hashes to calculate\n"

            "\nResult:\n"
            "{\n"
            "  \"height\" : n,           (number) The test height\n"
            "  \"epoch\" : n,            (number) The DAG epoch\n"
            "  \"iterations\" : n,       (number) Hashes calculated\n"
            "  \"time_ms\" : n,          (number) Total time in milliseconds\n"
            "  \"hashrate_mhs\" : n.nnn, (number) Hashrate in MH/s\n"
            "  \"best_hash\" : \"hash\", (string) Best hash found\n"
            "  \"best_nonce\" : n        (number) Nonce of best hash\n"
            "}\n"

            "\nExamples:\n"
            + HelpExampleCli("testkawpow", "500000 1000")
        );

    uint32_t nHeight = params[0].get_uint();
    uint32_t nIterations = params[1].get_uint();

    if (nIterations > 1000000)
        throw JSONRPCError(RPC_INVALID_PARAMS, "Iterations limited to 1,000,000");

    // Create test header hash
    uint256 headerHash = GetRandHash();
    ethash::hash256 header_hash;
    memcpy(header_hash.bytes, headerHash.begin(), 32);

    // Get epoch context
    const int epoch_number = ethash::get_epoch_number(nHeight);
    auto context = ethash::create_epoch_context(epoch_number);

    // Run test
    auto start_time = GetTimeMillis();
    uint256 bestHash = ~uint256();
    uint64_t bestNonce = 0;

    for (uint32_t i = 0; i < nIterations; i++)
    {
        uint64_t nonce = GetRand(numeric_limits<uint64_t>::max());
        const auto result = progpow::hash(*context, nHeight, header_hash, nonce);
        uint256 hash = uint256S(to_hex(result.final_hash));

        if (hash < bestHash)
        {
            bestHash = hash;
            bestNonce = nonce;
        }
    }

    auto elapsed = GetTimeMillis() - start_time;
    double hashrate = (nIterations * 1000.0) / elapsed / 1000000.0;

    Object result_obj;
    result_obj.push_back(Pair("height", (int)nHeight));
    result_obj.push_back(Pair("epoch", epoch_number));
    result_obj.push_back(Pair("iterations", (int)nIterations));
    result_obj.push_back(Pair("time_ms", elapsed));
    result_obj.push_back(Pair("hashrate_mhs", hashrate));
    result_obj.push_back(Pair("best_hash", bestHash.GetHex()));
    result_obj.push_back(Pair("best_nonce", (uint64_t)bestNonce));

    return result_obj;
}

Value setgenerate(const Array& params, bool fHelp)
{
    if (fHelp || (params.size() < 1) || (params.size() > 2))
    {
        throw runtime_error(
            "setgenerate <generate> [genproclimit]\n\n"
            "Set 'generate' true or false to turn generation on or off.\n"
            "Generation is limited to 'genproclimit' processors, -1 is "
               "unlimited.\n"

            "\nArguments:\n"
            "1. generate         (boolean, required) Set to true to turn on "
               "generation, false to turn off.\n"
            "2. genproclimit     (numeric, optional) Set the processor limit "
               "for when generation is on. Can be -1 for unlimited.\n"

            "\nResult:\n"
            "\"message\"          (string) Status message about mining "
                "threads\n"

            "\nExamples:\n\n"
            "Set the generation on with a limit of one processor using auto "
               "algorithm\n" +
            HelpExampleCli("setgenerate", "true 1") + "\n" +
            "Turn on mining with 4 threads\n" +
            HelpExampleCli("getmininginfo", "") + "\nTurn off generation\n" +
            HelpExampleCli("setgenerate", "false"));
    }

    bool fGenerate = true;
    if (params.size() > 0)
    {
        fGenerate = params[0].get_bool();
    }

    int nGenProcLimit = GetArg("-genproclimit", DEFAULT_GENERATE_THREADS);
    if (params.size() > 1)
    {
        nGenProcLimit = params[1].get_int();
        if (nGenProcLimit == 0)
        {
            fGenerate = false;
        }
    }

    int numCores = GetNumCores();

    if (!pwalletMain && fGenerate)
    {
        throw JSONRPCError(RPC_METHOD_NOT_FOUND,
                           "No wallet available for mining");
    }

    // Stop all existing mining first
    StopAllMining();

    if (!fGenerate)
    {
        // 10 second time out
        static constexpr int64_t POLL_TIMEOUT = 10000;
        int64_t nDeadline = GetTimeMillis() + POLL_TIMEOUT;
        while (nKawpowThreadCount != 0 && GetTimeMillis() < nDeadline)
        {
            MilliSleep(100);
        }
        char pchBuf[64];
        if (nKawpowThreadCount != 0)
        {
            snprintf(pchBuf,
                     sizeof(pchBuf),
                     "Mining disabled (WARNING: waiting on %d threads)\n",
                     (int)nKawpowThreadCount);
        }
        else
        {
            strncpy(pchBuf, "Mining disabled", sizeof(pchBuf) - 1);
        }
        pchBuf[sizeof(pchBuf) - 1] = '\0';
        return pchBuf;
    }

    // Determine which algorithm to use
    bool useKawpow = KawpowIsActive();
    string actualAlgorithm = "KawPow";

    // Start the appropriate mining
    int threadsStarted = 0;
    if (useKawpow)
    {
        threadsStarted = StartKawpowMining(fGenerate, nGenProcLimit, pwalletMain);
    }
    else
    {
        // threadsStarted = StartSHA256Mining(fGenerate,
        //                                    nGenProcLimit,
        //                                    pwalletMain);
        throw JSONRPCError(RPC_MISC_ERROR,
                           "SHA256d mining not supported by client");
    }

    if (threadsStarted == 0)
    {
        throw JSONRPCError(RPC_MISC_ERROR, "Failed to start mining threads");
    }


    nGenProcLimit = (nGenProcLimit >= 0) ? nGenProcLimit : numCores;

    // Update configuration
    SoftSetArg("-gen", (fGenerate ? "1" : "0"));
    SoftSetArg("-genproclimit", itostr(nGenProcLimit));

    string msg = strprintf("Mining started: %s algorithm, %d of %d threads",
                           actualAlgorithm.c_str(),
                           nGenProcLimit,
                           numCores);
    printf("%s\n", msg.c_str());

    return msg;
}
