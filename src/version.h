// Copyright (c) 2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_VERSION_H
#define BITCOIN_VERSION_H

#include <stdint.h>

#include "clientversion.h"
#include <string>

//
// client versioning
//

static const int CLIENT_VERSION =
                           1000000 * CLIENT_VERSION_MAJOR
                         +   10000 * CLIENT_VERSION_MINOR
                         +     100 * CLIENT_VERSION_REVISION
                         +       1 * CLIENT_VERSION_BUILD;

extern const std::string CLIENT_NAME;
extern const std::string CLIENT_BUILD;
extern const std::string CLIENT_DATE;

//
// database format versioning
//
static const int DATABASE_VERSION = 70508;

//
// network protocol versioning
//

// 61002: original release version
// 61005: mainnet launch
// 61006: finalized deck PoS reward
// 61007: added burn protocol (1.4.0.0)
// 61008: staking improvement (1.4.2.0)
// 61009: card staking and BRK inflation correction (1.4.5.0)
//        createmultisigaddress (1.4.6.0)
// 61010: card staking rewards nonzero
//        hard-coded clearnet nodes
//        code cleanup & parameter consolidation
//        multisig api
//        newest leveldb (v1.2)
//        tor v0.3: 0.3.0.9
//        pool friendly mining
//        PoW is now scrypt (was sha256d)
//        (1.5.0.0)
// 61011: card staking fix (1.5.1.0)
static const int PROTOCOL_VERSION = 61011;


// intial proto version, to be increased after version/verack negotiation
// TODO: it seems like somewhere this is transmitted as an 8 bit int or something
// version numbers in the 2 byte range play havoc with node handshake
// the answer is somewhere in class CDataStream
// probably inconsistency with Serialize() and GetSerializeSize()
// will enforce appropriate width with int8_t until I figure it out or it matters
static const int8_t INIT_PROTO_VERSION = 209;


// This is replaced by a function, peers will be dropped that do not
// support the same chain as this node.
// earlier versions not supported as of Feb 2012, and are disconnected
// static const int MIN_PEER_PROTO_VERSION = 209;

// nTime field added to CAddress, starting with this version;
// if possible, avoid requesting addresses nodes older than this
static const int CADDR_TIME_VERSION = 31402;

// only request blocks from nodes outside this range of versions
static const int NOBLKS_VERSION_START = 60002;
static const int NOBLKS_VERSION_END = 60006;

// BIP 0031, pong message, is enabled for all versions AFTER this one
static const int BIP0031_VERSION = 60000;

// "mempool" command, enhanced "getdata" behavior starts with this version:
static const int MEMPOOL_GD_VERSION = 60002;

#endif
