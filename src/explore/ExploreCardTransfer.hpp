// Copyright (c) 2026 The Breakout Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _EXPLORECARDTRANSFER_H_
#define _EXPLORECARDTRANSFER_H_ 1

#include "uint256.h"
#include "serialize.h"

#include "json/json_spirit_utils.h"


// A single entry in a card's provenance: either its mint (coinbase) or a
// later change of holder (a plain send, or a "staketo" that moved the card
// to a different address while staking it). Plain same-address staking of a
// card is not ownership-changing and is not recorded here -- see
// ExploreCardInfo::stakes.
class ExploreCardTransfer
{
public:
    int block;
    uint256 txid;
    std::string to;
    std::string from;
    bool mint;
    unsigned int timestamp;

    void SetNull();

    ExploreCardTransfer();

    ExploreCardTransfer(const int blockIn,
                        const uint256& txidIn,
                        const std::string& toIn,
                        const std::string& fromIn,
                        const bool mintIn,
                        const unsigned int timestampIn);

    void AsJSON(json_spirit::Object& objRet) const;

    IMPLEMENT_SERIALIZE
    (
        READWRITE(block);
        READWRITE(txid);
        READWRITE(to);
        READWRITE(from);
        READWRITE(mint);
        READWRITE(timestamp);
    )
};

#endif  /* _EXPLORECARDTRANSFER_H_ */
