#!/usr/bin/env python3
#
# linearize-data.py: Construct a linear, no-fork version of the chain.
#
# Copyright (c) 2013 The Bitcoin developers
# Copyright (c) 2016-2018 Stealth R&D LLC
# Distributed under the MIT/X11 software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
#
# Requires:
#   pip install kawpow
#
# kawpow (https://github.com/RavenCommunity/cpp-kawpow) is a C/C++
# implementation of the KawPoW / ProgPoW algorithm exposed via CFFI.
# It also bundles keccak-256/512, so no extra crypto library is needed.

import json
import struct
import re
import os
import base64
import sys
import hashlib
import datetime
import time
import codecs
import argparse

# KawPoW / ProgPoW support (C/C++ extension via CFFI — fast)
import kawpow as _kawpow_lib

# for config file parsing
TRUE_VALUES =  ('true', 'yes', 'TRUE', 'YES', 'True', 'Yes', 1, True)
FALSE_VALUES =  ('false', 'no', 'FALSE', 'NO', 'False', 'No', 0, False)

SHA256D = "sha256d"
SCRYPT = "scrypt"
ETHASH = "ethash"

SCRYPT_START_TIME = 1502434800
ETHASH_START_TIME = 1809208853

SCRYPT_START_HEIGHT =  110766
ETHASH_START_HEIGHT = 3000000

hash_type = SHA256D

# ---------------------------------------------------------------------------
# KawPoW / ProgPoW helpers
# ---------------------------------------------------------------------------
#
# ProgPoW block header layout (120 bytes total):
#
#   nVersion    ( 4 bytes)  [  0:  4]
#   prev_hash   (32 bytes)  [  4: 36]
#   merkle_root (32 bytes)  [ 36: 68]
#   nTime       ( 4 bytes)  [ 68: 72]
#   nBits       ( 4 bytes)  [ 72: 76]
#   nHeight     ( 4 bytes)  [ 76: 80]
#   ---- GetKAWPOWHeaderHash covers bytes [0:80] via sha256d ----
#   nNonce64    ( 8 bytes)  [ 80: 88]   64-bit PoW nonce
#   mix_hash    (32 bytes)  [ 88:120]   stored mix hash
#
# The equivalent C++ call is:
#   uint256 nHeaderHash = block.GetKAWPOWHeaderHash();  // sha256d([0:80])
#   const ethash::hash256 hashHeader = to_hash256(nHeaderHash);
#   const ethash::result result = progpow::hash(
#       *pcontext, nHeight, hashHeader, block.nNonce64);
#
# kawpow.light_verify(header_hash, mix_hash, nonce) replicates this without
# needing to rebuild the DAG, making it suitable for read-only block scanning.
# It returns the 32-byte final hash in little-endian order.

PROGPOW_HEADER_LEN = 120   # bytes on disk for a ProgPoW-era block header


def progpow_digest(blk_hdr: bytes) -> bytes:
    """Return the 32-byte ProgPoW final hash (little-endian) for *blk_hdr*.

    Uses kawpow.light_verify, which verifies the stored mix_hash without
    regenerating the full DAG.  This is the correct approach for a
    linearizer that reads, verifies, and copies existing chain data.
    """
    if len(blk_hdr) < PROGPOW_HEADER_LEN:
        raise ValueError(
            "ProgPoW header must be %d bytes, got %d"
            % (PROGPOW_HEADER_LEN, len(blk_hdr))
        )

    # GetKAWPOWHeaderHash: sha256d of the first 80 bytes (no nonce, no mix_hash)
    header_hash = sha256d(blk_hdr[:80])

    # Extract nNonce64 and mix_hash from the remaining fields
    nonce    = struct.unpack("<Q", blk_hdr[80:88])[0]
    mix_hash = blk_hdr[88:120]

    # light_verify(header_hash, mix_hash, nonce) -> 32-byte final_hash (LE)
    return _kawpow_lib.light_verify(header_hash, mix_hash, nonce)


def progpow_hash_str(blk_hdr: bytes) -> bytes:
    """Return the hex block-ID string (as UTF-8 bytes) for a ProgPoW header.

    The display convention for Bitcoin-derived chains is to reverse the raw
    hash bytes before hex-encoding, so that the most-significant byte is
    printed first.
    """
    raw = progpow_digest(blk_hdr)
    return raw[::-1].hex().encode("utf-8")


class ReMap(object):
   def __init__(self, reason, new_hash):
      self.reason = reason
      self.new_hash = new_hash


settings = {}

def getTF(v):
  if v in TRUE_VALUES:
    return True
  if v in FALSE_VALUES:
    return False
  return None

def uint32(x):
    return x & 0xffffffff

def bytereverse(x):
    return uint32(( ((x) << 24) | (((x) << 8) & 0x00ff0000) |
               (((x) >> 8) & 0x0000ff00) | ((x) >> 24) ))

def bufreverse(in_buf):
    out_words = []
    for i in range(0, len(in_buf), 4):
        word = struct.unpack('@I', in_buf[i:i+4])[0]
        out_words.append(struct.pack('@I', bytereverse(word)))
    return b''.join(out_words)

def wordreverse(in_buf):
    out_words = []
    for i in range(0, len(in_buf), 4):
        out_words.append(in_buf[i:i+4])
    out_words.reverse()
    return b''.join(out_words)

def sha256d(blk_hdr):
    hash1 = hashlib.sha256()
    hash1.update(blk_hdr)
    hash1_o = hash1.digest()

    hash2 = hashlib.sha256()
    hash2.update(hash1_o)
    hash2_o = hash2.digest()
    return hash2_o

def scrypt(blk_hdr):
    return hashlib.scrypt(blk_hdr, salt=blk_hdr, n=1024, r=1, p=1, dklen=32)

def calc_hdr_hash(blk_hdr):
    global hash_type
    if hash_type == SCRYPT:
        return scrypt(blk_hdr)
    elif hash_type == SHA256D:
        return sha256d(blk_hdr)
    elif hash_type == ETHASH:
        return progpow_digest(blk_hdr)
    else:
        raise Exception("Unknown hash type")

def calc_hash_str(blk_hdr):
    global hash_type
    if hash_type in (SCRYPT, SHA256D):
        hash = calc_hdr_hash(blk_hdr)
        hash = bufreverse(hash)
        hash = wordreverse(hash)
        hash_str = hash.hex().encode("utf-8")
    elif hash_type == ETHASH:
        hash_str = progpow_hash_str(blk_hdr)
    else:
        raise Exception("Unknown hash type")
    return hash_str

def get_blk_dt(blk_hdr):
    members = struct.unpack("<I", blk_hdr[68:68+4])
    nTime = members[0]
    dt = datetime.datetime.fromtimestamp(nTime)
    dt_ym = datetime.datetime(dt.year, dt.month, 1)
    return (dt_ym, nTime)

def get_block_hashes(settings):
    blkindex = []
    f = open(settings['hashlist'], "r")
    for line in f:
        line = line.rstrip()
        blkindex.append(bytes(line, "utf-8"))

    print("Read %s hashes" % len(blkindex))

    return blkindex

def mkblockset(blkindex):
    blkmap = {}
    for hash in blkindex:
        blkmap[hash] = True
    return blkmap

# Make a lookup file that facilitates random access
# because indices are not guaranteed to be in order.
def mklookup(settings, blkindex):
    """
    settings: from linearize.cfg
    blkindex: list of main chain block indices in order
    """

    global hash_type

    max_height = settings['max_height']

    # construct lookup for reference indices that are out of order
    # key: block hash, value: (filename, seek)
    lookup = {}

    # blk0001.dat -> inFn=1, blk0002 -> inFn=2, etc
    inFn = 1
    # file() opened with blk####.dat
    inF = None
    # keeps track of blocks
    blkCount = 0

    VERBOSE = settings['verbose']
    DO_PROGRESS = not settings['quiet']
    UPDATE_EVERY = 1000
    while True:

        if DO_PROGRESS and (blkCount > 0) and ((blkCount % UPDATE_EVERY) == 0):
            print("Read %s blocks" % blkCount, end="\r", flush=True,
                                                         file=sys.stderr)

        # height is 1 + number of blocks
        if (max_height is not None) and (len(lookup) >= (max_height+1)):
            break

        if inF is None:
            print("Read %s blocks" % blkCount)
            fname = "%s/blk%04d.dat" % (settings['input'], inFn)
            if not (os.path.exists(fname) and os.path.isfile(fname)):
                print("No such file: %s" % fname)
                if blkCount == 0:
                    sys.exit(1)
            print("Input file: %s" % fname)
            try:
                inF = open(fname, "rb")
            except IOError:
                print("Done")
                return lookup

        position = (fname, inF.tell())

        inhdr = inF.read(8)
        if (not inhdr or (inhdr[0:1] == b"\0")):
            inF.close()
            inF = None
            inFn = inFn + 1
            continue

        inMagic = inhdr[:4]
        if (inMagic != settings['netmagic']):
            print("Read %s blocks" % blkCount)
            print("Invalid magic: 0x%s" % base64.b16encode(inMagic))
            return lookup
        inLenLE = inhdr[4:]
        su = struct.unpack("<I", inLenLE)
        inLen = su[0]
        rawblock = inF.read(inLen)
        blk_time = struct.unpack("<i", rawblock[68: 72])[0]
        blk_ver = struct.unpack("<i", rawblock[:4])[0]
        if blk_ver < 7:
            header_len = 80
        else:
            # nVersion(4) + hashes(32*2) + nTime(4) + nBits(4) + nHeight(4)
            # + nNonce64(8) + mix_hash(32) = 120 bytes for ProgPoW-era blocks
            header_len = PROGPOW_HEADER_LEN
        blk_hdr = rawblock[:header_len]

        # Determine hash type for this block based on running count.
        if blk_time >= ETHASH_START_TIME:
            print(blkCount)
            raise SystemExit
            _ht = ETHASH
        elif blk_time >= SCRYPT_START_TIME:
            _ht = SCRYPT
        else:
            _ht = SHA256D
        # Temporarily set the global so calc_hash_str picks the right algorithm.
        hash_type = _ht
        hash_str = calc_hash_str(blk_hdr)

        blkCount += 1

        if hash_str not in blkset:
            if VERBOSE:
                print("Read %s blocks" % blkCount)
                print("Skipping unknown block: %s" % hash_str.decode("utf-8"))
            continue

        lookup[hash_str] = position
    print("Read %s blocks" % blkCount)

    return lookup



def copydata(settings, blkindex, blkset):
    """
    settings: from linearize.cfg
    blkindex: list of main chain block indices in order
    blkset: map of key: main chain block hash, value: irrelevant
            used for lookup in old method
    """

    global hash_type

    print("Making lookup")
    # random access to block index
    # key: block hash, value: (filename, seek)
    lookup = mklookup(settings, blkindex)

    print("Writing data")

    # open all the files you need to read data, won't be that many
    # key: filename, value: open file()
    fileset = {}

    # output bootstrap file number
    outFn = 0
    # current output file size
    outsz = 0
    # current output bootstrap file() (opens for write)
    outF = None
    # current output bootstrap file name
    outFname = None
    # keeps track of blocks
    blkCount = 0


    lastDate = datetime.datetime(2000, 1, 1)
    highTS = 1408893517 - 315360000

    hash_type = SHA256D

    found_first_scrypt = False

    DO_PROGRESS = not settings['quiet']
    for (blkCount, hash_str) in enumerate(blkindex):
        fname, fpos = lookup[hash_str]
        if fname in fileset:
            inF = fileset[fname]
        else:
            try:
                inF = open(fname, "rb")
            except IOError:
                print("Wrote %s blocks" % blkCount)
                print("Suddenly can't read \"%s\". Aborting." % fname)
                return
            fileset[fname] = inF

        inF.seek(fpos)
        inhdr = inF.read(8)
        if (not inhdr or (inhdr[0:1] == b"\0")):
            inF.close()
            inF = None
            inFn = inFn + 1
            print("Wrote %s blocks" % blkCount)
            print("File \"%s\" changed. Aborting." % fname)
            return

        inMagic = inhdr[:4]
        if (inMagic != settings['netmagic']):
            print("Wrote %s blocks" % blkCount)
            print("Invalid magic: 0x%s. Aborting" % base64.b16encode(inMagic))
            return

        inLenLE = inhdr[4:]
        su = struct.unpack("<I", inLenLE)
        inLen = su[0]
        rawblock = inF.read(inLen)
        blk_time = struct.unpack("<i", rawblock[68: 72])[0]
        blk_ver = struct.unpack("<i", rawblock[:4])[0]
        if blk_ver < 7:
            header_len = 80
        else:
            # nVersion(4) + hashes(32*2) + nTime(4) + nBits(4) + nHeight(4)
            # + nNonce64(8) + mix_hash(32) = 120 bytes for ProgPoW-era blocks
            header_len = PROGPOW_HEADER_LEN
        blk_hdr = rawblock[:header_len]

        # Greater than because blkCount starts at 1 but height starts at 0
        if blk_time >= ETHASH_START_TIME:
            print(blkCount)
            raise SystemExit
            hash_type = ETHASH
        elif blk_time >= SCRYPT_START_TIME:
            hash_type = SCRYPT
            if not found_first_scrypt:
                print(f"First scrypt: {blk_time}, Version: {blk_ver}")
                print(f"   {calc_hash_str(blk_hdr)}")
                found_first_scrypt = True
        else:
            hash_type = SHA256D

        hash_str_check = calc_hash_str(blk_hdr)

        if hash_str_check != hash_str:
            _h = hash_str.decode("utf-8")
            print("Wrote %s blocks" % blkCount)
            print("Block %s unexpectedly changed. Aborting " % _h)
            return

        (blkDate, blkTS) = get_blk_dt(blk_hdr)
        if not outF:
            outFname = settings['output_file']
            print("Output file: %s" % outFname)
            outF = open(outFname, "wb")

        outF.write(inhdr)
        outF.write(rawblock)
        outsz = outsz + inLen + 8

        if blkTS > highTS:
            highTS = blkTS

        if DO_PROGRESS and ((blkCount % 1000) == 0):
            print("Wrote %s blocks" % blkCount, end="\r", flush=True,
                                                          file=sys.stderr)

    print("Wrote %s blocks" % blkCount)

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description="Construct a linear, no-fork version of the chain.")
    parser.add_argument("config", metavar="CONFIG-FILE",
        help="Configuration file (key=value format)")
    parser.add_argument("-H", "--hashlist", metavar="FILE",
        help="Path to the hash list file (overrides 'hashlist' in config)")
    parser.add_argument("-c", "--chain_id", metavar="HEX",
        help="64-character hex block hash identifying the chain head "
             "(overrides 'chain_id' in config)")
    parser.add_argument("-q", "--quiet", action="store_true", default=False,
        help="Suppress progress output to stderr (default: show progress)")
    args = parser.parse_args()

    # Validate --chain_id if provided
    if args.chain_id is not None:
        if not re.fullmatch(r'[0-9a-fA-F]{64}', args.chain_id):
            parser.error("--chain_id must be exactly 64 hexadecimal characters")

    f = open(args.config)
    for line in f:
        # skip comment lines
        m = re.search(r'^\s*#', line)
        if m:
            continue

        # parse key=value lines
        m = re.search(r'^(\w+)\s*=\s*(\S.*)$', line)
        if m is None:
            continue
        settings[m.group(1)] = m.group(2)
    f.close()

    # Apply CLI overrides (take precedence over config file values)
    if args.hashlist is not None:
        settings['hashlist'] = args.hashlist
    if args.chain_id is not None:
        settings['chain_id'] = args.chain_id
    settings['quiet'] = args.quiet

    if 'max_height' in settings:
        try:
            settings['max_height'] = int(settings['max_height'])
        except (ValueError, TypeError):
            settings['max_height'] = None
    else:
        settings['max_height'] = None
    if 'netmagic' not in settings:
        settings['netmagic'] = 'f9cfcbdf'
    if 'input' not in settings:
        settings['input'] = 'input'
    if 'hashlist' not in settings:
        print("Missing required configuration option \"hashlist\" "
              "(set in config or via -H/--hashlist)")
        sys.exit(1)
    if 'verbose' in settings:
        settings['verbose'] = getTF(settings['verbose'])
        if settings['verbose'] is None:
            print("Value for \"verbose\" setting should be \"true\"/\"false\"")
            sys.exit(1)
    else:
        settings['verbose'] = False

    settings['netmagic'] = codecs.decode(settings['netmagic'], 'hex')

    if 'output_file' not in settings and 'output' not in settings:
        print("Missing output file / directory")
        sys.exit(1)

    blkindex = get_block_hashes(settings)
    print("Length of block index: %d" % len(blkindex))
    blkset = mkblockset(blkindex)


    if "chain_id" in settings:
        chain_id = settings['chain_id']
        print(f"Chain ID: {chain_id}")
        chain_id_bytes = bytes(chain_id, "utf-8")
        if chain_id_bytes not in blkset:
            print("Chain ID not found, exiting")
            sys.exit(1)
    else:
        print("WARNING: No chain ID provided, not verifying")

    copydata(settings, blkindex, blkset)
