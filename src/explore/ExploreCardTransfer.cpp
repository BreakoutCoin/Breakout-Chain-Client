// Copyright (c) 2026 The Breakout Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ExploreCardTransfer.hpp"

using namespace json_spirit;
using namespace std;

void ExploreCardTransfer::SetNull()
{
    block = 0;
    txid = 0;
    to.clear();
    from.clear();
    mint = false;
    timestamp = 0;
}

ExploreCardTransfer::ExploreCardTransfer()
{
    SetNull();
}

ExploreCardTransfer::ExploreCardTransfer(const int blockIn,
                                         const uint256& txidIn,
                                         const string& toIn,
                                         const string& fromIn,
                                         const bool mintIn,
                                         const unsigned int timestampIn)
{
    block = blockIn;
    txid = txidIn;
    to = toIn;
    from = fromIn;
    mint = mintIn;
    timestamp = timestampIn;
}

void ExploreCardTransfer::AsJSON(Object& objRet) const
{
    objRet.clear();
    objRet.push_back(Pair("block", block));
    objRet.push_back(Pair("txid", txid.GetHex()));
    objRet.push_back(Pair("to", to));
    objRet.push_back(Pair("from", from));
    objRet.push_back(Pair("mint", mint));
    objRet.push_back(Pair("timestamp", (boost::int64_t)timestamp));
}
