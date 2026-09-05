#######################################################################
##  Target
#######################################################################

TEMPLATE = app
TARGET = "Breakout-Chain"

macx {
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 11
    QMAKE_APPLE_DEVICE_ARCHS = x86_64 arm64
}

# Testnet (true) or mainnet build (unset or any thing but true)
TESTNET = false


#######################################################################
##  Version
#######################################################################
# src/clientversion.h is the single place the version is written.  Every
# other consumer derives it from there: src/version.h composes
# CLIENT_VERSION, src/qt/res/bitcoin-qt.rc stringizes the macros into the
# Windows PE version resource, and the block below reads them for qmake
# and for the macOS property list.
#
# This is pure qmake on purpose.  A $$system(sed ...) form was tried first
# and does not work: qmake's tokenizer consumes the quotes before the shell
# sees them, and it fails with "Missing closing ' quote".
#
# Note that the version a *built* binary reports is a separate matter.
# share/genbuild.sh sets BUILD_DESC from `git describe --dirty`, so inside
# a git tree the reported version comes from the tag, not from this file.
# Tag before building release artifacts.
#######################################################################

CV_LINES = $$cat($$PWD/src/clientversion.h, lines)
for(line, CV_LINES) {
    tokens = $$split(line, " ")
    key = $$member(tokens, 1)
    equals(key, CLIENT_VERSION_MAJOR)    { VER_MAJOR = $$last(tokens) }
    equals(key, CLIENT_VERSION_MINOR)    { VER_MINOR = $$last(tokens) }
    equals(key, CLIENT_VERSION_REVISION) { VER_REV   = $$last(tokens) }
    equals(key, CLIENT_VERSION_BUILD)    { VER_BUILD = $$last(tokens) }
}

# An unreadable header must fail the build loudly rather than quietly
# produce a VERSION of "...".
isEmpty(VER_MAJOR)|isEmpty(VER_MINOR)|isEmpty(VER_REV)|isEmpty(VER_BUILD) {
    error("could not read the client version from src/clientversion.h")
}

VERSION = $${VER_MAJOR}.$${VER_MINOR}.$${VER_REV}.$${VER_BUILD}


#######################################################################
##  Network Configuration
#######################################################################

equals(TESTNET, true) {
    DEFINES += TESTNET_BUILD=1
    message("ATTENTION: Building for testnet.")
} else {
    DEFINES += TESTNET_BUILD=0
}


#######################################################################
##  Directory Locations
#######################################################################

win32 {
    message("x86_64 64-bit build (MXE cross-compile)")
}

INCLUDEPATH += src src/json src/qt src/explore src/bip32
INCLUDEPATH += src/ethash src/ethash/include src/ethash/lib


#######################################################################
# qmake's intermediate output is written to a platform-specific directory,
# for the same reason LevelDB's is (see the out-* comment further down):
# one source tree has to serve builds for more than one platform without
# them stomping on each other.
#
#   build/universal  - macOS universal (x86_64 + arm64)
#   build/mac        - macOS single-arch
#   build/windows    - Windows (MXE cross-compile)
#   build/posix      - Linux, FreeBSD, and other POSIX hosts
#
# Sharing one directory used to mean that running a Windows gmake in a tree
# last used for macOS found several hundred Mach-O objects, judged them up
# to date, and tried to link them into a PE. The tags are chosen to match
# the LevelDB suffixes so the two schemes read alike.
#
# Note that qmake still writes Makefile itself at the top level, which is
# NOT tagged; use `qmake -o <dir>/Makefile` if two platforms need to be
# configured simultaneously rather than merely to coexist.
#######################################################################
macx {
    contains(QMAKE_APPLE_DEVICE_ARCHS, arm64):contains(QMAKE_APPLE_DEVICE_ARCHS, x86_64) {
        BUILD_TAG = universal
    } else {
        BUILD_TAG = mac
    }
} else:win32 {
    BUILD_TAG = windows
} else {
    BUILD_TAG = posix
}

OBJECTS_DIR = build/$$BUILD_TAG
MOC_DIR = build/$$BUILD_TAG
UI_DIR = build/$$BUILD_TAG


#######################################################################
##  macOS Bundle Property List
#######################################################################
# The bundle's Info.plist is generated from contrib/macdeploy/Info.plist.in
# with the derived VERSION substituted in, and installed by the build.  It
# used to be copied over the qmake-generated placeholder by hand after the
# build, which both carried its own hardcoded version and was easy to skip
# -- skipping it left the bundle identifier as "Big.Breakout-Chain", which
# breaks signing and notarization.
#
# The generated file goes to the platform build directory rather than next
# to the template, so that no generated file lands in the source tree and
# so that it cannot collide with the placeholder qmake writes to OUT_PWD.
#
# The substitution is done with sed and NOT with QMAKE_SUBSTITUTES.  That
# engine lexes its input as qmake source, so it mangles XML: it strips the
# double quotes from <?xml version="1.0" ...?> and silently deletes any line
# containing an apostrophe.  Only comments carry apostrophes today and qmake
# re-serialises the plist on the way into the bundle, so it was not visibly
# broken -- but a <string> value with an apostrophe would be dropped along
# with its key, and the only warning is a "Missing closing ' quote" line
# that reads as noise.  That is the same trap that rules out $$system(sed
# ...) for reading clientversion.h above; here it is reached through the
# file being substituted rather than through the command.
#
# Generation stays at qmake time, as it was under QMAKE_SUBSTITUTES, because
# qmake itself consumes QMAKE_INFO_PLIST.  A QMAKE_EXTRA_TARGETS rule -- the
# right shape for build.h, which only the compiler reads -- would not exist
# yet on the first qmake run in a clean tree, and qmake would quietly install
# its own placeholder instead.
#
# Paths go through shell_quote so a tree whose path contains a space still
# works.  The quotes it emits are produced when the line is evaluated, not
# written in this file, so they never reach qmake's lexer.
#######################################################################

macx {
    PLIST_DIR = $$OUT_PWD/build/$$BUILD_TAG
    PLIST_IN  = $$PWD/contrib/macdeploy/Info.plist.in
    PLIST_OUT = $$PLIST_DIR/Info.plist

    !exists($$PLIST_IN) {
        error("missing $$PLIST_IN, needed to generate the bundle Info.plist")
    }

    # mkdir because qmake creates OBJECTS_DIR only at generate time, which is
    # after this file is parsed -- so on a clean tree the directory does not
    # exist yet.  The genbuild rule needs the same mkdir for the same reason.
    Q_DIR = $$shell_quote($$PLIST_DIR)
    Q_IN  = $$shell_quote($$PLIST_IN)
    Q_OUT = $$shell_quote($$PLIST_OUT)

    !system(mkdir -p $$Q_DIR && sed -e s/@VERSION@/$$VERSION/g $$Q_IN > $$Q_OUT) {
        error("could not generate $$PLIST_OUT from the Info.plist template")
    }

    QMAKE_INFO_PLIST = $$PLIST_OUT
}


#######################################################################
##  Qt Modules
#######################################################################

QT += core gui widgets printsupport


#######################################################################
##  Environment Specific Dependencies
#######################################################################
# Inside local-env-*.pri, users are encouraged to set the
#   following environment specific variables to ensure
#   compatibility with the build:
#     - BOOST_LIB_SUFFIX
#     - BOOST_LIB_PATH
#     - BOOST_INCLUDE_PATH
#     - BDB_LIB_SUFFIX (Berkeley DB)
#     - BDB_LIB_PATH
#     - BDB_INCLUDE_PATH
#     - OPENSSL_LIB_PATH
#     - OPENSSL_INCLUDE_PATH
#     - EVENT_LIB_PATH (Libevent 2)
#     - EVENT_INCLUDE_PATH
#     - ZLIB_LIB_PATH (https://www.zlib.net/)
#     - ZLIB_LIB_PATH (https://www.zlib.net/)
#     - ZLIB_INCLUDE_PATH (https://www.zlib.net/)
# Default values are used if not overridden elsewhere.
# MacOS is given default values for homebrew, because MacOS has
#    no platform supported package manager.
# Windows is cross-compiled with MXE (mingw-w64 static toolchain),
#    which supplies every dependency itself; see local-env-win.pri.
# Linux distributions generally have package managers
#    that install libraries to /usr/local.
#######################################################################
# Name the file this platform expects up front, so the diagnostics below can
# say which one is missing rather than listing all three.
macx {
    LOCAL_ENV_PRI = local-env-osx.pri
} else:win32 {
    LOCAL_ENV_PRI = local-env-win.pri
} else {
    LOCAL_ENV_PRI = local-env-linux.pri
}

exists($$LOCAL_ENV_PRI) {
    # include() returns false when the file is there but does not parse.
    # Without this test a syntax error looks exactly like a missing file:
    # both leave every variable unset, and the only symptom is the
    # BOOST_LIB_PATH gate below firing on a file you are looking straight at.
    !include($$LOCAL_ENV_PRI) {
        error("$$LOCAL_ENV_PRI was found but could not be parsed. Check it for qmake syntax errors; see doc/build-qt.md.")
    }
    LOCAL_ENV_LOADED = 1
}


#######################################################################
##  Environment Specific Defaults
#######################################################################

# Hard gate: if the local-env include did not load, nothing below works.
# BOOST_LIB_PATH is the variable tested because every local-env-*.pri sets it
# and the build genuinely needs it. The two cases are reported separately --
# "the file never loaded" and "the file loaded but is incomplete" have
# different fixes, and a single message covering both sends people looking in
# the wrong place. A file that exists but does not parse is caught earlier,
# at the include() above.
isEmpty(BOOST_LIB_PATH) {
    isEmpty(LOCAL_ENV_LOADED) {
        error("$$LOCAL_ENV_PRI was not found. Copy the matching section of local-env.pri.example to $$LOCAL_ENV_PRI, or, if it is a symlink, check that it is not dangling with: ls -lL $$LOCAL_ENV_PRI  (see doc/build-qt.md)")
    }
    error("$$LOCAL_ENV_PRI was loaded but does not set BOOST_LIB_PATH. See doc/build-qt.md for every variable it has to define.")
}

equals(BOOST_LIB_SUFFIX, "-") {
    BOOST_LIB_SUFFIX = ""
} else:isEmpty(BOOST_LIB_SUFFIX) {
    warning("BOOST_LIB_SUFFIX is not set")
}

isEmpty(BOOST_THREAD_LIB_SUFFIX) {
    BOOST_THREAD_LIB_SUFFIX = $$BOOST_LIB_SUFFIX
}

isEmpty(BOOST_INCLUDE_PATH) {
    warning("BOOST_INCLUDE_PATH is not set")
}

# BDB 4.8 is ancient, so most package managers will install a
#    newer version by default.
equals(BDB_LIB_SUFFIX, "-") {
    BDB_LIB_SUFFIX = ""
} else:isEmpty(BDB_LIB_SUFFIX) {
    warning("BDB_LIB_SUFFIX is not set")
}

isEmpty(BDB_LIB_PATH) {
    warning("BDB_LIB_PATH is not set")
}

isEmpty(BDB_INCLUDE_PATH) {
    warning("BDB_INCLUDE_PATH is not set")
}

isEmpty(OPENSSL_LIB_PATH) {
    warning("OPENSSL_LIB_PATH is not set")
}

isEmpty(OPENSSL_INCLUDE_PATH) {
    warning("OPENSSL_INCLUDE_PATH is not set")
}

isEmpty(EVENT_LIB_PATH) {
    warning("EVENT_LIB_PATH is not set")
}

isEmpty(EVENT_INCLUDE_PATH) {
    warning("EVENT_INCLUDE_PATH is not set")
}

isEmpty(ZLIB_LIB_PATH) {
    warning("ZLIB_LIB_PATH is not set")
}

isEmpty(ZLIB_INCLUDE_PATH) {
    warning("ZLIB_INCLUDE_PATH is not set")
}


#######################################################################
##  Defines
#######################################################################

DEFINES += QT_GUI BOOST_THREAD_USE_LIB BOOST_SPIRIT_THREADSAFE \
           BOOST_THREAD_PROVIDES_GENERIC_SHARED_MUTEX_ON_WIN \
           __NO_SYSTEM_INCLUDES

macx {
   DEFINES += MAC_OSX
   QMAKE_MOC_OPTIONS += -DQ_OS_MAC
}

!win32:!macx {
    DEFINES += LINUX
}

win32 {
    DEFINES += WIN32
}


#######################################################################
##  Compile flags
#######################################################################

CONFIG += no_include_pwd
CONFIG += thread

!macx:!win32 {
    CONFIG += static
}

CONFIG += c17
CONFIG -= c99 c18

CONFIG += c++17
CONFIG -= c++11 c++14 c++1z

macx {
    QMAKE_CXXFLAGS += -mmacosx-version-min=$$QMAKE_MACOSX_DEPLOYMENT_TARGET
}

QMAKE_CXXFLAGS_WARN_ON = -fdiagnostics-show-option -Wall -Wextra -Wformat \
                         -Wformat-security -Wno-unused-parameter -Wstack-protector

!win32 {
    # for extra security against potential buffer overflows
    # do not enable this on windows, as it will result in a non-working executable!
    QMAKE_CXXFLAGS += -fstack-protector
    QMAKE_LFLAGS += -fstack-protector
}


#######################################################################
## Environment Specific Include Paths
#######################################################################
INCLUDEPATH += $$BOOST_INCLUDE_PATH $$BDB_INCLUDE_PATH \
               $$OPENSSL_INCLUDE_PATH $$EVENT_INCLUDE_PATH \
               $$QRENCODE_INCLUDE_PATH


#######################################################################
## Package Manager Include Paths
#######################################################################
# Assumed package managers are:
# - MacOS: homebrew
# - Debian: apt
# - Windows: MXE (cross-built dependencies, no host package manager)


#######################################################################
## System Include Paths
#######################################################################

## Linux
!macx:!win32 {
    isEmpty(SYSTEM_INCLUDE_PATH) {
        error("SYSTEM_INCLUDE_PATH is not set")
    }
    !equals(SYSTEM_INCLUDE_PATH, "-") {
        # Debian: usually /usr/include/x86_64-linux-gnu
        INCLUDEPATH += $$SYSTEM_INCLUDE_PATH
    }
}

## Windows
win32 {
    # MXE provides all headers under MXE_PREFIX/include, which the
    # *_INCLUDE_PATH variables already cover; no separate system include
}



#######################################################################
##  Qr Code Support
#######################################################################
# libqrencode (http://fukuchi.org/works/qrencode/index.en.html)
#    must be installed for support
message("Building with QRCode support")
DEFINES += USE_QRCODE



#######################################################################
##  DBUS Notifications
#######################################################################
# use: qmake "USE_DBUS=1"
contains(USE_DBUS, 1) {
    message("Building with DBUS (Freedesktop notifications) support")
    DEFINES += USE_DBUS
    QT += dbus
}


#######################################################################
##  First Class Messaging
#######################################################################
# use: qmake "FIRST_CLASS_MESSAGING=1"
contains(FIRST_CLASS_MESSAGING, 1) {
    message("Building with first-class messaging")
    DEFINES += FIRST_CLASS_MESSAGING
}


#######################################################################
##  LevelDB
#######################################################################
# LevelDB is built by qmake into a platform-specific output directory so
# that a single source tree can serve simultaneous builds without the
# directories stomping on each other:
#
#   src/leveldb/out-universal  – macOS universal (x86_64 + arm64 lipo'd)
#   src/leveldb/out-mac        – macOS single-arch
#   src/leveldb/out-windows    – Windows (MXE cross-compile)
#   src/leveldb/out-static     – Linux, FreeBSD, and other POSIX hosts
#
# The reference-client Makefile continues to use out-static on all
# non-Mac platforms, so the Qt build on Linux/BSD writes to the same
# directory; that is intentional and matches prior behaviour.
#######################################################################
DEFINES += USE_LEVELDB
INCLUDEPATH += $$PWD/src/leveldb/include $$PWD/src/leveldb/helpers
INCLUDEPATH += $$PWD/src/leveldb/include/leveldb $$PWD/src/leveldb/helpers/memenv

macx {
    # ------------------------------------------------------------------ #
    #  macOS – build LevelDB via qmake extra target.
    #  QMAKE_MACOSX_DEPLOYMENT_TARGET is forwarded so the resulting
    #  library matches the SDK target of the rest of the Qt application,
    #  which suppresses spurious linker warnings.
    # ------------------------------------------------------------------ #

    contains(QMAKE_APPLE_DEVICE_ARCHS, arm64):contains(QMAKE_APPLE_DEVICE_ARCHS, x86_64) {
        # ---- Universal (arm64 + x86_64): build each slice then lipo ---- #
        LEVELDB_OUT_DIR = $$PWD/src/leveldb/out-universal
        LEVELDB_OUT_X86 = $$PWD/src/leveldb/out-universal-x86_64
        LEVELDB_OUT_ARM = $$PWD/src/leveldb/out-universal-arm64

        LEVELDB_OPT_BASE = $$QMAKE_CXXFLAGS_RELEASE \
                           -mmacosx-version-min=$$QMAKE_MACOSX_DEPLOYMENT_TARGET \
                           -isysroot $$ISYSROOT_SDK

        genleveldb.target  = $$LEVELDB_OUT_DIR/libleveldb.a
        genleveldb.depends = FORCE

        genleveldb.commands = \
            cd $$PWD/src/leveldb && \
            CC=\"$$QMAKE_CC\" CXX=\"$$QMAKE_CXX\" \
            MACOSX_DEPLOYMENT_TARGET=$$QMAKE_MACOSX_DEPLOYMENT_TARGET \
            $(MAKE) OPT=\"$$LEVELDB_OPT_BASE -arch x86_64\" \
                OUT_DIR=out-universal-x86_64 staticlibs && \
            CC=\"$$QMAKE_CC\" CXX=\"$$QMAKE_CXX\" \
            MACOSX_DEPLOYMENT_TARGET=$$QMAKE_MACOSX_DEPLOYMENT_TARGET \
            $(MAKE) OPT=\"$$LEVELDB_OPT_BASE -arch arm64\" \
                OUT_DIR=out-universal-arm64 staticlibs && \
            mkdir -p $$LEVELDB_OUT_DIR && \
            lipo -create $$LEVELDB_OUT_X86/libleveldb.a \
                         $$LEVELDB_OUT_ARM/libleveldb.a \
                         -output $$LEVELDB_OUT_DIR/libleveldb.a && \
            lipo -create $$LEVELDB_OUT_X86/libmemenv.a \
                         $$LEVELDB_OUT_ARM/libmemenv.a \
                         -output $$LEVELDB_OUT_DIR/libmemenv.a

        cleanleveldb.target   = cleanleveldb
        cleanleveldb.commands = rm -rf $$LEVELDB_OUT_DIR \
                                       $$LEVELDB_OUT_X86 \
                                       $$LEVELDB_OUT_ARM
    } else {
        # ---- Single-arch macOS build ------------------------------------ #
        LEVELDB_OUT_DIR  = $$PWD/src/leveldb/out-mac
        LEVELDB_ARCH     = $$first(QMAKE_APPLE_DEVICE_ARCHS)

        genleveldb.target  = $$LEVELDB_OUT_DIR/libleveldb.a
        genleveldb.depends = FORCE

        genleveldb.commands = \
            cd $$PWD/src/leveldb && \
            CC=\"$$QMAKE_CC\" CXX=\"$$QMAKE_CXX\" \
            MACOSX_DEPLOYMENT_TARGET=$$QMAKE_MACOSX_DEPLOYMENT_TARGET \
            $(MAKE) OPT=\"$$QMAKE_CXXFLAGS $$QMAKE_CXXFLAGS_RELEASE \
                        -arch $$LEVELDB_ARCH -isysroot $$ISYSROOT_SDK\" \
                OUT_DIR=out-mac staticlibs

        cleanleveldb.target   = cleanleveldb
        cleanleveldb.commands = rm -rf $$LEVELDB_OUT_DIR
    }

    LIBS += $$LEVELDB_OUT_DIR/libleveldb.a $$LEVELDB_OUT_DIR/libmemenv.a

    PRE_TARGETDEPS      += $$LEVELDB_OUT_DIR/libleveldb.a
    QMAKE_EXTRA_TARGETS += genleveldb
    QMAKE_EXTRA_TARGETS += cleanleveldb
    clean.depends       += cleanleveldb
    QMAKE_EXTRA_TARGETS += clean
    QMAKE_CLEAN         += $$PWD/src/leveldb/build_config.mk

} else:win32 {
    # ------------------------------------------------------------------ #
    #  Windows – cross-compiled with MXE (mingw-w64 static toolchain).
    #  local-env-win.pri must export MXE_ROOT, e.g.:
    #    MXE_ROOT = /home/user/mxe
    #  The CROSS variable is derived from that here so it never needs to
    #  be set manually in the .pri file.
    # ------------------------------------------------------------------ #
    isEmpty(MXE_ROOT) {
        error("MXE_ROOT is not set – add it to local-env-win.pri")
    }

    LEVELDB_OUT_DIR  = $$PWD/src/leveldb/out-windows
    MXE_CROSS        = $$MXE_ROOT/usr/bin/x86_64-w64-mingw32.static

    genleveldb.target  = $$LEVELDB_OUT_DIR/libleveldb.a
    genleveldb.depends = FORCE

    # LevelDB's Makefile does not auto-detect Windows when cross-compiling,
    # so TARGET_OS=OS_WINDOWS_CROSSCOMPILE must be set explicitly.
    # AR is also overridden so the correct archiver is used for the target.
    genleveldb.commands = \
        cd $$PWD/src/leveldb && \
        CC=\"$$MXE_CROSS-gcc\" \
        CXX=\"$$MXE_CROSS-g++\" \
        AR=\"$$MXE_CROSS-ar\" \
        TARGET_OS=OS_WINDOWS_CROSSCOMPILE \
        $(MAKE) OUT_DIR=out-windows staticlibs && \
        mkdir -p $$LEVELDB_OUT_DIR

    cleanleveldb.target   = cleanleveldb
    cleanleveldb.commands = rm -rf $$LEVELDB_OUT_DIR

    LIBS += $$LEVELDB_OUT_DIR/libleveldb.a $$LEVELDB_OUT_DIR/libmemenv.a

    PRE_TARGETDEPS      += $$LEVELDB_OUT_DIR/libleveldb.a
    QMAKE_EXTRA_TARGETS += genleveldb
    QMAKE_EXTRA_TARGETS += cleanleveldb
    clean.depends       += cleanleveldb
    QMAKE_EXTRA_TARGETS += clean
    QMAKE_CLEAN         += $$PWD/src/leveldb/build_config.mk

} else {
    # ------------------------------------------------------------------ #
    #  Linux, FreeBSD, and other POSIX hosts – native build.
    #  LevelDB's Makefile detects the host OS via uname, so no TARGET_OS
    #  override is needed.  The output directory matches the one used by
    #  the reference-client Makefile (out-static), keeping both build
    #  systems compatible without an extra copy step.
    # ------------------------------------------------------------------ #
    LEVELDB_OUT_DIR = $$PWD/src/leveldb/out-static

    genleveldb.target  = $$LEVELDB_OUT_DIR/libleveldb.a
    genleveldb.depends = FORCE

    genleveldb.commands = \
        cd $$PWD/src/leveldb && \
        CC=\"$$QMAKE_CC\" CXX=\"$$QMAKE_CXX\" \
        $(MAKE) OUT_DIR=out-static staticlibs

    cleanleveldb.target   = cleanleveldb
    cleanleveldb.commands = rm -rf $$LEVELDB_OUT_DIR

    LIBS += $$LEVELDB_OUT_DIR/libleveldb.a $$LEVELDB_OUT_DIR/libmemenv.a

    PRE_TARGETDEPS      += $$LEVELDB_OUT_DIR/libleveldb.a
    QMAKE_EXTRA_TARGETS += genleveldb
    QMAKE_EXTRA_TARGETS += cleanleveldb
    clean.depends       += cleanleveldb
    QMAKE_EXTRA_TARGETS += clean
    QMAKE_CLEAN         += $$PWD/src/leveldb/build_config.mk
}
#######################################################################


#######################################################################
##  Git Versioning with share/genbuild.sh
#######################################################################
# regenerate src/build.h
!windows|contains(USE_BUILD_INFO, 1) {
    genbuild.depends = FORCE
    genbuild.commands = cd $$PWD; mkdir -p $$OUT_PWD/build/$$BUILD_TAG; /bin/sh share/genbuild.sh $$OUT_PWD/build/$$BUILD_TAG/build.h
    genbuild.target = $$OUT_PWD/build/$$BUILD_TAG/build.h
    PRE_TARGETDEPS += $$OUT_PWD/build/$$BUILD_TAG/build.h
    QMAKE_EXTRA_TARGETS += genbuild
    DEFINES += HAVE_BUILD_INFO
}


#######################################################################
##  Linker
#######################################################################

win32 {
  QMAKE_LFLAGS += -Wl,-enable-auto-import
}


#######################################################################
##  Optimization
#######################################################################

contains(USE_O3, 1) {
    message("Building with O3 optimization flag")
    QMAKE_CXXFLAGS_RELEASE -= -O2
    QMAKE_CFLAGS_RELEASE -= -O2
    QMAKE_CXXFLAGS += -O3
    QMAKE_CFLAGS += -O3
}


#######################################################################
##  Header Dependencies
#######################################################################
DEPENDPATH += src src/json src/qt
DEPENDPATH += src/leveldb/include
DEPENDPATH += src/tor src/tor/adapter


#######################################################################
##  Header Files
#######################################################################

HEADERS += \
    src/addednode.h \
    src/addresscontrol.h \
    src/addrman.h \
    src/alert.h \
    src/allocators.h \
    src/base58.h \
    src/bignum.h \
    src/bitcoinrpc.h \
    src/checkpoints.h \
    src/checkqueue.h \
    src/clientversion.h \
    src/coincontrol.h \
    src/colors.h \
    src/compat.h \
    src/crypter.h \
    src/db.h \
    src/hash.h \
    src/init.h \
    src/irc.h \
    src/kernel.h \
    src/key.h \
    src/keystore.h \
    src/limitedmap.h \
    src/main.h \
    src/miner.h \
    src/mruset.h \
    src/net.h \
    src/netbase.h \
    src/onionseed.h \
    src/pbkdf2.h \
    src/protocol.h \
    src/script.h \
    src/scrypt.h \
    src/serialize.h \
    src/servicetypeids.h \
    src/smessage.h \
    src/stealth.h \
    src/bitcoin-strlcpy.h \
    src/sync.h \
    src/threadsafety.h \
    src/txdb-leveldb.h \
    src/exploredb-leveldb.h \
    src/explore/explore.hpp \
    src/explore/ExploreConstants.hpp \
    src/explore/ExploreDestination.hpp \
    src/explore/ExploreInput.hpp \
    src/explore/ExploreOutput.hpp \
    src/explore/ExploreInOutLookup.hpp \
    src/explore/ExploreInOutList.hpp \
    src/explore/ExploreTx.hpp \
    src/explore/ExploreCardTransfer.hpp \
    src/explore/ExploreCardInfo.hpp \
    src/explore/InOutInfo.hpp \
    src/explore/AddrTxInfo.hpp \
    src/explore/AddrInOutInfo.hpp \
    src/explore/HDTxInfo.hpp \
    src/bip32/hdkeys.h \
    src/bip32/secp256k1_openssl.h \
    src/bip32/BigInt.h \
    src/bip32/uchar_vector.h \
    src/bip32/bip32-hash.h \
    src/bip32/typedefs.h \
    src/ui_interface.h \
    src/uint256.h \
    src/util.h \
    src/version.h \
    src/wallet.h \
    src/walletdb.h \
    src/qt/aboutdialog.h \
    src/qt/addressbookpage.h \
    src/qt/addresstablemodel.h \
    src/qt/askpassphrasedialog.h \
    src/qt/bitcoinaddressvalidator.h \
    src/qt/bitcoinamountfield.h \
    src/qt/bitcoingui.h \
    src/qt/bitcoinunits.h \
    src/qt/clientmodel.h \
    src/qt/coincontroldialog.h \
    src/qt/coincontroltreewidget.h \
    src/qt/csvmodelwriter.h \
    src/qt/editaddressdialog.h \
    src/qt/findaddressdialog.h \
    src/qt/getprivkeysdialog.h \
    src/qt/guiconstants.h \
    src/qt/guiutil.h \
    src/qt/monitoreddatamapper.h \
    src/qt/notificator.h \
    src/qt/optionsdialog.h \
    src/qt/optionsmodel.h \
    src/qt/overviewpage.h \
    src/qt/qrcodedialog.h \
    src/qt/qtipcserver.h \
    src/qt/qvalidatedlineedit.h \
    src/qt/qvaluecombobox.h \
    src/qt/rpcconsole.h \
    src/qt/sendcoinsdialog.h \
    src/qt/sendcoinsentry.h \
    src/qt/signverifymessagedialog.h \
    src/qt/transactiondesc.h \
    src/qt/transactiondescdialog.h \
    src/qt/transactionfilterproxy.h \
    src/qt/transactionrecord.h \
    src/qt/transactiontablemodel.h \
    src/qt/transactionview.h \
    src/qt/walletmodel.h \
    src/tor/adapter/orconfig.h \
    src/tor/adapter/orconfig-apple-intel.h \
    src/tor/adapter/orconfig-apple-silicon.h \
    src/tor/adapter/orconfig-freebsd.h \
    src/tor/adapter/orconfig-linux.h \
    src/tor/adapter/toradapter.h \
    src/leveldb/include/leveldb/c.h \
    src/leveldb/include/leveldb/cache.h \
    src/leveldb/include/leveldb/comparator.h \
    src/leveldb/include/leveldb/db.h \
    src/leveldb/include/leveldb/dumpfile.h \
    src/leveldb/include/leveldb/env.h \
    src/leveldb/include/leveldb/filter_policy.h \
    src/leveldb/include/leveldb/iterator.h \
    src/leveldb/include/leveldb/options.h \
    src/leveldb/include/leveldb/slice.h \
    src/leveldb/include/leveldb/status.h \
    src/leveldb/include/leveldb/table.h \
    src/leveldb/include/leveldb/table_builder.h \
    src/leveldb/include/leveldb/write_batch.h \
    src/lz4/lz4.h \
    src/json/json_spirit.h \
    src/json/json_spirit_error_position.h \
    src/json/json_spirit_reader.h \
    src/json/json_spirit_reader_template.h \
    src/json/json_spirit_stream_reader.h \
    src/json/json_spirit_utils.h \
    src/json/json_spirit_value.h \
    src/json/json_spirit_writer.h \
    src/json/json_spirit_writer_template.h \
    src/xxhash/xxhash.h


#######################################################################
##  Source Files
#######################################################################

SOURCES += \
    src/colors.cpp \
    src/addrman.cpp \
    src/alert.cpp \
    src/bitcoinrpc.cpp \
    src/checkpoints.cpp \
    src/crypter.cpp \
    src/db.cpp \
    src/hash.cpp \
    src/init.cpp \
    src/irc.cpp \
    src/kernel.cpp \
    src/key.cpp \
    src/keystore.cpp \
    src/main.cpp \
    src/miner.cpp \
    src/net.cpp \
    src/netbase.cpp \
    src/noui.cpp \
    src/pbkdf2.cpp \
    src/protocol.cpp \
    src/rpcblockchain.cpp \
    src/rpcdump.cpp \
    src/rpcmining.cpp \
    src/rpcnet.cpp \
    src/rpcrawtransaction.cpp \
    src/rpcwallet.cpp \
    src/script.cpp \
    src/scrypt.cpp \
    src/smessage.cpp \
    src/stealth.cpp \
    src/sync.cpp \
    src/txdb-leveldb.cpp \
    src/exploredb-leveldb.cpp \
    src/rpcexplore.cpp \
    src/explore/explore.cpp \
    src/explore/ExploreDestination.cpp \
    src/explore/ExploreInput.cpp \
    src/explore/ExploreOutput.cpp \
    src/explore/ExploreInOutLookup.cpp \
    src/explore/ExploreInOutList.cpp \
    src/explore/ExploreTx.cpp \
    src/explore/ExploreCardTransfer.cpp \
    src/explore/ExploreCardInfo.cpp \
    src/explore/InOutInfo.cpp \
    src/explore/AddrTxInfo.cpp \
    src/explore/AddrInOutInfo.cpp \
    src/explore/HDTxInfo.cpp \
    src/bip32/hdkeys.cpp \
    src/bip32/secp256k1_openssl.cpp \
    src/util.cpp \
    src/version.cpp \
    src/wallet.cpp \
    src/walletdb.cpp \
    src/hamsi_helper.c \
    src/ethash/lib/keccak/keccak.c \
    src/ethash/lib/ethash/progpow.cpp \
    src/ethash/lib/ethash/ethash.cpp \
    src/ethash/lib/ethash/managed.cpp \
    src/ethash/lib/ethash/primes.c \
    src/ethash/lib/keccak/keccakf800.c \
    src/ethash/lib/keccak/keccakf1600.c \
    src/qt/aboutdialog.cpp \
    src/qt/addressbookpage.cpp \
    src/qt/addresstablemodel.cpp \
    src/qt/askpassphrasedialog.cpp \
    src/qt/bitcoin.cpp \
    src/qt/bitcoinaddressvalidator.cpp \
    src/qt/bitcoinamountfield.cpp \
    src/qt/bitcoingui.cpp \
    src/qt/bitcoinstrings.cpp \
    src/qt/bitcoinunits.cpp \
    src/qt/clientmodel.cpp \
    src/qt/coincontroldialog.cpp \
    src/qt/coincontroltreewidget.cpp \
    src/qt/csvmodelwriter.cpp \
    src/qt/editaddressdialog.cpp \
    src/qt/findaddressdialog.cpp \
    src/qt/getprivkeysdialog.cpp \
    src/qt/guiutil.cpp \
    src/qt/monitoreddatamapper.cpp \
    src/qt/notificator.cpp \
    src/qt/optionsdialog.cpp \
    src/qt/optionsmodel.cpp \
    src/qt/overviewpage.cpp \
    src/qt/qrcodedialog.cpp \
    src/qt/qtipcserver.cpp \
    src/qt/qvalidatedlineedit.cpp \
    src/qt/qvaluecombobox.cpp \
    src/qt/rpcconsole.cpp \
    src/qt/sendcoinsdialog.cpp \
    src/qt/sendcoinsentry.cpp \
    src/qt/signverifymessagedialog.cpp \
    src/qt/transactiondesc.cpp \
    src/qt/transactiondescdialog.cpp \
    src/qt/transactionfilterproxy.cpp \
    src/qt/transactionrecord.cpp \
    src/qt/transactiontablemodel.cpp \
    src/qt/transactionview.cpp \
    src/qt/walletmodel.cpp \
    src/json/json_spirit_reader.cpp \
    src/json/json_spirit_value.cpp \
    src/json/json_spirit_writer.cpp \
    src/tor/adapter/toradapter.cpp


#######################################################################
##  Resource Files
#######################################################################

RESOURCES += \
    src/qt/bitcoin.qrc


#######################################################################
##  UI Forms
#######################################################################

FORMS += \
    src/qt/forms/coincontroldialog.ui \
    src/qt/forms/findaddressdialog.ui \
    src/qt/forms/sendcoinsdialog.ui \
    src/qt/forms/addressbookpage.ui \
    src/qt/forms/signverifymessagedialog.ui \
    src/qt/forms/getprivkeysdialog.ui \
    src/qt/forms/aboutdialog.ui \
    src/qt/forms/editaddressdialog.ui \
    src/qt/forms/transactiondescdialog.ui \
    src/qt/forms/overviewpage.ui \
    src/qt/forms/sendcoinsentry.ui \
    src/qt/forms/askpassphrasedialog.ui \
    src/qt/forms/rpcconsole.ui \
    src/qt/forms/optionsdialog.ui \
    src/qt/forms/qrcodedialog.ui


#######################################################################
##  Other Files
#######################################################################

# shown in Qt Creator
OTHER_FILES += \
    contrib/gitian-descriptors/* \
    doc/*.rst doc/*.txt doc/README README.md \
    res/bitcoin-qt.rc \
    share/setup.nsi


#######################################################################
##  Platform Specific Tor Files
#######################################################################

macx {
    include(qmake-include/breakout-tor-osx.pri)
} else:win32 {
    include(qmake-include/breakout-tor-win.pri)
} else {
    include(qmake-include/breakout-tor-linux.pri)
}


#######################################################################
##  Test
#######################################################################
contains(BITCOIN_QT_TEST, 1) {
    SOURCES += src/qt/test/test_main.cpp \
        src/qt/test/uritests.cpp
    HEADERS += src/qt/test/uritests.h
    DEPENDPATH += src/qt/test
    QT += testlib
    TARGET = breakoutcoin-qt_TEST
    DEFINES += BITCOIN_QT_TEST
}


#######################################################################
##  Translations
#######################################################################

# for lrelease/lupdate
# also add new translations to src/qt/bitcoin.qrc under translations/
TRANSLATIONS = $$files(src/qt/locale/bitcoin_*.ts)

# lrelease has to be a binary that runs on the *build* host.  For a native
# build that is simply the one belonging to the active Qt.  Cross-compiling
# for Windows it is not: $$[QT_INSTALL_BINS] then names the target Qt, whose
# lrelease is a Windows .exe.
#
# MXE keeps a host build of the Qt tools alongside the target one, under a
# directory named for the build triplet - x86_64-apple-darwin25.5.0,
# x86_64-pc-linux-gnu, and so on.  Rather than hardcode a triplet, scan for
# the host tool by its extensionless name: only the host build has a plain
# "lrelease", the Windows target build has "lrelease.exe".
#
# Setting QMAKE_LRELEASE in local-env-win.pri overrides all of this, and is
# the right place for anything specific to one machine.
isEmpty(QMAKE_LRELEASE) {
    win32 {
        !isEmpty(MXE_ROOT) {
            # $$files() expands a wildcard only in the last path component -
            # a mid-path "*" silently yields an empty list - so glob the
            # triplet directories and test each candidate underneath.
            MXE_TRIPLET_DIRS = $$files($$MXE_ROOT/usr/*)
            for(dir, MXE_TRIPLET_DIRS) {
                isEmpty(QMAKE_LRELEASE):exists($$dir/qt6/bin/lrelease) {
                    QMAKE_LRELEASE = $$dir/qt6/bin/lrelease
                }
            }
        }
        isEmpty(QMAKE_LRELEASE) {
            error("QMAKE_LRELEASE is not set and no host lrelease was found under MXE_ROOT - set QMAKE_LRELEASE in local-env-win.pri")
        }
    } else {
        QMAKE_LRELEASE = $$[QT_INSTALL_BINS]/lrelease
    }
}

isEmpty(QM_DIR):QM_DIR = $$PWD/src/qt/locale
# automatically build translations, so they can be included in resource file
TSQM.name = lrelease ${QMAKE_FILE_IN}
TSQM.input = TRANSLATIONS
TSQM.output = $$QM_DIR/${QMAKE_FILE_BASE}.qm
TSQM.commands = $$QMAKE_LRELEASE ${QMAKE_FILE_IN} -qm ${QMAKE_FILE_OUT}
TSQM.CONFIG = no_link
QMAKE_EXTRA_COMPILERS += TSQM


#######################################################################
##  Platform Specific Features
#######################################################################

win32 {
    RC_FILE = src/qt/res/bitcoin-qt.rc
}

win32:!contains(MINGW_THREAD_BUGFIX, 0) {
    # At least qmake win32-g++-cross profile is missing the -lmingwthrd
    # thread-safety flag. GCC has -mthreads to enable this, but it does not
    # work with static linking. -lmingwthrd must come BEFORE -lmingw, so
    # it is prepended to QMAKE_LIBS_QT_ENTRY.
    # It can be turned off with MINGW_THREAD_BUGFIX=0, just in case it causes
    # any problems on some untested qmake profile now or in the future.
    DEFINES += _MT
    QMAKE_LIBS_QT_ENTRY = -lmingwthrd $$QMAKE_LIBS_QT_ENTRY
}

macx {
   HEADERS += src/qt/macdockiconhandler.h
   OBJECTIVE_SOURCES += src/qt/macdockiconhandler.mm
   LIBS += -framework Foundation -framework ApplicationServices \
           -framework AppKit
   DEFINES += MSG_NOSIGNAL=0
   ICON = src/qt/res/icons/bitcoin.icns
   # QMAKE_CFLAGS_THREAD += -pthread
   # QMAKE_CXXFLAGS_THREAD += -pthread
   # Following purposefully kept as a reference:
   #    the MacOS linker does not accept the -pthread flag
   # QMAKE_LFLAGS_THREAD += -pthread
}


#######################################################################
##  Libs
#######################################################################

# Environment Specific Library Paths
LIBS += $$join(BOOST_LIB_PATH,,-L,) $$join(BDB_LIB_PATH,,-L,) \
        $$join(OPENSSL_LIB_PATH,,-L,) $$join(EVENT_LIB_PATH,,-L,) \
        $$join(ZLIB_LIB_PATH,,-L,) $$join(QRENCODE_LIB_PATH,,-L,)

# Linux Specific Library Paths
!win32:!macx {
    isEmpty(SYSTEM_LIB_PATH) {
        error("SYSTEM_LIB_PATH is not set")
    }
    !equals(SYSTEM_LIB_PATH, "-") {
        # Debian: usually /usr/lib/x86_64-linux-gnu
        LIBS += -L$$SYSTEM_LIB_PATH
    }
}

# Windows Specific Library Paths
win32 {
    # MXE: all libs are under MXE_PREFIX/lib, already added by the
    # *_LIB_PATH joins above, which all point there
}


LIBS += -lssl -levent -lz -lcrypto -ldb_cxx$$BDB_LIB_SUFFIX \
        -lqrencode

# Boost
LIBS += -lboost_atomic$$BOOST_LIB_SUFFIX \
        -lboost_chrono$$BOOST_LIB_SUFFIX \
        -lboost_filesystem$$BOOST_LIB_SUFFIX \
        -lboost_program_options$$BOOST_LIB_SUFFIX \
        -lboost_thread$$BOOST_THREAD_LIB_SUFFIX
        

!win32:!macx {
    # Linux: static link
    LIBS += -static -Bstatic
    # debian
    LIBS += -lrt
    LIBS += -ldl
    # Linux: turn dynamic linking back on for c/c++ runtime libraries
    LIBS += -pthread
    LIBS += -Wl,-Bdynamic,-rpath,.
}

win32 {
    LIBS += -static
    LIBS += -lCrypt32 -liphlpapi
    LIBS += -pthread
    LIBS += -lws2_32 -lshlwapi -lmswsock -lole32 -loleaut32 -luuid -lgdi32
}


#######################################################################
##  Make It
#######################################################################

system($$QMAKE_LRELEASE -silent $$_PRO_FILE_)
