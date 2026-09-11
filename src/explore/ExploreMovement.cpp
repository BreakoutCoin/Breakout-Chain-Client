// Copyright (c) 2026 The Breakout Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ExploreMovement.hpp"
#include "main.h"
#include "bitcoinrpc.h"   // ValueFromAmount
#include "colors.h"       // COLOR_TICKER

using namespace json_spirit;
using namespace std;

void ExploreMovement::SetNull()
{
    nVersion = ExploreMovement::CURRENT_VERSION;
    txid = 0;
    nHeight = -1;
    nValue = 0;
    nColor = 0;
}

ExploreMovement::ExploreMovement()
{
    SetNull();
}

ExploreMovement::ExploreMovement(const uint256& txidIn,
                                 const int nHeightIn,
                                 const int64_t nValueIn,
                                 const int nColorIn)
{
    nVersion = ExploreMovement::CURRENT_VERSION;
    txid = txidIn;
    nHeight = nHeightIn;
    nValue = nValueIn;
    nColor = nColorIn;
}

void ExploreMovement::AsJSON(Object& objRet) const
{
    objRet.clear();
    objRet.push_back(Pair("txid", txid.GetHex()));
    objRet.push_back(Pair("height", nHeight));
    objRet.push_back(Pair("value", ValueFromAmount(nValue, nColor)));
    objRet.push_back(Pair("currency", COLOR_TICKER[nColor]));
}
