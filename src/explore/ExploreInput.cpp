// Copyright (c) 2019 2020 The Stealth Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ExploreConstants.hpp"
#include "ExploreInput.hpp"


using namespace json_spirit;
using namespace std;

extern Value ValueFromAmount(int64_t amount, int nColor);

void ExploreInput::SetNull()
{
    txid = 0;
    vin = 0;
    prev_txid = 0;
    prev_vout = 0;
    color = 0;
    amount = 0;
    balance = 0;
}


void ExploreInput::Set(const uint256& txidIn,
                       const int vinIn,
                       const uint256& prev_txidIn,
                       const int prev_voutIn,
                       const int colorIn,
                       const int64_t amountIn,
                       const int64_t balanceIn)
{
    txid = txidIn;
    vin = vinIn;
    prev_txid = prev_txidIn;
    prev_vout = prev_voutIn;
    color = colorIn;
    amount = amountIn;
    balance = balanceIn;
}

void ExploreInput::AsJSON(Object& objRet) const
{
    objRet.clear();
    objRet.push_back(Pair("txid", txid.GetHex()));
    objRet.push_back(Pair("vin", (boost::int64_t)vin));
    objRet.push_back(Pair("prev_txid", prev_txid.GetHex()));
    objRet.push_back(Pair("prev_vout", (boost::int64_t)prev_vout));
    objRet.push_back(Pair("color", (boost::int64_t)color));
    objRet.push_back(Pair("amount", ValueFromAmount(amount, color)));
    objRet.push_back(Pair("balance", ValueFromAmount(balance, color)));
}

