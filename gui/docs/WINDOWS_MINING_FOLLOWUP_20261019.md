# Windows mining report follow-up — 19 October 2026

The supplied wallet log confirms intermittent daemon connection failures. It does
not establish whether the daemon crashed, was overloaded, rejected a mining RPC,
or continued mining without finding a block. No dynamic reproduction is claimed.

## Source changes

- Start with one CPU mining thread; users can raise it explicitly. This reduces
  steady-state CPU demand, not RandomX dataset initialization memory requirements.
- Immediately replace a stale hashrate/status reading when the local node becomes
  disconnected. Unknown status does not mean mining stopped.
- Ignore a command result for a different selected wallet/node or remote mode.
- Return fixed error categories for busy/rejected/unanswered commands, invalid
  local requests and scheduler rejection. Display actionable messages and log
  only the category/action, never daemon-supplied text or wallet addresses.
- Failed Stop explicitly warns that mining may still be running. No automatic
  mining start, retry or daemon restart was introduced.

## Validation and limits

Using the existing Linux GUI test executable with current source QML and mocked
wallet/node objects: 20 mining QML results and 13 startup results passed. The new
cases cover stale text, wallet/node switching, thread-count rejection and Stop
without acknowledgement, plus immediate rechecking after an old pending status
reply crosses a disconnect/reconnect. Claude reviewed the source; follow-up fixes
also treat an already-idle Stop as success, categorize startup/address rejection
and scheduler shutdown, and catch command exceptions without logging arbitrary
exception text. The one-thread default is checked before test reset on an
eight-thread fixture. Logs are under `core/build/gui-mining-followup-*20261019.log`.
These tests do not exercise the changed C++ binary or a real Windows daemon.
No builds, mining, transfers, mainnet mutation or release replacement were run.
C++ compilation and native Windows startup/mining validation remain outstanding;
the original disconnect cause remains unresolved. Do not advertise this as a
confirmed repair or replace the published download with an unvalidated package.
