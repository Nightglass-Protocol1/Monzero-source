<?php
/* api-version: 2026-11-08b (adds openingRate) */
/**
 * Monzero swap-pair market-price API.
 *
 * Serves live USD market prices (and 30-day history) for the assets that Monzero's
 * atomic-swap DEX trades XMZ against: BTC, LTC, BNB, ETH, SOL. XMZ itself has NO
 * verified market price and is deliberately not invented here — see /markets/.
 *
 * Public + CORS-enabled so other sites may use it as a price tracker.
 * Upstream: CoinGecko. Responses are cached ~5 min to stay well inside rate limits.
 *
 *   GET /markets/api.php            -> full payload (coins + history + dex swaps)
 *   GET /markets/api.php?only=coins -> just the live prices (lighter)
 */
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, OPTIONS');
header('Cache-Control: public, max-age=120');
if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'OPTIONS') { http_response_code(204); exit; }

$COINS = [
    'BTC' => ['id' => 'bitcoin',     'name' => 'Bitcoin'],
    'LTC' => ['id' => 'litecoin',    'name' => 'Litecoin'],
    'BNB' => ['id' => 'binancecoin', 'name' => 'BNB'],
    'ETH' => ['id' => 'ethereum',    'name' => 'Ethereum'],
    'SOL' => ['id' => 'solana',      'name' => 'Solana'],
];

// The real mainnet atomic-swap test settlements. These are SETTLEMENT amounts, not a
// market price for XMZ — the test sizes were arbitrary, deliberately small, not calibrated
// to any market. 2 XMZ = 200000000000 atoms (1 XMZ = 1e11 atoms).
$DEX_SWAPS = [
    ['pair' => 'BTC/XMZ', 'status' => 'settled',    'date' => '2026-11-04',
     'settled' => '0.00114 BTC ⇄ 2 XMZ', 'coin' => 'BTC', 'coinAmount' => 0.00114, 'xmz' => 2],
    ['pair' => 'BNB/XMZ', 'status' => 'settled',    'date' => '2026-10-31',
     'settled' => '0.01 BNB ⇄ 7000 XMZ', 'coin' => 'BNB', 'coinAmount' => 0.01, 'xmz' => 7000],
    ['pair' => 'SOL/XMZ', 'status' => 'settled',    'date' => '2026-11-06',
     'settled' => null, 'coin' => 'SOL', 'coinAmount' => null, 'xmz' => 2],
    ['pair' => 'LTC/XMZ', 'status' => 'validating', 'date' => null,
     'settled' => '0.00201 LTC ⇄ 2 XMZ (test size)', 'coin' => 'LTC', 'coinAmount' => 0.00201, 'xmz' => 2],
    ['pair' => 'ETH/XMZ', 'status' => 'settled',    'date' => '2026-11-08',
     'settled' => null, 'coin' => 'ETH', 'coinAmount' => null, 'xmz' => null],
];

$cache = sys_get_temp_dir() . '/monzero-markets-cache.json';
$ttl = 300;
if (is_file($cache) && (time() - filemtime($cache)) < $ttl) {
    $cached = file_get_contents($cache);
    if ($cached !== false && $cached !== '') { echo maybe_only($cached); exit; }
}

function fetch_json(string $url) {
    $ch = curl_init($url);
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_TIMEOUT => 12,
        CURLOPT_USERAGENT => 'monzero.org-markets',
        CURLOPT_HTTPHEADER => ['Accept: application/json'],
    ]);
    $body = curl_exec($ch);
    $ok = $body !== false && curl_getinfo($ch, CURLINFO_RESPONSE_CODE) === 200;
    curl_close($ch);
    return $ok ? json_decode($body, true) : null;
}

// ONE upstream call gives spot price, 24h change AND a 7-day sparkline for all five
// coins. The free tier hard-rate-limits the per-coin market_chart endpoint after a
// single call, so a burst of five silently drops most history — this avoids that.
$ids = implode(',', array_map(fn($c) => $c['id'], $COINS));
$market = fetch_json("https://api.coingecko.com/api/v3/coins/markets?vs_currency=usd&ids=$ids&sparkline=true&price_change_percentage=24h");
$byId = [];
if (is_array($market)) { foreach ($market as $row) { if (isset($row['id'])) { $byId[$row['id']] = $row; } } }
$ok = count($byId) > 0;

$now = time();
$coinsOut = [];
$history = [];
foreach ($COINS as $sym => $meta) {
    $row = $byId[$meta['id']] ?? null;
    $coinsOut[] = [
        'symbol' => $sym,
        'name' => $meta['name'],
        'usd' => $row['current_price'] ?? null,
        'change24h' => $row['price_change_percentage_24h'] ?? null,
    ];
    // 7-day sparkline is hourly and timestamp-less; synthesize evenly-spaced times ending now.
    $spark = $row['sparkline_in_7d']['price'] ?? [];
    $n = count($spark);
    $pts = [];
    foreach ($spark as $i => $p) { $pts[] = [$now - ($n - 1 - $i) * 3600, $p]; }
    $history[$sym] = $pts;
}

// Align every implemented pair to the single $0.001/XMZ opening rate, priced live.
$openingUsd = 0.001;
$openingPairs = [];
foreach ($coinsOut as $c) {
    if (!is_numeric($c['usd']) || $c['usd'] <= 0) { continue; }
    $openingPairs[] = [
        'symbol' => $c['symbol'],
        'coinUsd' => $c['usd'],
        'coinPerXmz' => $openingUsd / $c['usd'],   // 1 XMZ = this much coin
        'xmzPerCoin' => $c['usd'] / $openingUsd,    // 1 coin = this many XMZ
    ];
}

$payload = [
    'updated' => gmdate('c'),
    'source' => 'CoinGecko',
    'note' => 'Live USD market prices for the assets Monzero XMZ atomically swaps against. XMZ itself has no verified market price.',
    'openingRate' => [
        'perXmzUsd' => $openingUsd,
        'type' => 'dex-opening-quote',
        'basis' => '2026-10-31 mainnet test: 0.01 BNB for 7000 XMZ at BNB ~$700',
        'note' => 'The rate the Monzero DEX will first offer XMZ at, set by the project. NOT a market price; real swaps move it from here. Every pair below is this one rate priced in each coin live.',
        'pairs' => $openingPairs,
    ],
    'coins' => $coinsOut,
    'history' => $history,
    'dexSwaps' => array_map(function ($s) {
        $s['settled'] = $s['settled'] !== null ? json_decode('"' . $s['settled'] . '"') : null;
        return $s;
    }, $DEX_SWAPS),
];
$json = json_encode($payload, JSON_UNESCAPED_SLASHES);

// Only overwrite the cache when the upstream fetch actually succeeded; otherwise serve
// the last good cache rather than a hollow payload.
if ($ok) { @file_put_contents($cache, $json); }
elseif (is_file($cache)) { echo maybe_only(file_get_contents($cache)); exit; }

echo maybe_only($json);

function maybe_only(string $json): string {
    if (($_GET['only'] ?? '') !== 'coins') return $json;
    $d = json_decode($json, true);
    return json_encode([
        'updated' => $d['updated'] ?? null, 'source' => $d['source'] ?? null,
        'note' => $d['note'] ?? null, 'coins' => $d['coins'] ?? [],
    ], JSON_UNESCAPED_SLASHES);
}
