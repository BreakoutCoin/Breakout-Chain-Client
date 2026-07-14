////////////////////////////////////////////////////////////////////////////////
//
// typedefs.h
//
// Copyright (c) 2013 Eric Lombrozo
// Copyright (c) 2011-2016 Ciphrex Corp.
//
// Distributed under the MIT software license, see the accompanying
// file LICENSE or http://www.opensource.org/licenses/mit-license.php.
//
// Breakout Explore port note:
//   Trimmed from the original. The original pulled in Stealth's
//   primitives/valtype.hpp (which also declared IncrementN helpers the bip32
//   library never uses). Here we reuse Breakout's own valtype (std::vector of
//   unsigned char, defined in uint256.h) and the secure_allocator / SecureString
//   from allocators.h, and define secure_valtype locally.
//

#ifndef __TYPEDEFS_H___
#define __TYPEDEFS_H___

#include "allocators.h"    // secure_allocator, SecureString

#include <vector>
#include <set>
#include <string>

#define BYTES(x) bytes_t((x).begin(), (x).end())
#define SECURE_BYTES(x) secure_bytes_t((x).begin(), (x).end())

// valtype is std::vector<unsigned char>; also defined in Breakout's uint256.h.
// Repeating an identical typedef in a translation unit is legal C++.
typedef std::vector<unsigned char> valtype;
typedef std::vector<unsigned char,
                    secure_allocator<unsigned char> > secure_valtype;

typedef valtype bytes_t;
typedef secure_valtype secure_bytes_t;

typedef std::vector<bytes_t> hashvector_t;
typedef std::set<bytes_t> hashset_t;

// from allocators.h
typedef SecureString secure_string_t;

typedef std::vector<int> ints_t;
typedef std::vector<int, secure_allocator<int> > secure_ints_t;

#endif // __TYPEDEFS_H__
