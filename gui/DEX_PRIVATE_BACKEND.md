# Private backend provisioning (experimental)

This is local service provisioning, not live DEX activation. Normal startup is
unchanged without `--dex-private-backend <config>`. With that option, the existing
`MONZERO_ENABLE_PRIVATE_DEX=I_UNDERSTAND_THIS_IS_A_LOCAL_PROTOTYPE` opt-in is also
required. Invalid configuration causes startup to fail with a generic diagnostic.

The Linux-only configuration must be a canonical absolute path to a single-link,
user-owned regular file with mode 0600, at most 8192 bytes. Its contents must be
compact sorted-key JSON with no trailing newline. Duplicate and unknown fields
are rejected. All nine fields are strings:

- `schema`: `monzero-private-backend-v1`
- `directory`: canonical private, user-owned parent directory
- `helper`, `helperDigest`: canonical native helper executable and trusted SHA256
- `rpc`, `rpcDigest`: canonical wallet-RPC executable and trusted SHA256
- `genesis`: expected lowercase 64-character genesis hash
- `daemonPort`, `walletPort`: distinct canonical decimal port strings

Executable hashes must come from trusted local installation metadata, not an
offer or peer. Files must not be group/other writable. Hash checking is neither
code signing nor a race-free execution handle; protect installation paths and
their ancestors. No credentials, wallet names or arbitrary network URLs are
accepted in this file. Do not put existing wallet directories in this config.

The GUI constructs a separate process owner/session, pins the helper and recovery
directory, and attaches its read-only presentation to DexEngine. The process
owner creates a new private service subdirectory and binds wallet RPC to
loopback. The daemon must already be running on the explicitly selected local
port. Loopback alone does not prove that daemon is on a disposable chain: verify
and isolate the daemon separately before testing.

No wallet is automatically opened or created. Initial identity is unknown.
There is no user-facing wallet-selection/recovery workflow yet, and no new
permission to sign, fund, trade, reset a journal or retry a payment. Normal exit
uses the owner's shutdown/credential cleanup path and retains service data for
recovery. Forced-exit supervision and full GUI startup/shutdown still need
end-to-end tests; this document does not claim those tests passed.

Verification so far: the standalone configuration regression compiled with
warnings treated as errors and passed valid configuration, unknown/duplicate
fields, malformed identities, invalid ports, symlink/config permissions and
unsafe executable permissions. The full GUI now links successfully. The
`test_gui_private_startup.py` fixture passed against that binary inside a bwrap
namespace with networking disabled and the host home directory unavailable.
It checks self-test startup, missing opt-in/config rejection, creation of one
separate mock RPC service directory, and credential removal on smoke-test exit.
The hidden `--startup-smoke-test` alias loads normal application QML and exits;
unlike `--test-qml`, it is not intercepted by a QML_TESTS build's test harness.

This fixture uses `dex_tests` as the mock RPC executable, not the actual native
wallet RPC, and creates no funded wallet. Interactive quit, forced exit and
real wallet-RPC provisioning still need their own integration evidence. No
configuration has been deployed and no user's wallet has been opened by this
work.

Real-service follow-up: the same isolated full-GUI fixture passed with
`--rpc-fixture build/release/bin/monzero-wallet-rpc` and `--native-helper` pointing
to the built native helper. Its namespace-local probe waits for an HTTP 401
challenge, verifies authenticated `get_version`, and requires `get_address` to
report no wallet open. After timed graceful GUI exit it requires the service
state to be `stopped`, credential files to be absent, and no wallet key files.
No daemon is started and no funding or settlement is tested by this case.

`--startup-smoke-delay-ms` is a hidden test-only option requiring smoke mode,
bounded to 0–15000 ms. It permits startup events and the isolated probe before
normal quit; it does not change normal interactive startup. Forced exits,
interactive wallet selection and funded trade orchestration remain unverified.
