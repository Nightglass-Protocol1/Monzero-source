<?php
declare(strict_types=1);

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {
    http_response_code(405);
    header('Allow: POST');
    echo json_encode(['status' => 'METHOD_NOT_ALLOWED']);
    exit;
}

const NODE_CACHE_TTL_SECONDS = 15;
const NODE_FAILURE_TTL_SECONDS = 5;
const NODE_MAX_RESPONSE_BYTES = 262144;

const NODE_RPC = 'http://node.monzero.org:6175';
const MAX_TIP_AGE_SECONDS = 1800;
const MAX_FUTURE_SKEW_SECONDS = 300;
const GENESIS_HASH = '84f9ebdac8924806f037482ec16fd59b271e954d3e00363dd6c7e4ce9dd659e4';

function nodeRequest(string $path, string $body): array
{
    $url = NODE_RPC . $path;
    if (function_exists('curl_init')) {
        $response = '';
        $curl = curl_init($url);
        curl_setopt_array($curl, [
            CURLOPT_POST => true,
            CURLOPT_POSTFIELDS => $body,
            CURLOPT_HTTPHEADER => ['Content-Type: application/json'],
            CURLOPT_FOLLOWLOCATION => false,
            CURLOPT_WRITEFUNCTION => static function ($handle, string $chunk) use (&$response): int {
                if (strlen($response) + strlen($chunk) > NODE_MAX_RESPONSE_BYTES) return 0;
                $response .= $chunk;
                return strlen($chunk);
            },
            CURLOPT_CONNECTTIMEOUT => 2,
            CURLOPT_TIMEOUT => 5,
        ]);
        $ok = curl_exec($curl);
        $status = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
        $error = curl_error($curl);
        curl_close($curl);
        return [$ok === false ? false : $response, $status, $error];
    }

    $context = stream_context_create(['http' => [
        'method' => 'POST',
        'header' => "Content-Type: application/json\r\nConnection: close\r\n",
        'content' => $body,
        'timeout' => 5,
        'ignore_errors' => true,
        'follow_location' => 0,
    ]]);
    $deadline = microtime(true) + 5;
    $stream = @fopen($url, 'rb', false, $context);
    $response = '';
    if (!$stream) $response = false;
    else {
        try {
            while (!feof($stream)) {
                $remaining = $deadline - microtime(true);
                if ($remaining <= 0) { $response = false; break; }
                $seconds = (int)$remaining;
                stream_set_timeout($stream, $seconds, max(1, (int)(($remaining - $seconds) * 1000000)));
                $chunk = fread($stream, 8192);
                if ($chunk === false || stream_get_meta_data($stream)['timed_out']
                    || strlen($response) + strlen($chunk) > NODE_MAX_RESPONSE_BYTES) {
                    $response = false; break;
                }
                $response .= $chunk;
            }
        } finally { fclose($stream); }
    }
    $statusLine = $http_response_header[0] ?? '';
    preg_match('/^HTTP\/\S+\s+(\d{3})\b/', $statusLine, $match);
    $status = isset($match[1]) ? (int)$match[1] : 0;
    return [$response, $status, 'HTTP ' . $status];
}

// Cache public node observations only: no request bodies, addresses or visitor IDs.
// Separate worktrees/deployments have distinct private state directories.
function nodeCachedObservation($file): ?array
{
    $bytes = stream_get_contents($file, 2 * NODE_MAX_RESPONSE_BYTES + 4097);
    $cached = json_decode($bytes, true);
    $now = time();
    if (is_array($cached) && is_bool($cached['available'] ?? null)
        && (!$cached['available'] || (is_array($cached['info'] ?? null)
        && array_key_exists('header', $cached) && ($cached['header'] === null || is_array($cached['header']))))
        && is_int($cached['observed_at'] ?? null)
        && $cached['observed_at'] <= $now
        && $now - $cached['observed_at'] < (($cached['complete'] ?? false) ? NODE_CACHE_TTL_SECONDS : NODE_FAILURE_TTL_SECONDS))
        return $cached;
    return null;
}

function nodeObservation(): array
{
    $directory = sys_get_temp_dir() . '/monzero-node-status-' . substr(hash('sha256', __DIR__), 0, 16);
    if (!file_exists($directory)) @mkdir($directory, 0700);
    if (!is_dir($directory) || is_link($directory) || (fileperms($directory) & 0077) !== 0
        || (function_exists('posix_geteuid') && fileowner($directory) !== posix_geteuid()))
        throw new RuntimeException('Observation cache unavailable');
    $path = $directory . '/observation.json';
    if (is_link($path)) throw new RuntimeException('Observation cache unavailable');
    $file = @fopen($path, 'c+');
    if (!$file) throw new RuntimeException('Observation cache unavailable');
    try {
        if (!flock($file, LOCK_SH | LOCK_NB)) throw new RuntimeException('Observation busy');
        if (!@chmod($path, 0600)) throw new RuntimeException('Observation cache unavailable');
        $cached = nodeCachedObservation($file);
        if ($cached !== null) return $cached;
        flock($file, LOCK_UN);
        if (!flock($file, LOCK_EX | LOCK_NB)) throw new RuntimeException('Observation busy');
        rewind($file);
        $cached = nodeCachedObservation($file);
        if ($cached !== null) return $cached;
        $now = time();
        [$response, $status] = nodeRequest('/get_info', '{}');
        $info = is_string($response) ? json_decode($response, true) : null;
        $observation = ['observed_at' => $now, 'available' => false];
        if ($status === 200 && is_array($info) && ($info['status'] ?? '') === 'OK') {
            [$response, $status] = nodeRequest('/json_rpc', json_encode([
                'jsonrpc' => '2.0', 'id' => 'website-readiness', 'method' => 'get_last_block_header',
            ]));
            $data = is_string($response) ? json_decode($response, true) : null;
            $header = $status === 200 && is_array($data) ? ($data['result']['block_header'] ?? null) : null;
            if (is_array($header)) {
                $complete = isset($header['height'], $header['hash'])
                    && (int)$header['height'] === (int)($info['height'] ?? 0) - 1
                    && is_string($header['hash']) && is_string($info['top_block_hash'] ?? null)
                    && hash_equals($info['top_block_hash'], $header['hash']);
                $observation = ['observed_at' => $now, 'available' => true, 'complete' => $complete,
                    'info' => $info, 'header' => $header];
            }
        }
        $bytes = json_encode($observation, JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE);
        if (strlen($bytes) > 2 * NODE_MAX_RESPONSE_BYTES + 4096) throw new RuntimeException('Observation too large');
        rewind($file);
        if (!ftruncate($file, 0) || fwrite($file, $bytes) !== strlen($bytes) || !fflush($file))
            throw new RuntimeException('Observation cache unavailable');
        return $observation;
    } finally {
        flock($file, LOCK_UN);
        fclose($file);
    }
}
try {
    $observation = nodeObservation();
} catch (Throwable $error) {
    http_response_code(503);
    header('Retry-After: 2');
    echo json_encode(['status' => 'NODE_OBSERVATION_UNAVAILABLE']);
    exit;
}
if (!$observation['available']) {
    http_response_code(502);
    header('Retry-After: 5');
    echo json_encode(['status' => 'NODE_UNAVAILABLE']);
    exit;
}
$decoded = $observation['info'];
$header = $observation['header'];
$decoded['observed_at'] = $observation['observed_at'];
$decoded['observation_age_seconds'] = max(0, time() - $observation['observed_at']);

$peerCount = (int)($decoded['incoming_connections_count'] ?? 0)
    + (int)($decoded['outgoing_connections_count'] ?? 0);
$decoded['connections_hidden'] = !empty($decoded['restricted']) && $peerCount === 0;
$decoded['peer_ready'] = !$decoded['connections_hidden'] && $peerCount >= 2;

$tipTimestamp = is_array($header) ? ($header['timestamp'] ?? null) : null;
$tipAge = is_int($tipTimestamp) ? time() - $tipTimestamp : null;
$genesisOnly = (int)($decoded['height'] ?? 0) === 1
    && hash_equals(GENESIS_HASH, (string)($decoded['top_block_hash'] ?? ''));
$tipMatches = is_array($header) && isset($header['height'], $header['hash'])
    && (int)$header['height'] === (int)($decoded['height'] ?? 0) - 1
    && hash_equals((string)($decoded['top_block_hash'] ?? ''), (string)$header['hash']);
$tipFresh = $tipMatches && ($genesisOnly || (is_int($tipAge)
    && $tipAge <= MAX_TIP_AGE_SECONDS
    && $tipAge >= -MAX_FUTURE_SKEW_SECONDS));

$decoded['tip_timestamp'] = $tipTimestamp;
$decoded['tip_height'] = $tipMatches ? $header['height'] : null;
$decoded['tip_reward_atomic'] = $tipMatches ? ($header['reward'] ?? null) : null;
$decoded['tip_age_seconds'] = $tipAge;
$decoded['tip_fresh'] = $tipFresh;
$decoded['network_ready'] = ($decoded['status'] ?? '') === 'OK'
    && ($decoded['nettype'] ?? '') === 'mainnet'
    && ($decoded['synchronized'] ?? false) === true
    && $tipFresh
    && $decoded['peer_ready'];
echo json_encode($decoded, JSON_UNESCAPED_SLASHES);
