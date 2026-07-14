// Copyright (c) 2009-2012 The Bitcoin Developers.
// Copyright (c) 2020 The Stealth Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Breakout Explore stores its address/transaction index in a LevelDB that is
// SEPARATE from the transaction index (txleveldb).  Keeping the explore data
// in its own database (exploredb) means it can be cleared and rebuilt
// independently of the chain state, which is what makes -reindexexplore (and
// the automatic best-block "auto-heal") a simple wipe-and-replay operation.
//
// CExploreDB mirrors the lightweight global-state wrapper design of CTxDB:
// each instance is cheap to construct and just points at a single global
// leveldb::DB instance.

#ifndef BREAKOUT_EXPLOREDB_LEVELDB_H
#define BREAKOUT_EXPLOREDB_LEVELDB_H

#include "main.h"

#include "explore/ExploreConstants.hpp"

#include <map>
#include <set>
#include <string>
#include <vector>

#include <leveldb/db.h>
#include <leveldb/write_batch.h>

class ExploreTx;

// Explore debug logging flag (defined alongside the explore engine in
// explore.cpp; declared here so the DB layer can honour -debugexplore).
extern bool fDebugExplore;

///////////////////////////////////////////////////////////////////////////////
// LevelDB Keys (explore)
///////////////////////////////////////////////////////////////////////////////
// Breakout is multi-color: the indexing unit is (address, color), so every
// per-address record is scoped to a single currency.
typedef std::pair<std::string, int> addr_color_t;
// generalized explore keys
typedef std::pair<exploreKey_t, addr_color_t> ss_key_t;
// amount key (e.g. balances)
typedef std::pair<ss_key_t, int64_t> amount_key_t;
// in-out lookup key
typedef std::pair<uint256, int> txidn_key_t;
typedef std::pair<ss_key_t, txidn_key_t> lookup_key_t;
// balance-set key: rich list is per-color, keyed by (type, (color, balance))
typedef std::pair<exploreKey_t, std::pair<int, int64_t> > balance_set_key_t;
///////////////////////////////////////////////////////////////////////////////

// Explore database schema version. Bump this to force existing exploredb
// contents to be discarded and rebuilt on next startup (independent of the
// txleveldb DATABASE_VERSION).
static const int EXPLOREDB_VERSION = 1;


template<typename K>
std::string DBKeyToString(const K& key)
{
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(1000);
    ssKey << key;
    return ssKey.str();
}


class CExploreDB
{
public:
    CExploreDB(const char* pszMode="r+");
    ~CExploreDB() {
        // Note that this is not the same as Close() because it deletes only
        // data scoped to this CExploreDB object.
        delete activeBatch;
    }

    // Destroys the underlying shared global state accessed by this CExploreDB.
    void Close();

    // Wipe the entire exploredb (close, remove the directory, reopen empty)
    // and stamp the schema version. Intended for -reindexexplore / auto-heal.
    // Must only be called at startup while no other CExploreDB instance is
    // holding an active batch.
    bool ClearAll();

private:
    leveldb::DB *pdb;  // Points to the global instance.

    // A batch stores up writes and deletes for atomic application. When this
    // field is non-NULL, writes/deletes go there instead of directly to disk.
    leveldb::WriteBatch *activeBatch;
    leveldb::Options options;
    bool fReadOnly;
    int nVersion;

protected:
    // Returns true and sets (value,false) if activeBatch contains the given key
    // or leaves value alone and sets deleted = true if activeBatch contains a
    // delete for it.
    bool ScanBatch(const CDataStream &key, std::string *value, bool *deleted) const;

    template<typename K, typename T>
    bool Read(const K& key, T& value, bool& fOk)
    {
        fOk = true;
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        std::string strValue;

        bool readFromDb = true;
        if (activeBatch) {
            // First we must search for it in the currently pending set of
            // changes to the db. If not found in the batch, go on to read disk.
            bool deleted = false;
            readFromDb = ScanBatch(ssKey, &strValue, &deleted) == false;
            if (deleted) {
                return false;
            }
        }
        if (readFromDb) {
            leveldb::Status status = pdb->Get(leveldb::ReadOptions(),
                                              ssKey.str(), &strValue);
            if (!status.ok())
            {
                if (status.IsNotFound())
                {
                    return false;
                }
                // Some unexpected error.
                printf("LevelDB read failure: %s\n", status.ToString().c_str());
                fOk = false;
                return false;
            }
        }
        // Unserialize value
        try {
            CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(),
                                SER_DISK, CLIENT_VERSION);
            ssValue >> value;
        }
        catch (std::exception &e) {
            fOk = false;
            return false;
        }
        return true;
    }

    template<typename K, typename T>
    bool Read(const K& key, T& value)
    {
        bool fReadOk;
        return Read(key, value, fReadOk);
    }

    template<typename K, typename T>
    bool Write(const K& key, const T& value)
    {
        if (fReadOnly)
            assert(!"Write called on database in read-only mode");

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);
        ssValue.reserve(10000);
        ssValue << value;

        if (activeBatch) {
            activeBatch->Put(ssKey.str(), ssValue.str());
            return true;
        }
        leveldb::Status status = pdb->Put(leveldb::WriteOptions(), ssKey.str(), ssValue.str());
        if (!status.ok()) {
            printf("LevelDB write failure: %s\n", status.ToString().c_str());
            return false;
        }
        return true;
    }

    template<typename K>
    bool Erase(const K& key)
    {
        if (!pdb)
        {
            return false;
        }
        if (fReadOnly)
            assert(!"Erase called on database in read-only mode");

        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        if (activeBatch)
        {
            activeBatch->Delete(ssKey.str());
            return true;
        }
        leveldb::Status status = pdb->Delete(leveldb::WriteOptions(), ssKey.str());
        if (!status.ok())
        {
            if (status.IsNotFound())
            {
                return true;
            }
            else
            {
                return false;
            }
        }
        return true;
    }

    template<typename K>
    bool Exists(const K& key)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        std::string unused;

        if (activeBatch) {
            bool deleted;
            if (ScanBatch(ssKey, &unused, &deleted) && !deleted) {
                return true;
            }
        }

        leveldb::Status status = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &unused);
        return status.IsNotFound() == false;
    }

    // IsViable() has slightly different semantics from Exists().
    // The latter will return true even when the record is marked
    // for deletion in the active batch but hasn't yet been deleted
    // on disk. The former will return true only if the record
    // is both found somewhere and not marked for deletion anywhere.
    template<typename K>
    bool IsViable(const K& key)
    {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(1000);
        ssKey << key;
        std::string unused;

        if (activeBatch)
        {
            bool deleted;
            if (ScanBatch(ssKey, &unused, &deleted))
            {
                if (deleted)
                {
                    return false;
                }
                else
                {
                    return true;
                }
            }
        }

        leveldb::Status status = pdb->Get(leveldb::ReadOptions(), ssKey.str(), &unused);
        return status.IsNotFound() == false;
    }

    template<typename K, typename T>
    bool ReadRecord(const K& key, T& value)
    {
        bool fReadOk;
        Read(key, value, fReadOk);
        return fReadOk;
    }

    template<typename K>
    bool RemoveRecord(const K& key)
    {
        if (IsViable(key))
        {
            return Erase(key);
        }
        return false;
    }

public:
    bool TxnBegin();
    bool TxnCommit();
    bool TxnAbort()
    {
        delete activeBatch;
        activeBatch = NULL;
        return true;
    }

    bool ReadVersion(int& nVersionRet)
    {
        nVersionRet = 0;
        return Read(std::string("version"), nVersionRet);
    }

    bool WriteVersion(int nVersionIn)
    {
        return Write(std::string("version"), nVersionIn);
    }

    // The hash and height of the last block whose transactions were indexed
    // into the exploredb. Compared against txleveldb's best chain at startup
    // to decide whether the explore index needs to be rebuilt (auto-heal).
    bool ReadExploreBest(uint256& hashRet, int& heightRet);
    bool WriteExploreBest(const uint256& hash, int height);

    bool EraseStartsWith(const std::string& strSentinel,
                         const std::string& strSearch,
                         bool fActiveBatchOK);

    bool WriteExploreSentinel(int value=0);
    bool ReadAddrQty(const exploreKey_t& t, const std::string& addr, int nColor, int& qtyRet);
    bool WriteAddrQty(const exploreKey_t& t, const std::string& addr, int nColor, const int& qty);

     // Parameters - t:type, addr:address, nColor:color, qty:quantity,
     //              value:input_info|output_info|inout_lookup|inout_list
    template<typename T>
    bool ReadAddrTx(const exploreKey_t& t, const std::string& addr, int nColor,
                    const int& qty, T& value)
    {
        value.SetNull();
        std::pair<ss_key_t, int> key =
            std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)), qty);
        return ReadRecord(key, value);
    }
    template<typename T>
    bool WriteAddrTx(const exploreKey_t& t, const std::string& addr, int nColor,
                     const int& qty, const T& value)
    {
        std::pair<ss_key_t, int> key =
            std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)), qty);
        return Write(key, value);
    }

    bool RemoveAddrTx(const exploreKey_t& t, const std::string& addr, int nColor, const int& qty);
    bool AddrTxIsViable(const exploreKey_t& t, const std::string& addr, int nColor, const int& qty);

    template<typename T>
    bool ReadAddrList(const exploreKey_t& t, const std::string& addr, int nColor,
                      const int& qty, T& value)
    {
        value.SetNull();
        std::pair<ss_key_t, int> key =
            std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)), qty);
        return ReadRecord(key, value);
    }
    template<typename T>
    bool WriteAddrList(const exploreKey_t& t, const std::string& addr, int nColor,
                       const int& qty, const T& value)
    {
        std::pair<ss_key_t, int> key =
            std::make_pair(std::make_pair(t, std::make_pair(addr, nColor)), qty);
        return Write(key, value);
    }

    bool RemoveAddrList(const exploreKey_t& t, const std::string& addr, int nColor, const int& qty);
    bool AddrListIsViable(const exploreKey_t& t, const std::string& addr, int nColor, const int& qty);

    bool ReadAddrLookup(const exploreKey_t& t, const std::string& addr, int nColor,
                        const uint256& txid, const int& n,
                        int& qtyRet);
    bool WriteAddrLookup(const exploreKey_t& t, const std::string& addr, int nColor,
                         const uint256& txid, const int& n,
                         const int& qty);
    bool RemoveAddrLookup(const exploreKey_t& t, const std::string& addr, int nColor,
                          const uint256& txid, const int& n);
    bool AddrLookupIsViable(const exploreKey_t& t, const std::string& addr, int nColor,
                            const uint256& txid, const int& n);
    bool ReadAddrValue(const exploreKey_t& t, const std::string& addr, int nColor,
                       int64_t& vRet);
    bool WriteAddrValue(const exploreKey_t& t, const std::string& addr, int nColor,
                        const int64_t& v);
    bool AddrValueIsViable(const exploreKey_t& t, const std::string& addr, int nColor);
    bool ReadAddrSet(const exploreKey_t& t, int nColor, const int64_t b,
                     std::set<std::string>& sRet);
    bool WriteAddrSet(const exploreKey_t& t, int nColor, const int64_t b,
                      const std::set<std::string>& s);
    bool RemoveAddrSet(const exploreKey_t& t, int nColor, const int64_t b);

    bool ReadExploreTx(const uint256& txid, ExploreTx& extxRet);
    bool WriteExploreTx(const uint256& txid, const ExploreTx& extx);
    bool RemoveExploreTx(const uint256& txid);
};


#endif // BREAKOUT_EXPLOREDB_LEVELDB_H
