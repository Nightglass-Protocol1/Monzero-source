# Monzero Box: security design

## Nothing unique in the image

Every image is the same for everyone and holds no secret. On the first start each box makes
its own SSH host keys, machine id, setup-page TLS key, one-time setup code and relay key. Root
and the admin user have no password; the admin user logs in by SSH key only, and SSH is off
until the owner adds a key on the setup page.

## Updates are signed

`monzero-update` installs a Monzero release only when:

1. `v<version>.json` verifies with `gpgv` against the keyring built into the image, and the
   signature's primary key is the pinned release key `4C37C618789F86D81FEF28DF390AEFB735F270D6`;
2. the signed file names the same version and the Monzero network (genesis hash);
3. the artifact for this machine matches the signed size and SHA-256 exactly (the download is
   cut off as soon as it is longer than signed);
4. the version is newer than the installed one (no downgrades: `latest.json` can only point).

Archives unpack with Python's `data` filter (no absolute paths, `..`, links out, devices or
setuid bits), into their own folder, owned by root, not writable by services. The switch is an
atomic symlink change; a node that does not answer within two minutes is switched back.
Tested refusals: changed download, other signing key, edited signed file, downgrade, malformed
version.

## The setup page

- HTTPS only (TLS 1.2+), answers only addresses from private, loopback and link-local ranges.
- Runs as the unprivileged user `monzero-web` with a tight systemd sandbox (no capabilities,
  read-only system, system-call filter, memory limit). It never changes the system: it writes a
  request file; `monzero-apply` (root) accepts only known request kinds, roles and OpenSSH key
  formats, and drops anything else.
- No JavaScript, no outside resources, strict Content-Security-Policy, frame denial,
  no-referrer, no-store.
- The one-time code is compared in constant time; wrong codes and passwords slow down
  exponentially after five tries. The password is stored as scrypt (N=2^15, r=8, p=1).
- Sessions: random 256-bit token, `Secure; HttpOnly; SameSite=Strict`, 12 hours; every change
  needs a per-session CSRF token and a same-origin request.

## Network

nftables, default drop. Open to everyone: only the XMZ peer port 6174 (new connections
rate-limited), and 8790 only if the owner shares the relay through a router port. Everything
else (page, node wallet RPC, relay, remote screen, SSH) from home-network ranges only. The
firewall is rewritten from the box's settings on every change. Kernel hardening sysctls
(kptr/dmesg restrict, no unprivileged BPF, ptrace scope 2, no redirects, rp_filter), AppArmor on.

## Services

Each service runs as its own user under systemd sandboxing (`ProtectSystem=strict`,
`NoNewPrivileges`, private /tmp and devices, only its own state folder writable): `monzero`
(node), `monzero-relay` (relay; also `MemoryDenyWriteExecute`, no capabilities, 1 GB cap),
`monzero-web`, `monzero-wallet` (the screen session; no password, no SSH).

## The shared relay

Shared through Tor (default), the box runs an onion service pointing at its relay only: no
SOCKS port, and the box is not a Tor relay or exit. The relay announces its onion name to
relay1/relay2 with its own Ed25519 key. The hubs accept only Tor v3 onions or public IP
literals (no private ranges, no DNS names), read the relay's `/v1/info` first, keep it only if
it serves the same swap contracts, and check every offer it carries again like any other.
A relay can leave offers out; it cannot change or invent them (they are signed by traders).

## Remote screen

Off by default. When on, x11vnc listens on the box itself only; noVNC serves it over TLS with
its own copy of the page certificate, behind a fresh random password shown on the status page.

## Known limits

- The page's certificate is self-signed: compare the fingerprint on the console once.
- VNC passwords are at most 8 characters (the VNC protocol); the screen is LAN-only and off by
  default.
- The wallet's own password and seed protect the funds; full-disk encryption is not on by
  default (a box with a screen must start unattended). Treat the box like the PC your wallet
  would otherwise be on.
