<?php
declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

const ACTIVE_SECONDS = 180;
const MAX_RECORD_AGE = 604800;

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
    if (!is_string($installationId) || !preg_match('/^[a-f0-9-]{32,64}$/i', $installationId)) fail(400, 'INVALID_INSTALLATION_ID');
    if ($hashrate === false || $hashrate < 0 || $hashrate > 1000000000000) fail(400, 'INVALID_HASHRATE');
    if ($blocks === false || $blocks < 0 || $blocks > 1000000000) fail(400, 'INVALID_BLOCK_COUNT');

    $privateId = hash_hmac('sha256', strtolower($installationId), $secret);
    $records[$privateId] = [
        'hashrate' => round((float)$hashrate, 2),
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

$miners = [];
foreach ($records as $id => $record) {
    $active = (int)$record['updated_at'] >= $now - ACTIVE_SECONDS;
    if (!$active) continue;
    $miners[] = [
        'name' => 'Miner ' . strtoupper(substr($id, 0, 8)),
        'hashrate' => (float)$record['hashrate'],
        'blocks_found' => (int)$record['blocks_found'],
        'active' => true,
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
