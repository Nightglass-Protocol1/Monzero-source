#!/bin/bash
# Open (or close) the airdrop by updating ~/monzero-private/airdrop-config.php on the webspace.
# Keeps existing moderators and any other keys, backs the old file up first, removes stale
# reward/cap overrides so the code defaults apply (25 XMZ cap, halved rewards), and creates a
# master admin_token if there is none (printed once so you can open the moderator panel).
#   bash utils/website/airdrop-open.sh           # open: enabled, 8000 XMZ base, ends 2027-01-31 23:59:59 UTC
#   bash utils/website/airdrop-open.sh --close   # close again (enabled = false), nothing else changes
set -euo pipefail
WEB=${WEB_HOST:-monzero-webspace}
MODE=${1:-open}
case "$MODE" in open|--open) MODE=open;; --close|close) MODE=close;; *) echo "usage: $0 [--close]"; exit 2;; esac

ssh -o BatchMode=yes "$WEB" MODE="$MODE" 'bash -s' <<'REMOTE'
set -euo pipefail
cd ~/monzero-private
umask 077
[ -f airdrop-config.php ] && cp -p airdrop-config.php "airdrop-config.php.bak-$(date -u +%Y%m%dT%H%M%SZ)"
MODE="$MODE" php -r '
$f = "airdrop-config.php";
$c = is_file($f) ? (require $f) : [];
if (!is_array($c)) $c = [];
if (getenv("MODE") === "close") { $c["enabled"] = false; }
else {
  foreach (["rewards", "points", "per_person_cap", "referral_cap", "admin_reward_xmz"] as $k) unset($c[$k]);  // use the code defaults
  $c = array_merge($c, [
    "enabled" => true, "pool_xmz" => 8000, "auto_renew" => true, "topup_buffer" => 1000,
    "mining_days_required" => 7, "max_per_ip" => $c["max_per_ip"] ?? 3, "starts_at" => 0, "ends_at" => 1801439999,
  ]);
  $c["admins"] = is_array($c["admins"] ?? null) ? $c["admins"] : [];
  if (!is_string($c["admin_token"] ?? null) || strlen($c["admin_token"]) < 24) {
    $c["admin_token"] = bin2hex(random_bytes(20));
    fwrite(STDERR, "NEW master moderator key (save it in your password manager):\n  https://monzero.org/airdrop/admin.php?key=" . $c["admin_token"] . "\n");
  }
}
$tmp = $f . ".tmp";
file_put_contents($tmp, "<?php\n// Written by airdrop-open.sh " . gmdate("c") . "\nreturn " . var_export($c, true) . ";\n");
chmod($tmp, 0600); rename($tmp, $f);
echo "config: enabled=" . var_export($c["enabled"], true) . " moderators=" . count($c["admins"] ?? []) . " ends_at=" . ($c["ends_at"] ?? 0) . "\n";
'
php -l airdrop-config.php >/dev/null && echo "config syntax ok"
REMOTE

echo "== public view"
curl -fsS https://monzero.org/airdrop/api.php?action=config; echo
