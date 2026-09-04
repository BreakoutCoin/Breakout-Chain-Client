# Building the Qt GUI client

This document covers building `Breakout-Chain` (the Qt GUI) from a source
checkout. It does not cover producing a release: code signing, notarization,
installer packaging and artifact delivery are maintained separately and are
deliberately not in this repository.

For the headless daemon and RPC client, see [build-daemon.md](build-daemon.md).

## Supported platforms

Portability bugs in this codebase are usually invisible until a *second*
toolchain compiles the tree — libc++ supplies headers transitively that
libstdc++ does not, so a macOS-only build will not notice a missing
`#include`. Two such bugs surfaced in the 1.9.1.x series alone (a missing
`<cstdint>` in `src/explore/ExploreConstants.hpp`, a missing `<atomic>` in
`src/miner.h`). The point of the table below is that these are the
configurations expected to keep compiling; breaking one is a regression.

| Platform | Toolchain | Status |
|---|---|---|
| macOS (universal: x86_64 + arm64) | Apple clang, Homebrew dependencies | Supported, released |
| Windows x64 | MXE cross-compile from macOS (mingw-w64, static) | Supported, released |
| Linux | system Qt6 + distribution packages | `.pro` branch exists and configures, but is not currently built or deployed |
| FreeBSD / other BSD | — | Not currently supported |

The daemon's platform support differs; see [build-daemon.md](build-daemon.md).

## Prerequisites

Qt 6 (`core`, `gui`, `widgets`, `printsupport`), plus:

- Boost
- Berkeley DB (C++ API)
- OpenSSL 3
- libevent
- zlib
- libqrencode — required in practice, since `USE_QRCODE` is defined
  unconditionally

Tor and LevelDB are vendored in `src/tor` and `src/leveldb` and are built as
part of the project; do not install them separately.

`lrelease` (from Qt's tools) compiles the `.ts` translations. On macOS and
Linux the host Qt's copy is used automatically. Cross-compiling for Windows it
must be named explicitly — see below.

## Configuring: the `local-env-*.pri` contract

`breakout-qt.pro` hardcodes no dependency location. It expects a
machine-specific include file, selected by platform:

| Platform | File |
|---|---|
| macOS | `local-env-osx.pri` |
| Windows | `local-env-win.pri` |
| Linux | `local-env-linux.pri` |

**These are gitignored** (`.gitignore` carries `*local-env-*`, which covers
every variant), so a fresh clone has none and will not configure until you
create one. Start from [`local-env.pri.example`](../local-env.pri.example) in
the repository root, which carries a filled-in section for each platform.

Keeping one file per machine and symlinking it is a convenient arrangement:

```sh
ln -sfn myhost-local-env-osx.pri local-env-osx.pri
ls -lL local-env-osx.pri     # -L, so a dangling link fails rather than passes
```

### What the project requires

| Variable | If unset | Platforms |
|---|---|---|
| `BOOST_LIB_PATH` | **error** | all |
| `SYSTEM_INCLUDE_PATH`, `SYSTEM_LIB_PATH` | **error** | Linux only |
| `MXE_ROOT` | **error** | Windows only |
| `BOOST_INCLUDE_PATH`, `BOOST_LIB_SUFFIX` | warning | all |
| `BDB_LIB_PATH`, `BDB_INCLUDE_PATH`, `BDB_LIB_SUFFIX` | warning | all |
| `OPENSSL_LIB_PATH`, `OPENSSL_INCLUDE_PATH` | warning | all |
| `EVENT_LIB_PATH`, `EVENT_INCLUDE_PATH` | warning | all |
| `ZLIB_LIB_PATH`, `ZLIB_INCLUDE_PATH` | warning | all |
| `QRENCODE_LIB_PATH`, `QRENCODE_INCLUDE_PATH` | no check | all (needed in practice) |
| `BOOST_THREAD_LIB_SUFFIX` | defaults to `BOOST_LIB_SUFFIX` | all |
| `QMAKE_LRELEASE` | defaults to host Qt's `lrelease`; on Windows, derived from `MXE_ROOT` | all |
| `QM_DIR` | defaults to `src/qt/locale` | all |

A warning here usually becomes a link error later, so fill them all in.

`BOOST_LIB_PATH` is the designated gate: it is checked with `error()` while its
siblings are only warnings. The three ways the include can fail are reported
separately, because they have different fixes:

| Message | Cause |
|---|---|
| `local-env-<plat>.pri was not found. …` | No such file — or a symlink pointing at nothing. `exists()` is false for a dangling link, so `ls -l` will show the link and tell you nothing; use `ls -lL`. |
| `local-env-<plat>.pri was found but could not be parsed. …` | qmake syntax error in the file. |
| `local-env-<plat>.pri was loaded but does not set BOOST_LIB_PATH. …` | The file loaded; the variable is genuinely missing. |

Each names the file your platform actually expects rather than the pattern, so
there is no ambiguity about which of the three to create.

### The `-` sentinel

Several variables accept `-` to mean "not applicable here, add nothing to the
flags". This is not the same as leaving them unset, which trips the `isEmpty()`
check. Use `-` to record that the emptiness is deliberate: `BDB_LIB_SUFFIX = -`
when the library has no suffix, `SYSTEM_LIB_PATH = -` when the compiler already
finds everything.

## Building

```sh
qmake breakout-qt.pro
make
```

Cross-compiling for Windows, invoke the MXE Qt6 qmake instead and name the
mkspec:

```sh
"$MXE_ROOT/usr/bin/x86_64-w64-mingw32.static-qt6-qmake" \
    breakout-qt.pro -spec win32-g++ CONFIG+=release
```

Cross-compiling, `$$[QT_INSTALL_BINS]` names the *target* Qt, whose `lrelease`
is a Windows `.exe` that cannot run on the build host. Set `QMAKE_LRELEASE` in
`local-env-win.pri`, or leave it unset and let the project locate the host
build by globbing `$MXE_ROOT/usr/*/qt6/bin/lrelease` — the extensionless name
is what distinguishes it from the target's `lrelease.exe`.

## Where the output goes

Intermediate output is written to a directory tagged by platform, so one
checkout can serve more than one target without the two overwriting each
other:

| Configuration | `OBJECTS_DIR` / `MOC_DIR` / `UI_DIR` | LevelDB |
|---|---|---|
| macOS universal | `build/universal` | `src/leveldb/out-universal` |
| macOS single-arch | `build/mac` | `src/leveldb/out-mac` |
| Windows | `build/windows` | `src/leveldb/out-windows` |
| Linux / BSD | `build/posix` | `src/leveldb/out-static` |

**qmake still writes `Makefile` at the top level, and that is not tagged.** Two
platforms can therefore coexist in one checkout, but configuring both at once
needs `qmake -o <dir>/Makefile` or a second worktree. Before the directories
were tagged, running a Windows `make` in a tree last used for macOS found
several hundred Mach-O objects, judged them up to date, and tried to link them
into a PE; the symptom was `file format not recognized` at link time.

## Troubleshooting

**`… was not found` / `… could not be parsed` / `… does not set BOOST_LIB_PATH`**
— the three local-env failures, described in the table above.

**`Project ERROR: MXE_ROOT is not set`** — `local-env-win.pri` was found but
does not define `MXE_ROOT`, which the LevelDB cross-build and the `lrelease`
lookup both need.

**`Project ERROR: SYSTEM_INCLUDE_PATH is not set`** (Linux) — set it to the
multiarch directory, or to `-` if the compiler already finds system headers.

**`file format not recognized` at link time** — object files from another
platform. This should no longer happen now that the directories are tagged;
if it does, clear the stale flat `build/` directory left by an older tree.

**LevelDB compiles POSIX sources when cross-compiling for Windows** — a stale
`src/leveldb/build_config.mk` caches the detected platform. Remove it and the
LevelDB output directory, then re-run qmake.

**Missing fixed-width integer types** (`'int64_t' was not declared`) or a
missing `std::atomic` — a header relies on a transitive include that libc++
provides and libstdc++ does not. Add the explicit `#include <cstdint>` or
`#include <atomic>` to the header named in the error and commit it; this is a
real portability bug, not a toolchain problem.
