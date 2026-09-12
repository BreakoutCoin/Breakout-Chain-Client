# Breakout Explore — RPC API Manual

The Breakout Explore RPCs query a per-`(address, color)` index of the chain. They are
read-only and return JSON. For architecture and status, see
`BREAKOUT_EXPLORE_SUMMARY.md`.

---

## Getting started

**Enable the index.** Start the node with the explore API turned on:

```
exploreapi=1        # in breakout.conf, or -exploreapi=1 on the command line
```

On first enable (or after a chain change / `-reindexexplore`) the index builds automatically;
thereafter it opens in sync and is ready immediately. All commands below error with
`** ERROR: Explore API only **` if the index is not enabled.

**Invoke** with the daemon acting as its own client:

```
breakoutd <command> [args...]
```

### Conventions used throughout

- **Colors.** Breakout is multi-currency. A *color* is an integer `1‥57`; amounts in different
  colors are not fungible. Commands that can't infer the color from an address take a color
  index. Common colors:

  | Color | Ticker | | Color | Ticker |
  |------:|:-------|-|------:|:-------|
  | 1 | BRX | | 3 | BAM |
  | 2 | BRK | | 57 | SIS |

  Colors `4‥56` are the deck currencies (`DJK`, then the 52 cards `DAS`…`DKH`). The full table
  is `COLOR_TICKER` in `colors.h`.

- **Addresses encode their color.** A Breakout address decodes to both a pubkey-hash and a
  color, so address commands recover the color automatically — no color argument is needed.

- **Amounts** are JSON numbers in the color's display units (e.g. `1.97000000`). Balances,
  `received`, and `sent` are always per color.

- **Range pagination** — `[start] [max]`: return up to `max` records beginning at the
  `start`-th (1-based). Defaults: `start = 1`, `max = 100`.

- **Page pagination** (`*pg` commands) — `<page> <perpage> [ordering]`: return page `page`
  of `perpage` records. `ordering` is a boolean (default `true` = forward / chronological;
  `false` = reverse). These return an envelope:

  ```json
  { "total": <int>, "page": <int>, "per_page": <int>, "last_page": <int>, "data": [ ... ] }
  ```

---

## Shared record shapes

Several commands return **input**, **output**, or **transaction** records with a common shape.

**Input record**
```json
{
  "txid": "…",          "height": 1415300,   "vtx": 1,      "vin": 0,
  "address": "bx…",     "amount": 1.00000000,"balance": 4.00000000,
  "blockhash": "…",     "confirmations": 42, "blocktime": 1784008580,
  "prev_txid": "…",     "prev_vout": 3
}
```

**Output record**
```json
{
  "txid": "…",          "height": 1415389,   "vtx": 1,      "vout": 1,
  "address": "bx…",     "amount": 1.85000000,"balance": 1.85000000,
  "blockhash": "…",     "confirmations": 18, "blocktime": 1784008580,
  "isspent": "false"                          // "true" adds "next_txid" + "next_in"
}
```

**UTXO record** — identical to an output record but **without** `isspent` (a UTXO is always
unspent).

---

## Address commands

### `getaddressbalance <address>`
Balance of the address, as a formatted string.
```
$ breakoutd getaddressbalance bxPwEo5PJf3hHv8qMhafwqeBxaR9zDgVeA6
1.97000000 BRX
```

### `getaddressinfo <address>`
Summary object for the address.
```json
{
  "address": "bxPwEo5P…", "color": 1, "balance": 1.97000000, "rank": 0,
  "transactions": 2, "outputs": 2, "received": 1.97000000,
  "inputs": 0, "sent": 0.00000000, "unspent": 2, "in-outs": 2,
  "blocks": 1415406
}
```

### `getaddressinputs <address> [start] [max]`
Range-paginated list of the address's **input** records.

### `getaddressoutputs <address> [start] [max]`
Range-paginated list of the address's **output** records (includes `isspent`).

### `getaddressutxos <address> [start] [max]`
Range-paginated list of the address's **unspent** outputs (UTXO records; no `isspent`). This is
`getaddressoutputs` with spent outputs filtered out.

### `getaddressutxospg <address> <page> <perpage> [ordering]`
Page-paginated UTXOs (envelope + `data` of UTXO records).

### `getaddressinouts <address> [start] [max]`
Range-paginated list of the address's inputs **and** outputs, interleaved by chain position.

### `getaddressinoutspg <address> <page> <perpage> [ordering]`
Page-paginated in-outs.

### `getaddresstxspg <address> <page> <perpage> [ordering]`
Page-paginated **transactions** touching the address (each entry is a transaction rollup).

---

## Rich-list commands

Rich-list data comes from an in-memory map keyed by balance. It is filled by the
replay during a (re)index and, on a start that skips the replay, loaded from the
on-disk balance sets — so it is complete from the first request either way.

Balances at or below one cent of a currency are not tracked, so those addresses
appear in no rich-list answer and are not counted by `getrichlistsize`.

### `getrichlistsize <color> [minbalance]`
Number of `<color>` addresses with a balance greater than `minbalance` (default: one cent of
the color). Returns an integer.

### `getrichlist <color> [start] [max]`
Richest `<color>` addresses from rank `[start]`, as an object mapping `address → balance`.

Addresses tied with the last rank returned are included as well, the way a leader
board shares a place, so **more than `[max]` addresses can come back**: ask for the
top 20 while 30 addresses are tied at 20th and all 49 are returned. Use
`getrichlistpg` when you need exact counts.

### `getrichlistpg <color> <page> <perpage> [ordering]`
Page-paginated rich list. Returns `total`, `page`, `per_page`, `last_page` and
`data`, the last being an object mapping `address → balance`.

Pages tile exactly: a tie crossing a page boundary is split between the two pages
rather than repeated on both, so paging straight through yields each address once
and ranks run `1 .. total`. `[ordering]` defaults to true, meaning descending.

A page past the end is an error, not an empty result.

---

## Block commands

### `getbestblock [txinfo]`
The block at the tip, in one call — the same object `getblockbynumber` returns,
for `hashBestChain`. Saves a `getblockcount` round trip, and cannot straddle a
block change the way the two-call form can. `[txinfo]` expands the `tx` array
into full transactions rather than txids.

The per-currency `moneysupply` and `totalmint` maps are carried on this object,
so it is the cheapest way to read current supply.

---

## Movement commands

### `getmovementspg <page> <perpage> [ordering] [mincoins] [color]`
Page-paginated index of transactions that moved value between parties.

A transaction is indexed when, summing by recipient, some address that none of
the inputs came from receives at least the floor for that currency. That excludes
proof-of-stake self-returns, which are the great majority of large outputs, and
it measures what actually reached a party rather than the largest output in the
transaction.

The floors are a property of the index, not of the query. `[mincoins]` is an
additional cutoff applied on top of them and defaults to 0; asking below a floor
is refused rather than silently under-reporting. The response reports the floors
in force under `floors`, so callers need not keep their own copy.

Currencies with a floor of 0 are untracked — that includes the Deck, whose cards
have a supply of 1 each and their own provenance views (`getcardinfo`).

`[ordering]` here is by blockchain position and defaults to true meaning
*forward*, unlike the rich-list calls. `[color]` restricts to one currency; 0 or
omitted means all of them.

---

## Block-statistics commands

All take a sliding-window specification and return one entry per window
(`window_start`, `number_blocks`, and the metric):

```
<period> <windowsize> <windowspacing>       (all durations in seconds)
  - <period>       : total duration to cover, back from the most recent block
  - <windowsize>   : duration of each window
  - <windowspacing>: spacing between the start of consecutive windows
```

| Command | Per-window metric |
|---|---|
| `gettxvolume <period> <windowsize> <windowspacing>` | `tx_volume` — number of transactions |
| `getblockinterval <period> <windowsize> <windowspacing>` | `block_interval` — total interval (sec) |
| `getblockintervalmean <period> <windowsize> <windowspacing>` | `block_interval_mean` (sec) |
| `getblockintervalrmsd <period> <windowsize> <windowspacing>` | `block_interval_rmsd` (sec) |

---

## HD-account commands

These run **consolidated queries against a supplied HD extended public key (xpub)** — the node
has no HD wallet of its own. The xpub is a BIP44 **account** node (`m/44'/coin'/account'`); the
commands derive its external (`…/0/i`) and change (`…/1/i`) addresses and aggregate over them,
applying the standard gap limit (scanning stops at the first unused child).

Because a derived pubkey-hash is color-agnostic, most commands take an **optional trailing
`[color]`**:
- **omitted** → aggregate across *all* colors;
- **supplied** → scope to that one color.

Caps on the scan are configurable: `-maxhdchildren` (1024), `-maxhdinouts` (65535),
`-maxhdtxs` (16383).

### `getchildkey <extended key> <child> [color]`
Derive one child of an extended key. `[color]` defaults to the node's default currency (BRX).
```json
{
  "extended": "xpub…",
  "pubkey":   "035bcc0d…",
  "color":    1,
  "address":  "bxH6TyL4Mt51Q9EyQQsBLyPvJvN3MVyyxwU"
}
```
Deriving an address at `account/0/i`: `getchildkey <account_xpub> 0` gives the external-branch
xpub; `getchildkey <that> <i> [color]` gives the leaf address.

### `gethdaddresses <extended key> [color]`
All known addresses of the account, split into `external` and `change`. Each child lists the
colors it has activity in (with per-color address and in-out count); an empty `addresses`
array marks the gap-limit terminator.
```json
{
  "external": [
    { "child": 0, "pubkey": "035bcc0d…", "addresses": [
        { "color": 1,  "address": "bxH6TyL4…", "inouts": 2 },
        { "color": 2,  "address": "breN2Bot…", "inouts": 2 },
        { "color": 57, "address": "cNRNf3q…",  "inouts": 2 } ] },
    { "child": 1, "pubkey": "03b147a8…", "addresses": [] }
  ],
  "change": [ … ]
}
```

### `gethdaccountbalance <extended key> [color]`
Account balance. With `[color]`, a formatted string; without, an object keyed by ticker.
```
$ breakoutd gethdaccountbalance <xpub> 2
0.00000000 BRK

$ breakoutd gethdaccountbalance <xpub>
{ "BRX": 0.00000000, "BRK": 0.00000000, "SIS": 0.00000000 }
```

### `gethdaccountinfo <extended key> [color]`
Aggregated account summary: counts of used external/change addresses, plus per-color balance /
received / sent / in-out and transaction counts. Without `[color]` the per-color rows are in a
`colors` array; with `[color]` a single `account` object is returned.
```json
{
  "external_addresses": 1, "change_addresses": 1, "blocks": 1415406,
  "colors": [
    { "color": 1, "balance": 0.0, "transactions": 4, "outputs": 2,
      "received": 2.0, "inputs": 2, "sent": 2.0, "unspent": 0, "in-outs": 4 },
    { "color": 2, … }, { "color": 57, … }
  ]
}
```

### `gethdaccountinputs <extended key> [start] [max] [color]`
Range-paginated **input** records across the account's addresses, ordered by chain position.

### `gethdaccountoutputs <extended key> [start] [max] [color]`
Range-paginated **output** records across the account's addresses.

### `gethdaccountutxos <extended key> [start] [max] [color]`
Range-paginated **unspent** outputs across the account (UTXO records; no `isspent`).

### `gethdaccountutxospg <extended key> <page> <perpage> [ordering] [color]`
Page-paginated account UTXOs.

### `gethdaccountinouts <extended key> [start] [max] [color]`
Range-paginated **consolidated transactions**. Each entry rolls up *every* input and output of
the account that touches one transaction, with the net change reported **per color**:
```json
{
  "txid": "7a3ac7ca…",
  "account_balance_change": [ { "color": 1, "change": 1.00000000 } ],
  "inputs":  [ … ],
  "outputs": [ … ],
  "payees":  [ … ],
  "txinfo":  { "blockhash": "…", "height": …, "confirmations": …, "vtx": …,
               "sources": [ … ], "destinations": [ … ] }
}
```

### `gethdaccountinoutspg <extended key> <page> <perpage> [ordering] [color]`
Page-paginated consolidated transactions (envelope + `data` of the objects above).

---

## Deriving a fundable address (worked example)

To watch an HD account, derive its addresses from the account xpub and (optionally) fund them.
The address a command will scan for `account/0/0` in BRX is:

```
$ breakoutd getchildkey <account_xpub> 0        # -> external branch xpub  (EXT)
$ breakoutd getchildkey <EXT> 0 1               # -> BRX address at account/0/0
```

The same leaf pubkey yields a different address per color (`… <EXT> 0 2` for BRK, `… 57` for
SIS). A wallet tool that exports the matching WIF private keys (BIP44 `m/44'/coin'/account'`,
with Breakout's WIF network byte `147`) can then import them to spend.
