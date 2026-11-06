<?php
declare(strict_types=1);
// Ingest for the country-level node census (utils/nodemap/node_census.py). Every request is
// signed with the shared secret over "timestamp\nnonce\nbody"; stale or replayed requests are refused.
require dirname(__DIR__, 2) . '/lib/node-census.php';

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

function reply(int $status, array $body): never { http_response_code($status); echo json_encode($body); exit; }

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') { header('Allow: POST'); reply(405, ['error' => 'POST only']); }
$secret = censusSecret();
if ($secret === '') reply(503, ['error' => 'Not configured']);
$body = (string)file_get_contents('php://input', false, null, 0, 65537);
if (strlen($body) > 65536) reply(413, ['error' => 'Too large']);
$ts = (string)($_SERVER['HTTP_X_CENSUS_TIMESTAMP'] ?? '');
$nonce = (string)($_SERVER['HTTP_X_CENSUS_NONCE'] ?? '');
$signature = (string)($_SERVER['HTTP_X_CENSUS_SIGNATURE'] ?? '');
$now = time();
if (!preg_match('/\A\d{1,12}\z/', $ts) || abs($now - (int)$ts) > 300 || !preg_match('/\A[0-9a-f]{32}\z/', $nonce)
    || !hash_equals(hash_hmac('sha256', "$ts\n$nonce\n" . $body, $secret), $signature)) reply(401, ['error' => 'Bad signature']);

$directory = censusDir();
if (!is_dir($directory)) @mkdir($directory, 0700, true);
$lock = @fopen($directory . '/.lock', 'c');
if (!$lock || !flock($lock, LOCK_EX)) reply(503, ['error' => 'Storage unavailable']);

$nonces = array_filter(censusRead('nonces.json') ?? [], fn($at) => is_int($at) && $at > $now - 900);
if (isset($nonces[$nonce])) reply(409, ['error' => 'Replayed']);
$nonces[$nonce] = $now;

$payload = json_decode($body, true);
if (is_array($payload) && ($payload['schema'] ?? null) === CENSUS_TAGGED_SCHEMA) {
    // One vantage's tags: keep only its newest report (private), then publish the merged counts.
    if ($problem = censusTaggedProblem($payload, $now)) reply(400, ['error' => "Invalid census ($problem)"]);
    $name = 'vantage-' . $payload['vantage'] . '.json';
    $previous = censusRead($name);
    if ($previous && is_int($previous['observed_at'] ?? null) && $payload['observed_at'] <= $previous['observed_at']) reply(409, ['error' => 'Older than the stored census']);
    try {
        censusWrite('nonces.json', $nonces);
        censusWrite($name, $payload);
        $reports = [];
        foreach (array_keys(CENSUS_VANTAGES) as $v) {
            $r = censusRead("vantage-$v.json");
            if ($r && censusTaggedProblem($r, (int)$r['observed_at']) === null) $reports[] = $r;
        }
        $payload = censusMerge($reports);
    } catch (Throwable $e) { reply(503, ['error' => 'Storage unavailable']); }
} else {
    if ($problem = censusProblem($payload, $now)) reply(400, ['error' => "Invalid census ($problem)"]);
}
$latest = censusRead('latest.json');
if ($latest && is_int($latest['observed_at'] ?? null) && $payload['observed_at'] < $latest['observed_at']) reply(409, ['error' => 'Older than the stored census']);

try {
    censusWrite('nonces.json', $nonces);
    censusWrite('latest.json', $payload);
    // One snapshot per UTC day (the day's latest), kept for CENSUS_HISTORY_DAYS.
    $daily = censusRead('daily.json') ?? [];
    $daily[gmdate('Y-m-d', $payload['observed_at'])] = ['observed_at' => $payload['observed_at'], 'totals' => $payload['totals'], 'countries' => $payload['countries']] + (isset($payload['vantages']) ? ['vantages' => $payload['vantages']] : []);
    ksort($daily);
    censusWrite('daily.json', array_slice($daily, -CENSUS_HISTORY_DAYS, null, true));
} catch (Throwable $e) { reply(503, ['error' => 'Storage unavailable']); }
reply(200, ['ok' => true]);
