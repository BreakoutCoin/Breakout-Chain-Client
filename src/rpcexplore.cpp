// Copyright (c) 2020 Stealth R&D LLC
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main.h"
#include "bitcoinrpc.h"
#include "txdb-leveldb.h"
#include "base58.h"

#include <functional>
#include <limits>
#include <cmath>

#include <map>

#include "exploredb-leveldb.h"
#include "explore/explore.hpp"
#include "explore/AddrTxInfo.hpp"
#include "explore/AddrInOutInfo.hpp"
#include "explore/HDTxInfo.hpp"
#include "explore/ExploreInOutList.hpp"
#include "explore/ExploreInOutLookup.hpp"
#include "explore/ExploreTx.hpp"

#include "bip32/hdkeys.h"


using namespace json_spirit;
using namespace std;


extern MapColorBalances mapAddressBalances;

static const unsigned int SEC_PER_DAY = 86400;

//
// Check Explore API
//
string CheckExploreAPI(bool& fHelp)
{
    if (fWithExploreAPI)
    {
       return "== Explore API ==\n";
    }
    else if (fHelp)
    {
       return "== Explore API ==\n";
    }
    else
    {
       fHelp = true;
       return "** ERROR: Explore API only **\n";
    }
}


//
// Address decoding
//
// Breakout addresses encode their color, so decoding a user-supplied address
// yields both the canonical (color-scoped) address string used as the explore
// key and the color needed to look up per-color records. Throws if invalid.
int ExploreAddrColor(string& strAddress)
{
    CBitcoinAddress addr(strAddress);
    if (!addr.IsValid())
    {
        throw runtime_error("Invalid address.");
    }
    strAddress = addr.ToString();
    return addr.nColor;
}


//
// Inputs/Outputs
//
int GetInOutID(const int nData)
{
    return nData & MASK_ADDR_TX;
}

void GetInputInfo(CExploreDB& exploredb,
                   const string& strAddress, int nColor,
                   const int id,
                   vector<AddrTxInfo>& vRet)
{
    ExploreInput input;
    if (!exploredb.ReadAddrTx(ADDR_TX_INPUT, strAddress, nColor, id, input))
    {
        throw runtime_error("TSNH: Problem reading input.");
    }
    bool fMakeNew = false;
    if (vRet.empty())
    {
        fMakeNew = true;
    }
    else if (*vRet.back().GetTxID() != input.txid)
    {
        fMakeNew = true;
    }
    if (fMakeNew)
    {
        ExploreTx extx;
        if (!exploredb.ReadExploreTx(input.txid, extx))
        {
            throw runtime_error("TSNH: Problem reading transaction.");
        }
        InOutInfo inout(extx.height, extx.vtx, input);
        AddrTxInfo addrtx;
        addrtx.address = strAddress;
        addrtx.extx = extx;
        addrtx.inouts.insert(inout);
        vRet.push_back(addrtx);
    }
    else
    {
        AddrTxInfo& addrtx = vRet.back();
        InOutInfo inout(addrtx.extx.height, addrtx.extx.vtx, input);
        addrtx.inouts.insert(inout);
    }
}

void GetAddrInputs(CExploreDB& exploredb,
                   const string& strAddress, int nColor,
                   const int nStart,
                   const int nMax,
                   const int nQtyInputs,
                   vector<AddrTxInfo>& vRet)
{
    int nStop = min(nStart + nMax - 1, nQtyInputs);
    for (int id = nStart; id <= nStop; ++id)
    {
        GetInputInfo(exploredb, strAddress, nColor, id, vRet);
    }
}

void GetOutputInfo(CExploreDB& exploredb,
                   const string& strAddress, int nColor,
                   const int id,
                   vector<AddrTxInfo>& vRet)
{
    ExploreOutput output;
    if (!exploredb.ReadAddrTx(ADDR_TX_OUTPUT, strAddress, nColor, id, output))
    {
        throw runtime_error("TSNH: Problem reading output.");
    }
    if (vRet.empty() || (*vRet.back().GetTxID() != output.txid))
    {
        ExploreTx extx;
        if (!exploredb.ReadExploreTx(output.txid, extx))
        {
            throw runtime_error("TSNH: Problem reading transaction.");
        }
        InOutInfo inout(extx.height, extx.vtx, output);
        AddrTxInfo addrtx;
        addrtx.address = strAddress;
        addrtx.extx = extx;
        addrtx.inouts.insert(inout);
        vRet.push_back(addrtx);
    }
    else
    {
        AddrTxInfo& addrtx = vRet.back();
        InOutInfo inout(addrtx.extx.height, addrtx.extx.vtx, output);
        addrtx.inouts.insert(inout);
    }
}

void GetAddrOutputs(CExploreDB& exploredb,
                    const string& strAddress, int nColor,
                    const int nStart,
                    const int nMax,
                    const int nQtyOutputs,
                    vector<AddrTxInfo>& vRet)
{
    int nStop = min(nStart + nMax - 1, nQtyOutputs);
    for (int i = nStart; i <= nStop; ++i)
    {
        GetOutputInfo(exploredb, strAddress, nColor, i, vRet);
    }
}

void GetInOut(CExploreDB& exploredb,
              const string& strAddress, int nColor,
              const int id,
              vector<AddrTxInfo>& vAddrTxRet)
{
    ExploreInOutLookup inout;
    if (!exploredb.ReadAddrTx(ADDR_TX_INOUT, strAddress, nColor, id, inout))
    {
        throw runtime_error("TSNH: Problem reading inout.");
    }
    if (inout.IsInput())
    {
        GetInputInfo(exploredb, strAddress, nColor, inout.GetID(), vAddrTxRet);
    }
    else
    {
        GetOutputInfo(exploredb, strAddress, nColor, inout.GetID(), vAddrTxRet);
    }
}

void GetInOuts(CExploreDB& exploredb,
               const string& strAddress, int nColor,
               const int nStart,
               const int nMax,
               const int nQtyInOuts,
               vector<AddrTxInfo>& vAddrTxRet)
{
    int nStop = min(nStart + nMax - 1, nQtyInOuts);
    for (int i = nStart; i <= nStop; ++i)
    {
        GetInOut(exploredb, strAddress, nColor, i, vAddrTxRet);
    }
}


//
// Addresses
//
void GetAddrInfo(const string& strAddress, int nColor, Object& objRet)
{
    CExploreDB exploredb;

    if (!exploredb.AddrValueIsViable(ADDR_BALANCE, strAddress, nColor))
    {
        throw runtime_error("Address does not exist.");
    }

    int64_t nBalance;
    if (!exploredb.ReadAddrValue(ADDR_BALANCE, strAddress, nColor, nBalance))
    {
         throw runtime_error("TSNH: Can't read balance.");
    }

    int nQtyVIO;
    if (!exploredb.ReadAddrQty(ADDR_QTY_VIO, strAddress, nColor, nQtyVIO))
    {
         throw runtime_error("TSNH: Can't read number of transactions.");
    }

    int nQtyOutputs;
    if (!exploredb.ReadAddrQty(ADDR_QTY_OUTPUT, strAddress, nColor, nQtyOutputs))
    {
         throw runtime_error("TSNH: Can't read number of outputs.");
    }

    int64_t nValueIn;
    if (!exploredb.ReadAddrValue(ADDR_VALUEIN, strAddress, nColor, nValueIn))
    {
         throw runtime_error("TSNH: Can't read total value in.");
    }

    int nQtyInputs;
    if (!exploredb.ReadAddrQty(ADDR_QTY_INPUT, strAddress, nColor, nQtyInputs))
    {
         throw runtime_error("TSNH: Can't read number of inputs.");
    }

    int64_t nValueOut;
    if (!exploredb.ReadAddrValue(ADDR_VALUEOUT, strAddress, nColor, nValueOut))
    {
         throw runtime_error("TSNH: Can't read total value out.");
    }

    int nRank = 0;
    if (nBalance > CENT[nColor])
    {
        const MapBalanceCounts& mapForColor = mapAddressBalances[nColor];
        MapBalanceCounts::const_iterator it;
        for (it = mapForColor.begin(); it != mapForColor.end(); ++it)
        {
            if ((*it).first < nBalance)
            {
                break;
            }
            else if ((*it).first == nBalance)
            {
                // all in a tie have the same rank
                nRank += 1;
                break;
            }
            else
            {
                nRank += (*it).second;
            }
        }
    }

    int nQtyUnspent = nQtyOutputs - nQtyInputs;

    objRet.push_back(Pair("address", strAddress));
    objRet.push_back(Pair("color", (boost::int64_t)nColor));
    objRet.push_back(Pair("balance", ValueFromAmount(nBalance, nColor)));
    objRet.push_back(Pair("rank", (boost::int64_t)nRank));
    objRet.push_back(Pair("transactions", (boost::int64_t)nQtyVIO));
    objRet.push_back(Pair("outputs", (boost::int64_t)nQtyOutputs));
    objRet.push_back(Pair("received", ValueFromAmount(nValueIn, nColor)));
    objRet.push_back(Pair("inputs", (boost::int64_t)nQtyInputs));
    objRet.push_back(Pair("sent", ValueFromAmount(nValueOut, nColor)));
    objRet.push_back(Pair("unspent", (boost::int64_t)nQtyUnspent));
    objRet.push_back(Pair("in-outs", (boost::int64_t)(nQtyOutputs + nQtyInputs)));
    objRet.push_back(Pair("blocks", (boost::int64_t)nBestHeight));
}

void GetAddrTx(CExploreDB& exploredb,
               const string& strAddress, int nColor,
               const int i,
               AddrTxInfo& addrtxRet)
{
    ExploreInOutList vIO;
    if (!exploredb.ReadAddrList(ADDR_LIST_VIO, strAddress, nColor, i, vIO))
    {
        throw runtime_error("TSNH: Can't read transaction in-outs");
    }
    // sanity check: ensure the vio has transactions
    if (vIO.vinouts.empty())
    {
        throw runtime_error("TSNH: transaction has no in-outs");
    }
    addrtxRet.inouts.clear();
    BOOST_FOREACH(const int& j, vIO.vinouts)
    {
        InOutInfo inoutinfo(j);
        if (inoutinfo.IsInput())
        {
            if (!exploredb.ReadAddrTx(ADDR_TX_INPUT, strAddress, nColor, GetInOutID(j),
                                 inoutinfo.inout.input))
            {
                throw runtime_error("TSNH: Problem reading input.");
            }
        }
        else
        {
            if (!exploredb.ReadAddrTx(ADDR_TX_OUTPUT, strAddress, nColor, GetInOutID(j),
                                 inoutinfo.inout.output))
            {
                throw runtime_error("TSNH: Problem reading input.");
            }
        }
        const uint256* ptxid = inoutinfo.GetTxID();
        if (addrtxRet.extx.IsNull())
        {
            if (!exploredb.ReadExploreTx(*ptxid, addrtxRet.extx))
            {
                throw runtime_error("TSNH: Problem reading transaction.");
            }
        }
        else if (*(addrtxRet.GetTxID()) != *ptxid)
        {
           throw runtime_error("TSNH: In-outs not from the same tx.");
        }
        addrtxRet.address = strAddress;
        inoutinfo.height = addrtxRet.extx.height;
        inoutinfo.vtx = addrtxRet.extx.vtx;
        addrtxRet.inouts.insert(inoutinfo);
    }
}

void GetAddrTxs(CExploreDB& exploredb,
                const string& strAddress, int nColor,
                const int nStart,
                const int nMax,
                const int nQtyTxs,
                vector<AddrTxInfo>& vAddrTxRet)
{
    int nStop = min(nStart + nMax - 1, nQtyTxs);
    for (int i = nStart; i <= nStop; ++i)
    {
        AddrTxInfo addrtx;
        GetAddrTx(exploredb, strAddress, nColor, i, addrtx);
        vAddrTxRet.push_back(addrtx);
    }
}


//
// Stats
//
class StatHelper
{
public:
    string label;
    std::function<int64_t(CBlockIndex*)> Get;
    Value (*Reduce)(const vector<int64_t>&);

    // No CTxDB
    StatHelper(const string& labelIn,
               int64_t (*GetIn)(CBlockIndex*),
               Value (*ReduceIn)(const vector<int64_t>&))
        : label(labelIn), Get(GetIn), Reduce(ReduceIn) {}

    // Using a CTxDB
    StatHelper(const string& labelIn,
               int64_t (*GetIn)(CBlockIndex*, CTxDB*),
               Value (*ReduceIn)(const vector<int64_t>&),
               CTxDB* ptxdb)
        : label(labelIn),
          Get([GetIn, ptxdb](CBlockIndex* p) { return GetIn(p, ptxdb); }),
          Reduce(ReduceIn) {}

    string GetLabel() const { return label; }
};


//
// HD Wallets
//

//////////////////////////////////////////////////////////////////////////////
//
// Addresses

Value getaddressbalance(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size()  != 1))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddressbalance <address>\n"
            "Returns the balance of <address>.");
    }

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    CExploreDB exploredb;

    if (!exploredb.AddrValueIsViable(ADDR_BALANCE, strAddress, nColor))
    {
        throw runtime_error("Address does not exist.");
    }

    int64_t nBalance;
    if (!exploredb.ReadAddrValue(ADDR_BALANCE, strAddress, nColor, nBalance))
    {
         throw runtime_error("TSNH: Can't read balance.");
    }

    return FormatMoney(nBalance, nColor).c_str();
}

Value getaddressinfo(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() != 1))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddressinfo <address>\n"
            "Returns info about <address>.");
    }

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    Object obj;
    GetAddrInfo(strAddress, nColor, obj);
    return obj;
}


Value getaddressinputs(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size()  < 1) || (params.size() > 3))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddressinputs <address> [start] [max]\n"
            "Returns [max] inputs of <address> beginning with [start]\n"
            "  For example, if [start]=101 and [max]=100 means to\n"
            "  return the second 100 inputs (if possible).\n"
            "    [start] is the nth input (default: 1)\n"
            "    [max] is the max inputs to return (default: 100)");
    }

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    CExploreDB exploredb;

    int nQtyInputs;
    if (!exploredb.ReadAddrQty(ADDR_QTY_INPUT, strAddress, nColor, nQtyInputs))
    {
         throw runtime_error("TSNH: Can't read number of inputs.");
    }

    Array result;

    if (nQtyInputs == 0)
    {
        return result;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
        if (nStart > nQtyInputs)
        {
            throw runtime_error(
                    strprintf("Start must be less than %d.", nQtyInputs));
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    int nBestHeightStart = nBestHeight;
    vector<AddrTxInfo> vAddrTx;
    GetAddrInputs(exploredb, strAddress, nColor, nStart, nMax, nQtyInputs, vAddrTx);
    BOOST_FOREACH(const AddrTxInfo& addrtx, vAddrTx)
    {
        unsigned int i = 0;
        BOOST_FOREACH(const InOutInfo& inout, addrtx.inouts)
        {
            if (inout.IsOutput())
            {
                throw runtime_error("TSNH: In-out is an output.");
            }
            Object objOutput;
            addrtx.AsJSON(nBestHeightStart, i, objOutput);
            result.push_back(objOutput);
            i += 1;
        }
    }

    return result;
}

Value getaddressoutputs(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size()  < 1) || (params.size() > 3))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddressoutputs <address> [start] [max]\n"
            "Returns [max] outputs of <address> beginning with [start]\n"
            "  For example, if [start]=101 and [max]=100 means to\n"
            "  return the second 100 outputs (if possible).\n"
            "    [start] is the nth output (default: 1)\n"
            "    [max] is the max outputs to return (default: 100)");
    }

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    CExploreDB exploredb;

    int nQtyOutputs;
    if (!exploredb.ReadAddrQty(ADDR_QTY_OUTPUT, strAddress, nColor, nQtyOutputs))
    {
         throw runtime_error("TSNH: Can't read number of outputs.");
    }

    Array result;

    if (nQtyOutputs == 0)
    {
        return result;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
        if (nStart > nQtyOutputs)
        {
            throw runtime_error(
                    strprintf("Start must be less than %d.", nQtyOutputs));
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    int nBestHeightStart = nBestHeight;
    vector<AddrTxInfo> vAddrTx;
    GetAddrOutputs(exploredb, strAddress, nColor, nStart, nMax, nQtyOutputs, vAddrTx);
    BOOST_FOREACH(const AddrTxInfo& addrtx, vAddrTx)
    {
        unsigned int i = 0;
        BOOST_FOREACH(const InOutInfo& inout, addrtx.inouts)
        {
            if (inout.IsInput())
            {
                throw runtime_error("TSNH: In-out is an input.");
            }
            Object objOutput;
            addrtx.AsJSON(nBestHeightStart, i, objOutput);
            result.push_back(objOutput);
            i += 1;
        }
    }

    return result;
}


//
// UTXOs (unspent outputs)
//
// A UTXO is just an output whose "isspent" would be false, so these commands
// are the get*outputs commands with spent outputs filtered out. The always-
// false "isspent" field is dropped from the result.

// Remove a key from a json_spirit Object (drops the "isspent" field).
static void EraseKey(Object& obj, const string& key)
{
    for (Object::iterator it = obj.begin(); it != obj.end(); ++it)
    {
        if (it->name_ == key)
        {
            obj.erase(it);
            return;
        }
    }
}

// Collect an address's unspent outputs as ready-to-return JSON objects
// (in output order, "isspent" stripped).
void GetAddrUtxos(CExploreDB& exploredb,
                  const string& strAddress, int nColor,
                  int nQtyOutputs, int nBestHeightStart,
                  vector<Object>& vUtxosRet)
{
    vector<AddrTxInfo> vAddrTx;
    GetAddrOutputs(exploredb, strAddress, nColor, 1, nQtyOutputs, nQtyOutputs, vAddrTx);
    BOOST_FOREACH(const AddrTxInfo& addrtx, vAddrTx)
    {
        unsigned int i = 0;
        BOOST_FOREACH(const InOutInfo& io, addrtx.inouts)
        {
            if (io.IsOutput() && io.inout.output.IsUnspent())
            {
                Object obj;
                addrtx.AsJSON(nBestHeightStart, i, obj);
                EraseKey(obj, "isspent");
                vUtxosRet.push_back(obj);
            }
            i += 1;
        }
    }
}

Value getaddressutxos(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size()  < 1) || (params.size() > 3))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddressutxos <address> [start] [max]\n"
            "Returns [max] unspent outputs (UTXOs) of <address> beginning with [start]\n"
            "  For example, if [start]=101 and [max]=100 means to\n"
            "  return the second 100 UTXOs (if possible).\n"
            "    [start] is the nth UTXO (default: 1)\n"
            "    [max] is the max UTXOs to return (default: 100)");
    }

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    CExploreDB exploredb;

    int nQtyOutputs;
    if (!exploredb.ReadAddrQty(ADDR_QTY_OUTPUT, strAddress, nColor, nQtyOutputs))
    {
         throw runtime_error("TSNH: Can't read number of outputs.");
    }

    Array result;
    if (nQtyOutputs == 0)
    {
        return result;
    }

    int nBestHeightStart = nBestHeight;
    vector<Object> vUtxos;
    GetAddrUtxos(exploredb, strAddress, nColor, nQtyOutputs, nBestHeightStart, vUtxos);

    int nQty = (int)vUtxos.size();
    if (nQty == 0)
    {
        return result;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
        if (nStart > nQty)
        {
            throw runtime_error(strprintf("Start must be less than %d.", nQty));
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    int nStop = min(nStart + nMax - 1, nQty);
    for (int k = nStart; k <= nStop; ++k)
    {
        result.push_back(vUtxos[k - 1]);
    }
    return result;
}

Value getaddressutxospg(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 3) || (params.size() > 4))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddressutxospg <address> <page> <perpage> [ordering]\n"
            "Returns up to <perpage> unspent outputs (UTXOs) of <address>\n"
            "  beginning with 1 + (<perpage> * (<page> - 1)).\n"
            "    <page> is the page number\n"
            "    <perpage> is the number of UTXOs per page\n"
            "    [ordering] by blockchain position (default=true -> forward)");
    }

    // leading params = 1 (1st param is <address>, 2nd is <page>)
    static const unsigned int LEADING_PARAMS = 1;

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    CExploreDB exploredb;

    int nQtyOutputs;
    if (!exploredb.ReadAddrQty(ADDR_QTY_OUTPUT, strAddress, nColor, nQtyOutputs))
    {
         throw runtime_error("TSNH: Can't read number of outputs.");
    }

    if (nQtyOutputs == 0)
    {
         throw runtime_error("Address has no outputs.");
    }

    int nBestHeightStart = nBestHeight;
    vector<Object> vUtxos;
    GetAddrUtxos(exploredb, strAddress, nColor, nQtyOutputs, nBestHeightStart, vUtxos);

    int nTotal = (int)vUtxos.size();
    if (nTotal == 0)
    {
         throw runtime_error("Address has no unspent outputs.");
    }

    pagination_t pg;
    GetPagination(params, LEADING_PARAMS, nTotal, pg);
    int nStop = min(pg.start + pg.max - 1, nTotal);
    Array data;
    for (int k = pg.start; k <= nStop; ++k)
    {
        data.push_back(vUtxos[k - 1]);
    }
    if (!pg.forward)
    {
        reverse(data.begin(), data.end());
    }

    Object result;
    result.push_back(Pair("total", (boost::int64_t)nTotal));
    result.push_back(Pair("page", pg.page));
    result.push_back(Pair("per_page", pg.per_page));
    result.push_back(Pair("last_page", pg.last_page));
    result.push_back(Pair("data", data));
    return result;
}

Value getaddresstxspg(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 3) || (params.size() > 4))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddresstxspg <address> <page> <perpage> [ordering]\n"
            "Returns up to <perpage> transactions of <address>\n"
            "  beginning with 1 + (<perpage> * (<page> - 1>))\n"
            "  For example, <page>=2 and <perpage>=20 means to\n"
            "  return transactions 21 - 40 (if possible).\n"
            "    <page> is the page number\n"
            "    <perpage> is the number of transactions per page\n"
            "    [ordering] by blockchain position (default=true -> forward)");
    }

    // leading params = 1 (1st param is <address>, 2nd is <page>)
    static const unsigned int LEADING_PARAMS = 1;

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    CExploreDB exploredb;

    int nQtyTxs;
    if (!exploredb.ReadAddrQty(ADDR_QTY_VIO, strAddress, nColor, nQtyTxs))
    {
         throw runtime_error("TSNH: Can't read number of transactions.");
    }

    if (nQtyTxs == 0)
    {
         throw runtime_error("Address has no transactions.");
    }

    pagination_t pg;
    GetPagination(params, LEADING_PARAMS, nQtyTxs, pg);

    vector<AddrTxInfo> vAddrTx;
    GetAddrTxs(exploredb, strAddress, nColor, pg.start, pg.max, nQtyTxs, vAddrTx);

    if (!pg.forward)
    {
        reverse(vAddrTx.begin(), vAddrTx.end());
    }

    int nBestHeightStart = nBestHeight;
    Array data;
    BOOST_FOREACH(const AddrTxInfo& addrtx, vAddrTx)
    {
        Object obj;
        addrtx.AsJSON(nBestHeightStart, obj);
        data.push_back(obj);
    }

    Object result;
    result.push_back(Pair("total", nQtyTxs));
    result.push_back(Pair("page", pg.page));
    result.push_back(Pair("per_page", pg.per_page));
    result.push_back(Pair("last_page", pg.last_page));
    result.push_back(Pair("data", data));

    return result;
}

Value getaddressinouts(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 3))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddressinouts <address> [start] [max]\n"
            "Returns [max] inputs + outputs of <address> beginning with [start]\n"
            "  For example, if [start]=101 and [max]=100 means to\n"
            "  return the second 100 in-outs (if possible).\n"
            "    [start] is the nth in-out (default: 1)\n"
            "    [max] is the max in-outs to return (default: 100)");
    }

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    CExploreDB exploredb;

    int nQtyInOuts;
    if (!exploredb.ReadAddrQty(ADDR_QTY_INOUT, strAddress, nColor, nQtyInOuts))
    {
         throw runtime_error("TSNH: Can't read number of in-outs.");
    }

    Array result;

    if (nQtyInOuts == 0)
    {
        return result;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
        if (nStart > nQtyInOuts)
        {
            throw runtime_error(
                    strprintf("Start must be less than %d.", nQtyInOuts));
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    int nBestHeightStart = nBestHeight;
    vector<AddrTxInfo> vAddrTx;
    GetInOuts(exploredb, strAddress, nColor, nStart, nMax, nQtyInOuts, vAddrTx);

    BOOST_FOREACH(const AddrTxInfo& addrtx, vAddrTx)
    {
        for (unsigned int i = 0; i < addrtx.inouts.size(); ++i)
        {
            Object objInput;
            addrtx.AsJSON(nBestHeightStart, i, objInput);
            result.push_back(objInput);
        }
    }

    return result;
}

Value getaddressinoutspg(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 3) || (params.size() > 4))
    {
        throw runtime_error(
            strExploreHelp +
            "getaddressinoutspg <address> <page> <perpage> [ordering]\n"
            "Returns up to <perpage> inputs + outputs of <address>\n"
            "  beginning with 1 + (<perpage> * (<page> - 1>))\n"
            "  For example, <page>=2 and <perpage>=20 means to\n"
            "  return in-outs 21 - 40 (if possible).\n"
            "    <page> is the page number\n"
            "    <perpage> is the number of input/outputs per page\n"
            "    [ordering] by blockchain position (default=true -> forward)");
    }

    // leading params = 1 (1st param is <address>, 2nd is <page>)
    static const unsigned int LEADING_PARAMS = 1;

    string strAddress = params[0].get_str();
    int nColor = ExploreAddrColor(strAddress);

    CExploreDB exploredb;

    int nQtyInOuts;
    if (!exploredb.ReadAddrQty(ADDR_QTY_INOUT, strAddress, nColor, nQtyInOuts))
    {
         throw runtime_error("TSNH: Can't read number of in-outs.");
    }

    if (nQtyInOuts == 0)
    {
         throw runtime_error("Address has no in-outs.");
    }

    pagination_t pg;
    GetPagination(params, LEADING_PARAMS, nQtyInOuts, pg);

    vector<AddrTxInfo> vAddrTx;
    GetInOuts(exploredb, strAddress, nColor, pg.start, pg.max, nQtyInOuts, vAddrTx);

    int nBestHeightStart = nBestHeight;
    Array data;
    BOOST_FOREACH(const AddrTxInfo& addrtx, vAddrTx)
    {
        for (unsigned int i = 0; i < addrtx.inouts.size(); ++i)
        {
            Object objOutput;
            addrtx.AsJSON(nBestHeightStart, i, objOutput);
            data.push_back(objOutput);
        }
    }

    if (!pg.forward)
    {
        reverse(data.begin(), data.end());
    }

    Object result;
    result.push_back(Pair("total", nQtyInOuts));
    result.push_back(Pair("page", pg.page));
    result.push_back(Pair("per_page", pg.per_page));
    result.push_back(Pair("last_page", pg.last_page));
    result.push_back(Pair("data", data));

    return result;
}


//////////////////////////////////////////////////////////////////////////////
//
// HD Wallets
//
// Breakout has no HD wallet; these commands run consolidated explore queries
// against an externally-supplied HD *extended public key* (xpub). A BIP32
// extended key yields raw pubkey-hashes, which in Breakout are color-agnostic
// (the same 20-byte hash is a valid address in every color). So each command
// takes an optional trailing [color]: supplied -> scope to that one currency;
// omitted -> aggregate across all colors.

// defined in the Richlist section below
int ExploreCheckColor(const Value& v);

// Caps (Breakout has no chainParams; mirror StealthExplore's defaults).
static const unsigned int DEFAULT_MAX_HD_CHILDREN = 1024;
static const unsigned int DEFAULT_MAX_HD_INOUTS   = 65535;
static const unsigned int DEFAULT_MAX_HD_TXS      = 16383;

unsigned int GetMaxHDChildren()
{
    return (unsigned int)GetArg("-maxhdchildren", (int64_t)DEFAULT_MAX_HD_CHILDREN);
}
unsigned int GetMaxHDInOuts()
{
    return (unsigned int)GetArg("-maxhdinouts", (int64_t)DEFAULT_MAX_HD_INOUTS);
}
unsigned int GetMaxHDTxs()
{
    return (unsigned int)GetArg("-maxhdtxs", (int64_t)DEFAULT_MAX_HD_TXS);
}

typedef std::pair<int, int> txkey_t;

// An address's in-out list for a single tx, tagged with the address + color
// it belongs to (needed to re-read the per-(address,color) records).
class AddrInOutList : public ExploreInOutList
{
public:
    std::string address;
    int color;
    AddrInOutList(const std::string& addressIn,
                  int colorIn,
                  const ExploreInOutList& inoutlistIn)
        : ExploreInOutList(inoutlistIn)
    {
        address = addressIn;
        color = colorIn;
    }
};

// A used (address, color) belonging to the HD account.
struct HDAddr
{
    std::string address;
    int color;
    CPubKey pubkey;
    uint32_t child;
    int branch;   // 0 = external, 1 = change
};

// Decode a user-supplied extended key into secure bytes, or throw.
secure_bytes_t ExploreDecodeExtKey(const string& strExtKey)
{
    valtype vch;
    if (!DecodeBase58Check(strExtKey, vch))
    {
        throw runtime_error("Invalid extended key.");
    }
    return secure_bytes_t(vch.begin(), vch.end());
}

// The two account branches: external (child 0) and change (child 1).
void GetHDKeychains(const secure_bytes_t& vchExtKey,
                    vector<Bip32::HDKeychain>& vRet)
{
    vRet.clear();
    Bip32::HDKeychain hdAccount(vchExtKey);
    vRet.push_back(hdAccount.getChild(0));   // external
    vRet.push_back(hdAccount.getChild(1));   // change
}

// Derive one child's public key (the pubkey-hash is color-agnostic).
CPubKey GetHDChildPubKey(const Bip32::HDKeychain& hdParent, uint32_t nChild)
{
    Bip32::HDKeychain hdChild(hdParent.getChild(nChild));
    uchar_vector_secure vchPub = uchar_vector_secure(hdChild.pubkey());
    CPubKey pubKey;
    pubKey.Set(vchPub.begin(), vchPub.end());
    return pubKey;
}

// The color-scoped Breakout address string for a pubkey.
string GetColorAddress(const CPubKey& pubKey, int nColor)
{
    CBitcoinAddress address;
    address.Set(pubKey.GetID());
    address.nColor = nColor;
    return address.ToString();
}

// Resolve the colors to scan from an optional trailing [color] param.
// Returns true when a single color was supplied.
bool GetHDColors(const Array& params, int idx, vector<int>& vColorsRet)
{
    vColorsRet.clear();
    if ((int)params.size() > idx)
    {
        vColorsRet.push_back(ExploreCheckColor(params[idx]));
        return true;
    }
    for (int c = 1; c < N_COLORS; ++c)
    {
        vColorsRet.push_back(c);
    }
    return false;
}

// Enumerate the account's used (address, color) entries, applying the BIP44
// gap limit: stop scanning a branch at the first child with no activity in any
// of the scanned colors.
void GetHDAccountAddrs(CExploreDB& exploredb,
                       const secure_bytes_t& vchExtKey,
                       const vector<int>& vColors,
                       vector<HDAddr>& vRet)
{
    const unsigned int nMaxHDChildren = GetMaxHDChildren();
    vector<Bip32::HDKeychain> vKeychains;
    GetHDKeychains(vchExtKey, vKeychains);
    for (int branch = 0; branch < (int)vKeychains.size(); ++branch)
    {
        const Bip32::HDKeychain& hdParent = vKeychains[branch];
        for (uint32_t nChild = 0; nChild < nMaxHDChildren; ++nChild)
        {
            CPubKey pubKey = GetHDChildPubKey(hdParent, nChild);
            bool fUsed = false;
            BOOST_FOREACH(int c, vColors)
            {
                string strAddress = GetColorAddress(pubKey, c);
                if (exploredb.AddrValueIsViable(ADDR_BALANCE, strAddress, c))
                {
                    HDAddr hd;
                    hd.address = strAddress;
                    hd.color = c;
                    hd.pubkey = pubKey;
                    hd.child = nChild;
                    hd.branch = branch;
                    vRet.push_back(hd);
                    fUsed = true;
                }
            }
            if (!fUsed)
            {
                break;   // gap limit
            }
        }
    }
}

// A single in-out with its blockchain position, for account-wide ordering.
struct HDInOutItem
{
    int height;
    int vtx;
    int n;
    Object obj;
    bool operator < (const HDInOutItem& other) const
    {
        if (height != other.height) return height < other.height;
        if (vtx != other.vtx) return vtx < other.vtx;
        return n < other.n;
    }
};

// Gather all inputs (fInputs) or all outputs (!fInputs) across the account's
// addresses, ordered by blockchain position.
// When fUnspentOnly is set (outputs only), spent outputs are skipped and the
// always-false "isspent" field is stripped -> the account's UTXOs.
void GetHDInOutItems(CExploreDB& exploredb,
                     const vector<HDAddr>& vHDAddr,
                     bool fInputs,
                     int nBestHeightStart,
                     vector<HDInOutItem>& vItems,
                     bool fUnspentOnly = false)
{
    const unsigned int nMaxHDInOuts = GetMaxHDInOuts();
    exploreKey_t qtyKey = fInputs ? ADDR_QTY_INPUT : ADDR_QTY_OUTPUT;
    BOOST_FOREACH(const HDAddr& hd, vHDAddr)
    {
        int nQty;
        if (!exploredb.ReadAddrQty(qtyKey, hd.address, hd.color, nQty))
        {
            throw runtime_error("TSNH: Can't read number of in-outs.");
        }
        if (nQty == 0)
        {
            continue;
        }
        vector<AddrTxInfo> vAddrTx;
        if (fInputs)
        {
            GetAddrInputs(exploredb, hd.address, hd.color, 1, nQty, nQty, vAddrTx);
        }
        else
        {
            GetAddrOutputs(exploredb, hd.address, hd.color, 1, nQty, nQty, vAddrTx);
        }
        BOOST_FOREACH(const AddrTxInfo& addrtx, vAddrTx)
        {
            unsigned int i = 0;
            BOOST_FOREACH(const InOutInfo& io, addrtx.inouts)
            {
                if (fUnspentOnly && io.IsOutput() && io.inout.output.IsSpent())
                {
                    i += 1;
                    continue;
                }
                HDInOutItem item;
                item.height = addrtx.extx.height;
                item.vtx = addrtx.extx.vtx;
                item.n = io.GetN();
                addrtx.AsJSON(nBestHeightStart, i, item.obj);
                if (fUnspentOnly)
                {
                    EraseKey(item.obj, "isspent");
                }
                vItems.push_back(item);
                if (vItems.size() > nMaxHDInOuts)
                {
                    throw runtime_error("Too many HD in-outs.");
                }
                i += 1;
            }
        }
    }
    sort(vItems.begin(), vItems.end());
}

// Build the consolidated, blockchain-ordered list of the account's
// transactions. Each HDTxInfo groups every in-out (across all the account's
// addresses and colors) that touches one transaction.
void GetHDTxList(CExploreDB& exploredb,
                 const vector<HDAddr>& vHDAddr,
                 vector<HDTxInfo>& vHDTxRet)
{
    const unsigned int nMaxHDInOuts = GetMaxHDInOuts();
    const unsigned int nMaxHDTxs = GetMaxHDTxs();

    unsigned int nTotalInOuts = 0;
    // natural blockchain ordering (by height, then vtx)
    map<txkey_t, vector<AddrInOutList> > mapHDTx;
    BOOST_FOREACH(const HDAddr& hd, vHDAddr)
    {
        int nQtyTxs;
        if (!exploredb.ReadAddrQty(ADDR_QTY_VIO, hd.address, hd.color, nQtyTxs))
        {
            throw runtime_error("TSNH: Can't read number of vios.");
        }
        for (int i = 1; i <= nQtyTxs; ++i)
        {
            ExploreInOutList vIO;
            if (!exploredb.ReadAddrList(ADDR_LIST_VIO, hd.address, hd.color, i, vIO))
            {
                throw runtime_error("TSNH: Can't read transaction in-outs");
            }
            if (vIO.vinouts.empty())
            {
                throw runtime_error("TSNH: transaction has no in-outs");
            }
            nTotalInOuts += vIO.vinouts.size();
            if (nTotalInOuts > nMaxHDInOuts)
            {
                throw runtime_error("Too many HD in-outs.");
            }
            txkey_t txkey = make_pair(vIO.height, vIO.vtx);
            mapHDTx[txkey].push_back(AddrInOutList(hd.address, hd.color, vIO));
            if (mapHDTx.size() > nMaxHDTxs)
            {
                throw runtime_error("Too many HD transactions.");
            }
        }
    }

    map<txkey_t, vector<AddrInOutList> >::const_iterator it;
    for (it = mapHDTx.begin(); it != mapHDTx.end(); ++it)
    {
        const vector<AddrInOutList>& vinoutlist = it->second;
        HDTxInfo hdtx;
        hdtx.extx.SetNull();
        // by address (each address has its own in-out list for the tx)
        vector<AddrInOutList>::const_iterator jt;
        for (jt = vinoutlist.begin(); jt != vinoutlist.end(); ++jt)
        {
            if (jt->vinouts.empty())
            {
                throw runtime_error("TSNH: In-out list is empty.");
            }
            // by in-out (for a single address for a single tx)
            BOOST_FOREACH(const int& n, jt->vinouts)
            {
                AddrInOutInfo addrinout(n);
                addrinout.address = jt->address;
                if (addrinout.IsInput())
                {
                    if (!exploredb.ReadAddrTx(ADDR_TX_INPUT,
                                              jt->address, jt->color, GetInOutID(n),
                                              addrinout.inoutinfo.inout.input))
                    {
                        throw runtime_error("TSNH: Problem reading input.");
                    }
                }
                else
                {
                    if (!exploredb.ReadAddrTx(ADDR_TX_OUTPUT,
                                              jt->address, jt->color, GetInOutID(n),
                                              addrinout.inoutinfo.inout.output))
                    {
                        throw runtime_error("TSNH: Problem reading output.");
                    }
                }
                const uint256* ptxid = addrinout.GetTxID();
                if (!ptxid)
                {
                    throw runtime_error("TSNH: In-out ptxid is null.");
                }
                if (hdtx.extx.IsNull())
                {
                    if (!exploredb.ReadExploreTx(*ptxid, hdtx.extx))
                    {
                        throw runtime_error("TSNH: Problem reading transaction.");
                    }
                }
                hdtx.addrinouts.insert(addrinout);
                if (!hdtx.GetTxID())
                {
                    throw runtime_error("TSNH: HD txid is null.");
                }
                if (*hdtx.GetTxID() != *ptxid)
                {
                    throw runtime_error("TSNH: Inouts are from different txs.");
                }
            }
        }
        hdtx.SetPayees();
        vHDTxRet.push_back(hdtx);
    }
}


Value getchildkey(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 2) || (params.size() > 3))
    {
        throw runtime_error(
            strExploreHelp +
            "getchildkey <extended key> <child> [color]\n"
            "Returns key and address information about the child.\n"
            "  <extended key> is the parent extended (public) key\n"
            "  <child> is the child index (>= 0)\n"
            "  [color] is the currency color for the address "
            "(default: the default currency)");
    }

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    int nChild = params[1].get_int();
    if (nChild < 0)
    {
        throw runtime_error("Child number should be positive.");
    }

    // Default to the node's default currency (e.g. BRX). Note DEFAULT_COLOR is
    // an internal fee/mint color (BRK), not the user-facing default currency.
    int nColor = nDefaultCurrency;
    if (params.size() > 2)
    {
        nColor = ExploreCheckColor(params[2]);
    }
    else if ((nColor < 1) || (nColor >= N_COLORS))
    {
        nColor = (int)BREAKOUT_COLOR_BRX;
    }

    Bip32::HDKeychain hdkeychain(vchExtKey);
    hdkeychain = hdkeychain.getChild((uint32_t)nChild);

    secure_bytes_t vchChildExt(hdkeychain.extkey());
    string strChildExt = EncodeBase58Check(valtype(vchChildExt.begin(),
                                                   vchChildExt.end()));

    uchar_vector_secure vchChildPub = uchar_vector_secure(hdkeychain.pubkey());
    string strChildPub = uchar_vector(vchChildPub.begin(),
                                      vchChildPub.end()).getHex();

    CPubKey pubKey;
    pubKey.Set(vchChildPub.begin(), vchChildPub.end());
    string strAddress = GetColorAddress(pubKey, nColor);

    Object obj;
    obj.push_back(Pair("extended", strChildExt));
    obj.push_back(Pair("pubkey", strChildPub));
    obj.push_back(Pair("color", (boost::int64_t)nColor));
    obj.push_back(Pair("address", strAddress));

    return obj;
}


Value gethdaddresses(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 2))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaddresses <extended key> [color]\n"
            "Returns all known addresses for the <extended key>, separated by\n"
            "external and change. With [color] the scan is scoped to that one\n"
            "currency; otherwise every color is reported per child.");
    }

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    GetHDColors(params, 1, vColors);

    CExploreDB exploredb;

    const unsigned int nMaxHDChildren = GetMaxHDChildren();
    vector<Bip32::HDKeychain> vKeychains;
    GetHDKeychains(vchExtKey, vKeychains);

    // one output array per branch (external, change)
    vector<Array> vBranches(vKeychains.size());
    for (int branch = 0; branch < (int)vKeychains.size(); ++branch)
    {
        const Bip32::HDKeychain& hdParent = vKeychains[branch];
        for (uint32_t nChild = 0; nChild < nMaxHDChildren; ++nChild)
        {
            CPubKey pubKey = GetHDChildPubKey(hdParent, nChild);

            Array aryAddrs;
            bool fUsed = false;
            BOOST_FOREACH(int c, vColors)
            {
                string strAddress = GetColorAddress(pubKey, c);
                if (!exploredb.AddrValueIsViable(ADDR_BALANCE, strAddress, c))
                {
                    continue;
                }
                int nQtyInOuts = 0;
                if (!exploredb.ReadAddrQty(ADDR_QTY_INOUT, strAddress, c, nQtyInOuts))
                {
                    throw runtime_error("TSNH: Can't read number of in-outs.");
                }
                Object objAddr;
                objAddr.push_back(Pair("color", (boost::int64_t)c));
                objAddr.push_back(Pair("address", strAddress));
                objAddr.push_back(Pair("inouts", (boost::int64_t)nQtyInOuts));
                aryAddrs.push_back(objAddr);
                fUsed = true;
            }

            Object objChild;
            objChild.push_back(Pair("child", (boost::int64_t)nChild));
            objChild.push_back(Pair("pubkey", HexStr(pubKey.Raw())));
            objChild.push_back(Pair("addresses", aryAddrs));
            vBranches[branch].push_back(objChild);

            if (!fUsed)
            {
                break;   // gap limit (unused terminator included)
            }
        }
    }

    Object result;
    result.push_back(Pair("external", vBranches[0]));
    result.push_back(Pair("change", vBranches[1]));
    return result;
}


Value gethdaccountbalance(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 2))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaccountbalance <extended key> [color]\n"
            "Returns the balance of the HD account. With [color] a single\n"
            "formatted balance is returned; otherwise an object keyed by\n"
            "currency ticker.");
    }

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    bool fSingle = GetHDColors(params, 1, vColors);

    CExploreDB exploredb;

    vector<HDAddr> vHDAddr;
    GetHDAccountAddrs(exploredb, vchExtKey, vColors, vHDAddr);

    map<int, int64_t> mapBalance;
    BOOST_FOREACH(const HDAddr& hd, vHDAddr)
    {
        int64_t nBalance;
        if (!exploredb.ReadAddrValue(ADDR_BALANCE, hd.address, hd.color, nBalance))
        {
            throw runtime_error("TSNH: Can't read balance.");
        }
        mapBalance[hd.color] += nBalance;
    }

    if (fSingle)
    {
        int nColor = vColors[0];
        return FormatMoney(mapBalance[nColor], nColor).c_str();
    }

    Object obj;
    for (map<int, int64_t>::const_iterator it = mapBalance.begin();
         it != mapBalance.end(); ++it)
    {
        obj.push_back(Pair(COLOR_TICKER[it->first],
                           ValueFromAmount(it->second, it->first)));
    }
    return obj;
}


// Per-color aggregate for an HD account.
struct HDColorInfo
{
    int64_t balance;
    int64_t received;
    int64_t sent;
    int qtyInputs;
    int qtyOutputs;
    int qtyTxs;
    HDColorInfo()
        : balance(0), received(0), sent(0),
          qtyInputs(0), qtyOutputs(0), qtyTxs(0) {}
};

void HDColorInfoAsJSON(int nColor, const HDColorInfo& info, Object& objRet)
{
    objRet.push_back(Pair("color", (boost::int64_t)nColor));
    objRet.push_back(Pair("balance", ValueFromAmount(info.balance, nColor)));
    objRet.push_back(Pair("transactions", (boost::int64_t)info.qtyTxs));
    objRet.push_back(Pair("outputs", (boost::int64_t)info.qtyOutputs));
    objRet.push_back(Pair("received", ValueFromAmount(info.received, nColor)));
    objRet.push_back(Pair("inputs", (boost::int64_t)info.qtyInputs));
    objRet.push_back(Pair("sent", ValueFromAmount(info.sent, nColor)));
    objRet.push_back(Pair("unspent",
                          (boost::int64_t)(info.qtyOutputs - info.qtyInputs)));
    objRet.push_back(Pair("in-outs",
                          (boost::int64_t)(info.qtyOutputs + info.qtyInputs)));
}


Value gethdaccountinfo(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 2))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaccountinfo <extended key> [color]\n"
            "Returns aggregated info about the HD account: per-color balance,\n"
            "received, sent, and in/out/tx counts, plus the number of used\n"
            "external and change addresses. [color] scopes to one currency.");
    }

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    bool fSingle = GetHDColors(params, 1, vColors);

    CExploreDB exploredb;

    vector<HDAddr> vHDAddr;
    GetHDAccountAddrs(exploredb, vchExtKey, vColors, vHDAddr);

    map<int, HDColorInfo> mapInfo;
    set<pair<int, uint32_t> > setExternal;   // (branch, child) used
    set<pair<int, uint32_t> > setChange;
    BOOST_FOREACH(const HDAddr& hd, vHDAddr)
    {
        int64_t nBalance, nValueIn, nValueOut;
        int nQtyInputs, nQtyOutputs, nQtyVIO;
        if (!exploredb.ReadAddrValue(ADDR_BALANCE, hd.address, hd.color, nBalance) ||
            !exploredb.ReadAddrValue(ADDR_VALUEIN, hd.address, hd.color, nValueIn) ||
            !exploredb.ReadAddrValue(ADDR_VALUEOUT, hd.address, hd.color, nValueOut) ||
            !exploredb.ReadAddrQty(ADDR_QTY_INPUT, hd.address, hd.color, nQtyInputs) ||
            !exploredb.ReadAddrQty(ADDR_QTY_OUTPUT, hd.address, hd.color, nQtyOutputs) ||
            !exploredb.ReadAddrQty(ADDR_QTY_VIO, hd.address, hd.color, nQtyVIO))
        {
            throw runtime_error("TSNH: Can't read account address records.");
        }
        HDColorInfo& info = mapInfo[hd.color];
        info.balance += nBalance;
        info.received += nValueIn;
        info.sent += nValueOut;
        info.qtyInputs += nQtyInputs;
        info.qtyOutputs += nQtyOutputs;
        info.qtyTxs += nQtyVIO;
        if (hd.branch == 0)
        {
            setExternal.insert(make_pair(hd.branch, hd.child));
        }
        else
        {
            setChange.insert(make_pair(hd.branch, hd.child));
        }
    }

    Object result;
    result.push_back(Pair("external_addresses",
                          (boost::int64_t)setExternal.size()));
    result.push_back(Pair("change_addresses",
                          (boost::int64_t)setChange.size()));
    result.push_back(Pair("blocks", (boost::int64_t)nBestHeight));

    if (fSingle)
    {
        int nColor = vColors[0];
        Object objColor;
        HDColorInfoAsJSON(nColor, mapInfo[nColor], objColor);
        result.push_back(Pair("account", objColor));
    }
    else
    {
        Array aryColors;
        for (map<int, HDColorInfo>::const_iterator it = mapInfo.begin();
             it != mapInfo.end(); ++it)
        {
            Object objColor;
            HDColorInfoAsJSON(it->first, it->second, objColor);
            aryColors.push_back(objColor);
        }
        result.push_back(Pair("colors", aryColors));
    }

    return result;
}


Value gethdaccountinputs(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 4))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaccountinputs <extended key> [start] [max] [color]\n"
            "Returns [max] inputs of the HD account beginning with [start],\n"
            "ordered by blockchain position (across all the account's addresses).\n"
            "    [start] is the nth input (default: 1)\n"
            "    [max] is the max inputs to return (default: 100)\n"
            "    [color] scopes to one currency (default: all)");
    }

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    GetHDColors(params, 3, vColors);

    CExploreDB exploredb;

    vector<HDAddr> vHDAddr;
    GetHDAccountAddrs(exploredb, vchExtKey, vColors, vHDAddr);

    int nBestHeightStart = nBestHeight;
    vector<HDInOutItem> vItems;
    GetHDInOutItems(exploredb, vHDAddr, true, nBestHeightStart, vItems);

    Array result;
    int nQty = (int)vItems.size();
    if (nQty == 0)
    {
        return result;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
        if (nStart > nQty)
        {
            throw runtime_error(strprintf("Start must be less than %d.", nQty));
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    int nStop = min(nStart + nMax - 1, nQty);
    for (int i = nStart; i <= nStop; ++i)
    {
        result.push_back(vItems[i - 1].obj);
    }
    return result;
}


Value gethdaccountoutputs(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 4))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaccountoutputs <extended key> [start] [max] [color]\n"
            "Returns [max] outputs of the HD account beginning with [start],\n"
            "ordered by blockchain position (across all the account's addresses).\n"
            "    [start] is the nth output (default: 1)\n"
            "    [max] is the max outputs to return (default: 100)\n"
            "    [color] scopes to one currency (default: all)");
    }

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    GetHDColors(params, 3, vColors);

    CExploreDB exploredb;

    vector<HDAddr> vHDAddr;
    GetHDAccountAddrs(exploredb, vchExtKey, vColors, vHDAddr);

    int nBestHeightStart = nBestHeight;
    vector<HDInOutItem> vItems;
    GetHDInOutItems(exploredb, vHDAddr, false, nBestHeightStart, vItems);

    Array result;
    int nQty = (int)vItems.size();
    if (nQty == 0)
    {
        return result;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
        if (nStart > nQty)
        {
            throw runtime_error(strprintf("Start must be less than %d.", nQty));
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    int nStop = min(nStart + nMax - 1, nQty);
    for (int i = nStart; i <= nStop; ++i)
    {
        result.push_back(vItems[i - 1].obj);
    }
    return result;
}


Value gethdaccountutxos(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 4))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaccountutxos <extended key> [start] [max] [color]\n"
            "Returns [max] unspent outputs (UTXOs) of the HD account beginning\n"
            "with [start], ordered by blockchain position (across all the\n"
            "account's addresses).\n"
            "    [start] is the nth UTXO (default: 1)\n"
            "    [max] is the max UTXOs to return (default: 100)\n"
            "    [color] scopes to one currency (default: all)");
    }

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    GetHDColors(params, 3, vColors);

    CExploreDB exploredb;

    vector<HDAddr> vHDAddr;
    GetHDAccountAddrs(exploredb, vchExtKey, vColors, vHDAddr);

    int nBestHeightStart = nBestHeight;
    vector<HDInOutItem> vItems;
    GetHDInOutItems(exploredb, vHDAddr, false, nBestHeightStart, vItems, true);

    Array result;
    int nQty = (int)vItems.size();
    if (nQty == 0)
    {
        return result;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
        if (nStart > nQty)
        {
            throw runtime_error(strprintf("Start must be less than %d.", nQty));
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    int nStop = min(nStart + nMax - 1, nQty);
    for (int i = nStart; i <= nStop; ++i)
    {
        result.push_back(vItems[i - 1].obj);
    }
    return result;
}


Value gethdaccountutxospg(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 3) || (params.size() > 5))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaccountutxospg <extended key> <page> <perpage> [ordering] [color]\n"
            "Returns up to <perpage> unspent outputs (UTXOs) of the HD account\n"
            "  beginning with 1 + (<perpage> * (<page> - 1)).\n"
            "    <page> is the page number\n"
            "    <perpage> is the number of UTXOs per page\n"
            "    [ordering] by blockchain position (default=true -> forward)\n"
            "    [color] scopes to one currency (default: all)");
    }

    // leading params = 1 (1st param is <extended key>, 2nd is <page>)
    static const unsigned int LEADING_PARAMS = 1;

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    GetHDColors(params, 4, vColors);

    CExploreDB exploredb;

    vector<HDAddr> vHDAddr;
    GetHDAccountAddrs(exploredb, vchExtKey, vColors, vHDAddr);

    int nBestHeightStart = nBestHeight;
    vector<HDInOutItem> vItems;
    GetHDInOutItems(exploredb, vHDAddr, false, nBestHeightStart, vItems, true);

    int nTotal = (int)vItems.size();
    if (nTotal == 0)
    {
        throw runtime_error("Account has no unspent outputs.");
    }

    pagination_t pg;
    GetPagination(params, LEADING_PARAMS, nTotal, pg);
    int nStop = min(pg.start + pg.max - 1, nTotal);
    Array data;
    for (int i = pg.start; i <= nStop; ++i)
    {
        data.push_back(vItems[i - 1].obj);
    }

    if (!pg.forward)
    {
        reverse(data.begin(), data.end());
    }

    Object result;
    result.push_back(Pair("total", (boost::int64_t)nTotal));
    result.push_back(Pair("page", pg.page));
    result.push_back(Pair("per_page", pg.per_page));
    result.push_back(Pair("last_page", pg.last_page));
    result.push_back(Pair("data", data));
    return result;
}


Value gethdaccountinouts(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 4))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaccountinouts <extended key> [start] [max] [color]\n"
            "Returns [max] transactions of the HD account beginning with [start],\n"
            "ordered by blockchain position. Each transaction consolidates every\n"
            "input and output of the account that touches it.\n"
            "    [start] is the nth transaction (default: 1)\n"
            "    [max] is the max transactions to return (default: 100)\n"
            "    [color] scopes to one currency (default: all)");
    }

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    GetHDColors(params, 3, vColors);

    CExploreDB exploredb;

    vector<HDAddr> vHDAddr;
    GetHDAccountAddrs(exploredb, vchExtKey, vColors, vHDAddr);

    vector<HDTxInfo> vHDTx;
    GetHDTxList(exploredb, vHDAddr, vHDTx);

    Array result;
    int nQty = (int)vHDTx.size();
    if (nQty == 0)
    {
        return result;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
        if (nStart > nQty)
        {
            throw runtime_error(strprintf("Start must be less than %d.", nQty));
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    int nBestHeightStart = nBestHeight;
    int nStop = min(nStart + nMax - 1, nQty);
    for (int i = nStart; i <= nStop; ++i)
    {
        Object obj;
        vHDTx[i - 1].AsJSON(nBestHeightStart, obj);
        result.push_back(obj);
    }
    return result;
}


Value gethdaccountinoutspg(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 3) || (params.size() > 5))
    {
        throw runtime_error(
            strExploreHelp +
            "gethdaccountinoutspg <extended key> <page> <perpage> [ordering] [color]\n"
            "Returns up to <perpage> transactions of the HD account\n"
            "  beginning with 1 + (<perpage> * (<page> - 1)).\n"
            "  Each transaction consolidates every input and output of the\n"
            "  account that touches it.\n"
            "    <page> is the page number\n"
            "    <perpage> is the number of transactions per page\n"
            "    [ordering] by blockchain position (default=true -> forward)\n"
            "    [color] scopes to one currency (default: all)");
    }

    // leading params = 1 (1st param is <extended key>, 2nd is <page>)
    static const unsigned int LEADING_PARAMS = 1;

    string strExtKey = params[0].get_str();
    secure_bytes_t vchExtKey = ExploreDecodeExtKey(strExtKey);

    vector<int> vColors;
    GetHDColors(params, 4, vColors);

    CExploreDB exploredb;

    vector<HDAddr> vHDAddr;
    GetHDAccountAddrs(exploredb, vchExtKey, vColors, vHDAddr);

    vector<HDTxInfo> vHDTx;
    GetHDTxList(exploredb, vHDAddr, vHDTx);

    int nTotalTxs = (int)vHDTx.size();

    if (nTotalTxs == 0)
    {
        throw runtime_error("Account has no transactions.");
    }

    pagination_t pg;
    GetPagination(params, LEADING_PARAMS, nTotalTxs, pg);

    int nBestHeightStart = nBestHeight;
    int nStop = min(pg.start + pg.max - 1, nTotalTxs);
    Array data;
    for (int i = pg.start; i <= nStop; ++i)
    {
        Object obj;
        vHDTx[i - 1].AsJSON(nBestHeightStart, obj);
        data.push_back(obj);
    }

    if (!pg.forward)
    {
        reverse(data.begin(), data.end());
    }

    Object result;
    result.push_back(Pair("total", (boost::int64_t)nTotalTxs));
    result.push_back(Pair("page", pg.page));
    result.push_back(Pair("per_page", pg.per_page));
    result.push_back(Pair("last_page", pg.last_page));
    result.push_back(Pair("data", data));

    return result;
}


//////////////////////////////////////////////////////////////////////////////
//
// Richlist

// Validate a user-supplied color index or throw.
int ExploreCheckColor(const Value& v)
{
    int nColor = v.get_int();
    if ((nColor < 1) || (nColor >= N_COLORS))
    {
        throw runtime_error(strprintf("Invalid color (expected 1..%d).", N_COLORS - 1));
    }
    return nColor;
}

boost::int64_t GetRichListSize(int nColor, int64_t nMinBalance)
{
    unsigned int nCount = 0;
    const MapBalanceCounts& mapForColor = mapAddressBalances[nColor];
    MapBalanceCounts::const_iterator it;
    for (it = mapForColor.begin(); it != mapForColor.end(); ++it)
    {
        if ((*it).first < nMinBalance)
        {
            break;
        }
        nCount += (*it).second;
    }
    return static_cast<boost::int64_t>(nCount);
}

Value getrichlistsize(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 2))
    {
        throw runtime_error(
            strExploreHelp +
            "getrichlistsize <color> [minbalance]\n"
            "Returns the number of <color> addresses with balances\n"
            "  greater than [minbalance] (default: 1 cent of the color).");
    }

    int nColor = ExploreCheckColor(params[0]);

    int64_t nMinBalance = CENT[nColor];
    if (params.size() > 1)
    {
        nMinBalance = AmountFromValue(params[1], nColor);
    }

    return GetRichListSize(nColor, nMinBalance);
}



void GetRichList(int nColor, int nStart, int nMax, Object& objRet)
{
    CExploreDB exploredb;
    int nLimit = nStart + nMax - 1;
    int nCount = 0;

    const MapBalanceCounts& mapForColor = mapAddressBalances[nColor];
    MapBalanceCounts::const_iterator it;
    for (it = mapForColor.begin(); it != mapForColor.end(); ++it)
    {
        int nSize = static_cast<int>((*it).second);
        if ((nSize + nCount) >= nStart)
        {
            int64_t nBalance = (*it).first;
            set<string> setBalances;
            if (!exploredb.ReadAddrSet(ADDR_SET_BAL, nColor, nBalance, setBalances))
            {
                throw runtime_error(
                        strprintf("TSNH: unable to read balance set %s",
                                  FormatMoney(nBalance, nColor).c_str()));
            }
            // sanity check
            if (nSize != static_cast<int>(setBalances.size()))
            {
                throw runtime_error(
                        strprintf("TSNH: balance set %s size mismatch",
                                  FormatMoney(nBalance, nColor).c_str()));
            }
            BOOST_FOREACH(const string& addr, setBalances)
            {
                objRet.push_back(Pair(addr, ValueFromAmount(nBalance, nColor)));
                nCount += 1;
            }
            // return all that tied for last spot
            if (nCount >= nLimit)
            {
                break;
            }
        }
        else
        {
            nCount += nSize;
        }
    }
}

Value getrichlist(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 1) || (params.size() > 3))
    {
        throw runtime_error(
            strExploreHelp +
            "getrichlist <color> [start] [max]\n"
            "Returns [max] <color> addresses from rich list beginning with [start]\n"
            "  For example, if [start]=101 and [max]=100 means to\n"
            "  return the second 100 richest (if possible).\n"
            "    <color> is the currency color index\n"
            "    [start] is the nth richest address (default: 1)\n"
            "    [max] is the max addresses to return (default: 100)");
    }

    int nColor = ExploreCheckColor(params[0]);

    Object obj;
    // nothing to count
    if (mapAddressBalances[nColor].empty())
    {
        return obj;
    }

    int nStart = 1;
    if (params.size() > 1)
    {
        nStart = params[1].get_int();
        if (nStart < 1)
        {
            throw runtime_error("Start must be greater than 0.");
        }
    }

    int nMax = 100;
    if (params.size() > 2)
    {
        nMax = params[2].get_int();
        if (nMax < 1)
        {
            throw runtime_error("Max must be greater than 0.");
        }
    }

    GetRichList(nColor, nStart, nMax, obj);

    return obj;
}


Value getrichlistpg(const Array &params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || (params.size() < 3) || (params.size() > 4))
    {
        throw runtime_error(
            strExploreHelp +
            "getrichlistpg <color> <page> <perpage> [ordering]\n"
            "Returns up to <perpage> addresses of the <color> rich list\n"
            "  beginning with 1 + (<perpage> * (<page> - 1>))\n"
            "  For example, <page>=2 and <perpage>=20 means to\n"
            "  return addresses ranking 21 - 40 (if possible).\n"
            "    <color> is the currency color index\n"
            "    <page> is the page number\n"
            "    <perpage> is the number of address per page\n"
            "    [ordering] by balance (default=true -> descending)");
    }

    // leading params = 1 (first param is <color>, second is <page>)
    static const unsigned int LEADING_PARAMS = 1;

    int nColor = ExploreCheckColor(params[0]);

    if (mapAddressBalances[nColor].empty())
    {
         throw runtime_error("No rich list.");
    }

    int64_t nRichListSize = GetRichListSize(nColor, CENT[nColor]);

    pagination_t pg;
    GetPagination(params, LEADING_PARAMS, nRichListSize, pg);

    Object data;
    GetRichList(nColor, pg.start, pg.max, data);

    if (!pg.forward)
    {
        reverse(data.begin(), data.end());
    }

    Object result;
    result.push_back(Pair("total", nRichListSize));
    result.push_back(Pair("page", pg.page));
    result.push_back(Pair("per_page", pg.per_page));
    result.push_back(Pair("last_page", pg.last_page));
    result.push_back(Pair("data", data));

    return result;
}




//////////////////////////////////////////////////////////////////////////////
//
// Blockchain Stats

int64_t IntSum(const vector<int64_t>& vNumbers)
{
    int64_t sum = 0;
    BOOST_FOREACH(const int64_t& value, vNumbers)
    {
        sum += value;
    }
    return sum;
}

Value SumAsAmount(const vector<int64_t>& vNumbers)
{
    return static_cast<boost::int64_t>(IntSum(vNumbers));
}

Value SumAsIntValue(const vector<int64_t>& vNumbers)
{
    // Block stats (e.g. intervals in seconds) are plain integers, not amounts.
    return static_cast<boost::int64_t>(IntSum(vNumbers));
}

double RealMean(const vector<int64_t>& vNumbers)
{
    if (vNumbers.empty())
    {
        return numeric_limits<double>::max();
    }
    int64_t sum = IntSum(vNumbers);
    return static_cast<double>(sum) / static_cast<double>(vNumbers.size());
}

Value MeanAsRealValue(const vector<int64_t>& vNumbers)
{
    return RealMean(vNumbers);
}

int64_t IntMean(const vector<int64_t>& vNumbers)
{
    if (vNumbers.empty())
    {
        return numeric_limits<int64_t>::max();
    }
    int64_t sum = IntSum(vNumbers);
    return sum / static_cast<int64_t>(vNumbers.size());
}


Value MeanAsIntValue(const vector<int64_t>& vNumbers)
{
    return IntMean(vNumbers);
}

double RealRMSD(const vector<int64_t>& vNumbers)
{
    if (vNumbers.empty())
    {
        return numeric_limits<double>::max();
    }
    double mean = RealMean(vNumbers);
    double sumsq = 0.0;
    BOOST_FOREACH(const int64_t& value, vNumbers)
    {
        double d = static_cast<double>(value) - mean;
        sumsq += d * d;
    }
    double variance = sumsq / static_cast<double>(vNumbers.size());
    return sqrt(variance);
}

Value RMSDAsRealValue(const vector<int64_t>& vNumbers)
{
    return RealRMSD(vNumbers);
}

Value GetWindowedValue(const Array& params,
                       const StatHelper& helper)
{
    int nPeriod = params[0].get_int();
    if (nPeriod < 1)
    {
        throw runtime_error(
            "Period should be greater than 0.\n");
    }
    if ((unsigned int) nPeriod > 36525 * SEC_PER_DAY)
    {
        throw runtime_error(
            "Period should be less than 100 years.\n");
    }

    int nWindow = params[1].get_int();
    if (nWindow < 1)
    {
        throw runtime_error(
            "Window size should be greater than 0.\n");
    }
    if (nWindow > nPeriod)
    {
        throw runtime_error(
            "Window size should be less than or equal to period.\n");
    }

    int nGranularity = params[2].get_int();
    if (nGranularity < 1)
    {
        throw runtime_error(
            "Window spacing should be greater than 0.\n");
    }
    if (nGranularity > nWindow)
    {
        throw runtime_error(
            "Window spacing should be less than or equal to window.\n");
    }

    if (pindexBest == NULL)
    {
        throw runtime_error("No blocks.\n");
    }

    CBlockIndex* pindex = pindexBest;
    unsigned int nTime = pindex->nTime;

    unsigned int nPeriodEnd = nTime;
    unsigned int nPeriodStart = 1 + nPeriodEnd - nPeriod;

    vector<unsigned int> vBlockTimes;
    vector<int64_t> vNumbers;
    while (pindex && pindex->pprev)
    {
        vBlockTimes.push_back(pindex->nTime);
        int64_t number = helper.Get(pindex);
        vNumbers.push_back(number);
        pindex = pindex->pprev;
        nTime = pindex->nTime;
        if (nTime < nPeriodStart)
        {
            break;
        }
    }

    std::reverse(vBlockTimes.begin(), vBlockTimes.end());
    std::reverse(vNumbers.begin(), vNumbers.end());

    unsigned int nSizePeriod = vBlockTimes.size();

    Array aryWindowStartTimes;
    Array aryTotalBlocks;
    Array aryTotals;

    unsigned int nWindowStart = nPeriodStart;
    unsigned int nWindowEnd = nWindowStart + nWindow - 1;

    unsigned int idx = 0;
    unsigned int idxNext = 0;
    bool fNextUnknown = true;

    while (nWindowEnd < nPeriodEnd)
    {
        if (fNextUnknown)
        {
            idxNext = idx;
        }
        else
        {
            fNextUnknown = true;
        }
        unsigned int nNextWindowStart = nWindowStart + nGranularity;
        unsigned int nWindowBlocks = 0;
        vector<int64_t> vWindowValues;
        for (idx = idxNext; idx < nSizePeriod; ++idx)
        {
            unsigned int nBlockTime = vBlockTimes[idx];
            // assumes blocks are chronologically ordered
            if (nBlockTime > nWindowEnd)
            {
                aryWindowStartTimes.push_back((boost::int64_t)nWindowStart);
                aryTotals.push_back(helper.Reduce(vWindowValues));
                aryTotalBlocks.push_back((boost::int64_t)nWindowBlocks);
                nWindowStart = nNextWindowStart;
                nWindowEnd += nGranularity;
                break;
            }
            nWindowBlocks += 1;
            vWindowValues.push_back(vNumbers[idx]);
            if (fNextUnknown && (nBlockTime >= nNextWindowStart))
            {
                idxNext = idx;
                fNextUnknown = false;
            }
        }
    }

    Object obj;
    obj.push_back(Pair("window_start", aryWindowStartTimes));
    obj.push_back(Pair("number_blocks", aryTotalBlocks));
    obj.push_back(Pair(helper.GetLabel(), aryTotals));

    return obj;
}


static const string strWindowHelp =
            "  last window ends at time of most recent block\n"
            "  - <period> : duration over which to calculate (sec)\n"
            "  - <windowsize> : duration of each window (sec)\n"
            "  - <windowspacing> : duration between start of consecutive windows (sec)\n"
            "Returns an object with attributes:\n"
            "  - window_start: starting time of each window\n"
            "  - number_blocks: number of blocks in each window\n";


int64_t GetTxVolume(CBlockIndex* pindex)
{
    // Breakout's CBlockIndex does not cache a tx count, so read the block.
    CBlock block;
    if (!block.ReadFromDisk(pindex, true))
    {
        return 0;
    }
    return (int64_t)block.vtx.size();
}

Value gettxvolume(const Array& params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || params.size() < 1 || params.size() > 3)
    {
        throw runtime_error(
            strExploreHelp +
            "gettxvolume <period> <windowsize> <windowspacing>\n" +
            strWindowHelp +
            "  - tx_volume: number of transactions in each window");
    }

    static const string strValueName = "tx_volume";

    StatHelper helper("tx_volume", &GetTxVolume, &SumAsAmount);

    return GetWindowedValue(params, helper);
}


int64_t GetBlockInterval(CBlockIndex* pindex)
{
    int64_t interval;
    if (pindex->pnext)
    {
        interval = (int64_t)pindex->pnext->nTime - (int64_t)pindex->nTime;
    }
    else
    {
        // doubling is an application of the Copernican Principle
        interval = 2 * (GetAdjustedTime() - (int64_t)pindex->nTime);
    }
    return interval;
}

Value getblockinterval(const Array& params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || params.size() < 1 || params.size() > 3)
    {
        throw runtime_error(
            strExploreHelp +
            "getblockinterval <period> <windowsize> <windowspacing>\n" +
            strWindowHelp +
            "  - block_interval: total block interval for the window in seconds");
    }

    StatHelper helper("block_interval",
                      &GetBlockInterval,
                      &SumAsIntValue);

    return GetWindowedValue(params, helper);
}

Value getblockintervalmean(const Array& params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || params.size() < 1 || params.size() > 3)
    {
        throw runtime_error(
            strExploreHelp +
            "getblockintervalmean <period> <windowsize> <windowspacing>\n" +
            strWindowHelp +
            "  - block_interval_mean: rmsd of the block intervals for the "
            "window in seconds");
    }

    StatHelper helper("block_interval_mean",
                      &GetBlockInterval,
                      &MeanAsRealValue);

    return GetWindowedValue(params, helper);
}

Value getblockintervalrmsd(const Array& params, bool fHelp)
{
    string strExploreHelp = CheckExploreAPI(fHelp);
    if (fHelp || params.size() < 1 || params.size() > 3)
    {
        throw runtime_error(
            strExploreHelp +
            "getblockintervalrmsd <period> <windowsize> <windowspacing>\n" +
            strWindowHelp +
            "  - block_interval_rmsd: rmsd of the block intervals for the "
            "window in seconds");
    }

    StatHelper helper("block_interval_rmsd",
                      &GetBlockInterval,
                      &RMSDAsRealValue);

    return GetWindowedValue(params, helper);
}
