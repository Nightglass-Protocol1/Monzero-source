# Monzero systemd service

The unit runs `monzerod` as a dedicated, unprivileged `monzero` account. It
keeps chain state in `/var/lib/monzero`, logs in `/var/log/monzero`, and reads
operator-reviewed settings from `/etc/monzero/monzerod.conf`.

Create the system account and configuration directory before enabling it:

```bash
sudo useradd --system --home-dir /var/lib/monzero --shell /usr/sbin/nologin monzero
sudo install -d -o root -g monzero -m 0750 /etc/monzero
sudo install -o root -g monzero -m 0640 monzerod.conf /etc/monzero/monzerod.conf
sudo install -o root -g root -m 0644 monzerod.service /etc/systemd/system/monzerod.service
sudo systemctl daemon-reload
sudo systemctl enable --now monzerod.service
```

For a public node, expose restricted RPC separately from the operator-only
readiness endpoint:

```text
rpc-bind-ip=127.0.0.1
rpc-bind-port=6177
rpc-restricted-bind-ip=0.0.0.0
rpc-restricted-bind-port=6175
confirm-external-bind=1
```

Port 6177 must remain loopback-only. Install the readiness gate and timer with:

```bash
sudo install -d -o root -g root -m 0755 /usr/local/libexec/monzero
sudo install -o root -g root -m 0755 utils/release/verify-network-readiness.sh \
  /usr/local/libexec/monzero/verify-network-readiness.sh
sudo install -o root -g root -m 0644 \
  utils/systemd/monzero-readiness.service \
  utils/systemd/monzero-readiness.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now monzero-readiness.timer
```

The oneshot service deliberately exits unsuccessfully when the node is not
ready. Inspect the latest result with
`journalctl -u monzero-readiness.service -n 30 --no-pager`. The default gate
requires two peers and a block tip no more than 15 minutes old.

Install the separate operational-health check alongside it:

```bash
sudo install -o root -g root -m 0755 utils/release/monitor-node-health.sh \
  /usr/local/libexec/monzero/monitor-node-health.sh
sudo install -o root -g root -m 0644 \
  utils/systemd/monzero-health.service \
  utils/systemd/monzero-health.timer /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now monzero-health.timer
```

This timer checks the systemd process, unrestricted loopback RPC identity and
synchronization, data-volume pressure, daemon resident memory, and recent log
error count every five minutes. It fails at more than 90% disk usage or 4 GiB
RSS by default; operators may set `MONZERO_MAX_DISK_PERCENT` and
`MONZERO_MAX_RSS_MB` in a reviewed service override. The log error count is
observational because old retained lines do not by themselves prove a current
fault. Inspect results with
`journalctl -u monzero-health.service -n 30 --no-pager`.

The production configuration must explicitly review P2P/RPC bind addresses,
restricted RPC, authentication, public ports, seed or priority nodes, pruning,
and log retention. Never expose unrestricted wallet or daemon RPC to the
internet. Confirm `status`, `get_info`, peer counts, synchronization, and an
empty-directory sync before promoting a node.
