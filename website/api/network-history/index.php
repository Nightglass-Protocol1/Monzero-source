<?php
declare(strict_types=1);
// Ingest for network history exports (utils/website/history_upload.py on the EU node). The body is
// gzip-compressed JSON {range: export}; it is signed with the shared secret over
// "timestamp\nnonce\nbody". Each accepted export replaces /network/data/<range>.json atomically.
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

const HISTORY_RANGES = ['24h', '7d', '30d', '90d', '1y', 'all'];
const HISTORY_MAX_BODY = 2097152;      // compressed
const HISTORY_MAX_JSON = 8388608;      // decompressed
const HISTORY_MAX_POINTS = 721;

function reply(int $status, array $body): never { http_response_code($status); echo json_encode($body); exit; }

function privateDir(): string {
    $configured = getenv('MONZERO_PRIVATE_DIR');
    return is_string($configured) && $configured !== '' ? $configured : dirname(__DIR__, 3) . '/monzero-private';
}

function dataDir(): string {
    $configured = getenv('MONZERO_NETWORK_DATA_DIR');
    return is_string($configured) && $configured !== '' ? $configured : dirname(__DIR__, 2) . '/network/data';
}

function exportProblem(string $range, mixed $e, int $now): ?string {
    if (!is_array($e) || ($e['schema'] ?? null) !== 'monzero-network-history-v1' || ($e['network'] ?? null) !== 'mainnet' || ($e['range'] ?? null) !== $range) return 'schema';
    if (!is_int($e['generated_at'] ?? null) || abs($now - $e['generated_at']) > 900) return 'time';
    if (($e['sources'] ?? null) !== ['live']) return 'source';
    if (!is_int($e['bucket_seconds'] ?? null) || $e['bucket_seconds'] < 60) return 'bucket';
    if (!is_array($e['points'] ?? null) || !array_is_list($e['points']) || count($e['points']) > HISTORY_MAX_POINTS) return 'points';
    foreach ($e['points'] as $p) if (!is_array($p) || !is_int($p['first_observed_at'] ?? null) || !is_int($p['samples'] ?? null)) return 'point';
    if (!is_array($e['limitations'] ?? null)) return 'limitations';
    return null;
}

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') { header('Allow: POST'); reply(405, ['error' => 'POST only']); }
$path = privateDir() . '/network-history-config.php';
$config = is_file($path) && !is_link($path) ? (static fn() => require $path)() : [];
$secret = is_array($config) && is_string($config['secret'] ?? null) && preg_match('/\A[0-9a-f]{64}\z/', $config['secret']) ? $config['secret'] : '';
if ($secret === '') reply(503, ['error' => 'Not configured']);

$body = (string)file_get_contents('php://input', false, null, 0, HISTORY_MAX_BODY + 1);
if (strlen($body) > HISTORY_MAX_BODY) reply(413, ['error' => 'Too large']);
$ts = (string)($_SERVER['HTTP_X_HISTORY_TIMESTAMP'] ?? '');
$nonce = (string)($_SERVER['HTTP_X_HISTORY_NONCE'] ?? '');
$signature = (string)($_SERVER['HTTP_X_HISTORY_SIGNATURE'] ?? '');
$now = time();
if (!preg_match('/\A\d{1,12}\z/', $ts) || abs($now - (int)$ts) > 300 || !preg_match('/\A[0-9a-f]{32}\z/', $nonce)
    || !hash_equals(hash_hmac('sha256', "$ts\n$nonce\n" . $body, $secret), $signature)) reply(401, ['error' => 'Bad signature']);

$state = privateDir() . '/network-history';
if (!is_dir($state)) @mkdir($state, 0700, true);
$lock = @fopen("$state/.lock", 'c');
if (!$lock || !flock($lock, LOCK_EX)) reply(503, ['error' => 'Storage unavailable']);
$noncesPath = "$state/nonces.json";
$nonces = is_file($noncesPath) ? json_decode((string)file_get_contents($noncesPath), true) : [];
$nonces = array_filter(is_array($nonces) ? $nonces : [], fn($at) => is_int($at) && $at > $now - 900);
if (isset($nonces[$nonce])) reply(409, ['error' => 'Replayed']);
$nonces[$nonce] = $now;
file_put_contents($noncesPath, json_encode($nonces), LOCK_EX);
@chmod($noncesPath, 0600);

$json = @gzdecode($body, HISTORY_MAX_JSON);
$exports = is_string($json) ? json_decode($json, true) : null;
if (!is_array($exports) || !$exports || array_diff(array_keys($exports), HISTORY_RANGES)) reply(400, ['error' => 'Invalid upload']);
$directory = dataDir();
if (!is_dir($directory)) @mkdir($directory, 0755, true);
if (!is_dir($directory) || is_link($directory)) reply(503, ['error' => 'Storage unavailable']);

$written = [];
foreach ($exports as $range => $export) {
    if ($problem = exportProblem($range, $export, $now)) reply(400, ['error' => "Invalid $range export ($problem)", 'written' => $written]);
    $target = "$directory/$range.json";
    $current = is_file($target) ? json_decode((string)file_get_contents($target, false, null, 0, HISTORY_MAX_JSON), true) : null;
    if (is_array($current) && is_int($current['generated_at'] ?? null) && $export['generated_at'] <= $current['generated_at']) continue;  // older or same: keep
    $tmp = "$directory/.history-upload-" . bin2hex(random_bytes(6));
    if (file_put_contents($tmp, json_encode($export, JSON_UNESCAPED_SLASHES | JSON_PRESERVE_ZERO_FRACTION)) === false || !chmod($tmp, 0644) || !rename($tmp, $target)) {
        @unlink($tmp); reply(503, ['error' => 'Storage unavailable', 'written' => $written]);
    }
    $written[] = $range;
}
reply(200, ['ok' => true, 'written' => $written]);
