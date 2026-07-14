// Copyright (c) 2019 2020 The Stealth Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _EXPLOREHDTXINFO_H_
#define _EXPLOREHDTXINFO_H_ 1

#include "AddrInOutInfo.hpp"

#include <map>

class HDTxInfo
{
public:
    std::set<AddrInOutInfo> addrinouts;
    VecDest payees;
    ExploreTx extx;

    void SetNull();

    HDTxInfo();

    bool operator < (const HDTxInfo& other) const;
    bool operator > (const HDTxInfo& other) const;

    void SetPayees();

    const AddrInOutInfo* GetLast() const;
    const uint256* GetTxID() const;

    // Breakout is multi-color, so an HD account's balance change for a single
    // transaction is per-color (a tx can move several colors at once).
    void GetAccountBalanceChange(std::map<int, int64_t>& mapChangeRet) const;


    void AsJSON(const int nBestHeight,
                const unsigned int i,
                json_spirit::Object& objRet) const;

    void AsJSON(const int nBestHeight, json_spirit::Object& objRet) const;
};

#endif  /* _EXPLOREHDTXINFO_H_ */
