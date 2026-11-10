<?php
declare(strict_types=1);
// Read-only window onto the open DEX relays (monzero-dex-relay) for this site's market view.
// The page may only fetch its own origin, so this forwards a few GET reads and caches them
// briefly. Nothing here is trusted for funds: offers are maker-signed and the app checks them
// itself; trades are ones a relay read on chain. Relays come from monzero-private/dex-status.php
// ('relays' => ['https://relay.example.org', ...]); with none set it answers "no relay".
header('Content-Type: application/json; charset=utf-8');
header('X-Content-Type-Options: nosniff');
header('Cache-Control: public, max-age=20');

function relayFail(int $code, string $why): never {
    http_response_code($code);
    echo json_encode(['error' => $why]);
    exit;
}

function relayList(): array {
    $d = getenv('MONZERO_PRIVATE_DIR');
    $dir = is_string($d) && $d !== '' ? $d : dirname(__DIR__, 2) . '/monzero-private';
    $path = $dir . '/dex-status.php';
    if (!is_file($path) || is_link($path)) return [];
    $o = (static fn() => require $path)();
    if (!is_array($o) || !isset($o['relays']) || !is_array($o['relays'])) return [];
    return array_values(array_filter($o['relays'], static fn($u) =>
        is_string($u) && strlen($u) <= 200 && preg_match('#^https?://[A-Za-z0-9.\-]+(:\d{1,5})?(/[A-Za-z0-9._\-/]*)?$#', $u)));
}

$what = $_GET['what'] ?? 'info';
$pair = $_GET['pair'] ?? null;
$query = [];
switch ($what) {
    case 'info':
        $path = '/v1/info';
        break;
    case 'listings':
    case 'prints':
    case 'candles':
        if (!is_string($pair) || !in_array($pair, ['ETH/XMZ', 'BNB/XMZ', 'SOL/XMZ', 'BTC/XMZ', 'LTC/XMZ'], true)) relayFail(400, 'unknown pair');
        // Bitcoin and Litecoin offers are kept apart from the EVM ones (amount in satoshis/litoshis)
        $utxo = $pair === 'BTC/XMZ' || $pair === 'LTC/XMZ';
        $path = ($utxo && $what === 'listings') ? '/v1/utxo/listings' : '/v1/' . $what;
        $query['pair'] = $pair;
        if ($what === 'candles') {
            $interval = (int)($_GET['interval'] ?? 3600);
            if (!in_array($interval, [3600, 14400, 86400], true)) relayFail(400, 'interval must be 3600, 14400 or 86400');
            $query['interval'] = $interval;
        }
        if ($what !== 'listings' && isset($_GET['since'])) $query['since'] = max(0, (int)$_GET['since']);
        break;
    default:
        relayFail(400, 'unknown request');
}

$relays = relayList();
if (!$relays) { echo json_encode(['relay' => null]); exit; }

$suffix = $path . ($query ? '?' . http_build_query($query) : '');
$cache = sys_get_temp_dir() . '/monzero-dex-relay-' . hash('sha256', $suffix) . '.json';
if (is_file($cache) && time() - filemtime($cache) < 20) {
    readfile($cache);
    exit;
}

foreach ($relays as $base) {
    $ch = curl_init(rtrim($base, '/') . $suffix);
    curl_setopt_array($ch, [
        CURLOPT_RETURNTRANSFER => true,
        CURLOPT_CONNECTTIMEOUT => 3,
        CURLOPT_TIMEOUT => 6,
        CURLOPT_FOLLOWLOCATION => false,
        CURLOPT_PROTOCOLS => CURLPROTO_HTTP | CURLPROTO_HTTPS,
        CURLOPT_USERAGENT => 'monzero.org-dex-view',
        CURLOPT_MAXFILESIZE => 4 * 1024 * 1024,
    ]);
    $body = curl_exec($ch);
    $ok = is_string($body) && curl_getinfo($ch, CURLINFO_RESPONSE_CODE) === 200;
    curl_close($ch);
    if (!$ok || strlen($body) > 4 * 1024 * 1024) continue;
    $data = json_decode($body, true);
    if (!is_array($data)) continue;
    $out = json_encode(['relay' => parse_url($base, PHP_URL_HOST)] + $data, JSON_UNESCAPED_SLASHES);
    @file_put_contents($cache . '.tmp', $out) && @rename($cache . '.tmp', $cache);
    echo $out;
    exit;
}
if (is_file($cache)) { readfile($cache); exit; }
relayFail(503, 'no relay answered');
