// Copyright (c) 2026 The Breakout Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _EXPLORECARDINFO_H_
#define _EXPLORECARDINFO_H_ 1

#include <vector>

#include "ExploreCardTransfer.hpp"

#include "json/json_spirit_utils.h"


typedef std::vector<ExploreCardTransfer> VecCardTransfer;

// Persistent per-card (deck NFT) provenance record, keyed by color in the
// exploredb. A card's total circulation is exactly 1 indivisible unit, so
// its entire history is an unbroken chain: the mint (coinbase) followed by
// one transfer per later change of holder.
class ExploreCardInfo
{
public:
    std::string holder;
    int mintBlock;
    VecCardTransfer transfers;
    int stakes;

    void SetNull();

    ExploreCardInfo();

    bool IsNull() const;

    void AsJSON(const std::string& ticker, json_spirit::Object& objRet) const;

    IMPLEMENT_SERIALIZE
    (
        READWRITE(holder);
        READWRITE(mintBlock);
        READWRITE(transfers);
        READWRITE(stakes);
    )
};

#endif  /* _EXPLORECARDINFO_H_ */
