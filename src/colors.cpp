// Copyright 2015-2017 James C. Stroud

#include "colors.h"

//////////////////////////////////////////////////////////////////////
///
/// Forks
///
//////////////////////////////////////////////////////////////////////
// fork times
// Wed Jul 20 00:00:00 2016 PDT
static const int64_t STAKING_FIX1_TIME = 1468998000;

// Tues Nov 28 00:00:00 2016 PST
static const int64_t STAKING_FIX2_TIME = 1480320000;

// Fri Aug 11 00:00:00 2017 PDT
static const int64_t FORK_003_TIME = 1502434800;

// Fri Aug 22 21:00:00 2017 PDT
static const int64_t FORK_004_TIME = 1503417600;

//////////////////////////////////////////////////////////////////////
///
/// Network Constants
///
//////////////////////////////////////////////////////////////////////

// network constants (used to be in netbase.cpp)

// random port number, not used much
unsigned short const TOR_PORT = 25615;

unsigned short const P2P_PORT = 11698;
unsigned short const P2P_PORT_TESTNET = 21698;

unsigned short const DEFAULT_PROXY = 9050;
unsigned short const DEFAULT_PROXY_TESTNET = 19050;

// rpc
unsigned short const RPC_PORT = 50542;
unsigned short const RPC_PORT_TESTNET = 60542;

// The message start string is designed to be unlikely to occur in normal data.
// The characters are rarely used upper ASCII, not valid as UTF-8, and produce
// a large 4-byte int at any alignment.
unsigned char pchMessageStart[4] = {  0xf9, 0xcf, 0xcb, 0xdf  };
unsigned char pchMessageStartTestnet[4] = { 0xcb, 0xad, 0xef, 0xfd };


//////////////////////////////////////////////////////////////////////
///
/// Minting Constants
///
//////////////////////////////////////////////////////////////////////
// proof limits
CBigNum bnProofOfWorkLimit(~uint256(0) >> 26);
CBigNum bnProofOfStakeLimit(~uint256(0) >> 14);
CBigNum bnProofOfWorkLimitTestNet(~uint256(0) >> 24);
CBigNum bnProofOfStakeLimitTestNet(~uint256(0) >> 8);

// target spacings
// bgw can have independent block spacings for PoS and PoW
// but with multicurrency hybrid PoW/PoS, there is no need
static const unsigned int nTargetSpacingPoS = 600;        // 10 min
static const unsigned int nTargetSpacingPoW = 600;        // 10 min
static const unsigned int nTargetSpacingPoSTestNet = 60;  // 1 min
static const unsigned int nTargetSpacingPoWTestNet = 300; // 5 min (1/6 PoW)

static const int nCoinbaseMaturity = 240;        // 240 blocks (12 hr)
static const int nCoinbaseMaturityTestNet = 10;  // 10 blocks

//////////////////////////////////////////////////////////////////////
///
/// PoS Constants
///
//////////////////////////////////////////////////////////////////////
#if PROOF_MODEL == PURE_POS
// must have a last PoW block if it is to be pure PoS
// optional overlap between PoW and PoS
// 2 min PoW and 2 min PoS
static const int LAST_POW_BLOCK = 3500;
static const int FIRST_POS_BLOCK = 1;
static const int LAST_POW_BLOCK_TESTNET = 3500;
static const int FIRST_POS_BLOCK_TESTNET = 1;
#elif PROOF_MODEL == MIXED_POW_POS
//
#endif  // PROOF_MODEL

// To decrease granularity of timestamp
// Relative prime to block spacing target
static const int STAKE_TIMESTAMP_MASK = 17;
static const int STAKE_TIMESTAMP_MASK_TESTNET = 1;

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
const int MODIFIER_INTERVAL_RATIO = 3;


//////////////////////////////////////////////////////////////////////
///
/// Money Constants
///
//////////////////////////////////////////////////////////////////////

unsigned int nStakeMinAge = 60 * 60 * 24 * 12;      // 12 days
unsigned int nStakeMinAgeTestNet = 10 * 60;         // 10 min
unsigned int nStakeMaxAge = 60 * 60 * 24 * 24;      // 24 days
unsigned int nStakeMaxAgeTestNet = 120 * 60;        // 120 minutes

// time to elapse before new modifier is computed (10 blocks)
unsigned int nModifierInterval = 30 * 60;           // 30 min
unsigned int nModifierIntervalTestNet = 60;         // 60 seconds


// make sure these are consistent with nStakeMinAge
// 12 days at 5 min block times
static const int nStakeMinConfirmations = 3456;
static const int nStakeMinConfirmationsTestnet = 10;
// 48 days at 5 min block times
static const int nStakeMinConfirmationsDeck = 4 * nStakeMinConfirmations;
static const int nStakeMinConfirmationsDeckTestnet = 4 * nStakeMinConfirmationsTestnet;


// avoid counting zeros
const int64_t BASE_COIN = 100000000;
const int64_t BASE_CENT = 1000000;

// different currencies (colored coins) have different money supplies
// fees are charged in the currency of the transaction
// const int BASE_FEE_EXPONENT = 5;

// some systems will want to multiply coinage by an interest rate
// breakout has a fixed and money supply dependent rewards
// const bool COINAGE_DEPENDENT_POS = false;

// for qualified addresses with currency as name
std::string ADDRESS_DELIMETER = "_";

// different currencies may have different divisibilities
//                               -     BRX        BRO     BAM
const int64_t COIN[N_COLORS] = { 0, BASE_COIN, BASE_COIN,  1,
                      // Joker
                                    1,
                      // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      //               SIS
                                    BASE_COIN };

//                               -     BRX        BRO     BAM
const int64_t CENT[N_COLORS] = { 0, BASE_CENT, BASE_CENT,  1,
                      // Joker
                                    1,
                      // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      //               SIS
                                    BASE_CENT };

// related to max supply
//                            -      BRX           BRO              BAM
//                            -   19,500,000   1,000,000,000   10,000,000,000
const int DIGITS[N_COLORS] = {0,           8,             10,              11,
                      // Joker
                                    1,
                      // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                      // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//                                    SIS
//                                21,000,000
                                           8 };


// related to divisibility
//                              -      BRX          BRO         BAM
//                              -   0.00000001      0.00000001    -
const int DECIMALS[N_COLORS] = {0,           8,              8,   0,
                        // Joker
                                      0,
                        // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                        // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//                                     SIS
//                                  0.00000001
                                             8 };


// this test is here to help with consistency
// but it could be that there could be a mix of coinage dependent and not
#if COINAGE_DEPENDENT_POS
// colored coins are generated based on relative preponderance
// these are in order of BREAKOUT_COLOR
//    "when brocoin is generated it is generated with a multiplier of 1"
//    "and brostake is generated with a multiplier of 0 (never generated)"
// if a currency can't mint, then this value is not relevant
//                                           0  BRX  BRO  BAM
const int64_t STAKE_MULTIPLIER[N_COLORS] = { 0,   0,   1,   0,
                                    // Joker
                                                  0,
                                    // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                    // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//                                              SIS
                                                  0 };

//                                                  0  BRX       BRO        BAM
const int64_t MAX_MINT_PROOF_OF_STAKE[N_COLORS] = { 0,  0,  40 * BASE_CENT,  0,
                                          // Joker
                                                        0,
                                          // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                          // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
//                                              SIS
                                                  0 };
#else
// Note: prior to BRK_FORK003 release, these were indexed by mint
// color, but the logic didn't work out. See GetProofOfStakeReward().
// If a currency can't stake, then this value is not relevant.
// These are somewhat like markers, and are
//    used for calculations in GetProofOfStakeReward().
// stake color (in order of BREAKOUT_COLOR)  -         BRX         BRO   BAM
const int64_t BASE_POS_REWARD[N_COLORS] = {  0,   BASE_COIN * 10,    0,    0,
          // Joker
                       14,
          // Spades     A  2  3  4  5  6  7  8  9  10   J   Q   K
                       14, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
          // Diamonds   A  2  3  4  5  6  7  8  9  10   J   Q   K
                       14, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
          // Clubs      A  2  3  4  5  6  7  8  9  10   J   Q   K
                       14, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
          // Hearts     A  2  3  4  5  6  7  8  9  10   J   Q   K
                       14, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,
//                                              SIS
                                                  0 };
#endif

// BROSTAKE MAX_MONEY is same as premine: no more created ever
// BROCOIN MAX_MONEY expected to be reached about 80 years after launch
// ATOMIC MAX_MONEY is same as premine: no more created ever
// MAX_MONEY also sets the max amount to spend
// IMPORTANT: make sure these values are in smallest divisible units
//                                    -                 BRX                    BRO           BAM (1e10)
const int64_t MAX_MONEY[N_COLORS] = { 0, BASE_COIN * 12500000, BASE_COIN * 1000000000,  BASE_COIN * 100,
                              // Joker
                                            1,
                              // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                              // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                              // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                              // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
//                                                      SIS
                                         BASE_COIN * 21000000 };

                   

// Fees are complicated. Each currency can be sent with a fee payable in it's
// FEE_COLOR. Most of the times the FEE_COLOR is the transaction currency itself.
// For example, the fee currency for BRO is BRO. However, delegating a fee currency
// is necessary for atomic currencies with a low coin count. They would cease to
// be useful very quickly. The fee currency for the proof of concept atomic
// currency, BAM, is BRO.
const int FEE_COLOR[N_COLORS] = { (int) BREAKOUT_COLOR_NONE,
                                  (int) BREAKOUT_COLOR_BROSTAKE,   // BRX
                                  (int) BREAKOUT_COLOR_BROCOIN,    // BRO
                                  (int) BREAKOUT_COLOR_ATOMIC,     // BAM
           // Cards mint Sister Coin, so their fees are in Sister Coin.
           // This also means that there will be a 1 block Sister Coin premine so
           //   cards can be sent out.
                                  // Joker
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  // Spades
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  // Spades
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  // Spades
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  // Spades
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  (int) BREAKOUT_COLOR_SISCOIN,
                                  // SIS
                                  (int) BREAKOUT_COLOR_SISCOIN };


// MIN_TX_FEE, MIN_RELAY_TX_FEE, and OP_RET_FEE_PER_CHAR are in units of the **FEE_COLOR**

// BRX is slightly more expensive to send because the money supply is greater.
// 1 for BAM is 1e-10 of the money supply, 10x cheaper than NXT.

// $2 M cap -> ~$0.001 tx fee
// if cap grows to $20 B, then readjust
// min tx fees are *not* weighted by a priority multiplier
const int64_t MIN_TX_FEE[N_COLORS] = {0, BASE_CENT * 15 / 10, BASE_CENT, 1,
               // Joker
               BASE_CENT,
               // Spades
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               // Diamonds
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               // Clubs
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               // Hearts
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               // SIS
               BASE_CENT };

const int64_t MIN_RELAY_TX_FEE[N_COLORS] = { 0, BASE_CENT * 15 / 10, BASE_CENT, 1,
               // Joker
               BASE_CENT,
               // Spades
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               // Diamonds
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               // Clubs
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               // Hearts
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT, BASE_CENT,
               // SIS
               BASE_CENT };

// (0.01 per byte)
const int64_t COMMENT_FEE_PER_CHAR[N_COLORS] = { 0,
                //   BRX                   BRO                   BAM
                     BASE_CENT * 15 / 100, BASE_CENT * 10 / 100,   1,
         // Joker
         BASE_CENT * 10 / 100,
         // Spades
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         // Diamonds
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         // Clubs
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         // Hearts
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100, BASE_CENT * 10 / 100,
         // SIS
         BASE_CENT * 10 / 100 };

// op returns can be big, but they are expensive (0.015 per byte)
const int64_t OP_RET_FEE_PER_CHAR[N_COLORS] = { 0,
                //   BRX                   BRO                   BAM
                     BASE_CENT * 23 / 100, BASE_CENT * 15 / 100,   2,
         // Joker
         BASE_CENT * 15 / 100,
         // Spades
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         // Diamonds
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         // Clubs
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         // Hearts
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100, BASE_CENT * 15 / 100,
         // SIS
         BASE_CENT * 15 / 100 };

// Can the currency be recovered by fee scavenging?
// There probably aren't any good reasons to make a currency non-scavengable,
//         but this is already written in, so keep it in case I come up with one.
//                                     -     BRX   BRO   BAM
const bool SCAVENGABLE[N_COLORS] = { false, true, true, true,
             // Joker
             true,
             // Spades
             true, true, true, true, true, true, true, true, true, true, true, true, true,
             // Diamonds
             true, true, true, true, true, true, true, true, true, true, true, true, true,
             // Clubs
             true, true, true, true, true, true, true, true, true, true, true, true, true,
             // Hearts
             true, true, true, true, true, true, true, true, true, true, true, true, true,
             // SIS
             true };

const int64_t MIN_TXOUT_AMOUNT[N_COLORS] = {0, BASE_CENT, BASE_CENT, 1,
                                 // Joker
                                               1,
                                 // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 //               SIS
                                               BASE_CENT };


const int64_t MIN_INPUT_VALUE[N_COLORS] = {0, BASE_CENT, BASE_CENT, 1,
                                 // Joker
                                               1,
                                 // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 //               SIS
                                               BASE_CENT };


// combine threshold for creating coinstake
// if a currency can't stake, then this value is not relevant
// IMPORTANT: make sure these values are in smallest divisible units
//                                                  -        BRX           BRO    BAM
const int64_t STAKE_COMBINE_THRESHOLD[N_COLORS] = { 0, 1000 * BASE_COIN,     0,     0,
                                          // Joker
                                                        1,
                                          // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                          // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                          // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                          // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                          //            SIS
                                                        0 };


// what does a given currency mint (see GetProofOfStakeReward)
// these are in order of BREAKOUT_COLOR
//    "brostake mints brocoin, but brocoin mints nothing"
const int MINT_COLOR[N_COLORS] = { (int) BREAKOUT_COLOR_NONE,
                                   (int) BREAKOUT_COLOR_BROCOIN,   // BRX
                                   (int) BREAKOUT_COLOR_NONE,      // BRO
                                   (int) BREAKOUT_COLOR_NONE,      // BAM
               // Joker
               (int) BREAKOUT_COLOR_SISCOIN,
               // Spades
               (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               // Diamonds
               (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               // Clubs
               (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               // Hearts
               (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN, (int) BREAKOUT_COLOR_SISCOIN,
               // SIS
               (int) BREAKOUT_COLOR_NONE };


const char *COLOR_TICKER[N_COLORS] = { "<none>", "BRX", "BRK", "BAM",
           // Deck Joker
                  "DJK",
           //  Deck Ace of Spades, Deck 2 of Spades, ... Deck Ten of Spades, Deck Jack of Spades
                  "DAS", "D2S", "D3S", "D4S", "D5S", "D6S", "D7S", "D8S", "D9S", "DTS", "DJS", "DQS", "DKS",
           //  Deck Ace of Diamonds, Deck 2 of Diamonds, ... Deck Ten of Diamonds, ...
                  "DAD", "D2D", "D3D", "D4D", "D5D", "D6D", "D7D", "D8D", "D9D", "DTD", "DJD", "DQD", "DKD",
           //  Deck Ace of Clubs, ...
                  "DAC", "D2C", "D3C", "D4C", "D5C", "D6C", "D7C", "D8C", "D9C", "DTC", "DJC", "DQC", "DKC",
           //  Deck Ace of Hearts, ...
                  "DAH", "D2H", "D3H", "D4H", "D5H", "D6H", "D7H", "D8H", "D9H", "DTH", "DJH", "DQH", "DKH",
           //                                     SIS
                                                 "SIS" };
                   
const char *COLOR_NAME[N_COLORS] = { "<none>", "Breakout Stake", "Breakout Coin", "Atomic",
        // Joker
        "Joker",
        // Spades
        "Ace of Spades", "Two of Spades", "Three of Spades", "Four of Spades",
        "Five of Spades", "Six of Spades", "Seven of Spades", "Eight of Spades",
        "Nine of Spades", "Ten of Spades", "Jack of Spades", "Queen of Spades", "King of Spades",
        // Diamonds
        "Ace of Diamonds", "Two of Diamonds", "Three of Diamonds", "Four of Diamonds",
        "Five of Diamonds", "Six of Diamonds", "Seven of Diamonds", "Eight of Diamonds",
        "Nine of Diamonds", "Ten of Diamonds", "Jack of Diamonds", "Queen of Diamonds", "King of Diamonds",
        // Clubs
        "Ace of Clubs", "Two of Clubs", "Three of Clubs", "Four of Clubs",
        "Five of Clubs", "Six of Clubs", "Seven of Clubs", "Eight of Clubs",
        "Nine of Clubs", "Ten of Clubs", "Jack of Clubs", "Queen of Clubs", "King of Clubs",
        // Hearts
        "Ace of Hearts", "Two of Hearts", "Three of Hearts", "Four of Hearts",
        "Five of Hearts", "Six of Hearts", "Seven of Hearts", "Eight of Hearts",
        "Nine of Hearts", "Ten of Hearts", "Jack of Hearts", "Queen of Hearts", "King of Hearts",
        // Siscoin
                                               "Sister Coin" };


// these must be unique, except color none (first) is 0 or 0, 0 etc.
// for thousands of currencies, initialize with a loop

// first dimension is indexed by ADDESS_VERSION_INDEX enum
// IMPORTANT: don't use aColorID directly, it gets copied to vector COLOR_ID
// Yes, fitting the deck into one byte is going to make for some ugly addresses.
const unsigned char aColorID[N_VERSIONS][N_COLORS][N_COLOR_BYTES] = {

                                                                  //      -    BRX    BRO    BAM
        /* Main Net PUBKEY */                                          { {0}, {174}, {160}, {120},
                   // Joker
                    {5},
                   // Spades
                    {16},  {17},  {18},  {19},  {20},  {21},  {22},  {23},  {24},  {25},  {26},  {27},  {29},
                   // Diamonds
                    {30},  {31},  {32},  {33},  {34},  {35},  {36},  {37},  {38},  {39},  {40},  {41},  {42},
                   // Clubs
                    {43},  {44},  {45},  {46},  {47},  {49},  {50},  {51},  {52},  {53},  {54},  {56},  {57},
                   // Hearts
                    {58},  {59},  {60},  {61},  {62},  {63},  {64},  {65},  {66},  {67},  {68},  {69},  {71},
                   // SIS
                    {234} },

                                                                  //      -    BRX    BRO    BAM
        /* Main Net SCRIPT */                                          { {0}, {107}, {203}, { 55},
                   // Joker
                    {9},
                   // Spades
                    {72},  {73},  {74},  {75},  {76},  {77},  {78},  {79},  {80},  {81},  {82},  {83},  {84},
                   // Diamonds
                    {85},  {86},  {87},  {88},  {89},  {90},  {91},  {92},  {93},  {94},  {95},  {96},  {97},
                   // Clubs
                    {98},  {99}, {100}, {101}, {102}, {103}, {104}, {105}, {106}, {108}, {109}, {110}, {111},
                   // Hearts
                   {112}, {113}, {114}, {115}, {116}, {117}, {118}, {119}, {121}, {123}, {124}, {125}, {126},
                   // SIS
                   {235} },
                                                                  //      -    BRX    BRO    BAM
        /* Test Net PUBKEY */                                          { {0}, {  6}, { 28}, { 48},
                   // Joker
                    {11},
                   // Spades
                   {127}, {128}, {129}, {130}, {131}, {132}, {133}, {134}, {135}, {136}, {137}, {138}, {139},
                   // Diamonds
                   {140}, {141}, {142}, {143}, {144}, {145}, {146}, {147}, {148}, {149}, {150}, {151}, {152},
                   // Clubs
                   {153}, {154}, {155}, {156}, {157}, {158}, {159}, {161}, {162}, {163}, {164}, {165}, {166},
                   // Hearts
                   {167}, {168}, {169}, {170}, {171}, {172}, {173}, {175}, {176}, {177}, {178}, {179}, {180},
                   // SIS
                   {236} },
                                                                  //      -    BRX    BRO    BAM
        /* Test Net SCRIPT */                                          { {0}, {122}, { 70}, {  8},
                   // Joker
                   {13},
                   // Spades
                   {181}, {182}, {183}, {184}, {185}, {186}, {187}, {188}, {189}, {190}, {191}, {192}, {193}, 
                   // Diamonds
                   {194}, {195}, {196}, {197}, {198}, {199}, {200}, {201}, {202}, {204}, {205}, {206}, {207}, 
                   // Clubs
                   {208}, {209}, {210}, {211}, {212}, {213}, {214}, {215}, {216}, {217}, {218}, {219}, {220}, 
                   // Hearts
                   {221}, {222}, {223}, {224}, {225}, {226}, {227}, {228}, {229}, {230}, {231}, {232}, {233},
                   // SIS
                   {237} }
};

// COLOR_ID (vector version of 3D aColorID)
std::vector<std::vector<std::vector<unsigned char> > > COLOR_ID(
                       N_VERSIONS, std::vector<std::vector<unsigned char> >(
                            N_COLORS, std::vector<unsigned char>(N_COLOR_BYTES)));

// Think of PRIORITY_MULTIPLIER this way:
//              priority ~ multiplier[color] * value_in * confs
// where value_in is in smallest divisible units (e.g. bitcoin -> satoshi).
// Miners will have to adjust these or have them set dynamically
// from the exchange values.

// PRIORITY_MULTIPLIER also adjusts fee-based prioritization.
// PRIORITY_MULTIPLIER should take into account these differences:
//   - total coin counts of the currencies
//   - differences in exchange values
//   - differences in how COIN is defined for each currency
// TODO: make this adjustable by RPC and configurable in the init.
const int64_t PRIORITY_MULTIPLIER[N_COLORS] = { 0, 1, 1, 1,
           // Joker
           100 * BASE_COIN,
           // Spades
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           // Diamonds
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           // Clubs
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           // Hearts
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN, 100 * BASE_COIN,
           // SIS
           1 };


// as of FORK002, owning 1 card is equivalent to owning 1048576 BRX for purposes
// of stake weight // this is like having 16.7% of all the stake weight of the full
// BRX money supply, which means cards will stake as soon as they are mature
// there is a chance to control the chain for 53 blocks if one person collects
// the deck, but that is probably more difficult and expensive that buying all
// available BRX
static const int64_t nCW = COIN[BREAKOUT_COLOR_BROSTAKE] * 1048576;

// their weights determine how readily they stake
// these are in order of BREAKOUT_COLOR
//    "when brostake generates, it does so with a weight of 1"
//    "and brocoin & atomic stakes with a weight of 0x (never stakes)"
// IMPORTANT: make sure to take money supply into account right here, these are per coin
//                                            -   BRX  BRO  BAM
const int64_t WEIGHT_MULTIPLIER[N_COLORS] = { 0,    1,   0,   0,
             // Joker
                           nCW,
             // Spades      A    2    3    4    5    6    7    8    9   10    J    Q    K
                           nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW,
             // Diamonds    A    2    3    4    5    6    7    8    9   10    J    Q    K
                           nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW,
             // Clubs       A    2    3    4    5    6    7    8    9   10    J    Q    K
                           nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW,
             // Hearts      A    2    3    4    5    6    7    8    9   10    J    Q    K
                           nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW, nCW,
             //            SIS
                           0 };


// These are for the premine blocks. No premine for Sistercoin.
// IMPORTANT: make sure these values are in smallest divisible units
//      the smallest divisble unit of BAM is the integer 1, which is the same as a BRK brotoshi
//                                     -        BRX                BRO                     BAM 1e10)
const int64_t POW_SUBSIDY[N_COLORS] = {0, 12500000 * BASE_COIN, 19500000 * BASE_COIN, 100 * BASE_COIN,
                                 // Joker
                                               1,
                                 // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                               1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                 // SIS
                                               0 };


// MAPS_COLOR_ID is to look up values in case there are many currencies
// has this structure: 
//      [ {version1_bytes_color1 : color1, version1_bytes_color2, ...},
//        {version2_bytes_color1 : color1, version2_bytes_color2, ...}, ... ]
std::vector<std::map <std::vector <unsigned char>, int > > MAPS_COLOR_ID;



//////////////////////////////////////////////////////////////////////
///
/// Deck
///
//////////////////////////////////////////////////////////////////////

const int CARD_SUIT[N_COLORS] =
                  { DECK_SUIT_NONE, DECK_SUIT_NONE, DECK_SUIT_NONE, DECK_SUIT_NONE,
                    DECK_SUIT_JOKERS,
                    DECK_SUIT_SPADES, DECK_SUIT_SPADES, DECK_SUIT_SPADES, DECK_SUIT_SPADES,
                    DECK_SUIT_SPADES, DECK_SUIT_SPADES, DECK_SUIT_SPADES,
                    DECK_SUIT_SPADES, DECK_SUIT_SPADES, DECK_SUIT_SPADES,
                    DECK_SUIT_SPADES, DECK_SUIT_SPADES, DECK_SUIT_SPADES,
                    DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS,
                    DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS,
                    DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS,
                    DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS, DECK_SUIT_DIAMONDS,
                    DECK_SUIT_CLUBS, DECK_SUIT_CLUBS, DECK_SUIT_CLUBS, DECK_SUIT_CLUBS,
                    DECK_SUIT_CLUBS, DECK_SUIT_CLUBS, DECK_SUIT_CLUBS,
                    DECK_SUIT_CLUBS, DECK_SUIT_CLUBS, DECK_SUIT_CLUBS,
                    DECK_SUIT_CLUBS, DECK_SUIT_CLUBS, DECK_SUIT_CLUBS,
                    DECK_SUIT_HEARTS, DECK_SUIT_HEARTS, DECK_SUIT_HEARTS, DECK_SUIT_HEARTS,
                    DECK_SUIT_HEARTS, DECK_SUIT_HEARTS, DECK_SUIT_HEARTS,
                    DECK_SUIT_HEARTS, DECK_SUIT_HEARTS, DECK_SUIT_HEARTS,
                    DECK_SUIT_HEARTS, DECK_SUIT_HEARTS, DECK_SUIT_HEARTS };

const int CARD_VALUE[N_COLORS] =
                  { DECK_VALUE_NONE, DECK_VALUE_NONE, DECK_VALUE_NONE, DECK_VALUE_NONE,
                    // Joker
                    DECK_VALUE_ANY,
                    // Spades
                    DECK_VALUE_ACE, DECK_VALUE_TWO, DECK_VALUE_THREE, DECK_VALUE_FOUR,
                    DECK_VALUE_FIVE, DECK_VALUE_SIX, DECK_VALUE_SEVEN,
                    DECK_VALUE_EIGHT, DECK_VALUE_NINE, DECK_VALUE_TEN,
                    DECK_VALUE_JACK, DECK_VALUE_QUEEN, DECK_VALUE_KING,
                    // Diamonds
                    DECK_VALUE_ACE, DECK_VALUE_TWO, DECK_VALUE_THREE, DECK_VALUE_FOUR,
                    DECK_VALUE_FIVE, DECK_VALUE_SIX, DECK_VALUE_SEVEN,
                    DECK_VALUE_EIGHT, DECK_VALUE_NINE, DECK_VALUE_TEN,
                    DECK_VALUE_JACK, DECK_VALUE_QUEEN, DECK_VALUE_KING,
                    // Clubs
                    DECK_VALUE_ACE, DECK_VALUE_TWO, DECK_VALUE_THREE, DECK_VALUE_FOUR,
                    DECK_VALUE_FIVE, DECK_VALUE_SIX, DECK_VALUE_SEVEN,
                    DECK_VALUE_EIGHT, DECK_VALUE_NINE, DECK_VALUE_TEN,
                    DECK_VALUE_JACK, DECK_VALUE_QUEEN, DECK_VALUE_KING,
                    // Hearts
                    DECK_VALUE_ACE, DECK_VALUE_TWO, DECK_VALUE_THREE, DECK_VALUE_FOUR,
                    DECK_VALUE_FIVE, DECK_VALUE_SIX, DECK_VALUE_SEVEN,
                    DECK_VALUE_EIGHT, DECK_VALUE_NINE, DECK_VALUE_TEN,
                    DECK_VALUE_JACK, DECK_VALUE_QUEEN, DECK_VALUE_KING };

//////////////////////////////////////////////////////////////////////
///
/// GUI Constants
///
//////////////////////////////////////////////////////////////////////

// The default currency for the gui-less client is NONE.
// These are default currencies for the gui client, where the user
//    will need to have an operational client without any configuration.
const int DEFAULT_COLOR = (int) BREAKOUT_COLOR_BROCOIN;
const int DEFAULT_STAKE_COLOR = (int) BREAKOUT_COLOR_BROSTAKE;

// For the gui, how divisible is the currency?
// For example, BTC is 3 (BTC, mBTC, uBTC)
//                                 -  BRX  BRO  BAM
const int COLOR_UNITS[N_COLORS] = {0,   3,   3,   1,
                          // Joker
                                        1,
                          // Spades     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                          // Diamonds   A  2  3  4  5  6  7  8  9 10  J  Q  K
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                          // Clubs      A  2  3  4  5  6  7  8  9 10  J  Q  K
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                          // Hearts     A  2  3  4  5  6  7  8  9 10  J  Q  K
                                        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                          //            SIS
                                        3 };


// The GUI can create overview stats for only a few (e.g. 3) currencies.
// These are the default ordering
const int aGuiOverviewColors[N_GUI_OVERVIEW_COLORS] =
                                         {BREAKOUT_COLOR_BROCOIN,
                                          BREAKOUT_COLOR_BROSTAKE,
                                          BREAKOUT_COLOR_SISCOIN};

// A vector is used so that number of currencies may be dynamic.
std::vector<int> GUI_OVERVIEW_COLORS;

const int aGuiDeckColors[N_GUI_DECK_COLORS] = {
              JOKER,
              // Spades
              ACE_OF_SPADES, TWO_OF_SPADES, THREE_OF_SPADES, FOUR_OF_SPADES, FIVE_OF_SPADES,
              SIX_OF_SPADES, SEVEN_OF_SPADES, EIGHT_OF_SPADES, NINE_OF_SPADES, TEN_OF_SPADES,
              JACK_OF_SPADES, QUEEN_OF_SPADES, KING_OF_SPADES,
              // Diamonds
              ACE_OF_DIAMONDS, TWO_OF_DIAMONDS, THREE_OF_DIAMONDS, FOUR_OF_DIAMONDS, FIVE_OF_DIAMONDS,
              SIX_OF_DIAMONDS, SEVEN_OF_DIAMONDS, EIGHT_OF_DIAMONDS, NINE_OF_DIAMONDS, TEN_OF_DIAMONDS,
              JACK_OF_DIAMONDS, QUEEN_OF_DIAMONDS, KING_OF_DIAMONDS,
              // Clubs
              ACE_OF_CLUBS, TWO_OF_CLUBS, THREE_OF_CLUBS, FOUR_OF_CLUBS, FIVE_OF_CLUBS,
              SIX_OF_CLUBS, SEVEN_OF_CLUBS, EIGHT_OF_CLUBS, NINE_OF_CLUBS, TEN_OF_CLUBS,
              JACK_OF_CLUBS, QUEEN_OF_CLUBS, KING_OF_CLUBS,
              // Hearts
              ACE_OF_HEARTS, TWO_OF_HEARTS, THREE_OF_HEARTS, FOUR_OF_HEARTS, FIVE_OF_HEARTS,
              SIX_OF_HEARTS, SEVEN_OF_HEARTS, EIGHT_OF_HEARTS, NINE_OF_HEARTS, TEN_OF_HEARTS,
              JACK_OF_HEARTS, QUEEN_OF_HEARTS, KING_OF_HEARTS };

std::vector<int> GUI_DECK_COLORS;



//////////////////////////////////////////////////////////////////////
///
/// Forks
///
//////////////////////////////////////////////////////////////////////

int GetFork(int64_t nTime)
{
    // Make sure Heights are ascending!
    const int64_t aForks[TOTAL_FORKS][2] = {
    //                                   Time,         Fork Number
                               {                   0,  BRK_GENESIS},
                               {   STAKING_FIX1_TIME,  BRK_FORK001},
                               {   STAKING_FIX2_TIME,  BRK_FORK002},
                               {       FORK_003_TIME,  BRK_FORK003},
                               {       FORK_004_TIME,  BRK_FORK004}
                                           };

    if (fTestNet)
    {
        return (int) TOTAL_FORKS;
    }

    // loop has strange logic, but if fork i height is greater than
    // nTimeBlockPrev then you are on fork i-1
    int nFork = aForks[0][1];
    for (int i = 1; i < TOTAL_FORKS; ++i)
    {
       if (aForks[i][0] > nTime)
       {
           break;
       }
       nFork = aForks[i][1];
    }
    return (int) nFork;
}




//////////////////////////////////////////////////////////////////////
///
/// Network
///
//////////////////////////////////////////////////////////////////////

int GetMinPeerProtoVersion(int64_t nTime)
{
    // helps to prevent buffer overrun
    static const int nVersions = 2;

    // Make sure forks are ascending!
    const int aVersions[nVersions][2] = {
    //                                    Fork, Proto Version
                   {               BRK_FORK003,         61010 },
                   {               BRK_FORK004,         61011 }
                                          };

    int nFork = GetFork(nTime);

    int nVersion = aVersions[0][1];

    for (int i = 1; i < nVersions; ++i)
    {
       if (aVersions[i][0] > nFork)
       {
           break;
       }
       nVersion = aVersions[i][1];
    }

    return nVersion;
}


//////////////////////////////////////////////////////////////////////
///
/// Currency Methods
///
//////////////////////////////////////////////////////////////////////

bool GetColorFromTicker(const std::string &ticker, int &nColorIn)
{
    nColorIn = (int) BREAKOUT_COLOR_NONE;
    for (int nColor = 1; nColor < N_COLORS; ++nColor)
    {
           if (std::string(COLOR_TICKER[nColor]) == ticker)
           {
                 nColorIn = nColor;
                 return true;
           }
    }
    return false;
}

bool GetTickerFromColor(int nColor, std::string &ticker)
{
     if (nColor < 1 || nColor > N_COLORS)
     {
              ticker = COLOR_TICKER[BREAKOUT_COLOR_NONE];
              return false;
     }
     ticker = COLOR_TICKER[nColor];
     return true;
}

bool CheckColor(int nColor)
{
    return (nColor >= 1 && nColor < N_COLORS);
}

bool CanStake(int nColor)
{
    if (!CheckColor(nColor))
    {
        return false;
    }
    if (MINT_COLOR[nColor] == BREAKOUT_COLOR_NONE)
    {
        return false;
    }
    return true;
}


unsigned int GetStakeMinAge()
{
    return fTestNet ? nStakeMinAgeTestNet : nStakeMinAge;
}


unsigned int GetStakeMaxAge()
{
    return fTestNet ? nStakeMaxAgeTestNet : nStakeMaxAge;
}


unsigned int GetModifierInterval()
{
     return fTestNet ? nModifierIntervalTestNet : nModifierInterval;
}

int GetStakeMinConfirmations(int nColor)
{
    if (!CanStake(nColor))
    {
       return std::numeric_limits<int>::max();
    }
    if (IsDeck(nColor))
    {
       if (fTestNet)
       {
           return nStakeMinConfirmationsDeckTestnet;
       }
       return nStakeMinConfirmationsDeck;
    }
    if (fTestNet)
    {
       return nStakeMinConfirmationsTestnet;
    }
    return nStakeMinConfirmations;
}

int64_t GetWeightMultiplier(int nColor, int64_t nTime)
{
    if (IsDeck(nColor) && (GetFork(nTime) < BRK_FORK002))
    {
        // before FORK002 it was much harder for cards to stake
        return WEIGHT_MULTIPLIER[nColor] / 256;
    }
    return WEIGHT_MULTIPLIER[nColor];
}



bool SplitQualifiedAddress(const std::string &qualAddress,
                              std::string &address, int &nColor, bool fDebug)
{

    // find the delimeter
    size_t x = qualAddress.find(ADDRESS_DELIMETER);
    if (x == std::string::npos)
    {
          if (fDebug)
          {
              printf("Unable to find ticker suffix for %s\n", qualAddress.c_str());
          }
          return false;
    }

    // make the ticker, check, set nColor
    std::string ticker = qualAddress.substr(x + ADDRESS_DELIMETER.size(),
                                                          qualAddress.size());
    if (!GetColorFromTicker(ticker, nColor))
    {
          if (fDebug)
          {
              printf("Ticker is not valid for %s\n", qualAddress.c_str());
          }
          return false;
    }

    // make the address
    address = qualAddress.substr(0, x);

    return true;
}

// add b58 compatible bytes of n to end of vch, little byte first
bool AppendColorBytes(int n, std::vector<unsigned char> &vch)
{
        if(!CheckColor(n))
        {
               return false;
        }
        while (n >= 256)
        {
            vch.push_back(n & 255);   //  fast % 256
            n = n >> 8;               //  fast / 256
        }
        vch.push_back(n);
        return true;
}


//////////////////////////////////////////////////////////////////////
///
/// Data Structures
///
//////////////////////////////////////////////////////////////////////

bool ValueMapAllPositive(const std::map<int, int64_t> &mapNet)
{
    if (mapNet.empty())
    {
       return false;
    }
    std::map<int, int64_t>::const_iterator itnet;
    for (itnet = mapNet.begin(); itnet != mapNet.end(); ++itnet)
    {
        if (itnet->second <= 0)
        {
            return false;
        }
    }
    return true;
}

bool ValueMapAllZero(const std::map<int, int64_t> &mapNet)
{
    std::map<int, int64_t>::const_iterator itnet;
    for (itnet = mapNet.begin(); itnet != mapNet.end(); ++itnet)
    {
        if (itnet->second != 0)
        {
            return false;
        }
    }
    return true;
}


// effectively mapCredit - mapDebit
void FillNets(const std::map<int, int64_t> &mapDebit,
              const std::map<int, int64_t> &mapCredit,
              std::map<int, int64_t> &mapNet)
{
    mapNet = mapCredit;
    std::map<int, int64_t>::iterator itnet;
    std::map<int, int64_t>::const_iterator itdeb;
    for (itnet = mapNet.begin(); itnet != mapNet.end(); ++itnet)
    {
        for (itdeb = mapDebit.begin(); itdeb != mapDebit.end(); ++itdeb)
        {
            if (itnet->first == itdeb->first)
            {
                itnet->second -= itdeb->second;
                break;
            }
        }
    }
    for (itdeb = mapDebit.begin(); itdeb != mapDebit.end(); ++itdeb)
    {
        itnet = mapNet.find(itdeb->first);
        if (itnet == mapNet.end())
        {
            mapNet[itdeb->first] = -itdeb->second;
        }
    }
}


//////////////////////////////////////////////////////////////////////
///
/// Deck
///
//////////////////////////////////////////////////////////////////////

bool IsDeck(int nColor)
{
   return (nColor >= JOKER) && (nColor <= KING_OF_HEARTS);
}

int GetCardSuit(int nColor)
{
    if (!CheckColor(nColor))
    {
       return DECK_SUIT_NONE;
    }
    return CARD_SUIT[nColor];
}

int GetCardValue(int nColor)
{
    if (!CheckColor(nColor))
    {
       return DECK_VALUE_NONE;
    }
    return CARD_VALUE[nColor];
}


// sorts descending
struct CardSorter cardSorter;


//////////////////////////////////////////////////////////////////////
///
/// Minting
///
//////////////////////////////////////////////////////////////////////

CBigNum GetTargetLimit(bool fProofOfStake)
{
    CBigNum bnTargetLimit;
    if (fProofOfStake)
    {
        bnTargetLimit = fTestNet ? bnProofOfStakeLimitTestNet : bnProofOfStakeLimit;
    }
    else
    {
        bnTargetLimit = fTestNet ? bnProofOfWorkLimitTestNet : bnProofOfWorkLimit;
    }
    return bnTargetLimit;
}

int64_t GetTargetSpacing(bool fProofOfStake)
{
    int64_t nTargetSpacing;
    if (fProofOfStake)
    {
        nTargetSpacing = fTestNet ? nTargetSpacingPoSTestNet : nTargetSpacingPoS;
    }
    else
    {
        nTargetSpacing = fTestNet ? nTargetSpacingPoWTestNet : nTargetSpacingPoW;
    }
    return nTargetSpacing;
}

int GetCoinbaseMaturity()
{
    return fTestNet ? nCoinbaseMaturityTestNet : nCoinbaseMaturity;
}

int GetStakeTimestampMask()
{
   return fTestNet ? STAKE_TIMESTAMP_MASK_TESTNET : STAKE_TIMESTAMP_MASK;
}


#if PROOF_MODEL == PURE_POS
int GetLastPoWBlock()
{
    return fTestNet ? LAST_POW_BLOCK_TESTNET : LAST_POW_BLOCK;
}

int GetFirstPoSBlock()
{
    return fTestNet ? FIRST_POS_BLOCK_TESTNET : FIRST_POS_BLOCK;
}
#endif
