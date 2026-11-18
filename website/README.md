# Monzero website

Website for `monzero.org`, including the PHP-backed block explorer at
`/explorer/` and the local-draft asset workspace at `/app/`. It contains no
build-time dependencies.

The public source repository is
<https://github.com/Nightglass-Protocol1/monzero>.

The homepage links to `/WHITEPAPER.md`. Deploy the repository-root
`WHITEPAPER.md` at that exact document-root path whenever the site is updated;
the packaged copies and public copy must come from the same reviewed source.

## Preview locally

```bash
cd website
php -S 127.0.0.1:8080
```

Open `http://127.0.0.1:8080` for the website and
`http://127.0.0.1:8080/explorer/` for the explorer. Studio is available at
`http://127.0.0.1:8080/app/`; it can preview and download unsigned JSON drafts
but deliberately has no wallet connection, signing, or submission path.

## VPS deployment

Copy this directory's contents to `/var/www/monzero`, install
`nginx-monzero.conf` as `/etc/nginx/sites-available/monzero`, enable the site,
set the `fastcgi_pass` socket to the PHP-FPM listener installed on the host,
test Nginx, and reload it. The included Nginx route proxies only the homepage's
`/api/node-info/` request to the restricted local RPC service. PHP-FPM serves
the allowlisted explorer proxy and opt-in miner statistics endpoint.

On IONOS web hosting, upload the complete contents of this directory to the
domain's assigned document root. PHP must be enabled so `/explorer/api.php` can
proxy the allowlisted, read-only requests to the restricted public node.

`releases/genesis-pre11-notes.txt` is the human-readable companion to the
machine-readable pre11 metadata. It covers the CLI, source, and separately
packaged Windows graphical-wallet archive. Keep its release identity,
platform scope, and limitations aligned with `docs/RELEASE_NOTES.md` without
changing published archive hashes.

## Anonymous miner statistics

The miner table uses an opt-in heartbeat endpoint. It never accepts or returns
a wallet address, hostname, IP address, serial number, or hardware identifier.
Each reporter creates a random UUID; PHP pseudonymizes it with a server-side
HMAC secret before storage and exposes only the first eight pseudonym digits.

Configure these secrets in the PHP environment (use independent random values):

```text
MONZERO_STATS_SECRET=<at least 32 random characters, server only>
MONZERO_STATS_INGEST_TOKEN=<at least 24 random characters, team reporters>
MONZERO_STATS_FILE=/an/apache-writable/private/path/miner-stats.json
```

For shared hosting without environment-variable controls, create
`.monzero-miner-stats.php` one directory above the document root:

```php
<?php
return [
    'MONZERO_STATS_SECRET' => 'server-only-random-value',
    'MONZERO_STATS_INGEST_TOKEN' => 'team-reporter-random-value',
    'MONZERO_STATS_FILE' => __DIR__ . '/.monzero-miner-stats.json',
];
```

Generate suitable values with `openssl rand -hex 32`. Do not place the HMAC
secret in a download or reporter. Give only the ingest token to miners who opt
in, then run the reporter alongside their already-running local daemon:

```bash
MONZERO_STATS_TOKEN='team-ingest-token' \
  python3 utils/monzero-miner-reporter.py --interval 60
```

The table counts a miner as active for three minutes after its last heartbeat.
Hash rate and blocks found are self-reported. Block counts are derived from the
local daemon log and are not consensus-verified leaderboard claims.

The Windows heartbeat helper is
`utils/release/windows/monzero-miner-reporter.ps1`. Install its ingest token in
`%APPDATA%\Monzero\miner-stats-token.txt` with access limited to the reporting
account; never put that token in a public archive. `start-mining.bat` launches
the reporter automatically when that file exists; the reporter's named mutex
prevents duplicate instances. The homepage separately
shows `difficulty / target` as an estimated network hash rate even when no
miner has opted into reporting.

On Windows the daemon's default data and mining log directory is
`%PROGRAMDATA%\monzero`; the reporter diagnostic log remains under
`%APPDATA%\Monzero`.
