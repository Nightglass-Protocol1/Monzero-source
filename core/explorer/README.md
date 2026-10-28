# Monzero block explorer

A dependency-free PHP and JavaScript explorer for the Monzero restricted RPC.
It is read-only and never handles wallet seeds, private keys, or passwords.

## Run locally

Requirement: PHP 8. The API uses cURL when available and otherwise falls back
to PHP's native HTTP streams.

```bash
cd explorer
php -S 127.0.0.1:8081
```

Open `http://127.0.0.1:8081`.

The RPC URL is defined by `NODE_RPC` at the top of `api.php`. It currently uses
the public node at `http://node.monzero.org:6175`.

## Later deployment

Upload the directory to a PHP-enabled web root or subdomain. For production,
place it at a dedicated hostname such as `explorer.monzero.org`, enable HTTPS,
and retain a restricted node RPC. The PHP endpoint uses an action allowlist and
does not expose a generic RPC proxy.
