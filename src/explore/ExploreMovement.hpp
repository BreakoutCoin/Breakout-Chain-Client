// Copyright (c) 2026 The Breakout Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef _EXPLOREMOVEMENT_H_
#define _EXPLOREMOVEMENT_H_ 1

#include "uint256.h"
#include "serialize.h"

#include "json/json_spirit_utils.h"


// One entry in the chain-wide movement index: a transaction that actually
// moved a non-trivial quantity of some currency between parties.
//
// Two conditions, both applied when the transaction is connected:
//
//   * some output is at least MOVEMENT_MIN of its currency, and
//   * some output pays an address that none of the inputs came from.
//
// The second is what makes the index worth having. On a proof-of-stake chain
// the overwhelming majority of large outputs are a stake returning its own
// principal to the address that staked it: at a cutoff of 100 coins those are
// 82% of all large outputs, and at 1000 they are 69%. They are not movement by
// any reading, and excluding them is what makes a cutoff low enough to be
// interesting also cheap enough to store.
//
// nValue is the largest qualifying output, kept so that a caller can ask for a
// higher cutoff than the one the index was built with, without the index
// having to be rebuilt to answer.
class ExploreMovement
{
private:
    int nVersion;
public:
    static const int CURRENT_VERSION = 1;

    uint256 txid;
    int nHeight;
    int64_t nValue;     // largest qualifying output, in that currency's toshis
    int nColor;         // currency of nValue

    void SetNull();

    ExploreMovement();

    ExploreMovement(const uint256& txidIn,
                    const int nHeightIn,
                    const int64_t nValueIn,
                    const int nColorIn);

    void AsJSON(json_spirit::Object& objRet) const;

    IMPLEMENT_SERIALIZE
    (
        READWRITE(this->nVersion);
        nSerVersion = this->nVersion;
        READWRITE(txid);
        READWRITE(nHeight);
        READWRITE(nValue);
        READWRITE(nColor);
    )
};

#endif  /* _EXPLOREMOVEMENT_H_ */
