# Breakout Chain Client v1.9.5.0 — Release Notes

    version:           v1.9.5.0    (previous release: v1.9.3.0;
                                    previous tag: v1.9.4.0)
    release date:      Sat Sep 12 2026

    NO CONSENSUS CHANGE. NO PROTOCOL CHANGE. NO FORK SCHEDULE CHANGE.
    The network protocol version is unchanged at 61030, and the
    BRK_FORK008 / BRK_FORK009 / BRK_FORK010 rules that activated on
    Sun Sep 6 02:00:00 2026 UTC (epoch 1788660000) are untouched.

This release fixes an abort and a crash in `breakoutd` at shutdown, makes assertion failures leave
a record, and fixes two defects in the Explore API rich list. It also adds two RPCs:
`getbestblock`, and `getmovementspg` over a new Explore API index. It includes the v1.9.4.0
explore-index startup fix, which was published as a tag only.

It is a **recommended upgrade for anyone running `breakoutd`**, and in particular for anyone
running the Explore API (`-exploreapi`). Wallet users on the Qt client are unaffected by most of
it; the shutdown fixes apply to any build that serves RPC.

> **Explore API nodes rebuild their index once.** `EXPLOREDB_VERSION` goes from 2 to 3 for the new
> movement index, so the first start on v1.9.5.0 replays the explore index from genesis. Nodes not
> running `-exploreapi` do no extra work.

---

## Which release do I need?

| you are | take |
|---|---|
| Running `breakoutd`, and have seen it abort or hang for a minute or more on `stop` | **v1.9.5.0** |
| Running the Explore API | **v1.9.5.0** — the rich list is wrong on v1.9.3.0 after any warm start |
| Running the Qt wallet only, without RPC clients | v1.9.5.0 is optional |

---

## Fixed: shutdown abort with an idle RPC connection open

**Symptom.** `breakoutd` aborts during shutdown with

```
Assertion failed: (!posix::pthread_mutex_lock(&m)),
function lock, file recursive_mutex.hpp, line 108
```

or, before that, shutdown stalls for a long time and `debug.log` reports
`ThreadsRPCServer still running`.

**Cause.** Nothing in the shutdown path brought the RPC server down. The listener thread sat in
`io_service.run_one()` and re-tested `fShutdown` only when a client connected; each connection
handler sat in `ReadHTTP()` and re-tested it only when the next request arrived. A client holding a
keep-alive connection open therefore pinned a handler indefinitely. `StopNode()` waits twenty
seconds for threads, then proceeds regardless, so globals were destroyed with handlers still live,
and the next request served on a surviving connection locked a destroyed mutex.

**Fix.** `StopNode()` now calls `StopRPCServer()`, which half-closes the sockets of idle
connections — returning their blocked reads — and stops the listener's `io_service` so it wakes and
exits. Connections in the middle of a request are left alone: they are about to write a reply and
exit on their own. `stop` is served over one of those, so `breakoutd stop` still gets its answer.

Measured with an idle keep-alive connection held open: shutdown went from about 80 seconds (and an
abort if anything polled during teardown) to one second.

## Fixed: segfault at shutdown, reported as a Tor crash

Waking the listener promptly exposed a use-after-free. The `io_service` is a local of
`ThreadRPCServer2`, and each connection's destructor deregisters its socket from the reactor that
`io_service` owns. Returning while handlers were still deleting connections ran those destructors
against a destroyed reactor:

```
ThreadRPCServer3
  -> ~AcceptedConnectionImpl<ip::tcp>
    -> reactive_socket_service_base::destroy
      -> kqueue_reactor::deregister_descriptor      SIGSEGV
```

`breakoutd` embeds Tor, so a segfault on any thread takes Tor down with it, and what is left behind
is a Tor crash log — `Tor 0.4.8.12 died: Caught signal 11` — naming a component that had nothing to
do with it. The listener now waits for the handler count to reach zero before returning, bounded at
ten seconds and logged if the bound is hit.

## Improved: assertion failures are recorded in `debug.log`

`BOOST_ASSERT` and `BOOST_VERIFY` used to write one line to `stderr` and abort. With `daemon=1`
that line usually goes nowhere, and Boost names its own header rather than the call site. The
shutdown assertion above arrived with nothing else to go on.

Boost assertions now go through a handler that writes the expression, the function, our file and
line, the thread name, the client version and a backtrace to `debug.log`, repeats it on `stderr`,
and aborts as before. Thread names are now set on macOS too; previously only Linux had them.

## Fixed: Explore API rich list missing most holders after a warm start

The in-memory cache behind `getrichlist`, `getrichlistpg` and `getrichlistsize` was written only as
blocks connected and disconnected. A start that rebuilt the explore index filled it as a side
effect of the replay; every other start left it empty for the session. The rich list then held only
balances that some address happened to move onto, and looked well-formed while omitting most
holders. On mainnet SIS it returned 135 addresses totalling 254,230 SIS against a 3,956,997 SIS
supply, and the largest holder was absent entirely. `getaddressinfo` was correct throughout.

The cache is now loaded from the on-disk balance sets whenever the replay is skipped, and logs
`Loaded rich list: N addresses over M balance sets in K colors`.

## Fixed: rich list pages overlapping inside a tie

The rich list is stored as balance → set of addresses, and `GetRichList` emitted the whole set
containing the starting rank, including addresses ranked above it. On BRX, a 100-address tie at
6034 spans ranks 85–184, so pages 2, 3 and 4 of `getrichlistpg BRX` at 50 per page all began at
rank 85 and returned the same addresses under different ranks. Ascending reads put the surplus in
the middle of the result.

Replies now begin at the rank asked for. At the trailing edge the two RPCs differ, and their help
says so:

* `getrichlist` answers "the top *n*" and still includes everyone tied at the last place.
* `getrichlistpg` tiles exactly: a tie crossing a page boundary is split, and the next page resumes
  inside it.

No reindex is needed for this fix.

## New: `getmovementspg` and the movement index (Explore API)

    getmovementspg <page> <perpage> [ordering] [mincoins] [color]

Pages an index of transactions that moved value to another party. A transaction is recorded when,
summing outputs by recipient, some address that none of the inputs came from receives at least that
currency's floor:

| currency | floor | share of supply |
|---|---|---|
| BRK | 2500 | ~0.010% |
| BRX | 600 | ~0.010% |
| SIS | 400 | ~0.010% |
| BAM, Deck | — | not tracked |

Excluding stake self-returns is what keeps the index small — most large outputs on this chain are a
stake returning its principal to the address that staked it. Summing per recipient and counting
only new recipients means change back to the sender never inflates a movement. On mainnet at height
1,493,731 the index holds 13,456 records.

`[mincoins]` is an additional cutoff applied on top of the floors, defaulting to 0. The response
reports the floors in force.

## New: `getbestblock`

    getbestblock [txinfo]

Returns the tip block, the same object as `getblockbynumber` at the current height, in one call and
therefore self-consistent — including the per-colour `moneysupply` and `totalmint` maps. Available
on every node, not only the Explore API.

## Also in this release

* **`help` lists Explore API commands properly.** The listing shows only the first line of each
  command's help, which for every explore RPC was its `== Explore API ==` banner. The marker is now
  a suffix on the usage line.
* **From v1.9.4.0 (tag only):** a new Explore API node, or the first start after enabling
  `-exploreapi`, no longer throws `init_exploredb(): ... does not exist (create_if_missing is false)`
  when startup connects blocks before the index is created.
* **Documentation.** `doc/BREAKOUT_EXPLORE_RPC_MANUAL.md` covers `getbestblock` and
  `getmovementspg`, and documents the rich-list tie behaviour.

**Client version.** Now **1.9.5.0**, composing to `CLIENT_VERSION` 1090500. The network protocol
version is unchanged at **61030**; nothing here affects peer compatibility or the
`GetMinPeerProtoVersion()` requirement.

---

## Known issues

**Shutdown can still, rarely, abort or crash.** `Shutdown()` calls `exit(0)` once `StopNode()` is
done, and `StopNode()` does not wait for every thread, so a thread can still be running while the
process's globals are destroyed. The node has already stopped when this happens; restart it
normally. A fix is planned for v1.9.5.1.

This was measured after release, on Debian 12, using AddressSanitizer builds of v1.9.3.0 and
v1.9.5.0 with Tor and live peers, over roughly 400 start / `backupwallet` / `stop` cycles:

* **The v1.9.2.0 `corrupted size vs. prev_size` abort is most likely fixed.** v1.9.3.0 (identical
  to v1.9.2.0 in this code) shows the RPC use-after-free fixed in this release, even on a plain
  stop with no keep-alive client, and that bug writes into freed memory in the way glibc heap
  aborts arise. v1.9.5.0 showed none in any normal scenario. The exact glibc message was not
  reproduced, so this is an inference, not proof.
* **The connection thread can outlive `exit()`.** Seen on about 3 of 190 v1.9.5.0 cycles, as
  `Assertion failed: (!posix::pthread_mutex_lock(&m))` on thread `breakout-opencon`.
  `OpenNetworkConnection()` stops counting itself as running while it connects through Tor, so
  `StopNode()` does not wait for it, and the thread then takes an already-destroyed lock in
  `ProcessOneShot()`. The field report of that assertion that prompted this release's RPC fix may
  have been this bug.
* **An RPC client that stops reading its reply** leaves a handler blocked writing. The listener
  gives up waiting for it after ten seconds and destroys the socket machinery anyway, so the
  handler's eventual clean-up is the use-after-free this release otherwise fixed, and shutdown
  does not finish until that client disconnects. This needs a misbehaving client.
* **Nothing waits for the Tor thread.** It crashed inside OpenSSL during `exit()` once on v1.9.3.0.
  v1.9.5.0 has the same exposure, though it was not seen to crash there.
