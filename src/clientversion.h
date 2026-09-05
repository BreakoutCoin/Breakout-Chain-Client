#ifndef CLIENTVERSION_H
#define CLIENTVERSION_H

//
// client versioning
//
// This is the single place the version number is written.  Bumping a release
// means editing the four macros below and nothing else; every other consumer
// derives its value from here:
//
//   src/version.h                 composes CLIENT_VERSION from them
//   src/qt/res/bitcoin-qt.rc      stringizes them into the Windows PE
//                                   version resource
//   breakout-qt.pro               parses them into qmake's VERSION, which in
//                                   turn fills contrib/macdeploy/Info.plist.in
//
// Anything else that needs the version should read it from this file too,
// e.g. `awk '/define CLIENT_VERSION_MAJOR/{print $3}' src/clientversion.h`.
//
// Note that the version a built binary *reports* is a separate matter: inside
// a git tree share/genbuild.sh takes it from `git describe --dirty`, and only
// falls back to CLIENT_VERSION when that produces nothing.  Tag, with an
// ANNOTATED tag, before building release artifacts: plain `git describe` sees
// only annotated tags and fails outright at a lightweight one, which genbuild
// swallows -- so the fallback then reports a clean version for a dirty tree.
//

// These need to be macros, as version.cpp's and bitcoin-qt.rc's voodoo requires it
#define CLIENT_VERSION_MAJOR       1
#define CLIENT_VERSION_MINOR       9
#define CLIENT_VERSION_REVISION    2
#define CLIENT_VERSION_BUILD       0

// Converts the parameter X to a string after macro replacement on X has been performed.
// Don't merge these into one macro!
#define STRINGIZE(X) DO_STRINGIZE(X)
#define DO_STRINGIZE(X) #X

#endif // CLIENTVERSION_H
