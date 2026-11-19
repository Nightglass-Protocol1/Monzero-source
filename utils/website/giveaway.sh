#!/bin/bash
# Telegram giveaway through the Monzero bot: the first SLOTS people who open the link, are in
# @MonzeroOfficial, have a public @handle and send an XMZ payout address get AMOUNT XMZ each.
# Every claim is linked to the Telegram ID, the @handle and the payout address.
# Run from the owner's PC (needs the website code with website/lib/giveaway.php deployed):
#   bash utils/website/giveaway.sh open [SLOTS] [AMOUNT_XMZ] ["Title"]   # new link; default 20 x 500 XMZ
#   bash utils/website/giveaway.sh status                                  # places taken, with handles
#   bash utils/website/giveaway.sh export                                  # payout CSV to ~/monzero-giveaways/
#   bash utils/website/giveaway.sh close                                   # stop taking claims
# Paying: the export prints the airdrop_payout.py command (its own ledger, so nobody is paid twice).
set -euo pipefail
WEB=${WEB_HOST:-monzero-webspace}
MODE=${1:-}
case "$MODE" in
  open)
    SLOTS=${2:-20}; AMOUNT=${3:-500}; TITLE=${4:-Monzero Telegram giveaway}
    [[ "$SLOTS" =~ ^[0-9]+$ && "$SLOTS" -ge 1 && "$SLOTS" -le 1000 ]] || { echo "slots must be 1-1000"; exit 2; }
    [[ "$AMOUNT" =~ ^[0-9]+$ && "$AMOUNT" -ge 1 && "$AMOUNT" -le 100000 ]] || { echo "amount must be 1-100000 XMZ"; exit 2; }
    CODE=$(openssl rand -hex 6)
    echo "Opening: $SLOTS places x $AMOUNT XMZ = $((SLOTS * AMOUNT)) XMZ in total"
    ;;
  status|export|close) ;;
  *) sed -n '2,10p' "$0"; exit 2;;
esac

remote() {   # runs PHP on the webspace with the site's giveaway library
  # %q so a title with spaces or quotes reaches the server as one value
  ssh -o BatchMode=yes "$WEB" "$(printf 'MODE=%q SLOTS=%q AMOUNT=%q CODE=%q TITLE=%q bash -s' "$MODE" "${SLOTS:-}" "${AMOUNT:-}" "${CODE:-}" "${TITLE:-}")" <<'REMOTE'
set -euo pipefail
cd ~/monzero-private
# The live document root, found as deploy.py finds it (next to admin/bootstrap.php)
f=$(ls ~/*/admin/bootstrap.php); [ "$(echo "$f" | wc -l)" = 1 ] || { echo "!! cannot find the document root" >&2; exit 1; }
LIB="$(dirname "$(dirname "$f")")/lib/giveaway.php"
[ -f "$LIB" ] || { echo "!! lib/giveaway.php is not on the server yet: deploy the site first" >&2; exit 1; }
umask 077
GWLIB="$LIB" php -r '
require getenv("GWLIB");
$f = "giveaway-config.php";
$c = is_file($f) ? (require $f) : [];
if (!is_array($c)) $c = [];
$mode = getenv("MODE");
$save = function (array $c) use ($f) {
  if (is_file($f)) copy($f, $f . ".bak-" . gmdate("Ymd\THis\Z"));
  file_put_contents($f . ".tmp", "<?php\n// Written by giveaway.sh " . gmdate("c") . "\nreturn " . var_export($c, true) . ";\n");
  chmod($f . ".tmp", 0600); rename($f . ".tmp", $f);
};
if ($mode === "open") {
  $c = ["enabled" => true, "code" => getenv("CODE"), "slots" => (int)getenv("SLOTS"), "amount_xmz" => (int)getenv("AMOUNT"),
        "title" => getenv("TITLE"), "opens_at" => 0, "closes_at" => 0, "created_at" => time()];
  $save($c);
  $tg = (require "telegram-config.php");
  echo "LINK: https://t.me/" . ($tg["bot_username"] ?? "BOT") . "?start=gw-" . $c["code"] . "\n";
  exit;
}
$g = gwConfig();
if (!$g) { fwrite(STDERR, "!! no giveaway is set up (run: giveaway.sh open)\n"); exit(1); }
if ($mode === "close") { $c["enabled"] = false; $save($c); echo "closed: no more claims for code " . $g["code"] . "\n"; exit; }
if ($mode === "export") { echo gwExportCsv($g["code"]); exit; }
$n = gwClaimed($g["code"]);
echo ($g["enabled"] ? "OPEN" : "CLOSED") . ": " . $g["title"] . "  code " . $g["code"] . "  " . $n . "/" . $g["slots"] . " claimed  (" . $g["amount_xmz"] . " XMZ each)\n";
$s = gwDb()->prepare("SELECT slot, handle, user_id, address, claimed_at FROM claims WHERE code = ? ORDER BY slot");
$s->execute([$g["code"]]);
foreach ($s->fetchAll() as $r) printf("%3d  %-24s id %-12s %s  %s UTC\n", $r["slot"], $r["handle"], $r["user_id"], gwShort($r["address"]), gmdate("Y-m-d H:i:s", (int)$r["claimed_at"]));
'
REMOTE
}

case "$MODE" in
  export)
    DIR=~/monzero-giveaways; mkdir -p "$DIR"
    OUT="$DIR/giveaway-$(date -u +%Y%m%dT%H%M%SZ).csv"
    remote > "$OUT"
    CODE=$(MODE=status remote | head -1 | sed -n 's/.*  code \([0-9a-f]*\)  .*/\1/p')
    echo "Saved $(($(wc -l < "$OUT") - 1)) claims to $OUT"
    echo "Pay them (dry run first; add --send to pay). The ledger is per giveaway, so re-running never pays twice:"
    echo "  python3 $(cd "$(dirname "$0")" && pwd)/airdrop_payout.py --csv $OUT --ledger $DIR/giveaway-$CODE-ledger.jsonl --wallet-rpc http://127.0.0.1:18090 --rpc-login USER:PASS"
    ;;
  *) remote;;
esac
