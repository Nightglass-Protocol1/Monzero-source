# Deploying the public relays

relay1.monzero.org runs on monzero-eu (82.165.112.237), relay2.monzero.org on monzero-us
(67.217.247.219). Each relay listens on 127.0.0.1:8790 behind Caddy (HTTPS), keeps its state in
/var/lib/monzero-dex-relay, and syncs offers with the other one (`peers`).

## Build (generic x86-64)

Build in the Debian container, not on the CachyOS workstation: its system libraries mark every
binary as needing x86-64-v4 and the servers stop it with "CPU ISA level is lower than required".

    cd gui/dex-contracts
    podman run --rm --userns=keep-id -v $PWD:/src:Z,ro -v <cargo-home>:/cargo:Z -v <out>:/target:Z \
      -e CARGO_HOME=/cargo -e CARGO_TARGET_DIR=/target -w /src/relay \
      docker.io/library/rust:1.95-bookworm cargo build --offline --release
    readelf -n <out>/release/monzero-dex-relay | grep "ISA needed"   # must print nothing

## Install or update

    scp <out>/release/monzero-dex-relay deploy/* <host>:relay-deploy/
    ssh -t <host> 'cd ~/relay-deploy && sudo sh install.sh <relay1|relay2> ./monzero-dex-relay'

## Caddy

monzero-eu runs a custom Caddy build: validate and reload with /usr/local/bin/caddy-ratelimit
(`systemctl reload caddy` uses it), never the packaged `caddy` (it lacks rate_limit). Append
Caddyfile.snippet with HOST replaced, after backing up /etc/caddy/Caddyfile.

## Relay mesh (Monzero boxes)

Both relays are hubs (`"mesh": true`): Monzero boxes announce their own relays to them, and
they list them at `/v1/relays` and pull their offers (each offer is checked again as always).
Most boxes are Tor onion services, so the hubs read them through a local Tor client:

    sudo apt-get install -y tor            # SOCKS on 127.0.0.1:9050, the Debian default
    sudo systemctl enable --now tor

Without Tor the relay still starts and lists boxes with public IPs; onions are refused.
The Caddy snippet passes the sender's address (`header_up X-Real-IP {remote_host}`): without
it every sender looks like 127.0.0.1 and all of them share one write limit (older snippet).
