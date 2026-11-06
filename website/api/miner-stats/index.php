<?php
declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

const ACTIVE_SECONDS = 180;
const MAX_RECORD_AGE = 604800;
const HISTORY_DAYS = 30;        // daily totals kept for miners who opted in with "history": true
const HISTORY_SLOT = 600;       // uptime is counted in 10-minute slots
const HISTORY_BOARDS = ['7d' => 7, '30d' => 30];

$fileConfig = [];
$documentRoot = $_SERVER['DOCUMENT_ROOT'] ?? '';
if (is_string($documentRoot) && $documentRoot !== '') {
    $configPath = dirname($documentRoot) . '/.monzero-miner-stats.php';
    if (is_file($configPath)) {
        $loadedConfig = require $configPath;
        if (is_array($loadedConfig)) $fileConfig = $loadedConfig;
    }
}

function fail(int $status, string $message): never
{
    http_response_code($status);
    echo json_encode(['status' => $message]);
    exit;
}

function configValue(string $name): string
{
    global $fileConfig;
    if (isset($fileConfig[$name]) && is_string($fileConfig[$name])) {
        return trim($fileConfig[$name]);
    }
    $value = getenv($name);
    return is_string($value) ? trim($value) : '';
}

function dataPath(): string
{
    $configured = configValue('MONZERO_STATS_FILE');
    return $configured !== '' ? $configured : sys_get_temp_dir() . '/monzero-miner-stats.json';
}

function readRecords($handle): array
{
    rewind($handle);
    $contents = stream_get_contents($handle);
    if (!is_string($contents) || trim($contents) === '') return [];
    $decoded = json_decode($contents, true);
    return is_array($decoded) ? $decoded : [];
}

function historyPath(): string { return dataPath() . '.history.json'; }

// Opted-in daily totals: reports, summed hashrate, blocks gained (positive log increments) and a
// 144-bit mask of active 10-minute slots. Locked by the caller's lock on the stats file.
function historyLoad(): array {
    $data = json_decode((string)@file_get_contents(historyPath()), true);
    return is_array($data) && is_array($data['days'] ?? null) ? $data : ['days' => []];
}

function historySave(array $data, int $now): void {
    $cutoff = gmdate('Y-m-d', $now - HISTORY_DAYS * 86400);
    $data['days'] = array_filter($data['days'], fn($day) => is_string($day) && $day > $cutoff, ARRAY_FILTER_USE_KEY);
    $tmp = historyPath() . '.' . bin2hex(random_bytes(4));
    if (file_put_contents($tmp, json_encode($data, JSON_UNESCAPED_SLASHES)) !== false) { @chmod($tmp, 0600); rename($tmp, historyPath()); }
}

function recordHistory(string $id, float $hashrate, int $blocks, int $previousBlocks, int $now): void {
    $data = historyLoad();
    $day = gmdate('Y-m-d', $now);
    $entry = $data['days'][$day][$id] ?? ['r' => 0, 'h' => 0.0, 'b' => 0, 's' => str_repeat('0', 36)];
    $slot = intdiv($now % 86400, HISTORY_SLOT);
    $mask = str_pad((string)$entry['s'], 36, '0');
    $nibble = intdiv($slot, 4);
    $mask[$nibble] = dechex(hexdec($mask[$nibble]) | (1 << ($slot % 4)));
    $entry = ['r' => $entry['r'] + 1, 'h' => round($entry['h'] + $hashrate, 2), 'b' => $entry['b'] + max(0, $blocks - $previousBlocks), 's' => $mask];
    $data['days'][$day][$id] = $entry;
    historySave($data, $now);
}

// A report without the history flag withdraws consent: drop that miner's history at once.
function forgetHistory(string $id): void {
    if (!is_file(historyPath())) return;
    $data = historyLoad();
    $changed = false;
    foreach ($data['days'] as $day => $miners) if (isset($miners[$id])) { unset($data['days'][$day][$id]); $changed = true; }
    if ($changed) historySave($data, time());
}

function historyBoard(int $days, int $now): array {
    $data = historyLoad();
    $from = gmdate('Y-m-d', $now - ($days - 1) * 86400);
    $totals = [];
    foreach ($data['days'] as $day => $miners) {
        if (!is_string($day) || $day < $from || !is_array($miners)) continue;
        foreach ($miners as $id => $e) {
            $t = $totals[$id] ?? ['reports' => 0, 'hash' => 0.0, 'blocks' => 0, 'slots' => 0, 'days' => 0];
            $slots = 0;
            foreach (str_split(substr((string)($e['s'] ?? ''), 0, 36)) as $hex) $slots += substr_count(decbin(hexdec($hex)), '1');
            $totals[$id] = ['reports' => $t['reports'] + (int)$e['r'], 'hash' => $t['hash'] + (float)$e['h'], 'blocks' => $t['blocks'] + (int)$e['b'], 'slots' => $t['slots'] + $slots, 'days' => $t['days'] + 1];
        }
    }
    $miners = [];
    foreach ($totals as $id => $t) {
        if ($t['reports'] === 0) continue;
        $miners[] = ['name' => 'Miner ' . strtoupper(substr((string)$id, 0, 8)), 'average_hashrate' => round($t['hash'] / $t['reports'], 2),
            'uptime_percent' => round(100 * $t['slots'] / ($days * 86400 / HISTORY_SLOT), 1), 'blocks_found' => $t['blocks'], 'active_days' => $t['days']];
    }
    usort($miners, fn($a, $b) => $b['uptime_percent'] <=> $a['uptime_percent'] ?: strcmp($a['name'], $b['name']));
    return $miners;
}

$secret = configValue('MONZERO_STATS_SECRET');
if (strlen($secret) < 32) fail(503, 'NOT_CONFIGURED');

$path = dataPath();
$directory = dirname($path);
if (!is_dir($directory) || !is_writable($directory)) fail(503, 'STORAGE_UNAVAILABLE');

$handle = @fopen($path, 'c+');
if ($handle === false || !flock($handle, LOCK_EX)) fail(503, 'STORAGE_UNAVAILABLE');
$records = readRecords($handle);
$now = time();

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $token = configValue('MONZERO_STATS_INGEST_TOKEN');
    if (strlen($token) < 24) fail(503, 'INGEST_NOT_CONFIGURED');
    $authorization = $_SERVER['HTTP_AUTHORIZATION'] ?? '';
    if (!hash_equals('Bearer ' . $token, $authorization)) fail(401, 'UNAUTHORIZED');

    $raw = file_get_contents('php://input');
    $input = json_decode(is_string($raw) ? $raw : '', true);
    if (!is_array($input)) fail(400, 'INVALID_JSON');

    $installationId = $input['installation_id'] ?? '';
    $hashrate = filter_var($input['hashrate'] ?? null, FILTER_VALIDATE_FLOAT);
    $blocks = filter_var($input['blocks_found'] ?? null, FILTER_VALIDATE_INT);
    // Older reporters omitted active; a positive speed is their only evidence
    // of mining. New reporters can explicitly withdraw a stopped miner.
    $active = $input['active'] ?? ($hashrate !== false && $hashrate > 0);
    if (!is_bool($active)) fail(400, 'INVALID_ACTIVE_STATUS');
    if (!is_string($installationId) || !preg_match('/^[a-f0-9-]{32,64}$/i', $installationId)) fail(400, 'INVALID_INSTALLATION_ID');
    if ($hashrate === false || $hashrate < 0 || $hashrate > 1000000000000) fail(400, 'INVALID_HASHRATE');
    if ($blocks === false || $blocks < 0 || $blocks > 1000000000) fail(400, 'INVALID_BLOCK_COUNT');

    $privateId = hash_hmac('sha256', strtolower($installationId), $secret);
    $share = $input['history'] ?? false;
    if (!is_bool($share)) fail(400, 'INVALID_HISTORY_FLAG');
    if ($share && $active) recordHistory($privateId, (float)$hashrate, (int)$blocks, (int)($records[$privateId]['blocks_found'] ?? $blocks), $now);
    elseif (!$share) forgetHistory($privateId);
    $records[$privateId] = [
        'active' => $active,
        'hashrate' => $active ? round((float)$hashrate, 2) : 0,
        'blocks_found' => (int)$blocks,
        'updated_at' => $now,
    ];
}

foreach ($records as $id => $record) {
    if (!is_array($record) || ($record['updated_at'] ?? 0) < $now - MAX_RECORD_AGE) unset($records[$id]);
}

rewind($handle);
ftruncate($handle, 0);
fwrite($handle, json_encode($records, JSON_UNESCAPED_SLASHES));
fflush($handle);
flock($handle, LOCK_UN);
fclose($handle);

if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    echo json_encode(['status' => 'OK']);
    exit;
}
if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    header('Allow: GET, POST');
    fail(405, 'METHOD_NOT_ALLOWED');
}

$board = $_GET['board'] ?? null;
if ($board !== null) {
    if (!is_string($board) || !isset(HISTORY_BOARDS[$board])) fail(400, 'INVALID_BOARD');
    echo json_encode(['status' => 'OK', 'generated_at' => $now, 'window_days' => HISTORY_BOARDS[$board], 'miners' => historyBoard(HISTORY_BOARDS[$board], $now)], JSON_UNESCAPED_SLASHES);
    exit;
}

$miners = [];
foreach ($records as $id => $record) {
    $active = (int)$record['updated_at'] >= $now - ACTIVE_SECONDS
        && ($record['active'] ?? ((float)$record['hashrate'] > 0)) === true;
    if (!$active) continue;
    $miners[] = [
        'name' => 'Miner ' . strtoupper(substr($id, 0, 8)),
        'hashrate' => (float)$record['hashrate'],
        'blocks_found' => (int)$record['blocks_found'],
        'active' => true,
        'last_seen' => (int)$record['updated_at'],
    ];
}
usort($miners, static fn(array $a, array $b): int => $b['hashrate'] <=> $a['hashrate']);

echo json_encode([
    'status' => 'OK',
    'generated_at' => $now,
    'active_miners' => count($miners),
    'total_hashrate' => array_sum(array_column($miners, 'hashrate')),
    'total_blocks' => array_sum(array_column($miners, 'blocks_found')),
    'miners' => $miners,
], JSON_UNESCAPED_SLASHES);
