# Breakout Chain cryptocurrency [BRK/BRX/SIS/BAM/Deck].

## Overview

Breakout Chain is a peer-to-peer decentralized cryptocurrency that employs two types of blockchain security. The first is Proof-of-Work, where miners race to solve computationally difficult mathematical puzzles. The winner for each block of transactions is allowed to certify the block for the network and claim the signing fee. The second type of security is Proof-of-Stake, where stakers participate in a lottery to win the privilege to certify the block. A staker's chance of winning is proportional to how much stake they have.

In Breakout Chain, Proof-of-Work miners are awarded Sistercoin. Stakers, on the other hand, are awarded in two currencies, Breakout Coin and Sister Coin. Stakers who are awarded Breakout Coin prove ownership of a currency called Breakout Stake. Stakers who are awarded Sister Coin prove ownership of one of 53 playing cards, each card being a currency in its own right. The deck consists of a Joker and the other 52 cards of a standard deck.

The deck is essentially 53 colored coins with unique identities. Their identities are unambiguous in that their ordering is cemented into the genesis block as salt. The salt is a hash calculated by the following python script

    >>> # python
    >>> import hashlib
    >>> names = ", ".join(["<none>", "Brostake", "Brocoin", "Atomic",
                 "Joker",
                 "Ace of Spades", "Two of Spades", "Three of Spades", "Four of Spades",
                 "Five of Spades", "Six of Spades", "Seven of Spades", "Eight of Spades",
                 "Nine of Spades", "Ten of Spades", "Jack of Spades", "Queen of Spades", "King of Spades",
                 "Ace of Diamonds", "Two of Diamonds", "Three of Diamonds", "Four of Diamonds",
                 "Five of Diamonds", "Six of Diamonds", "Seven of Diamonds", "Eight of Diamonds",
                 "Nine of Diamonds", "Ten of Diamonds", "Jack of Diamonds", "Queen of Diamonds", "King of Diamonds",
                 "Ace of Clubs", "Two of Clubs", "Three of Clubs", "Four of Clubs",
                 "Five of Clubs", "Six of Clubs", "Seven of Clubs", "Eight of Clubs",
                 "Nine of Clubs", "Ten of Clubs", "Jack of Clubs", "Queen of Clubs", "King of Clubs",
                 "Ace of Hearts", "Two of Hearts", "Three of Hearts", "Four of Hearts",
                 "Five of Hearts", "Six of Hearts", "Seven of Hearts", "Eight of Hearts",
                 "Nine of Hearts", "Ten of Hearts", "Jack of Hearts", "Queen of Hearts", "King of Hearts"])
    >>> hashlib.sha256(names).hexdigest()
    'f4be9677f3caaa8a9a1f9e58a0f9a80dd9fd9f224455714c414f4963848e0b9b'


## Chain Stats

### General

* Block Times: 5 Minutes
* Security: Proof of Work + Proof of Stake v2
* Currencies: Breakout Coin, Breakout Stake, Sister Coin, Atomic, The Deck
* Coinbase Maturity: 240 Blocks (20 hr)


### Breakout Coin

* Ticker: BRK
* Supply: 19.5 M Upon Genesis of the Chain
* Inflation: 5% per year (compounded per block)
* Starting Block Reward: 10 BRK
* Min Transaction Fee: 0.01 BRK
* Retargeting: Breakout Gravity Wave


### Breakout Stake

* Ticker: BRX
* Supply: 12.5 M Fixed Supply
* Inflation: None
* Min Transaction Fee: 0.01 BRX
* Stakes: Breakout Coin
* Stake Minimum Confirmations: 3456 (12.3 Days)


### Sister Coin

* Ticker: SIS
* Max Supply: 21 M
* Starting PoW Reward: 50 SIS
* Inflation: Halving Every 420,000 Blocks (4 Years)
* Min Transaction Fee: 0.01 SIS


### Atomic

* Ticker: BAM
* Supply: 10 Billion BAM
* Inflation: None
* Min Transaction Fee: 1 BAM


### Deck

* Tickers: Follow the pattern D (for deck), value, suit.
  Examples: DAS = Ace of Spades, D2H = Two of Hearts
* Supply: 53 (Standard 52 Card Deck + Joker)
* Inflation: None
* Min Transaction Fee: 0.01 SIS
* Stakes: Sister Coin
* Stake Minimum Confirmations: 13,824 (49.2 Days)

The Deck mints SIS according to the card value, with higher cards minting slightly more SIS than lower cards. The value depends also on the block height in that the PoS reward is just over twice the hypothetical PoW reward at the block height. For example, if the hypothetical PoW at the block height were 50 SIS (as it would be for blocks numbered less than 420,000), then the Deck would mint according to the following list:

     Twos:     102 SIS
     Threes:   103 SIS
     Fours:    104 SIS
     ...
     Jacks:    111 SIS
     Queens:   112 SIS
     Kings:    113 SIS
     Aces:     114 SIS
     Joker:    114 SIS

Because the block reward for staking the Deck is based on the hypothetical PoW block reward at the same height, Deck cards will always mint about the same fraction (1.52%) of the total SIS output over a given duration. This number is comes from the fact that each card is expected to mint about once every 13,824 blocks, mints twice as much SIS as PoW blocks, and there are 53 cards in the deck:

     fraction ~ 2 * 53 / (13,824 + 53)
