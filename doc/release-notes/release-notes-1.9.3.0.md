# Breakout Chain Client v1.9.3.0 — Release Notes

    version:           v1.9.3.0    (previous release: v1.9.2.0)
    release date:      Sun Sep 6 2026

    NO CONSENSUS CHANGE. NO PROTOCOL CHANGE. NO FORK SCHEDULE CHANGE.
    The network protocol version is unchanged at 61030, and the
    BRK_FORK008 / BRK_FORK009 / BRK_FORK010 activation of
    Sun Sep 6 02:00:00 2026 UTC (epoch 1788660000) is untouched.

This release fixes a startup crash that a node hits when it upgrades **after** a consensus change
has already activated. It is an **urgent upgrade for anyone who did not upgrade before the
Sun Sep 6 02:00:00 UTC activation**, and it changes nothing at all for anyone else.

> **This release satisfies the fork.** It carries the v1.9.1.0 fork content unchanged — see the
> [v1.9.1.0 notes](release-notes-1.9.1.0.md) for what activates and why the deadline is hard.
> Nothing here alters any consensus rule, and nothing here changes the schedule.

---

## Which release do I need?

| you are | take |
|---|---|
| Still on a pre-fork client, upgrading now that the fork has activated | **v1.9.3.0** |
| On a client that crashes on startup, before the window appears | **v1.9.3.0** |
| Already upgraded before Sun Sep 6 02:00 UTC and running normally | v1.9.3.0 is optional; nothing you can reach has changed |

---

## The fix: the client crashed on startup if startup had to reorganize

**Symptom.** The client dies on startup. There is no window, no error dialog and no RPC — it
segfaults during initialization, before the wallet has even been opened. `debug.log` ends shortly
after a line of the form:

```
LoadBlockIndex() : *** found bad block at <height>, hash=<hash>
LoadBlockIndex() : *** moving best chain pointer back to block <height>
REORGANIZE
```

Restarting does not help: the stored chain is unchanged, so the same reorganization is attempted
and the same crash follows.

**Who hit it.** Anyone whose stored chain contains blocks that their *newly upgraded* client
rejects. In ordinary operation that does not happen — blocks were validated by the same rules
before they were stored. A consensus change is precisely the event that makes previously stored
blocks invalid, so the population that hit this is the one that upgraded late: nodes that stayed
on a pre-fork client past Sun Sep 6 02:00 UTC, accumulated blocks on the pre-fork chain, and then
upgraded. Their first start on the new client had to roll the tip back to the fork point, and that
is the path that crashed.

**Cause.** A null pointer dereference, from an ordering problem between two parts of startup.

`CTxDB::LoadBlockIndex()` verifies the last `-checkblocks` blocks (default 2500) at `-checklevel`
1 (the default) as the index loads. A block that fails `CheckBlock()` sets `pindexFork` to its
predecessor, and at the end of the load the client calls `SetBestChain(txdb, pindexFork)` to move
the best-chain pointer back to it — which runs `Reorganize()`.

`Reorganize()` maintains the wallet's per-color balance totals as it goes, computing deltas for
each transaction as blocks are disconnected and reconnected. To do that it takes
`LOCK(pwalletMain->cs_wallet)`.

But `AppInit2()` calls `LoadBlockIndex()` (`src/init.cpp:1343`) fifty lines before it constructs
the wallet (`pwalletMain = new CWallet(...)`, `src/init.cpp:1393`). On this path `pwalletMain` is
still `NULL`, and the lock dereferences it. Every other caller of `Reorganize()` runs later, with
a wallet in hand, which is why the fault only ever appeared at startup.

**Introduced in** commit c780234 (the KawPoW / Tor 0.4.8.12 / OpenSSL 3.6 / Qt6 / C++17
modernisation, v1.9.0.0's base), which added the incremental balance accounting to `Reorganize()`.
**Present in every 1.9.x release**, including every published binary. It was never reachable in
normal operation, which is why it survived undetected until a scheduled fork produced the
conditions for it.

**Fixed by** guarding the three wallet-balance sections of `Reorganize()` with `if (pwalletMain)`.

**No balances are lost by skipping that accounting.** The incremental deltas are an optimisation,
not the source of truth: `AppInit2()` calls `pwalletMain->FillSnapshot()` unconditionally once the
wallet is open (`src/init.cpp:1611`), which recomputes `mapConfirmed`, `mapStake`, `mapCoinbase`,
`mapReceived` and `mapSent` from the wallet's transactions on every single startup. A reorg that
happens before the wallet exists has nothing to update, and the totals the wallet reports are
built after it, from scratch.

**Nothing else in `Reorganize()` changed.** Block disconnection and reconnection, the transaction
database writes, `WriteHashBestChain`, the `TxnCommit`, the `pprev`/`pnext` and `mapBlockLookup`
fixups, and the mempool resurrect/delete handling all sit outside the guard and run exactly as
before. With a wallet present, the function does what it always did, statement for statement.

---

## Full change list

**`src/main.cpp`, `Reorganize()`.** The three blocks that compute and apply wallet balance deltas
are now guarded by `if (pwalletMain)`. This is the whole of the behavioural change in this
release. Consensus, chain selection, storage and mempool behaviour are untouched, and the guarded
code itself is unmodified — with a wallet loaded, the execution path is identical to v1.9.2.2's.

The same commit rewraps the guarded region to the project's 79-column limit and reflows the
comments inside it. Those are whitespace-only; a token-level comparison against v1.9.2.2 shows
the three added guards and nothing else.

**Client version.** Now **1.9.3.0**, composing to `CLIENT_VERSION` 1090300. The network protocol
version is unchanged at **61030**; this release adds no protocol behaviour and does not affect
peer compatibility or the `GetMinPeerProtoVersion()` requirement.

---

## Known issues

The shutdown abort described in the [v1.9.2.0 notes](release-notes-1.9.2.0.md#known-issues) has
not been attributed and is unchanged by this release.

---

## For maintainers

**`pwalletMain` is `NULL` for the whole of step 5.** `LoadBlockIndex()` runs at
`src/init.cpp:1343`; the wallet is constructed at `src/init.cpp:1393`. Anything reachable from
block-index loading — including `SetBestChain()` and `Reorganize()`, via the `pindexFork` rollback
at the end of `CTxDB::LoadBlockIndex()` — must not assume a wallet exists.

**Two sibling sites in `SetBestChain()` are still unguarded.** The `hashPrevBlock == hashBestChain`
fast path and the "Connect further blocks" loop that follows `Reorganize()` both dereference
`pwalletMain` without a check. Neither is reachable from the `LoadBlockIndex()` rollback: that call
passes an ancestor of `pindexBest`, so the fast path cannot trigger, and `vpindexSecondary` is
necessarily empty because every ancestor of the fork point has less chain trust than the current
best. They are latent, not live, and were left alone to keep this release's diff to the crash
itself. They should be guarded when the release pressure is off.

**A scheduled fork is a test of the late-upgrade path.** The bug was in a code path that only a
consensus change reaches, and it reached it in the worst possible population: users who were
already behind. Worth exercising deliberately before the next activation — take a node past a
fork point on old rules, then upgrade it.
