# Breakout Chain Client v1.9.1.0 — Release Notes

    version:           v1.9.1.0    (previous release: v1.8.0.0)
    base commit:       5d6bcaecb3d1f97a02d79ddd04ee6ea8b6166cf1
    release date:      Tue Sep 1 2026   (source-only release)

    FORK ACTIVATION:   Sun Sep  6 02:00:00 2026 UTC   (epoch 1788660000)
                       BRK_FORK008, BRK_FORK009 and BRK_FORK010 all activate
                       at that instant. THE EPOCH IS AUTHORITATIVE; block
                       timestamps are UTC and are compared against it directly.

This release restores block checkpoints, adds a local-correctness fix to the legacy
proof-of-work path, adds per-card provenance tracking to the Explore API, and **schedules three
consensus gates — `BRK_FORK008`, `BRK_FORK009` and `BRK_FORK010` — to activate on Sun Sep 6
02:00:00 2026 UTC**.

> **On v1.9.0.0.** The tag `v1.9.0.0` was created on Sat Aug 29 2026 and superseded by v1.9.1.0
> within days, before any binaries were published for it. It is a tag, not a release — see
> [Releases and tags](../../README.md#releases) — and nobody needs to install it. Everything it
> scheduled ships unchanged in v1.9.1.0, and these notes carry its content forward in full, so
> this is the only document you need for the fork.

> # ⚠ EVERY NODE MUST BE UPGRADED BEFORE Sun Sep 6 02:00:00 2026 UTC
>
> This is not "upgrade when convenient." A node still running v1.8.0.0 past that instant will
> **partition off the network**, not degrade gracefully. See *Why the deadline is hard* below.
>
> **If you cannot upgrade in time, stop your node** rather than let it run past the activation.

---

---

## Which release do I need?

**v1.9.1.0.** It is the release that satisfies the fork, and the one the signed executables are
built from. There is no other current option: v1.8.0.0 partitions off the network at activation,
and v1.9.0.0 was never released.

---

## ⚠ READ THIS FIRST — checkpoints are enforced by a hard reject

This release re-enables block checkpoints, which have been inactive for years, and extends them
to within a few thousand blocks of the current tip. **142 checkpoints are now active.**

**A block at a checkpointed height whose hash does not match is rejected outright, and the peer
supplying it is penalised with a ban score of 100 — the default `-banscore` threshold, so a single
offending block disconnects that peer.** There is no advisory mode, no warning-only setting, and no
way to soften it short of not running this release.

**When this can affect you:**

| situation | affected? |
|---|---|
| Already-synced node, upgrading in place | **No.** Checkpoints are checked in `AcceptBlock()`; loading an existing block index does not re-run them. |
| New node syncing from scratch | **Yes.** Every checkpointed height is enforced as it is passed. |
| Importing blocks with `-loadblock=` or an auto-imported `bootstrap.dat` | **Yes.** Same path. |

**If your node is on a chain that diverges from the checkpoints, it will not sync past the first
mismatch.** That is the intended behaviour — it is what stops a node following a wrong chain — but
it fails loudly and immediately rather than degrading gracefully. If you have been running on a
private, forked, or experimental chain, do not upgrade that node without a fresh data directory.

---


---

## Behaviour changes you will notice

**Initial sync is substantially faster.** The client skips per-transaction ECDSA signature
verification for blocks below the highest checkpoint during initial block download. This is the
long-standing Bitcoin-lineage optimisation: each block is transitively secured into the next
through proof-of-work/proof-of-stake, and the checkpoint pins the exact hash at its own height.
With only the genesis checkpoint active, this optimisation was effectively switched off; restoring
the checkpoint table switches it on for essentially the whole chain.

**Staking is suspended until your node has synced past height 1,475,000.** The highest checkpoint
is used as the "initial block download complete" threshold, and the staking loop does not run while
a node considers itself to be in initial block download. A node that is already synced is unaffected.
A node syncing from scratch will not stake until it passes that height — which is correct, but is a
change from the previous behaviour where the threshold was effectively zero.

**Nothing else changes for an already-synced node on the main chain.** No consensus rule that
affects block validity on the current chain has been altered by this release.

---


---

## What's new in the Explore API

**Per-card provenance tracking (`ExploreCardInfo` / `ExploreCardTransfer`).** The Explore API now
records each card's (deck NFT's) full holder history in the exploredb, keyed by color: the mint
(coinbase) followed by one entry per later change of holder. A card's total circulation is exactly
one indivisible unit, so its history is an unbroken chain from mint to current holder. Plain
same-address staking of a card is not ownership-changing and is not recorded as a transfer; it is
counted in `ExploreCardInfo::stakes` instead.

This is purely additive to the Explore RPC surface — no existing Explore data, RPC method, or
reply field is changed or removed. **It changes no consensus rule, network protocol requirement,
or fork schedule.**

---

## What activates on Sun Sep 6 02:00:00 2026 UTC

| gate | change |
|---|---|
| `BRK_FORK008` | **PoS kernel hardening** — a ceiling on the weighted kernel target so staking cannot become deterministic, and a change of the stake-timestamp mask from 17 to 3 |
| `BRK_FORK009` | **KawPoW mix-hash verification** against the DAG |
| `BRK_FORK010` | **KawPoW version-dispatch enforcement** — a proof-of-work block in the KawPoW era must declare the KawPoW version rather than silently falling back to the legacy validation path |

All three activate at the same instant: one activation, one upgrade window.

**Until that moment, this release changes no consensus rule affecting block validity.** An upgraded
node and a v1.8.0.0 node agree completely right up to the activation timestamp, and diverge
immediately after it.

---


---

## Peers: un-upgraded nodes will disconnect at activation

The network protocol version is raised to **61030** in this release, and from the activation instant
a node requires its peers to advertise at least that. Only v1.9.x builds do.

**So at Sun Sep 6 02:00:00 UTC, upgraded nodes stop accepting connections from un-upgraded ones.**

This is deliberate, and it is the friendlier of the two possible outcomes. Without it, a straggler
and the upgraded network would sit exchanging blocks each considers invalid and ban each other
within two blocks (see below). With it, the straggler simply loses its peers.

**What you will see:**

* **If you have upgraded:** un-upgraded peers drop off your connection list at activation. Expected.
  Your peer count may dip until the rest of the network finishes upgrading.
* **If you have not upgraded:** your node loses peers and stops making progress shortly after
  activation. It is not broken and its data is not damaged — it is running a protocol the network no
  longer accepts. Upgrade and restart, and it will reconnect and resume.

**A node that is stopped before activation and upgraded later is fine.** There is no state to
repair, and nothing expires. If you are going to miss the deadline, stopping the node is the clean
option.

---


---

## Why the deadline is hard

The stake-timestamp mask change is **lateral, not a tightening.** After activation some block
timestamps that were previously valid become invalid — **and some that were previously invalid
become valid.** Concretely, on mainnet the mask goes from 17 to 3: timestamp residues
{16, 20, 24, 28} mod 32 become newly **valid**, and {2, 6, 10, 14} become newly **invalid**. The
number of usable slots is unchanged; which slots they are is not.

A node that has not upgraded therefore diverges **in both directions at once**:

* it **rejects** roughly half of the upgraded network's proof-of-stake blocks, and
* it **produces** blocks the upgraded network rejects.

Both rejections carry a ban score of 50 against a default `-banscore` of 100, so **two blocks are
enough for nodes on either side to ban each other.** The result for a straggler is a clean partition
along the upgrade line — not a slow drift, not degraded service, not something that heals on its own.

**This is why the deadline is not negotiable, and why stopping an un-upgraded node is better than
leaving it running.**

---


---

## Timeline

| when (UTC) | what |
|---|---|
| Sat Aug 29 16:00 | `v1.9.0.0` tagged — superseded before any binaries were published |
| Tue Sep 1 | **v1.9.1.0** — this release, source |
| Sep 1–2 | **v1.9.1.0 signed executables** published |
| **Sun Sep 6 02:00** | **FORK ACTIVATION — all nodes must already be upgraded** |

That leaves roughly 4–5 days from the signed executables to activation.

---

## Full change list

**Checkpoints restored and extended (G12).** 90 previously-disabled checkpoints restored
(heights 13,300–960,000); 51 new entries filling 970,000–1,470,000 at the existing 10,000-block
interval; one current entry at 1,475,000. **142 active checkpoints**, contiguous to within 10,000
blocks. This closes a 515,012-block gap that spanned both the extended network outage and the
KawPoW migration.

**Legacy proof-of-work target ceiling (G19).** Removed a process-lifetime cache of the target
ceiling in `CheckSHA256ProofOfWork()`. The cached value was computed on first call from state that
is only guaranteed to be final by an unenforced initialisation ordering. **No value changes for any
state the daemon can reach**; this removes an order dependence, not a defect in current behaviour.
Also corrects a misspelled function name in one error message.

**PoS kernel hardening (`BRK_FORK008`).** A ceiling on the weighted kernel target so that staking
cannot become deterministic, and a change of the stake-timestamp mask from 17 to 3. **Scheduled for
Sun Sep 6 02:00:00 2026 UTC.**

**Explore API: per-card provenance.** Each card's full holder history is now tracked in the
exploredb — see *What's new in the Explore API* above. Purely additive; no consensus or protocol
effect.

**Client and network protocol version.** Client version is now **1.9.1.0**; the network protocol
version is raised from 61020 to **61030**. `GetMinPeerProtoVersion()` requires peers to advertise at
least 61030 once `BRK_FORK008` is active — see *Peers* above.

**KawPoW hardening (`BRK_FORK009` / `BRK_FORK010`).** Mix-hash verification against the DAG, and
enforcement that a proof-of-work block in the KawPoW era declares the KawPoW version rather than
silently falling back to the legacy validation path. **Scheduled for the same instant** — the two are
sequenced together deliberately, because activating version-dispatch enforcement without mix-hash
verification would provide no protection against the defect the latter fixes.

**KawPoW hash-cache concurrency and activation-order guard.** Local-safety changes, active
immediately, with no effect on block validity.

---


---

## How the checkpoint hashes were verified

Because a checkpoint is a consensus parameter, a wrong hash would make every node running this
release reject the real chain at that height. The hashes were therefore treated as the highest-risk
part of the release:

* **Every hash was taken from a fully-synced node's own block index**, walked main-chain-only from
  the tip back to genesis. **No hash came from a block explorer**, at any point.
* The 90 restored hashes were **re-verified against that walk before being re-enabled**: 90 of 90
  matched, zero mismatches. A commented-out checkpoint is an unverified claim; re-enabling one makes
  it a consensus rule.
* The 52 new entries were **emitted by** that same walk, so each is main-chain by construction.
* **99 of the 142 were then independently confirmed against a separately-synced node** running the
  real consensus code — including **all 52 new entries and the current entry at 1,475,000**. Zero
  mismatches. A read-only explorer independently agreed on all 142, though that source was
  deliberately not treated as authoritative.
* Two independent cold re-audits reviewed the change. Both found **no wrong hash by any method**.

**Known limitation, stated plainly:** 43 of the restored checkpoints (in the 410,000–910,000 range)
were observed passing validation on a live node, but the log evidence was lost to automatic log
rotation before it could be preserved, so they are recorded as credible rather than independently
substantiated. They are the lowest-risk group — restored entries with more than four years of live
mainnet exposure, and agreement from every other source consulted.

---


---

## For maintainers

**Refresh the final checkpoint every release.** A checkpoint bounds a chain rewrite only back to its
own height, and only for nodes that actually run the release carrying it. Its value decays with every
day it is not refreshed. Take the new hash from a synced node's own index — never from an explorer.

**Do not make the legacy target ceiling era-aware.** Before `FORK_010_TIME` passes, a block
declaring a legacy version reaches the legacy proof-of-work path regardless of its own timestamp, and
the tight pre-KawPoW ceiling is the only thing rejecting it. The KawPoW-era ceiling is 2^14 times
looser; using it there would accept blocks the current code rejects. After activation, version
dispatch enforces the same outcome by a different route — but the ceiling remains the correct
backstop, and nodes syncing historical chain still traverse the pre-activation era.

**The activation constant is an epoch, and the epoch is authoritative.** `FORK_008_TIME`,
`FORK_009_TIME` and `FORK_010_TIME` are all `1788660000` = Sun Sep 6 02:00:00 2026 UTC. Block
timestamps are UTC and are compared against this value directly. When announcing an activation, quote
the epoch alongside any human-readable time — a local-time string mistaken for UTC is a seven-hour
error that no code will catch, and it would move the deadline operators are working to.

