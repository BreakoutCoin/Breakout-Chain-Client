// Copyright (c) 2026 The Breakout Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "ExploreCardInfo.hpp"

#include <boost/foreach.hpp>

using namespace json_spirit;
using namespace std;

void ExploreCardInfo::SetNull()
{
    holder.clear();
    mintBlock = -1;
    transfers.clear();
    stakes = 0;
}

ExploreCardInfo::ExploreCardInfo()
{
    SetNull();
}

bool ExploreCardInfo::IsNull() const
{
    return mintBlock == -1;
}

void ExploreCardInfo::AsJSON(const string& ticker, Object& objRet) const
{
    objRet.clear();
    objRet.push_back(Pair("ticker", ticker));
    objRet.push_back(Pair("holder", holder));
    objRet.push_back(Pair("mintBlock", mintBlock));
    Array aryTransfers;
    BOOST_FOREACH(const ExploreCardTransfer& t, transfers)
    {
        Object objTransfer;
        t.AsJSON(objTransfer);
        aryTransfers.push_back(objTransfer);
    }
    objRet.push_back(Pair("transfers", aryTransfers));
    objRet.push_back(Pair("stakes", stakes));
}
