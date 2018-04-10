// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "db.h"
#include "txdb.h"
#include "init.h"
#include "miner.h"
#include "bitcoinrpc.h"
#include <boost/lexical_cast.hpp>

using namespace json_spirit;
using namespace std;

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

    diff.push_back(Pair("proof-of-work",        GetDifficulty()));
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
        throw runtime_error(
            "getworkex [data, coinbase]\n"
            "If [data, coinbase] is not specified, returns extended work data.\n"
        );

    if (vNodes.empty())
        throw JSONRPCError(-9, "breakout is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(-10, "breakout is downloading blocks...");

#if PROOF_MODEL == PURE_POS
    static const int nLastPoWBlock = GetLastPoWBlock();
    if (pindexBest->nHeight >= nLastPoWBlock)
        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
#endif

    typedef map<uint256, pair<CBlock*, CScript> > mapNewBlock_t;
    static mapNewBlock_t mapNewBlock;
    static vector<CBlock*> vNewBlock;
    static CReserveKey reservekey(pwalletMain);

    if (params.size() == 0)
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64_t nStart;
        static CBlock* pblock;
        if (pindexPrev != pindexBest ||
            (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 60))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                BOOST_FOREACH(CBlock* pblock, vNewBlock)
                    delete pblock;
                vNewBlock.clear();
            }
            nTransactionsUpdatedLast = nTransactionsUpdated;
            pindexPrev = pindexBest;
            nStart = GetTime();

            // Create new block
            pblock = CreateNewBlock(pwalletMain);
            if (!pblock)
                throw JSONRPCError(-7, "Out of memory");
            vNewBlock.push_back(pblock);
        }

        // Update nTime
        pblock->nTime = max(pindexPrev->GetPastTimeLimit()+1, GetAdjustedTime());
        pblock->nNonce = 0;

        // Update nExtraNonce
        static unsigned int nExtraNonce = 0;
        IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

        // Save
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        // Prebuild hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        CTransaction coinbaseTx = pblock->vtx[0];
        std::vector<uint256> merkle = pblock->GetMerkleBranch(0);

        Object result;
        result.push_back(Pair("data",     HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(Pair("target",   HexStr(BEGIN(hashTarget), END(hashTarget))));

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << coinbaseTx;
        result.push_back(Pair("coinbase", HexStr(ssTx.begin(), ssTx.end())));

        Array merkle_arr;

        BOOST_FOREACH(uint256 merkleh, merkle) {
            merkle_arr.push_back(HexStr(BEGIN(merkleh), END(merkleh)));
        }

        result.push_back(Pair("merkle", merkle_arr));


        return result;
    }
    else
    {
        // Parse parameters
        vector<unsigned char> vchData = ParseHex(params[0].get_str());
        vector<unsigned char> coinbase;

        if(params.size() == 2)
            coinbase = ParseHex(params[1].get_str());

        if (vchData.size() != 128)
            throw JSONRPCError(-8, "Invalid parameter");

        CBlock* pdata = (CBlock*)&vchData[0];

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
            ((unsigned int*)pdata)[i] = ByteReverse(((unsigned int*)pdata)[i]);

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
            return false;
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;

        if(coinbase.size() == 0)
            pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        else
            CDataStream(coinbase, SER_NETWORK, PROTOCOL_VERSION) >> pblock->vtx[0]; // FIXME - AGS!

        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        return CheckWork(pblock, *pwalletMain, reservekey);
    }
}


Value getwork(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getwork [data]\n"
            "If [data] is not specified, returns formatted hash data to work on:\n"
            "  \"midstate\" : precomputed hash state after hashing the first half of the data (DEPRECATED)\n" // deprecated
            "  \"data\" : block data\n"
            "  \"hash1\" : formatted hash buffer for second hash (DEPRECATED)\n" // deprecated
            "  \"target\" : little endian hash target\n"
            "If [data] is specified, tries to solve the block and returns true if it was successful.");

    if (vNodes.empty())
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "breakout is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "breakout is downloading blocks...");

#if PROOF_MODEL == PURE_POS
    static const int nLastPoWBlock = GetLastPoWBlock();
    if (pindexBest->nHeight >= nLastPoWBlock)
        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
#endif

    typedef map<uint256, pair<CBlock*, CScript> > mapNewBlock_t;
    static mapNewBlock_t mapNewBlock;    // FIXME: thread safety
    static vector<CBlock*> vNewBlock;
    static CReserveKey reservekey(pwalletMain);

    if (params.size() == 0)
    {
        // Update block
        static unsigned int nTransactionsUpdatedLast;
        static CBlockIndex* pindexPrev;
        static int64_t nStart;
        static CBlock* pblock;
        if ((pindexPrev != pindexBest) ||
            ((nTransactionsUpdated != nTransactionsUpdatedLast) && (GetTime() - nStart > 60)) ||
            // give it 12 seconds for a new getwork (because future drift is only 15 s)
            (pblock->GetBlockTime() >= FutureDrift((int64_t)pblock->vtx[0].nTime) + 12))
        {
            if (pindexPrev != pindexBest)
            {
                // Deallocate old blocks since they're obsolete now
                mapNewBlock.clear();
                BOOST_FOREACH(CBlock* pblock, vNewBlock)
                    delete pblock;
                vNewBlock.clear();
            }

            // Clear pindexPrev so future getworks make a new block, despite any failures from here on
            pindexPrev = NULL;

            // Store the pindexBest used before CreateNewBlock, to avoid races
            nTransactionsUpdatedLast = nTransactionsUpdated;
            CBlockIndex* pindexPrevNew = pindexBest;
            nStart = GetTime();

            // Create new block
            pblock = CreateNewBlock(pwalletMain);
            if (!pblock)
                throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");
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
        mapNewBlock[pblock->hashMerkleRoot] = make_pair(pblock, pblock->vtx[0].vin[0].scriptSig);

        // Pre-build hash buffers
        char pmidstate[32];
        char pdata[128];
        char phash1[64];
        FormatHashBuffers(pblock, pmidstate, pdata, phash1);

        uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

        Object result;
        result.push_back(Pair("midstate", HexStr(BEGIN(pmidstate), END(pmidstate)))); // deprecated
        result.push_back(Pair("data",     HexStr(BEGIN(pdata), END(pdata))));
        result.push_back(Pair("hash1",    HexStr(BEGIN(phash1), END(phash1)))); // deprecated
        result.push_back(Pair("target",   HexStr(BEGIN(hashTarget), END(hashTarget))));
        return result;
    }
    else
    {
        // Parse parameters
        vector<unsigned char> vchData = ParseHex(params[0].get_str());
        if (vchData.size() != 128)
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid parameter");
        CBlock* pdata = (CBlock*)&vchData[0];

        // Byte reverse
        for (int i = 0; i < 128/4; i++)
            ((unsigned int*)pdata)[i] = ByteReverse(((unsigned int*)pdata)[i]);

        // Get saved block
        if (!mapNewBlock.count(pdata->hashMerkleRoot))
            return false;
        CBlock* pblock = mapNewBlock[pdata->hashMerkleRoot].first;

        pblock->nTime = pdata->nTime;
        pblock->nNonce = pdata->nNonce;
        pblock->vtx[0].vin[0].scriptSig = mapNewBlock[pdata->hashMerkleRoot].second;
        pblock->hashMerkleRoot = pblock->BuildMerkleTree();

        return CheckWork(pblock, *pwalletMain, reservekey);
    }
}


Value getblocktemplate(const Array& params, bool fHelp)
{
    if (fHelp || params.size() > 1)
        throw runtime_error(
            "getblocktemplate [params]\n"
            "Returns data needed to construct a block to work on:\n"
            "  \"version\" : block version\n"
            "  \"previousblockhash\" : hash of current highest block\n"
            "  \"transactions\" : contents of non-coinbase transactions that should be included in the next block\n"
            "  \"coinbaseaux\" : data that should be included in coinbase\n"
            "  \"coinbasevalue\" : maximum allowable input to coinbase transaction, including the generation award and transaction fees\n"
            "  \"target\" : hash target\n"
            "  \"mintime\" : minimum timestamp appropriate for next block\n"
            "  \"curtime\" : current timestamp\n"
            "  \"mutable\" : list of ways the block template may be changed\n"
            "  \"noncerange\" : range of valid nonces\n"
            "  \"sigoplimit\" : limit of sigops in blocks\n"
            "  \"sizelimit\" : limit of block size\n"
            "  \"bits\" : compressed target of next block\n"
            "  \"height\" : height of the next block\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.");

    std::string strMode = "template";
    if (params.size() > 0)
    {
        const Object& oparam = params[0].get_obj();
        const Value& modeval = find_value(oparam, "mode");
        if (modeval.type() == str_type)
            strMode = modeval.get_str();
        else if (modeval.type() == null_type)
        {
            /* Do nothing */
        }
        else
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");
    }

    if (strMode != "template")
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid mode");

    if (vNodes.empty())
        throw JSONRPCError(RPC_CLIENT_NOT_CONNECTED, "breakout is not connected!");

    if (IsInitialBlockDownload())
        throw JSONRPCError(RPC_CLIENT_IN_INITIAL_DOWNLOAD, "breakout is downloading blocks...");

#if PROOF_MODEL == PURE_POS
    static const int nLastPoWBlock = GetLastPoWBlock();
    if (pindexBest->nHeight >= nLastPoWBlock)
        throw JSONRPCError(RPC_MISC_ERROR, "No more PoW blocks");
#endif

    static CReserveKey reservekey(pwalletMain);

    // Update block
    static unsigned int nTransactionsUpdatedLast;
    static CBlockIndex* pindexPrev;
    static int64_t nStart;
    static CBlock* pblock;
    if (pindexPrev != pindexBest ||
        (nTransactionsUpdated != nTransactionsUpdatedLast && GetTime() - nStart > 5))
    {
        // Clear pindexPrev so future calls make a new block, despite any failures from here on
        pindexPrev = NULL;

        // Store the pindexBest used before CreateNewBlock, to avoid races
        nTransactionsUpdatedLast = nTransactionsUpdated;
        CBlockIndex* pindexPrevNew = pindexBest;
        nStart = GetTime();

        // Create new block
        if(pblock)
        {
            delete pblock;
            pblock = NULL;
        }
        pblock = CreateNewBlock(pwalletMain);
        if (!pblock)
            throw JSONRPCError(RPC_OUT_OF_MEMORY, "Out of memory");

        // Need to update only after we know CreateNewBlock succeeded
        pindexPrev = pindexPrevNew;
    }

    // Update nTime
    pblock->UpdateTime(pindexPrev);
    pblock->nNonce = 0;

    Array transactions;
    map<uint256, int64_t> setTxIndex;
    int i = 0;
    CTxDB txdb("r");
    BOOST_FOREACH (CTransaction& tx, pblock->vtx)
    {
        uint256 txHash = tx.GetHash();
        setTxIndex[txHash] = i++;

        if (tx.IsCoinBase() || tx.IsCoinStake())
            continue;

        Object entry;

        CDataStream ssTx(SER_NETWORK, PROTOCOL_VERSION);
        ssTx << tx;
        entry.push_back(Pair("data", HexStr(ssTx.begin(), ssTx.end())));

        entry.push_back(Pair("hash", txHash.GetHex()));

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

            Array deps;
            BOOST_FOREACH (MapPrevTx::value_type& inp, mapInputs)
            {
                if (setTxIndex.count(inp.first))
                    deps.push_back(setTxIndex[inp.first]);
            }
            entry.push_back(Pair("depends", deps));

            int64_t nSigOps = tx.GetLegacySigOpCount();
            nSigOps += tx.GetP2SHSigOpCount(mapInputs);
            entry.push_back(Pair("sigops", nSigOps));
        }

        transactions.push_back(entry);
    }

    Object aux;
    aux.push_back(Pair("flags", HexStr(COINBASE_FLAGS.begin(), COINBASE_FLAGS.end())));

    uint256 hashTarget = CBigNum().SetCompact(pblock->nBits).getuint256();

    static Array aMutable;
    if (aMutable.empty())
    {
        aMutable.push_back("time");
        aMutable.push_back("transactions");
        aMutable.push_back("prevblock");
    }

    Object result;
    result.push_back(Pair("version", pblock->nVersion));
    result.push_back(Pair("previousblockhash", pblock->hashPrevBlock.GetHex()));
    result.push_back(Pair("transactions", transactions));
    result.push_back(Pair("coinbaseaux", aux));
    result.push_back(Pair("coinbasevalue", (int64_t)pblock->vtx[0].vout[0].nValue));
    result.push_back(Pair("coinbasecolor", (int64_t)pblock->vtx[0].vout[0].nColor));
    result.push_back(Pair("target", hashTarget.GetHex()));
    result.push_back(Pair("mintime", (int64_t)pindexPrev->GetPastTimeLimit()+1));
    result.push_back(Pair("mutable", aMutable));
    result.push_back(Pair("noncerange", "00000000ffffffff"));
    result.push_back(Pair("sigoplimit", (int64_t)MAX_BLOCK_SIGOPS));
    result.push_back(Pair("sizelimit", (int64_t)MAX_BLOCK_SIZE));
    result.push_back(Pair("curtime", (int64_t)pblock->nTime));
    result.push_back(Pair("bits", HexBits(pblock->nBits)));
    result.push_back(Pair("height", (int64_t)(pindexPrev->nHeight+1)));

    return result;
}

Value submitblock(const Array& params, bool fHelp)
{
    if (fHelp || params.size() < 1 || params.size() > 2)
        throw runtime_error(
            "submitblock <hex data> [optional-params-obj]\n"
            "[optional-params-obj] parameter is currently ignored.\n"
            "Attempts to submit new block to network.\n"
            "See https://en.bitcoin.it/wiki/BIP_0022 for full specification.");

    vector<unsigned char> blockData(ParseHex(params[0].get_str()));
    CDataStream ssBlock(blockData, SER_NETWORK, PROTOCOL_VERSION);
    CBlock block;
    try {
        ssBlock >> block;
    }
    catch (std::exception &e) {
        throw JSONRPCError(RPC_DESERIALIZATION_ERROR, "Block decode failed");
    }

    if (fDebug) {
          printf("The rpc block is.\n");
          block.print();
    }
    bool fAccepted = ProcessBlock(NULL, &block);
    if (!fAccepted)
        return "rejected";

    return Value::null;
}

