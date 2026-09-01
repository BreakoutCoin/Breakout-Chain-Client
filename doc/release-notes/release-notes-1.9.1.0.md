# Breakout Chain Client v1.9.1.0 — Release Notes

    version:           v1.9.1.0    (previous: v1.9.0.0)
    base commit:       5d6bcaecb3d1f97a02d79ddd04ee6ea8b6166cf1
    release date:      Tue Sep 1 2026   (source-only release)

This release adds essential Explore functionality on top of v1.9.0.0: per-card provenance
tracking. **It does not change any consensus rule, network protocol requirement, or fork
schedule** — see [v1.9.0.0](release-notes-1.9.0.0.md) for the FORK008/FORK009/FORK010 activation
on Sun Sep 6 02:00:00 2026 UTC and its requirements.

## Which release do I need?

Both v1.9.0.0 and v1.9.1.0 satisfy the fork. Take v1.9.1.0 unless you already have v1.9.0.0
running and have no need for the new Explore data below — either is fine, and there is no
deadline pressure specific to this release.

---

## What's new

**Per-card provenance tracking (`ExploreCardInfo` / `ExploreCardTransfer`).** The Explore API now
records each card's (deck NFT's) full holder history in the exploredb, keyed by color: the mint
(coinbase) followed by one entry per later change of holder. A card's total circulation is exactly
one indivisible unit, so its history is an unbroken chain from mint to current holder. Plain
same-address staking of a card is not ownership-changing and is not recorded as a transfer; it is
counted in `ExploreCardInfo::stakes` instead.

This is purely additive to the Explore RPC surface — no existing Explore data, RPC method, or
reply field is changed or removed.

---

## Versioning

Client version is now **1.9.1.0**. The network protocol version is unchanged at **61030** — this
release adds no new protocol behaviour and does not affect peer compatibility or the
`GetMinPeerProtoVersion()` requirement described in the v1.9.0.0 notes.
