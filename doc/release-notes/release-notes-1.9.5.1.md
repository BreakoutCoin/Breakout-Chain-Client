# Breakout Chain Client v1.9.5.1 — Release Notes

    version:           v1.9.5.1    (previous release: v1.9.5.0)

    NO CONSENSUS CHANGE. NO PROTOCOL CHANGE. NO FORK SCHEDULE CHANGE.
    The network protocol version is unchanged at 61030.

This release fixes the rare abort or crash of `breakoutd` at shutdown listed as a known issue in
the [v1.9.5.0 notes](release-notes-1.9.5.0.md#known-issues). It changes nothing else.

It is a **recommended upgrade for anyone running `breakoutd`**. The Qt client shuts down through
the same `StopNode()` and gets the same fixes, but it was only ever tested as the daemon. In both,
the fault struck after the wallet and databases had already been flushed and closed.

---

## Which release do I need?

| you are | take |
|---|---|
| Running `breakoutd`, and have seen it abort or crash while stopping | **v1.9.5.1** |
| Running `breakoutd` under a supervisor that treats a non-zero exit as a failure | **v1.9.5.1** |
| Running the Qt wallet | v1.9.5.1 is optional |

---

## The fault

`Shutdown()` calls `exit(0)` once `StopNode()` returns. `exit()` runs the process's static
destructors, which destroy global locks and containers. `StopNode()` waits for the threads it
knows about, within a bound, but three kinds of thread could still be running when it returned.
Any of them touching a global after that point is a use-after-free.

Symptoms seen, all after `debug.log` had already printed `breakout exited`:

```
ASSERTION FAILED: !posix::pthread_mutex_lock(&m)
  thread: breakout-opencon
```

```
Tor 0.4.8.12 died: Caught signal 11
```

The node has already stopped when this happens; nothing on disk is affected. The practical harm is
a non-zero exit status, a crash report, and a supervisor that may treat the stop as a failure.

## Fixed: the connection thread outlived `exit()`

**Cause.** `OpenNetworkConnection()` stepped out of the running-thread count around
`ConnectNode()`, so that a slow connect would not hold up shutdown. A connect through Tor's SOCKS
port blocks until Tor answers or goes away, which is usually after `StopNode()` has finished
waiting. When Tor stopped, the connect failed, and the connection loop went straight to
`ProcessOneShot()` without checking for shutdown — taking `cs_vOneShots` after `exit()` had
destroyed it.

This was the most common of the three: about **1.7% of stops** on v1.9.5.0 with Tor running.
It is very likely the field report of the same assertion that prompted the RPC fix in v1.9.5.0.

**Fix.** The thread now stays counted for the whole connect, and the loop checks for shutdown
before `ProcessOneShot()`. `StopNode()` stops Tor before it waits, which fails the pending SOCKS
exchange promptly, and its wait remains bounded at twenty seconds.

## Fixed: nothing waited for the Tor thread

**Cause.** `shutdown_tor()` only asks Tor's event loop to exit. Nothing waited for it to do so, so
`exit()` could run while Tor was still building circuits. Seen once, crashing inside OpenSSL:

```
CRYPTO_THREAD_write_lock <- RAND_bytes_ex <- crypto_rand
  <- origin_circuit_new <- second_elapsed_callback          (thread: onion)
```

**Fix.** The Tor thread is now counted, as `THREAD_TOR`, so `StopNode()` waits for it. Tor's own
cleanup does not call `OPENSSL_cleanup()`, so the rest of shutdown can still use OpenSSL after Tor
has gone. If Tor ever fails to exit in time, `debug.log` says `Tor thread still running`.

## Fixed: an RPC client that stopped reading

**Cause.** v1.9.5.0 made the RPC listener wait for its connection handlers before destroying the
`io_service` they use, and shut down idle connections so that wait is short. A handler blocked
*writing* a reply to a client that has stopped reading counts as busy, so its connection was
never shut. The listener gave up after ten seconds and destroyed the `io_service` anyway, and the
handler's eventual cleanup was the same use-after-free. Shutdown also hung until that client
disconnected.

**Fix.** Two seconds into the wait, connections still mid-request are shut down too, which fails
the blocked write:

```
Shut down 1 RPC connection(s) still open, including 1 mid-request
```

`stop` is unaffected: its reply is sent long before the two seconds are up. If a handler is still
running at ten seconds — busy inside a long request rather than on its socket — the listener now
leaves the `io_service` and SSL context alive for the rest of the process instead of destroying
them underneath it:

```
ThreadRPCServer2(): 1 RPC handler(s) still running; returning without destroying the io_service
```

That path is the only one that does so, and the process is about to exit.

---

## Verification

Tested on Debian 12 (glibc 2.36, gcc 12) with AddressSanitizer builds, Tor running and live peers.
Each cycle starts the node, waits a random 5–60 seconds, calls `backupwallet` and a few other RPCs,
and stops it. Keep-alive scenarios hold an idle RPC connection open across the stop, send one more
request on it during shutdown, close it during shutdown, or send a large batch and never read the
reply.

| build | cycles | faults | `stop` answered | typical stop |
|---|---|---|---|---|
| v1.9.5.1 with ASan, all scenarios | 204 | **0** | 204 / 204 | 0.7–1.9 s (stuck client ~2 s) |
| v1.9.5.1 uninstrumented, glibc `MALLOC_CHECK_=3` | 48 | **0** | 48 / 48 | ≤ 2.4 s |
| v1.9.5.0 with ASan, run alongside as a control | 40 | 1 (connection thread) | 40 / 40 | 0.7–1.6 s |

In every v1.9.5.1 cycle with a readable log, `Onion thread exited.` and `ThreadOpenConnections
exited` both appear before `breakout exited`. In the v1.9.5.0 control the connection thread was
still running at `exit()` in 16 of 38 cycles — the window the assertion needs.

The ten-second path was exercised separately with an RPC batch sized to run for about 25 seconds:
it logged the message above, and ASan reported nothing when the handler finished after the
listener had gone.

The grace period was measured in whole seconds in the tested build, so it could fire after as
little as one second. It is measured in milliseconds in this release.

**Client version.** Now **1.9.5.1**, composing to `CLIENT_VERSION` 1090501. The network protocol
version is unchanged at **61030**.

---

## Known issues

**A long RPC request can still delay exit.** `StopNode()`'s last step waits, without a bound, for
RPC handlers and the message handler to finish. A request that is still executing when `stop`
arrives — a large batch, say — holds up exit until it completes. It no longer causes a crash, and
its reply is cut off, but the process does not exit before it is done. Measured at about 20 seconds
for a batch built to run for 25.

---

## For maintainers

**Count a thread for as long as it can touch a global.** The pattern this lineage inherited —
stepping out of `vnThreadsRunning` around a blocking call so shutdown need not wait for it — is
only safe if the thread re-checks `fShutdown` before touching anything global *and* cannot outlive
`exit()`. The second half cannot be guaranteed from inside the thread. Several other threads in
`net.cpp` still step out around sleeps and semaphore waits; those return to a `fShutdown` check
within half a second and were left alone, but any new blocking call should stay counted.

**`vnThreadsRunning` is a plain `int` array** written from several threads without a lock. It has
not been seen to matter, but it is not a reliable counter.
