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

#include "bignum.h"

// breakout genesis block time
#define BRK_GENESIS_TIME 1465544351

// define to 1 to allow rpc to send multiple outputs to same address
#define ALLOW_DUPLICATE_DESTINATIONS 0

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


// this is a multiple of the MIN_TX_FEE
#define VERY_HIGH_FEE 1000

// Main Net (1) PUBKEY, (2) SCRIPT & Test Net (3) PUBKEY, (4) SCRIPT
#define N_VERSIONS 4


extern bool fTestNet;

extern const int64_t nKAWPOWActivationTime;

//////////////////////////////////////////////////////////////////////

// forks

// cloners: add your new forks higher than highest here
//          keep existing
//          also, rewrite GetFork
enum ForkNumbers
{
    BRK_GENESIS = 0,
    BRK_FORK001,
    BRK_FORK002,
    BRK_FORK003,
    BRK_FORK004,
    BRK_FORK005,
    BRK_FORK006,
    BRK_FORK007,  // KawPow
    TOTAL_FORKS
};


// networking
extern unsigned short const TOR_PORT;

extern unsigned short const P2P_PORT;
extern unsigned short const P2P_PORT_TESTNET;

extern unsigned short const DEFAULT_PROXY;
extern unsigned short const DEFAULT_PROXY_TESTNET;

// rpc
extern unsigned short const RPC_PORT;
extern unsigned short const RPC_PORT_TESTNET;

// pchMessageStart
extern unsigned char pchMessageStart[4];
extern unsigned char pchMessageStartTestnet[4];

// kernel
extern const int MODIFIER_INTERVAL_RATIO;


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

typedef std::pair<int, int64_t> ColorAmount;
typedef std::map<int, int64_t> AmountsMap;
typedef AmountsMap::iterator ColorsMapIter;
typedef AmountsMap::const_iterator ColorsMapConstIter;

// Complete enum of currencies
// These are the indices used throughout the code
typedef enum {
        BREAKOUT_COLOR_NONE = 0,
        // BroStake: Staking currency
        BREAKOUT_COLOR_BRX,   // Breakout Coin
        // BroCoin: Principal currency
        BREAKOUT_COLOR_BRK,   // Breakout Stake
        // Atomic currency
        BREAKOUT_COLOR_BAM,   // Atomic
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
        BREAKOUT_COLOR_SIS,  // Sistercoin: Mining Currency
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

extern const int MINT_COLOR[N_COLORS];

extern const char *COLOR_TICKER[N_COLORS];
extern const char *COLOR_NAME[N_COLORS];

extern const unsigned char aColorID[N_VERSIONS][N_COLORS][N_COLOR_BYTES];

extern std::vector<std::vector<valtype> > COLOR_ID;

extern const int64_t PRIORITY_MULTIPLIER[N_COLORS];

extern const int64_t WEIGHT_MULTIPLIER[N_COLORS];

extern const int64_t POW_SUBSIDY[N_COLORS];

extern std::vector<std::map<valtype, int> > MAPS_COLOR_ID;

int GetFork(int64_t nTime);

int GetMinPeerProtoVersion(int64_t nTime);

bool GetColorFromTicker(const std::string &ticker, int &nColorIn);

bool GetTickerFromColor(int nColor, std::string &ticker);

bool CheckColor(int nColor);

bool CanStake(int nColorIn);


unsigned int GetStakeMinAge(int64_t nTime);

unsigned int GetStakeMaxAge(int64_t nTime);

unsigned int GetModifierInterval(int64_t nTime);


int GetStakeMinConfirmations(int nColor);

int64_t GetWeightMultiplier(int nColor, int64_t nTimeBlockPrev);


bool SplitQualifiedAddress(const std::string &qualAddress,
                              std::string &address, int &nColor, bool fDebug);

// add b58 compatible bytes of n to end of vch, little byte first
bool AppendColorBytes(int n, valtype &vch);


// minting
CBigNum GetTargetLimit(bool fProofOfStake, unsigned int nTime);
int64_t GetTargetSpacing(bool fProofOfStake, int64_t nTime);
int GetCoinbaseMaturity();
int GetStakeTimestampMask();

#if PROOF_MODEL == PURE_POS
int GetLastPoWBlock();
int GetFirstPoSBlock();
#endif


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

inline double RealFromAmount(int64_t amount, int nColor)
{
    // None currency has no value ever.
    if (nColor == BREAKOUT_COLOR_NONE)
    {
        return 0;
    }
    if (nColor < 0 || nColor > N_COLORS)
    {
        char pchMsg[64];
        snprintf(pchMsg, sizeof(pchMsg), "Invalid currency: %d", nColor);
        throw std::runtime_error(pchMsg);
    }
    return (double)amount / (double)COIN[nColor];
}

// wrapper for mapAmounts, a map of colors (int) to amounts (int64_t)
// provides high level operations like Add and Subtract
class ColorsMap
{
public:
    AmountsMap mapAmounts;
    ColorsMap()
    {
        mapAmounts.clear();
    }
    ColorsMap(const AmountsMap mapAmountsIn)
    {
       mapAmounts = mapAmountsIn;
    }
    ColorsMap(const ColorsMap& other)
    {
       mapAmounts = other.mapAmounts;
    }
    ColorsMap& operator=(const ColorsMap& other)
    {
        if (this != &other)
        {
            mapAmounts = other.mapAmounts;
        }
        return *this;
    }
    bool operator==(const ColorsMap& other) const
    {
        // Check all entries in A against B
        for (ColorsMapConstIter it = mapAmounts.begin();
             it != mapAmounts.end(); ++it)
        {
            if (it->second != other.Get(it->first))
                return false;
        }
        // Check all entries in B not in A (A treats missing as 0)
        for (ColorsMapConstIter it = other.Begin();
             it != other.End(); ++it)
        {
            if (mapAmounts.find(it->first) == mapAmounts.end()
                    && it->second != 0)
                return false;
        }
        return true;
    }

    bool operator!=(const ColorsMap& other) const
    {
        return !(*this == other);
    }

    bool StrictEquals(const ColorsMap& other) const
    {
        if (Size() != other.Size())
            return false;

        std::set<int> setColors, setOtherColors;
        GetColors(setColors);
        other.GetColors(setOtherColors);
        if (setColors != setOtherColors)
            return false;

        for (ColorsMapConstIter it = mapAmounts.begin();
             it != mapAmounts.end(); ++it)
        {
            if (other.Get(it->first) != it->second)
                return false;
        }
        return true;
    }

    void Clear()
    {
        mapAmounts.clear();
    }
    bool Empty() const
    {
        return mapAmounts.empty();
    }
    size_t Size() const
    {
        return mapAmounts.size();
    }
    void Set(int nColor, int64_t nValue)
    {
        mapAmounts[nColor] = nValue;
    }
    size_t GetColors(std::set<int>& setColorsRet) const
    {
        setColorsRet.clear();
        ColorsMapConstIter it;
        for (it = mapAmounts.begin(); it != mapAmounts.end(); ++it)
        {
            setColorsRet.insert(it->first);
        }
        return setColorsRet.size();
    }
    int64_t GetDefault(int nColor, int64_t nDefault) const
    {
        ColorsMapConstIter it = mapAmounts.find(nColor);
        if (it == mapAmounts.end())
        {
            return nDefault;
        }
        return it->second;
    }
    int64_t SetDefault(int nColor, int64_t nDefault)
    {
        ColorsMapConstIter it = mapAmounts.find(nColor);
        if (it == mapAmounts.end())
        {
            mapAmounts[nColor] = nDefault;
            return nDefault;
        }
        return it->second;
    }
    bool Get(int nColor, int64_t& nAmountRet) const
    {
        ColorsMapConstIter it = mapAmounts.find(nColor);
        if (it == mapAmounts.end())
        {
            nAmountRet = 0;
            return false;
        }
        nAmountRet = it->second;
        return true;
    }
    int64_t Get(int nColor) const
    {
        return GetDefault(nColor, 0);
    }
    ColorsMapConstIter Begin() const
    {
        return mapAmounts.begin();
    }
    ColorsMapConstIter End() const
    {
        return mapAmounts.end();
    }
    ColorsMapIter Find(int nColor)
    {
        return mapAmounts.find(nColor);
    }
    int64_t Add(int nColor, int64_t nAmount)
    {
        ColorsMapIter it = mapAmounts.find(nColor);
        if (it == mapAmounts.end())
        {
            mapAmounts[nColor] = nAmount;
            return nAmount;
        }
        it->second += nAmount;
        return it->second;
    }
    int64_t Add(const ColorAmount& ca)
    {
        return Add(ca.first, ca.second);
    }
    ColorsMap& Add(const ColorsMap& other)
    {
        ColorsMapConstIter it;
        for (it = other.Begin(); it != other.End(); ++it)
        {
            Add(it->first, it->second);
        }
        return *this;
    }
    void Add(const ColorsMap& other, ColorsMap& ret)
    {
        ret = *this;
        ret.Add(other);
    }
    int64_t Subtract(int nColor, int64_t nAmount)
    {
        return Add(nColor, -nAmount);
    }
    ColorsMap& Subtract(const ColorsMap& other)
    {
        ColorsMapConstIter it;
        for (it = other.Begin(); it != other.End(); ++it)
        {
            Add(it->first, -(it->second));
        }
        return *this;
    }
    void Subtract(const ColorsMap& other, ColorsMap& ret)
    {
        ret = *this;
        ret.Subtract(other);
    }
    bool AllPositive() const
    {
        if (mapAmounts.empty())
        {
           return false;
        }
        ColorsMapConstIter it;
        for (it = mapAmounts.begin(); it != mapAmounts.end(); ++it)
        {
            if (it->second <= 0)
            {
                return false;
            }
        }
        return true;
    }
    bool AllNegative() const
    {
        if (mapAmounts.empty())
        {
           return false;
        }
        ColorsMapConstIter it;
        for (it = mapAmounts.begin(); it != mapAmounts.end(); ++it)
        {
            if (it->second >= 0)
            {
                return false;
            }
        }
        return true;
    }
    bool AllZero() const
    {
        ColorsMapConstIter it;
        for (it = mapAmounts.begin(); it != mapAmounts.end(); ++it)
        {
            if (it->second != 0)
            {
                return false;
            }
        }
        return true;
    }
    std::string ToString() const
    {
        std::string strBalances = "ColorsMap(";
        ColorsMapConstIter i;
        for (i = mapAmounts.begin(); i != mapAmounts.end(); ++i)
        {
            int nColor = i->first;
            int64_t nAmount = i->second;
            if ((nAmount == 0) && (IsDeck(nColor)))
            {
                continue;
            }
            char pchBal[64];
            snprintf(pchBal,
                     sizeof(pchBal),
                     "\n  %s : %f",
                     COLOR_TICKER[nColor],
                     RealFromAmount(nAmount, nColor));
            strBalances += pchBal;
        }
        strBalances += ")";
        return strBalances;
    }
};


#endif  // BREAKOUT_COLORS_H
