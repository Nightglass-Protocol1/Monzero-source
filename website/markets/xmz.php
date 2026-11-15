<?php
/* api-version: 2026-11-15 */
/**
 * XMZ market price, as traded on NoirTrade (XMZ/USDT).
 *
 * Reads NoirTrade's public market-data API (no key) at most once a minute and serves
 * the result to this site and to anyone else: last price, best bid and ask, 24-hour
 * volume, the top of the order book and the trades. Nothing here is estimated: if the
 * exchange cannot be reached, the last figures are served with the time they were read
 * and "stale" set once they are older than 15 minutes.
 *
 *   GET /markets/xmz.php
 */
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, OPTIONS');
header('Cache-Control: public, max-age=30');
if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'OPTIONS') { http_response_code(204); exit; }

const NT_BASE = 'https://noirtrade.com';
const NT_PAIR = 'XMZ_USDT';
const TTL = 60;            // seconds between reads of the exchange
const STALE_AFTER = 900;   // figures older than this are marked out of date
const KEEP_TRADES = 5000;

$dir = sys_get_temp_dir();
$cacheFile = $dir . '/monzero-xmz-noirtrade.json';
$tradesFile = $dir . '/monzero-xmz-noirtrade-trades.json';

function reply(string $json): void {
    $d = json_decode($json, true);
    if (is_array($d)) {
        $age = time() - (int) ($d['fetchedUnix'] ?? 0);
        $d['ageSeconds'] = $age;
        $d['stale'] = $age > STALE_AFTER;
        $json = json_encode($d, JSON_UNESCAPED_SLASHES);
    }
    echo $json;
    exit;
}

if (is_file($cacheFile) && (time() - filemtime($cacheFile)) < TTL) {
    $cached = file_get_contents($cacheFile);
    if ($cached !== false && $cached !== '') { reply($cached); }
}

function nt(string $path) {
    $ch = curl_init(NT_BASE . $path);
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_TIMEOUT => 10,
        CURLOPT_USERAGENT => 'monzero.org-markets',
        CURLOPT_HTTPHEADER => ['Accept: application/json'],
    ]);
    $body = curl_exec($ch);
    $ok = $body !== false && curl_getinfo($ch, CURLINFO_RESPONSE_CODE) === 200;
    curl_close($ch);
    return $ok ? json_decode($body, true) : null;
}

/** A plain non-negative decimal from the exchange, or null. */
function num($v): ?float {
    if (is_int($v) || is_float($v)) { $v = (string) $v; }
    if (!is_string($v) || !preg_match('/^\d{1,20}(\.\d{1,20})?$/', $v)) { return null; }
    return (float) $v;
}

function levels($rows, int $max): array {
    $out = [];
    if (!is_array($rows)) { return $out; }
    foreach ($rows as $r) {
        if (!is_array($r) || count($r) < 2) { continue; }
        $p = num($r[0]); $a = num($r[1]);
        if ($p === null || $a === null || $p <= 0 || $a <= 0) { continue; }
        $out[] = [$p, $a];
        if (count($out) >= $max) { break; }
    }
    return $out;
}

$ticker = null;
$tickers = nt('/api/v1/tickers');
if (is_array($tickers)) {
    foreach ($tickers as $t) {
        if (is_array($t) && ($t['ticker_id'] ?? '') === NT_PAIR) { $ticker = $t; break; }
    }
}
$last = $ticker ? num($ticker['last_price'] ?? null) : null;

if ($last === null || $last <= 0) {
    // The exchange did not answer or no longer lists the pair: serve what we last read.
    if (is_file($cacheFile)) { reply((string) file_get_contents($cacheFile)); }
    http_response_code(503);
    echo json_encode(['error' => 'The exchange could not be reached and no earlier figures are held.']);
    exit;
}

$book = nt('/api/v1/orderbook?ticker_id=' . NT_PAIR);
$bids = levels($book['bids'] ?? null, 10);
$asks = levels($book['asks'] ?? null, 10);
usort($bids, fn($a, $b) => $b[0] <=> $a[0]);
usort($asks, fn($a, $b) => $a[0] <=> $b[0]);

// Trades: the exchange returns its recent ones; keep every one we have seen so the
// chart does not lose its start when their list rolls over.
$known = [];
if (is_file($tradesFile)) {
    $k = json_decode((string) file_get_contents($tradesFile), true);
    if (is_array($k)) { $known = $k; }
}
$recent = nt('/api/v1/historical_trades?ticker_id=' . NT_PAIR);
if (is_array($recent)) {
    foreach (['buy', 'sell'] as $side) {
        foreach (($recent[$side] ?? []) as $t) {
            if (!is_array($t)) { continue; }
            $id = $t['trade_id'] ?? null;
            $ms = $t['trade_timestamp'] ?? null;
            $p = num($t['price'] ?? null); $x = num($t['base_volume'] ?? null); $u = num($t['target_volume'] ?? null);
            if (!(is_int($id) || (is_string($id) && ctype_digit($id)))) { continue; }
            if (!(is_int($ms) || (is_string($ms) && ctype_digit($ms)))) { continue; }
            if ($p === null || $x === null || $u === null || $p <= 0 || $x <= 0) { continue; }
            $known[(string) $id] = [(int) $ms, $p, $x, $u, $side];
        }
    }
}
uasort($known, fn($a, $b) => $a[0] <=> $b[0]);
if (count($known) > KEEP_TRADES) { $known = array_slice($known, -KEEP_TRADES, null, true); }
@file_put_contents($tradesFile, json_encode($known), LOCK_EX);
$trades = array_values($known);

// 24-hour change: against the last trade made before the 24 hours began.
$nowMs = time() * 1000;
$ref = null; $lastTradeMs = null;
foreach ($trades as $t) {
    if ($t[0] <= $nowMs - 86400000) { $ref = $t[1]; }
    $lastTradeMs = $t[0];
}
$change = ($ref !== null && $ref > 0) ? ($last / $ref - 1) * 100 : null;

$bid = num($ticker['bid'] ?? null); $ask = num($ticker['ask'] ?? null);
if ($bids) { $bid = $bids[0][0]; }
if ($asks) { $ask = $asks[0][0]; }
$spread = ($bid && $ask && $ask >= $bid) ? ($ask - $bid) / (($ask + $bid) / 2) * 100 : null;
$volX = num($ticker['base_volume'] ?? null); $volU = num($ticker['target_volume'] ?? null);
$traded24h = $volX !== null && $volX > 0;

$payload = [
    'updated' => gmdate('c'),
    'fetchedUnix' => time(),
    'exchange' => ['name' => 'NoirTrade', 'url' => NT_BASE, 'tradeUrl' => NT_BASE . '/trade?pair=' . NT_PAIR],
    'pair' => 'XMZ/USDT',
    'note' => 'Last trade price of XMZ on NoirTrade, in USDT. One exchange; not an average of several.',
    'last' => $last,
    'bid' => $bid,
    'ask' => $ask,
    'spreadPercent' => $spread,
    'change24hPercent' => $change,
    'volume24hXmz' => $volX,
    'volume24hUsdt' => $volU,
    'high24h' => $traded24h ? num($ticker['high'] ?? null) : null,
    'low24h' => $traded24h ? num($ticker['low'] ?? null) : null,
    'lastTradeAt' => $lastTradeMs !== null ? gmdate('c', intdiv($lastTradeMs, 1000)) : null,
    'book' => ['bids' => $bids, 'asks' => $asks],            // [price USDT, amount XMZ]
    'trades' => $trades,                                      // [time ms, price USDT, XMZ, USDT, side], oldest first
];
$json = json_encode($payload, JSON_UNESCAPED_SLASHES);
@file_put_contents($cacheFile, $json, LOCK_EX);
reply($json);
