# Change log

A curated history of what changed in each Breakout Chain release, indexed by
**network protocol version**.

The protocol version is the axis that matters most here: it is precisely what
determines whether two clients can talk to each other, so it is the most
fundamental way one release can differ from another. Releases that share a
protocol version are peer-compatible; a new protocol version is where
compatibility breaks.

This file is written by hand. It is a curated list of what mattered in each
release, not a log of commits, and it should stay that way.

Related:

* The version itself lives in `src/clientversion.h`, as four macros. Every
  other consumer -- `src/version.h`, the Windows PE version resource, qmake,
  the macOS bundle property list -- derives it from there.
* `PROTOCOL_VERSION` is defined in `src/version.h`.
* `doc/release-notes/` holds the detailed, per-release notes for recent
  releases. This file is the compact overview; those are the full account.

---

## Protocol version 61030

### 1.9.2.1

* No change in behavior; build system only. No consensus rule, network protocol
  requirement or fork schedule changes.
* The version number is now written in exactly one place, `src/clientversion.h`.
  `breakout-qt.pro`, the Windows PE version resource and the macOS bundle property
  list all derive it from there, and the property list is generated and installed
  by the build rather than copied over the qmake placeholder by hand. The v1.9.2.0
  bump had to touch four places and missed one.
* This change log was moved out of `src/version.h`, where it had grown to 107 lines
  of comment around 39 lines of code.
* `genbuild.sh` now writes `build.h` to the per-configuration output directory.
  Moving the object directories per-platform in 1.9.2.0 put `-Ibuild/<tag>` on the
  include path while `build.h` was still written to `build/`, so `src/version.cpp`
  could not find it. Windows was unaffected -- the enclosing guard skips the block
  there -- and 1.9.2.0 shipped Windows-only, which is why a broken non-Windows
  build survived a release.

### 1.9.2.0

* Fixes an RPC hang: a `wallet.dat` handle held open for the life of the daemon
  made `backupwallet` spin forever under `cs_main`/`cs_wallet`, wedging every
  subsequent RPC call. Daemon only; the Qt client was never affected. Build
  system reworked: C++17/gnu17 across the makefiles, per-configuration object
  directories, dead makefiles removed, build documentation added. No consensus
  rule, network protocol requirement or fork schedule changes.
  ([release notes](doc/release-notes/release-notes-1.9.2.0.md))

### 1.9.1.4

* No change in behavior; build system only. Windows cross-build fixed
  (`ExploreConstants.hpp` needed an explicit `<cstdint>`, which libstdc++ does
  not supply transitively), `DEPS_DIR` removed, and the MXE host `lrelease` is
  now located rather than hardcoded.

### 1.9.1.3

* Correct `CLIENT_VERSION` fields: 1.9.1.2 reported as 1.9.2.1

### 1.9.1.2

* Restore legacy background color of key panels in the GUI

### 1.9.1.1

* Using "Pending" instead of "Unconfirmed" in GUI

### 1.9.1.0

* Explore API: track per-card provenance (mint + transfer history)
  ([release notes](doc/release-notes/release-notes-1.9.1.0.md))

### 1.9.0.0

* BRK_FORK008: PoS kernel hardening (ceiling on weighted kernel target)
* BRK_FORK009: KawPoW mix-hash verification against the DAG
* BRK_FORK010: KawPoW version-dispatch enforcement (block versioning)

## Protocol version 61020

### 1.8.0.0

* Most of the Explore API

### 1.7.1.0

* removed txid from sighash
* tor update to v0.4.8.1-alpha
* moved libcrypto++ to an external dependency
* updated leveldb to 1.23
* balances tracked for main wallet to save cpus
* mining algo

## Protocol version 61014

### 1.6.3.0

* removed sync checkpoints

## Protocol version 61013

### 1.6.2.2

* gui for private keys
* better unlock/lock behavior

### 1.6.2.1

* adding `getprivatekeys` RPC

### 1.6.2.0

* reduce max pos reward for block time reduction

## Protocol version 61012

### 1.6.1.0

* SIS mining fix
* shorter block times (5 min -> 1 min)
* watch addresses

## Protocol version 61011

### 1.5.1.1

* change in `validateaddress` behavior

### 1.5.1.0

* card staking fix

## Protocol version 61010

### 1.5.0.0

* card staking rewards nonzero
* hard-coded clearnet nodes
* code cleanup & parameter consolidation
* multisig api
* newest leveldb (v1.2)
* tor v0.3: 0.3.0.9
* pool friendly mining
* PoW is now scrypt (was sha256d)

## Protocol version 61009

### 1.4.5.0

* card staking and BRK inflation correction
* `createmultisigaddress` (1.4.6.0)

## Protocol version 61008

### 1.4.2.0

* staking improvement

## Protocol version 61007

### 1.4.0.0

* added burn protocol

## Protocol version 61006

* finalized deck PoS reward

## Protocol version 61005

* mainnet launch

## Protocol version 61002

* original release version
