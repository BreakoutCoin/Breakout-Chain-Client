// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Copyright (c) 2020 The Stealth Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.

#include <map>

#include <boost/version.hpp>
#include <boost/filesystem.hpp>

#include <leveldb/env.h>
#include <leveldb/cache.h>
#include <leveldb/filter_policy.h>
#include <memenv/memenv.h>

#include "exploredb-leveldb.h"
#include "explore/ExploreTx.hpp"
#include "util.h"
#include "main.h"

using namespace std;
using namespace boost;

leveldb::DB *exploredb; // global pointer for the explore LevelDB instance

static leveldb::Options GetExploreOptions() {
    leveldb::Options options;
    int nCacheSizeMB = GetArg("-dbcache", 25);
    options.block_cache = leveldb::NewLRUCache(nCacheSizeMB * 1048576);
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    return options;
}

static void init_exploredb(leveldb::Options& options, bool fRemoveOld = false) {
    // Unlike the txleveldb, the exploredb never touches the block files; it is
    // an independent index that can be discarded and rebuilt at will.
    boost::filesystem::path directory = GetDataDir() / "exploredb";

    if (fRemoveOld) {
        boost::filesystem::remove_all(directory); // remove directory
    }

    boost::filesystem::create_directory(directory);
    printf("Opening explore LevelDB in %s\n", directory.string().c_str());
    leveldb::Status status = leveldb::DB::Open(options, directory.string(), &exploredb);
    if (!status.ok()) {
        throw runtime_error(strprintf("init_exploredb(): error opening database environment %s", status.ToString().c_str()));
    }
}

CExploreDB::CExploreDB(const char* pszMode)
{
    assert(pszMode);
    activeBatch = NULL;
    fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
    nVersion = 0;

    if (exploredb) {
        pdb = exploredb;
        return;
    }

    bool fCreate = strchr(pszMode, 'c');

    options = GetExploreOptions();
    options.create_if_missing = fCreate;

    init_exploredb(options); // Init directory
    pdb = exploredb;

    if (Exists(string("version")))
    {
        ReadVersion(nVersion);
        printf("Explore index version is %d\n", nVersion);

        if (nVersion < EXPLOREDB_VERSION)
        {
            printf("Required explore index version is %d, removing old database\n",
                   EXPLOREDB_VERSION);

            // Leveldb instance destruction
            delete exploredb;
            exploredb = pdb = NULL;
            delete activeBatch;
            activeBatch = NULL;

            init_exploredb(options, true); // Remove directory and create new database
            pdb = exploredb;

            bool fTmp = fReadOnly;
            fReadOnly = false;
            WriteVersion(EXPLOREDB_VERSION); // Save explore index version
            fReadOnly = fTmp;
        }
    }
    else if (fCreate)
    {
        bool fTmp = fReadOnly;
        fReadOnly = false;
        WriteVersion(EXPLOREDB_VERSION);
        fReadOnly = fTmp;
    }

    printf("Opened explore LevelDB successfully\n");
}

void CExploreDB::Close()
{
    delete exploredb;
    exploredb = pdb = NULL;
    delete options.filter_policy;
    options.filter_policy = NULL;
    delete options.block_cache;
    options.block_cache = NULL;
    delete activeBatch;
    activeBatch = NULL;
}

bool CExploreDB::ClearAll()
{
    // drop any pending batch
    delete activeBatch;
    activeBatch = NULL;

    // close the shared global instance
    delete exploredb;
    exploredb = pdb = NULL;

    // free the options owned by this instance
    delete options.filter_policy;
    options.filter_policy = NULL;
    delete options.block_cache;
    options.block_cache = NULL;

    // wipe the directory
    boost::filesystem::remove_all(GetDataDir() / "exploredb");

    // reopen empty
    options = GetExploreOptions();
    options.create_if_missing = true;
    init_exploredb(options);
    pdb = exploredb;

    // stamp the schema version
    bool fTmp = fReadOnly;
    fReadOnly = false;
    WriteVersion(EXPLOREDB_VERSION);
    fReadOnly = fTmp;

    printf("Cleared exploredb\n");
    return true;
}

bool CExploreDB::TxnBegin()
{
    assert(!activeBatch);
    activeBatch = new leveldb::WriteBatch();
    return true;
}

bool CExploreDB::TxnCommit()
{
    assert(activeBatch);
    leveldb::Status status = pdb->Write(leveldb::WriteOptions(), activeBatch);
    delete activeBatch;
    activeBatch = NULL;
    if (!status.ok()) {
        printf("Explore LevelDB batch commit failure: %s\n", status.ToString().c_str());
        return false;
    }
    return true;
}

class CExploreBatchScanner : public leveldb::WriteBatch::Handler {
public:
    std::string needle;
    bool *deleted;
    std::string *foundValue;
    bool foundEntry;

    CExploreBatchScanner() : foundEntry(false) {}

    virtual void Put(const leveldb::Slice& key, const leveldb::Slice& value) {
        if (key.ToString() == needle) {
            foundEntry = true;
            *deleted = false;
            *foundValue = value.ToString();
        }
    }

    virtual void Delete(const leveldb::Slice& key) {
        if (key.ToString() == needle) {
            foundEntry = true;
            *deleted = true;
        }
    }
};

// When performing a read, if we have an active batch we need to check it first
// before reading from the database, as the rest of the code assumes that once
// a database transaction begins reads are consistent with it.
bool CExploreDB::ScanBatch(const CDataStream &key, string *value, bool *deleted) const {
    assert(activeBatch);
    *deleted = false;
    CExploreBatchScanner scanner;
    scanner.needle = key.str();
    scanner.deleted = deleted;
    scanner.foundValue = value;
    leveldb::Status status = activeBatch->Iterate(&scanner);
    if (!status.ok()) {
        throw runtime_error(status.ToString());
    }
    return scanner.foundEntry;
}


/*  ExploreBest
 *  The hash and height of the last block indexed into the exploredb.
 */
bool CExploreDB::ReadExploreBest(uint256& hashRet, int& heightRet)
{
    hashRet = 0;
    heightRet = -1;
    std::pair<uint256, int> value;
    if (!Read(string("exploreBestBlock"), value))
    {
        return false;
    }
    hashRet = value.first;
    heightRet = value.second;
    return true;
}
bool CExploreDB::WriteExploreBest(const uint256& hash, int height)
{
    return Write(string("exploreBestBlock"), std::make_pair(hash, height));
}


// Generic prefix eraser retained from the Stealth implementation. With the
// separate exploredb, a full reindex uses ClearAll() (a directory wipe)
// instead, but this remains available for targeted record-type clearing.
bool CExploreDB::EraseStartsWith(const string& strSentinel,
                                 const string& strSearch,
                                 bool fActiveBatchOK)
{
    if ((!fActiveBatchOK) && activeBatch)
    {
        return error("EraseStartsWith(): active batch not allowed");
    }
    int count = 0;
    int xcount = 0;
    leveldb::Iterator *iter = pdb->NewIterator(leveldb::ReadOptions());
    iter->Seek(strSentinel);
    // don't erase the sentinel
    iter->Next();
    if (!TxnBegin())
    {
        return error("EraseStartsWith() : first TxnBegin failed");
    }
    while (iter->Valid())
    {
        xcount += 1;
        if (fDebugExplore && (xcount % 100000 == 0))
        {
            printf("examined %d records\n", xcount);
        }
        if (fRequestShutdown)
        {
            break;
        }
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.write(iter->key().data(), iter->key().size());
        string strPrefix;
        ssKey >> strPrefix;
        if (strPrefix != strSearch)
        {
            if (count > 0)
            {
                break;
            }
            else
            {
                iter->Next();
                continue;
            }
        }
        if ((xcount > 0) && ((count % 10000) == 0))
        {
            if (!TxnCommit())
            {
                return error("EraseStartsWith() : TxnCommit failed");
            }
            if (!TxnBegin())
            {
                return error("EraseStartsWith() : TxnBegin failed");
            }
        }
        count += 1;
        string strKeyDel(ssKey.full_str());
        if (activeBatch)
        {
            activeBatch->Delete(strKeyDel);
        }
        else
        {
            leveldb::Status status = pdb->Delete(leveldb::WriteOptions(), strKeyDel);
            if (!status.ok())
            {
                return error("TSNH: Can't erase record type \"%s\".",
                             strSearch.c_str());
            }
        }
        iter->Next();
    }
    delete iter;
    if (!TxnCommit())
    {
        return error("EraseStartsWith() : final TxnCommit failed");
    }
    return true;
}

bool CExploreDB::WriteExploreSentinel(int value)
{
    return Write(EXPLORE_SENTINEL, value);
}

/*  AddrQty
 *  Parameters - t:type, addr:address, nColor:color, qty:quantity
 */
bool CExploreDB::ReadAddrQty(const exploreKey_t& t, const string& addr, int nColor, int& qtyRet)
{
    qtyRet = 0;
    ss_key_t key = std::make_pair(t, std::make_pair(addr, nColor));
    return ReadRecord(key, qtyRet);
}
bool CExploreDB::WriteAddrQty(const exploreKey_t& t, const string& addr, int nColor, const int& qty)
{
    ss_key_t key = std::make_pair(t, std::make_pair(addr, nColor));
    return Write(key, qty);
}

/*  AddrTx
 *  Parameters - t:type, addr:address, nColor:color, qty:quantity,
                 value:input_info|output_info|inout
 */
bool CExploreDB::RemoveAddrTx(const exploreKey_t& t, const string& addr, int nColor, const int& qty)
{
    std::pair<ss_key_t, int> key =
        std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)), qty);
    return RemoveRecord(key);
}
bool CExploreDB::AddrTxIsViable(const exploreKey_t& t, const string& addr, int nColor, const int& qty)
{
    std::pair<ss_key_t, int> key =
        std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)), qty);
    return IsViable(key);
}

/*  AddrList
 *  Parameters - t:type, addr:address, nColor:color, qty:quantity, value:inout_list
 */
bool CExploreDB::RemoveAddrList(const exploreKey_t& t, const string& addr, int nColor, const int& qty)
{
    std::pair<ss_key_t, int> key =
        std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)), qty);
    return RemoveRecord(key);
}
bool CExploreDB::AddrListIsViable(const exploreKey_t& t, const string& addr, int nColor, const int& qty)
{
    std::pair<ss_key_t, int> key =
        std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)), qty);
    return IsViable(key);
}


/*  AddrLookup
 *  Parameters - t:type, addr:address, nColor:color,
 *               txid:TxID, n:vout|vin, qty:quantity
 */
bool CExploreDB::ReadAddrLookup(const exploreKey_t& t, const string& addr, int nColor,
                                const uint256& txid, const int& n,
                                int& qtyRet)
{
   qtyRet = -1;
   lookup_key_t key = std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)),
                                     std::make_pair(txid, n));
   return ReadRecord(key, qtyRet);
}
bool CExploreDB::WriteAddrLookup(const exploreKey_t& t, const string& addr, int nColor,
                                 const uint256& txid, const int& n,
                                 const int& qty)
{
   lookup_key_t key = std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)),
                                     std::make_pair(txid, n));
   return Write(key, qty);
}
bool CExploreDB::RemoveAddrLookup(const exploreKey_t& t, const string& addr, int nColor,
                                  const uint256& txid, const int& n)
{
   lookup_key_t key = std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)),
                                     std::make_pair(txid, n));
   return RemoveRecord(key);
}
bool CExploreDB::AddrLookupIsViable(const exploreKey_t& t, const string& addr, int nColor,
                                    const uint256& txid, const int& n)
{
   lookup_key_t key = std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)),
                                     std::make_pair(txid, n));
   return IsViable(key);
}


/*  AddrValue
 *  Parameters - t:type, addr:address, nColor:color, v:value
 */
bool CExploreDB::ReadAddrValue(const exploreKey_t& t, const string& addr, int nColor,
                               int64_t& vRet)
{
    vRet = 0;
    ss_key_t key = std::make_pair(t, std::make_pair(addr, nColor));
    return ReadRecord(key, vRet);
}
bool CExploreDB::WriteAddrValue(const exploreKey_t& t, const string& addr, int nColor,
                                const int64_t& v)
{
    ss_key_t key = std::make_pair(t, std::make_pair(addr, nColor));
    return Write(key, v);
}
bool CExploreDB::AddrValueIsViable(const exploreKey_t& t, const std::string& addr, int nColor)
{
    ss_key_t key = std::make_pair(t, std::make_pair(addr, nColor));
    return IsViable(key);
}

/*  AddrSet (rich list, per color)
 *  Parameters - t:type, nColor:color, b:balance, s:addresses
 */
bool CExploreDB::ReadAddrSet(const exploreKey_t& t, int nColor, const int64_t b, set<string>& sRet)
{
    sRet.clear();
    balance_set_key_t key = std::make_pair(t, std::make_pair(nColor, b));
    return ReadRecord(key, sRet);
}
bool CExploreDB::WriteAddrSet(const exploreKey_t& t, int nColor, const int64_t b, const set<string>& s)
{
    balance_set_key_t key = std::make_pair(t, std::make_pair(nColor, b));
    return Write(key, s);
}
bool CExploreDB::RemoveAddrSet(const exploreKey_t& t, int nColor, const int64_t b)
{
    balance_set_key_t key = std::make_pair(t, std::make_pair(nColor, b));
    return RemoveRecord(key);
}

/*  ExploreTx
 *  Parameters - txid:TxID, extx:ExploreTx
 */
bool CExploreDB::ReadExploreTx(const uint256& txid, ExploreTx& extxRet)
{
   extxRet.SetNull();
   std::pair<exploreKey_t, uint256> key = std::make_pair(EXPLORE_TX, txid);
   return ReadRecord(key, extxRet);
}
bool CExploreDB::WriteExploreTx(const uint256& txid, const ExploreTx& extx)
{
   std::pair<exploreKey_t, uint256> key = std::make_pair(EXPLORE_TX, txid);
   return Write(key, extx);
}
bool CExploreDB::RemoveExploreTx(const uint256& txid)
{
    std::pair<exploreKey_t, uint256> key = std::make_pair(EXPLORE_TX, txid);
    return RemoveRecord(key);
}
