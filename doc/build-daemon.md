# Building the daemon (`breakoutd`)

`breakoutd` is the headless node and the command-line RPC client — the same
binary serves both. It is built with plain `make`, independently of the Qt GUI.

For the GUI, see [build-qt.md](build-qt.md).

> **Note.** A CMake build is planned to replace these makefiles. This document
> describes what is in the tree today.

## Supported platforms

| Platform | Makefile | Status |
|---|---|---|
| Linux | `src/makefile.unix` | Supported |
| macOS | `src/makefile.osx` | Supported |
| FreeBSD / other BSD | — | Planned |
| Windows | — | Not supported; there is no demand for a Windows CLI |

The `mingw`, `linux-mingw` and `bsd` makefiles were removed in the 1.9.1.x
series: they predated the Tor, ethash and Explore sources and could not build
the tree.

See the note in [build-qt.md](build-qt.md#supported-platforms) on why keeping
more than one toolchain compiling matters — the same missing-header class of
bug affects this binary.

## Prerequisites

- Boost — `atomic`, `chrono`, `filesystem`, `program_options`, `thread`
- Berkeley DB (C++ API)
- OpenSSL 3
- libevent
- zlib
- A C++17 compiler

Tor and LevelDB are vendored under `src/tor` and `src/leveldb` and are built as
part of the target. Tor is a large part of the build: roughly 420 of the ~485
object files.

## Configuring: `local-env-*.mk`

Like the Qt build, the makefiles hardcode no dependency locations. Each
includes a machine-specific fragment from `src/`:

| Makefile | Include |
|---|---|
| `makefile.unix` | `src/local-env-unix.mk` |
| `makefile.osx` | `src/local-env-osx.mk` |

Both are optional as far as `make` is concerned (`-include`), so a missing file
produces confusing "cannot find `-lboost_thread`" style failures rather than a
clean error. Create one before building.

Define one `*_LIB_PATH` and one `*_INCLUDE_PATH` for each dependency:

```make
BOOST_LIB_PATH=/usr/lib/x86_64-linux-gnu
BOOST_INCLUDE_PATH=/usr/include
BDB_LIB_PATH=/usr/lib/x86_64-linux-gnu
BDB_INCLUDE_PATH=/usr/include
OPENSSL_LIB_PATH=/usr/lib/x86_64-linux-gnu
OPENSSL_INCLUDE_PATH=/usr/include
EVENT_LIB_PATH=/usr/lib/x86_64-linux-gnu
EVENT_INCLUDE_PATH=/usr/include
ZLIB_LIB_PATH=/usr/lib/x86_64-linux-gnu
ZLIB_INCLUDE_PATH=/usr/include
```

On macOS these point into the Homebrew Cellar; `makefile.osx` additionally uses
`HOMEBREW_INCLUDE` (an `-I` flag, not a bare path) in its C flags.

Note the two makefiles link Berkeley DB differently: `makefile.unix` links
`$(BDB_LIB_PATH)/libdb_cxx.a` statically, `makefile.osx` links `-l db_cxx`
dynamically.

## Building

```sh
cd src
make -f makefile.unix -j"$(nproc)" breakoutd        # Linux
make -f makefile.osx  -j"$(sysctl -n hw.logicalcpu)" breakoutd   # macOS
```

Targets are `breakoutd` (default via `all`) and `clean`.

### Switches

| Switch | Default | Effect |
|---|---|---|
| `DEBUG=1` | off | `-O0` instead of `-O2`, and a separate output directory |
| `STATIC=1` | `0` (`makefile.unix` only) | link dependencies statically |
| `USE_UPNP` | `-` (off) | set to `1` to link miniupnpc |
| `USE_IPV6` | `-` (off) | set to `1` to define `USE_IPV6` |
| `PIE` | unset | adds `-fPIE` / `-pie` |
| `CXXFLAGS=…` | — | appended last, so it overrides the defaults |

Language standards are set by the makefiles and match the Qt build:
`-std=c++17` for C++, `-std=gnu17` for C (including the vendored Tor sources,
which previously took whatever the compiler defaulted to). LevelDB's own
sub-build is invoked with `-std=c++17` as well.

## Where the output goes

Objects and the LevelDB static libraries are written to a directory tagged by
OS, CPU and build type, so one checkout can serve several configurations:

```
src/obj/Linux-x86_64/          src/leveldb/out-Linux-x86_64/
src/obj/Darwin-x86_64/         src/leveldb/out-Darwin-x86_64/
src/obj/Darwin-arm64-debug/    src/leveldb/out-Darwin-arm64-debug/
```

The tag is `$(uname -s)-$(uname -m)`, plus `-debug` when `DEBUG=1`. Override it
wholesale with `BUILD_TAG=`, or set `OBJDIR=` / `LEVELDB_OUT_DIR=` directly —
the make-side equivalent of `cmake -B <dir>`:

```sh
make -f makefile.unix OBJDIR=obj/experiment breakoutd
```

`make clean` removes only the current configuration's directory, so cleaning a
Linux build leaves a macOS one intact.

Before the directories were tagged, two platforms sharing a checkout would
overwrite each other's objects, and the link failed with
`file format not recognized`. If you have a tree from an older revision, clear
the stale flat output once:

```sh
rm -f src/obj/*.o src/obj/*.P src/obj/*.d src/obj/build.h
rm -rf src/leveldb/out-static
```

## Running

`breakoutd` reads `breakout.conf` from the data directory. A minimal
configuration for a node that also answers RPC:

```
rpcuser=<user>
rpcpassword=<a long random password>
server=1
daemon=1
defaultcurrency=BRX
defaultstake=BRX
```

`defaultcurrency` and `defaultstake` are required; without them the daemon
exits immediately with `Please assign ticker for -defaultcurrency=<ticker>`.

The same binary is the RPC client:

```sh
./breakoutd getinfo
./breakoutd -datadir=/path/to/data getblockcount
./breakoutd stop
```

The built-in Tor is enabled by default and startup blocks until it
initialises. Pass `-onlynet=ipv4` to skip it when you do not need onion
routing — useful for tests and for machines without outbound network access.
