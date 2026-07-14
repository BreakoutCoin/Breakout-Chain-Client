// Copyright (c) 2020 The Stealth Developers
// Copyright (c) 2026 The Breakout Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _BREAKOUTEXPLORE_H_
#define _BREAKOUTEXPLORE_H_ 1

#include <map>
#include <set>

#include "ExploreConstants.hpp"
#include "ExploreInput.hpp"
#include "ExploreOutput.hpp"
#include "ExploreInOutLookup.hpp"
#include "ExploreInOutList.hpp"
#include "ExploreTx.hpp"

class CBlock;
class CTransaction;
class CTxDB;
class CExploreDB;


// Breakout Explore is color-aware. The in-memory rich-list cache and the
// per-tx balance-change accumulators are therefore keyed by color.
//   MapColorBalances:       color -> (balance -> number of addresses)
//   MapColorBalancesRemove: color -> set of balances to drop from the cache
typedef std::map<int, MapBalanceCounts> MapColorBalances;
typedef std::map<int, std::set<int64_t> > MapColorBalancesRemove;


extern bool fWithExploreAPI;
extern bool fDebugExplore;
extern bool fReindexExplore;

// Per color, the in-memory ordered map used to serve the rich list.
extern MapColorBalances mapAddressBalances;


void UpdateMapAddressBalances(const MapColorBalances& mapAddressBalancesAdd,
                              const MapColorBalancesRemove& setAddressBalancesRemove,
                              MapColorBalances& mapAddressBalancesRet);

// The explore engine reads spent prevouts through the transaction index
// (CTxDB) and writes the address/tx index to the separate explore DB
// (CExploreDB).
bool ExploreConnectTx(CTxDB& txdb, CExploreDB& exploredb,
                      const CTransaction& tx,
                      const uint256& hashBlock,
                      const unsigned int nBlockTime,
                      const int nHeight,
                      const int nVtx);
bool ExploreConnectBlock(CTxDB& txdb, CExploreDB& exploredb, const CBlock *const block);

bool ExploreDisconnectTx(CTxDB& txdb, CExploreDB& exploredb, const CTransaction &tx);
bool ExploreDisconnectBlock(CTxDB& txdb, CExploreDB& exploredb, const CBlock *const block);


#endif  // _BREAKOUTEXPLORE_H_
