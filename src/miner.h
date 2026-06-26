// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2013 The NovaCoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef NOVACOIN_MINER_H
#define NOVACOIN_MINER_H

#include "main.h"
#include "wallet.h"


extern CCriticalSection cs_kawpow_mining;
extern std::atomic<bool> fKawpowMiningActive;
extern uint64_t nKawpowHashesDone;
extern uint64_t nKawpowHashesPerSec;
extern int64_t nKawpowMiningTimeStart;
extern std::atomic<int> nKawpowThreadCount;

extern std::map<std::string, CBlock> mapKawpowBlockTemplates;
extern std::mutex kawpowTemplateMutex;

CBlock* GetKawpowBlockTemplate(const std::string& headerHash);
void CleanKawpowTemplates(int64_t nExpiryTime);

/* Generate a new block, without valid proof-of-work */
CBlock* CreateNewBlock(CWallet* pwallet,
                       CBlockIndex* pindexPrev,
                       bool fProofOfStake,
                       int64_t* pFees = NULL);

/** Modify the extranonce in a block */
void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, unsigned int& nExtraNonce);

/** Do mining precalculation */
void FormatHashBuffers(CBlock* pblock, char* pmidstate, char* pdata, char* phash1);

/** Check mined proof-of-work block */
bool CheckWork(CBlock* pblock,
               CWallet& wallet,
               CReserveKey& reservekey,
               uint256& hashBlock,
               uint256& mix_hash,
               const uint256& hashTarget);

bool CheckWorkKawpow(CBlock* pblock,
                     CWallet& wallet,
                     CReserveKey& reservekey,
                     uint256& hashBlock,
                     uint256& mix_hash,
                     const uint256& hashTarget);

/** Check mined proof-of-stake block */
bool CheckStake(CBlock* pblock, CWallet& wallet);

void MineWithKawpow(CWallet* pwallet);
int  StartKawpowMining(bool fGenerate, int nThreads, CWallet* pwallet);
void StopKawpowMining();
void StopAllMining();
bool ValidateKawpowSubmission(const std::string& header_hash_hex,
                              const std::string& mix_hash_hex,
                              uint64_t nonce,
                              int height,
                              const uint256& target);

/** Base sha256 mining transform */
void SHA256Transform(void* pstate, void* pinput, const void* pinit);

#endif // NOVACOIN_MINER_H
