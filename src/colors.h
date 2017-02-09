// Copyright 2015 James C. Stroud

#ifndef BREAKOUT_COLORS_H
#define BREAKOUT_COLORS_H

#include <vector>
#include <map>
#include <string>
#include <limits>

#include <stdint.h>
#include <assert.h>
#include <stdio.h>


#define TESTNET_BUILD 0

// N_COLORS includes COLOR_NONE
#define N_COLORS 58
#define N_COLOR_BYTES 1

// proof model
#define PURE_POW 0
#define PURE_POS 1
#define MIXED_POW_POS 2
#define PROOF_MODEL MIXED_POW_POS

// does reward depend on age of coin?
#define COINAGE_DEPENDENT_POS 0

// for GetBalance, GetStake in the wallet model
#define N_GUI_OVERVIEW_COLORS 3

// for Deck QPixmaps
#define N_GUI_DECK_COLORS 53

// fork times
extern const int64_t STAKING_FIX1_TIME;
extern const int64_t STAKING_FIX2_TIME;


extern bool fTestNet;

// extern const int N_COLORS;
// extern const int N_COLOR_BYTES;
extern const int64_t BASE_COIN;
extern const int64_t BASE_CENT;
extern const int BASE_FEE_EXPONENT;
extern const int64_t BASE_COIN;
extern const int64_t BASE_CENT;
extern const int BASE_FEE_EXPONENT;
// extern const bool COINAGE_DEPENDENT_POS;
extern std::string ADDRESS_DELIMETER;


// this is a multiple of the MIN_TX_FEE
#define VERY_HIGH_FEE 1000

// Main Net (1) PUBKEY, (2) SCRIPT & Test Net (3) PUBKEY, (4) SCRIPT
#define N_VERSIONS 4

// Complete enum of currencies
// These are the indices used throughout the code
typedef enum {
        BREAKOUT_COLOR_NONE = 0,
        // BroStake: Staking currency
        BREAKOUT_COLOR_BROSTAKE,         // BRX
        // BroCoin: Principal currency
        BREAKOUT_COLOR_BROCOIN,          // BRO
        // Atomic currency
        BREAKOUT_COLOR_ATOMIC,           // BAM
        // Deck
        JOKER,                // Joker
        ACE_OF_SPADES,        // Spades
        TWO_OF_SPADES,
        THREE_OF_SPADES,
        FOUR_OF_SPADES,
        FIVE_OF_SPADES,
        SIX_OF_SPADES,
        SEVEN_OF_SPADES,
        EIGHT_OF_SPADES,
        NINE_OF_SPADES,
        TEN_OF_SPADES,
        JACK_OF_SPADES,
        QUEEN_OF_SPADES,
        KING_OF_SPADES,
        ACE_OF_DIAMONDS,      // Diamonds
        TWO_OF_DIAMONDS,
        THREE_OF_DIAMONDS,
        FOUR_OF_DIAMONDS,
        FIVE_OF_DIAMONDS,
        SIX_OF_DIAMONDS,
        SEVEN_OF_DIAMONDS,
        EIGHT_OF_DIAMONDS,
        NINE_OF_DIAMONDS,
        TEN_OF_DIAMONDS,
        JACK_OF_DIAMONDS,
        QUEEN_OF_DIAMONDS,
        KING_OF_DIAMONDS,
        ACE_OF_CLUBS,         // Clubs
        TWO_OF_CLUBS,
        THREE_OF_CLUBS,
        FOUR_OF_CLUBS,
        FIVE_OF_CLUBS,
        SIX_OF_CLUBS,
        SEVEN_OF_CLUBS,
        EIGHT_OF_CLUBS,
        NINE_OF_CLUBS,
        TEN_OF_CLUBS,
        JACK_OF_CLUBS,
        QUEEN_OF_CLUBS,
        KING_OF_CLUBS,
        ACE_OF_HEARTS,        // Hearts
        TWO_OF_HEARTS,
        THREE_OF_HEARTS,
        FOUR_OF_HEARTS,
        FIVE_OF_HEARTS,
        SIX_OF_HEARTS,
        SEVEN_OF_HEARTS,
        EIGHT_OF_HEARTS,
        NINE_OF_HEARTS,
        TEN_OF_HEARTS,
        JACK_OF_HEARTS,
        QUEEN_OF_HEARTS,
        KING_OF_HEARTS,
        // Sistercoin: Mining Currency
        BREAKOUT_COLOR_SISCOIN,  // SIS
        BREAKOUT_COLOR_END
} BREAKOUT_COLOR ;

typedef enum {
        MAIN_PUBKEY_IDX = 0,
        MAIN_SCRIPT_IDX,
        TEST_PUBKEY_IDX,
        TEST_SCRIPT_IDX
} ADDRESS_VERSION_INDEX ;

// ascending rank
typedef enum {
         DECK_SUIT_NONE = 0,
         DECK_SUIT_HEARTS,
         DECK_SUIT_CLUBS,
         DECK_SUIT_DIAMONDS,
         DECK_SUIT_SPADES,
         DECK_SUIT_JOKERS
} DECK_SUIT;

// ascending rank
typedef enum {
         DECK_VALUE_NONE = 0,
         DECK_VALUE_TWO,
         DECK_VALUE_THREE,
         DECK_VALUE_FOUR,
         DECK_VALUE_FIVE,
         DECK_VALUE_SIX,
         DECK_VALUE_SEVEN,
         DECK_VALUE_EIGHT,
         DECK_VALUE_NINE,
         DECK_VALUE_TEN,
         DECK_VALUE_JACK,
         DECK_VALUE_QUEEN,
         DECK_VALUE_KING,
         DECK_VALUE_ACE,
         DECK_VALUE_ANY
} DECK_VALUE;



extern const int DEFAULT_COLOR;

extern const int DEFAULT_STAKE_COLOR;

extern const int COLOR_UNITS[N_COLORS];

extern const int aGuiOverviewColors[N_GUI_OVERVIEW_COLORS];
extern std::vector<int> GUI_OVERVIEW_COLORS;

extern const int aGuiDeckColors[N_GUI_DECK_COLORS];
extern std::vector<int> GUI_DECK_COLORS;

extern const int64_t COIN[N_COLORS];
extern const int64_t CENT[N_COLORS];
 
extern const int DIGITS[N_COLORS];
extern const int DECIMALS[N_COLORS];


#if COINAGE_DEPENDENT_POS
extern const int64_t STAKE_MULTIPLIER[N_COLORS];
extern const int64_t MAX_MINT_PROOF_OF_STAKE[N_COLORS];
#else
extern const int64_t BASE_POS_REWARD[N_COLORS];
#endif

extern const int64_t MAX_MONEY[N_COLORS];

extern const int FEE_COLOR[N_COLORS];
extern const int64_t MIN_TX_FEE[N_COLORS];
extern const int64_t MIN_RELAY_TX_FEE[N_COLORS];
extern const int64_t COMMENT_FEE_PER_CHAR[N_COLORS];

extern const int64_t OP_RET_FEE_PER_CHAR[N_COLORS];

extern const bool SCAVENGABLE[N_COLORS];

extern const int64_t MIN_TXOUT_AMOUNT[N_COLORS];

extern const int64_t MIN_INPUT_VALUE[N_COLORS];

extern const int64_t STAKE_COMBINE_THRESHOLD[N_COLORS];

#if 0
extern const int LAST_FIXED_POS_BLOCK[N_COLORS];
#endif

extern const int MINT_COLOR[N_COLORS];

extern const char *COLOR_TICKER[N_COLORS];
extern const char *COLOR_NAME[N_COLORS];

extern const unsigned char aColorID[N_VERSIONS][N_COLORS][N_COLOR_BYTES];

extern std::vector<std::vector<std::vector<unsigned char> > > COLOR_ID;

extern const int64_t PRIORITY_MULTIPLIER[N_COLORS];

extern const int64_t WEIGHT_MULTIPLIER[N_COLORS];

extern const int64_t POW_SUBSIDY[N_COLORS];

extern std::vector<std::map <std::vector <unsigned char>, int > > MAPS_COLOR_ID;

bool GetColorFromTicker(const std::string &ticker, int &nColorIn);

bool GetTickerFromColor(int nColor, std::string &ticker);

bool CheckColor(int nColor);

bool CanStake(int nColorIn);

int GetStakeMinConfirmations(int nColor);

int64_t GetWeightMultiplier(int nColor, int64_t nTimeBlockPrev);


bool SplitQualifiedAddress(const std::string &qualAddress,
                              std::string &address, int &nColor, bool fDebug);

// add b58 compatible bytes of n to end of vch, little byte first
bool AppendColorBytes(int n, std::vector<unsigned char> &vch);

#if 0
bool AppendColorBytes(int n, data_chunk &vch, int nMax=N_COLORS);
#endif

bool ValueMapAllPositive(const std::map<int, int64_t> &mapNet);
bool ValueMapAllZero(const std::map<int, int64_t> &mapNet);

// Returns effectively mapCredit - mapDebit
//   much like vectors would be subtracted.
void FillNets(const std::map<int, int64_t> &mapDebit,
              const std::map<int, int64_t> &mapCredit,
              std::map<int, int64_t> &mapNet);




// Deck
bool IsDeck(int nColor);
int GetCardSuit(int nColor);
int GetCardValue(int nColor);

// sorts descending
struct CardSorter
{
    bool operator()(const int t1, const int t2) const
    {
        int v1 = GetCardValue(t1);
        int v2 = GetCardValue(t2);
        if (v1 == v2)
        {
            return GetCardSuit(t1) > GetCardSuit(t2);
        }
        return v1 > v2;
    }
};

extern struct CardSorter cardSorter;


#endif  // BREAKOUT_COLORS_H
