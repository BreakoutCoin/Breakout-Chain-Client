# Breakout Chain Client v1.9.2.0 — Release Notes

    version:           v1.9.2.0    (previous release: v1.9.1.0)
    release date:      Fri Sep 4 2026

    NO CONSENSUS CHANGE. NO PROTOCOL CHANGE. NO FORK SCHEDULE CHANGE.
    The network protocol version is unchanged at 61030, and the
    BRK_FORK008 / BRK_FORK009 / BRK_FORK010 activation scheduled for
    Sun Sep 6 02:00:00 2026 UTC (epoch 1788660000) is untouched.

This release fixes an RPC hang in `breakoutd`, and reworks the build system so that one source
tree can serve more than one platform. It is a **recommended upgrade for anyone running
`breakoutd`**, and an optional one for everyone else.

> **This release satisfies the fork.** If you are upgrading for the Sun Sep 6 activation, v1.9.2.0
> is a valid choice and carries the v1.9.1.0 fork content unchanged — see the
> [v1.9.1.0 notes](release-notes-1.9.1.0.md) for what activates, why the deadline is hard, and the
> checkpoint behaviour. Nothing in this release alters any of it.

---

## Which release do I need?

| you are | take |
|---|---|
| Running `breakoutd`, and you use `backupwallet` or any wallet RPC | **v1.9.2.0** |
| Running `breakoutd` generally | **v1.9.2.0** — the hang is reachable on any node |
| Running the Qt GUI client only | v1.9.1.0 or v1.9.2.0; the bug never affected the GUI |
| Building from source on more than one platform | **v1.9.2.0** — see *Build system* below |

---

## The fix: `backupwallet` hung, and took the RPC interface with it

**Symptom.** Calling `backupwallet` over RPC never returned. Worse, every RPC call made afterwards
hung too, and `stop` returned while leaving `ThreadRPCServer` running, so the daemon did not shut
down cleanly. Only killing the process recovered it. The Qt GUI's own console was unaffected,
which made the fault look like an RPC transport problem rather than a wallet one.

**Cause.** `AppInit2()` opened a `CWalletDB` handle to read the wallet's best block for the
`-rescan` decision, and left it at function scope. `CDB` increments
`bitdb.mapFileUseCount["wallet.dat"]` for the lifetime of the object — and in the non-Qt build
`AppInit2()` never returns, because it ends in a sleep loop. The handle was therefore held open
for the life of the process and the use count never returned to zero.

`BackupWallet()` waits for exactly that count to reach zero before it copies the file. It
therefore looped forever, and because `CRPCTable::execute()` had already taken
`LOCK2(cs_main, pwalletMain->cs_wallet)` around the call, it held both locks while it spun. Every
subsequent RPC that needs either lock blocked behind it.

**Why the GUI escaped.** In the Qt build `QT_GUI` is defined, the sleep loop is compiled out, and
`AppInit2()` returns normally — destroying the handle and releasing the count. The same source
line was harmless there and fatal in the daemon.

**Introduced in** v1.9.0.0's base (the Qt6/C++17/KawPoW modernisation), when the `-rescan` logic
gained tri-state semantics and the handle lost its enclosing block. **Present in v1.9.1.0.**

**Who was affected in practice.** The published v1.9.1.0 binaries are Qt GUI builds, which never
had the bug. The hang reached anyone running `breakoutd` built from source.

**Fixed by** scoping the handle so it is released as soon as the best block has been read, with a
comment recording why the scope is load-bearing.

Additionally, `BackupWallet()` now terminates its wait loop on shutdown (`while (!fShutdown)`
rather than `while (true)`). If anything ever holds the wallet open again, `backupwallet` will
return an error and the daemon will still shut down, instead of pinning two locks forever.

---

## Build system

None of this changes the compiled behaviour of the client. It changes how the tree builds.

**One checkout can now serve several platforms.** Object files and the LevelDB static libraries go
to a directory tagged by OS, CPU and build type — `src/obj/Darwin-x86_64`,
`src/obj/Linux-x86_64`, `src/obj/Linux-x86_64-debug` — and the Qt build does the same with
`build/universal`, `build/mac`, `build/windows`, `build/posix`. Previously a macOS build and a
Windows build sharing a tree overwrote each other's objects, and the link failed with
`file format not recognized`. `make clean` now removes only the current configuration.

If you have a tree from an earlier revision, clear the stale flat output once:

```sh
rm -f src/obj/*.o src/obj/*.P src/obj/*.d src/obj/build.h
rm -rf src/leveldb/out-static
```

**Language standards match the Qt build.** `makefile.unix` and `makefile.osx` were still pinning
`-std=c++11` while `breakout-qt.pro` had moved to C++17, and the vendored Tor sources were being
compiled with no `-std=` at all, silently following whatever the compiler defaulted to. Both
makefiles now use `-std=c++17` for C++ and `-std=gnu17` for C, and LevelDB's sub-build uses C++17.

**`breakoutd` builds from `makefile.unix` again.** `src/miner.h` declared `std::atomic` members
without including `<atomic>`; libc++ supplies it transitively and libstdc++ does not, so the
build failed on Linux. This is the same class of bug as the `<cstdint>` fix in v1.9.1.4.

**Dead makefiles removed.** `makefile.mingw`, `makefile.linux-mingw` and `makefile.bsd` had not
been touched since 2017, listed none of the Tor, ethash or Explore sources, and could not build
the tree. Windows is built by cross-compiling the Qt client with MXE; there is no Windows CLI
build. BSD support is planned and will not come back through these files.

**Clearer configuration errors.** A missing, dangling or unparseable `local-env-*.pri` used to
produce one message that named none of those three causes. Each is now reported separately, and
names the file your platform actually expects.

**One compiler warning cleared.** An unnecessary lambda capture in `src/colors.cpp`
(`-Wunused-lambda-capture` under clang). No behaviour change.

---

## Documentation

**Build documentation now exists in the repository.** `doc/readme-qt.rst` had been stale since
2017: it described Qt5, named a Debian package that no longer exists, and told you to run
`qmake && make` — which cannot work, because the project requires a gitignored `local-env-*.pri`
that nothing in the tree described. A fresh clone could not be built without asking a maintainer.

* [`doc/build-qt.md`](../build-qt.md) — the Qt GUI build, including the full `local-env-*.pri`
  contract: which variables are required, which only warn, which self-default, and what the `-`
  sentinel means.
* [`doc/build-daemon.md`](../build-daemon.md) — `breakoutd` via `makefile.unix` / `makefile.osx`.
* [`local-env.pri.example`](../../local-env.pri.example) — a template with a filled-in section per
  platform.

`doc/readme-qt.rst`, `doc/build-unix.txt`, `doc/build-osx.txt` and `doc/build-msw.txt` are removed;
the first three were superseded by the above, and the last described the MinGW/MSYS route that no
longer exists.

**Releases and tags are now distinguished.** The README states the policy: a version is a release
when it appears under Releases with published binaries and a notes document. Every other tag marks
a point in history. The v1.9.0.0 notes have been folded into the v1.9.1.0 notes, since v1.9.0.0 was
a tag that was superseded before any binaries were published for it.

---

## Full change list

**`backupwallet` RPC hang (`src/init.cpp`, `src/walletdb.cpp`).** The wallet DB handle opened for
the `-rescan` best-block read is now scoped so it is released immediately, and `BackupWallet()`
terminates its wait loop on shutdown. Daemon only. See above.

**`src/miner.h`.** Added `#include <atomic>` and `#include <mutex>`; the header declared
`std::atomic<bool>` and `std::atomic<int>` members without them.

**`src/colors.cpp`.** Removed an unnecessary lambda capture and made the captured constant
`constexpr`, which is what guarantees no capture is needed.

**`src/makefile.unix`, `src/makefile.osx`, `src/tor/tor-include.mk`.** `-std=c++17` and
`-std=gnu17`; LevelDB sub-build at C++17; per-configuration `OBJDIR` and LevelDB output
directories, overridable with `BUILD_TAG`, `OBJDIR` and `LEVELDB_OUT_DIR`; `clean` scoped to the
current configuration.

**`breakout-qt.pro`.** Per-platform `OBJECTS_DIR` / `MOC_DIR` / `UI_DIR`; the three `local-env-*.pri`
failure modes reported separately; stale MSYS+MinGW comments corrected to describe the MXE
cross-compile.

**`src/tor/adapter/orconfig-freebsd.h`.** `SRCDIR` no longer records an absolute path from the
machine where Tor's `configure` was once run. Inert — nothing in the compiled Tor sources reads
`SRCDIR` — and FreeBSD is not currently built.

**Removed.** `src/makefile.mingw`, `src/makefile.linux-mingw`, `src/makefile.bsd`,
`doc/build-msw.txt`, `doc/readme-qt.rst`, `doc/build-unix.txt`, `doc/build-osx.txt`,
`doc/release-notes/release-notes-1.9.0.0.md` (merged into the v1.9.1.0 notes).

**Added.** `doc/build-qt.md`, `doc/build-daemon.md`, `local-env.pri.example`.

**Client version.** Now **1.9.2.0**. The network protocol version is unchanged at **61030**; this
release adds no protocol behaviour and does not affect peer compatibility or the
`GetMinPeerProtoVersion()` requirement.

---

## Known issues

**Rare heap-corruption abort at shutdown (`breakoutd`).** In one run out of twenty-four, a daemon
stopped with `stop` aborted during teardown instead of exiting cleanly, with glibc reporting
`corrupted size vs. prev_size`. It was not reproducible: twenty-three further runs, including
fifteen identical start / `backupwallet` / `stop` cycles, all exited 0.

Everything before the abort behaved correctly — the RPC calls returned, and the wallet backup was
byte-identical to `wallet.dat`. The fault is at process teardown.

It has not been attributed. The most likely shape is the long-standing teardown race in this
lineage — `exit()` called from the RPC `stop` handler while detached threads are still touching
globals — which would predate this release. But it cannot be called purely pre-existing either,
because this release changes what shutdown does: with the wallet handle leak fixed,
`mapFileUseCount["wallet.dat"]` now reaches zero, so `bitdb.Flush(true)` actually closes and
flushes wallet.dat at shutdown where previously it skipped it. More teardown work is a plausible
way to widen an existing window.

If you see it, the node has already stopped; restart normally. If you can reproduce it with any
reliability, a core dump would be valuable — please report it.

---

## For maintainers

**The scope of a `CDB` is load-bearing in the daemon.** `AppInit2()` does not return in the non-Qt
build. Any `CDB` or `CWalletDB` left at its function scope is held for the life of the process, and
anything that waits on `bitdb.mapFileUseCount` — `BackupWallet()` today — will wait forever. Keep
such handles in the narrowest possible block.

**A second toolchain is what finds portability bugs.** Both header omissions fixed in the 1.9.1.x
and 1.9.2.0 series (`<cstdint>`, `<atomic>`) were invisible to the macOS build and immediate on
libstdc++. The platform table in [`doc/build-qt.md`](../build-qt.md) records what is expected to
keep compiling.

**Tag before building release binaries.** `share/genbuild.sh` derives `BUILD_DESC` from
`git describe`, so a binary built from an untagged tree reports the *previous* tag plus a commit
count — a v1.9.2.0 build made before tagging reports `v1.9.1.4-9-gdfeb880`. Only a build made from
a tagged tree reports `v1.9.2.0`. (A tree without `.git` at all falls back to `CLIENT_VERSION`,
which is why an exported source build reports correctly either way.)

**Check the composed client version after a bump.** `src/version.h` composes
`1000000*MAJOR + 10000*MINOR + 100*REVISION + BUILD`; v1.9.1.3 existed only to correct fields that
reported 1.9.1.2 as 1.9.2.1. For this release: 1, 9, 2, 0 composes to 1090200, and `getinfo`
reports `v1.9.2.0`.
