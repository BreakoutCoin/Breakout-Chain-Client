# Breakout Chain Client v1.9.6.0 — Release Notes

    version:           v1.9.6.0    (previous release: v1.9.5.1)

    NO CONSENSUS CHANGE. NO PROTOCOL CHANGE. NO FORK SCHEDULE CHANGE.
    The network protocol version is unchanged at 61030.

This release fixes a node that is syncing from well behind the network rejecting the good blocks
its peers relay to it, and banning those peers for sending them. Left alone, the node works
through its peers until it has disconnected them all.

It is a **recommended upgrade for anyone syncing a node from scratch or from a long outage**, and
for anyone running a node others sync from. Both `breakoutd` and the Qt client are affected, and
both proof-of-work and proof-of-stake blocks trigger it.

A node already at the tip is not affected, and nothing on disk is damaged by the fault — a node
that hits it still syncs correctly through its normal block download, just slowly and with an
ever-shrinking set of peers.

---

## Which release do I need?

| you are | take |
|---|---|
| Bringing a new node up, or one that has been off for a while | **v1.9.6.0** |
| Running a node that other people sync from | **v1.9.6.0** |
| Seeing `Misbehaving` and `DISCONNECTING` in `debug.log` while syncing | **v1.9.6.0** |
| Running a node that stays at the tip | v1.9.6.0 is optional |

---

## The fault

While a node is catching up, its peers keep relaying it the blocks they are hearing about now — at
the tip, hundreds of thousands of blocks ahead of where the syncing node has got to. Those blocks
are orphans as far as it is concerned: it has no parent for them, and it cannot validate them
until it does. It should hold them and move on.

Instead it rejected them and charged the sender a misbehaviour score. From one node's log, at
height 501 with peers announcing 1,498,074:

```
received block 0002eaf6895ef5f3f9e5a226fc046017c705f23f8a0799720c7fe849a0da6d51
ERROR: CheckKawpowProofOfWork() : could not recompute the ProgPoW mix for this block
       (implausible height or epoch-context allocation failure)
ERROR: CheckBlock() : proof of work failed
ERROR: ProcessBlock() : CheckBlock FAILED
Misbehaving: ua7ji53...tbid.onion:11698 (5 -> 55)
```

```
received block 6b2d9cb67caf59022a8d74808cb6df5125593e104a92675aca2ed8d2b07a3398

    code: 3
ERROR: CheckBlock() : bad proof-of-stake block signature
ERROR: ProcessBlock() : CheckBlock FAILED
Misbehaving: e77lnleh...m5qd.onion:11698 (50 -> 55)
```

Over a single sync from height 0 to 25,000, that log records 109 misbehaviour events against 8
peers — 38 proof-of-work and 71 proof-of-stake, which is every misbehaviour event in the file —
and all 8 peers were disconnected, several more than once. Nothing was wrong with any of the
blocks or with any of the peers.

## Where it came from

`CBlock::CheckBlock()` carries this comment, and `ProcessBlock()` relies on it by calling
`CheckBlock()` before it shunts a parentless block into the orphan pool:

```
// These are checks that are independent of context
// that can be verified before saving an orphan block.
```

Two of its checks had stopped being independent of context, and both carried a DoS score.

**The KawPoW mix recomputation.** `BRK_FORK009` added a rebuild of a block's ProgPoW mix from the
epoch DAG, so that proof of work could not be forged at keccak cost with no DAG. Sizing that DAG
needs the block's height, which at that point is only an unvalidated wire field, so a plausibility
window was put around it to keep an attacker-chosen height from driving ethash's light-cache
sizing into undefined behaviour. The window was anchored to `nBestHeight` — to **how far this node
had synced** — rather than to where the block sits in the chain. At height 501, a good block at
1,498,074 is about 1.5 million outside a ±100,000 window, so the mix could not be recomputed, and
"could not recompute" was treated as "forged": `DoS(50)`. Two blocks, and the peer was banned.

**The proof-of-stake block signature.** From `BRK_FORK007` on, a coinstake spending a
pay-to-pubkey output has its block signature checked against a public key recovered from the
prevout transaction, which has to be read from the node's own database. For a tip block, that
transaction is in a part of the chain the syncing node has not downloaded yet, so the read fails.
`BlockSigStatus::TX_UNREADABLE` — which says only that we cannot tell — was reported as "bad
proof-of-stake block signature" and scored `DoS(5)`. Twenty blocks, and the peer was banned. The
bare `code: 3` in the log above is that status.

## The fix

Both checks now defer when the chain state they need is missing, and both are settled at the point
where that state exists.

`CheckBlock()` takes a contextual height. `ConnectBlock()` passes `pindex->nHeight`, and
`ProcessBlock()` passes `pindexPrev->nHeight + 1` whenever it already holds the block's parent —
that is, whenever the block is not an orphan. A genuine orphan has neither, and gets `-1`.

* **With** a contextual height, an implausible wire `nHeight` is still a rejection with `DoS(50)`.
  A block claiming a height it cannot have is bogus, and that verdict cannot be dodged: the
  plausibility window is now anchored to the block's real position, which is a tighter bound than
  the old one, so the crash-safety property it was added for is stronger than before.
* **Without** one, the mix recomputation is deferred rather than rejected, and the block is held
  as an orphan. It cannot join the best chain without going through `ConnectBlock()`, which always
  supplies the anchor, so no block reaches the chain with an unverified mix.
* `TX_UNREADABLE` is deferred in `CheckBlock()` and settled in `AcceptBlock()`, which runs the
  signature check immediately after `CheckProofOfStake()` has read that same prevout transaction
  off disk. Since `ConnectBlock()` checks blocks with `fCheckSig=false`, that call is what
  guarantees every block entering the block index has had its signature verified.
* A proof of work that could not be evaluated for a purely local reason — an epoch context that
  would not allocate — is now rejected without scoring the sender. Banning a peer for a block we
  were unable to check charges our fault to them.

Nothing is skipped and no rule is relaxed. The set of blocks that can join the best chain is
exactly what it was; only the point at which two of the checks are applied has moved.

---

## Verification

The two changed translation units build clean under the project's `-Wall -Wextra`, and the daemon
links.

The decision logic was exercised against the heights from the log above:

| scenario | before | after |
|---|---|---|
| syncing at 501, real tip block 1,498,074 | reject, `DoS(50)` | accept, mix deferred |
| syncing at 25,000, real tip block 1,498,074 | reject, `DoS(50)` | accept, mix deferred |
| at the tip, next block (parent known) | accept, mix compared | accept, mix compared |
| that orphan, once it reaches `ConnectBlock()` | accept, mix compared | accept, mix compared |
| forged `nHeight` 2^31−1, parent known | reject, `DoS(50)` | reject, `DoS(50)` |
| forged `nHeight` 2^31−1, held as an orphan | reject, `DoS(50)` | deferred, then rejected at `ConnectBlock()` |
| forged `nHeight` −1, parent known | reject, `DoS(50)` | reject, `DoS(50)` |
| good block, epoch context will not allocate | reject, `DoS(50)` | reject, `DoS(0)` |

Swept across the chain's full height range, a block with a contextual height always reaches a
verdict — a deferral can never become permanent. With no contextual height and a block near the
node's own tip, behaviour is identical to the previous release.

**Client version.** Now **1.9.6.0**, composing to `CLIENT_VERSION` 1090600. The network protocol
version is unchanged at **61030**.

---

## Known issues

None new. The v1.9.5.1 known issue is unchanged: a long RPC request still delays exit, because
`StopNode()`'s last step waits without a bound for RPC handlers and the message handler to finish.

---

## For maintainers

**A check that reads chain state does not belong in `CheckBlock()`.** The contract in that
function's comment is load-bearing — `ProcessBlock()` calls it before the orphan shunt precisely
so that a parentless block can be held without being judged. Anything added there that consults
`nBestHeight`, the block index or the transaction database will be wrong for exactly the blocks
orphan handling exists to hold, and if it carries a DoS score it will ban the peers that send
them. `CheckTransaction()`, the timestamp checks and the merkle and sigop checks were re-read and
are genuinely context-free.

**"Cannot verify" is not "invalid".** Both halves of this fault were a check reporting that it
lacked the information to reach a verdict, and a caller reading that as a verdict against the
block and its sender. Where a check can fail for a local reason — a database read, an allocation —
the failure needs to be distinguishable from a real rejection before it reaches a misbehaviour
score.
