<?php
declare(strict_types=1);
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
// Shared read-only API v1 helpers. Only fixed node methods are reachable; no caller input is
// forwarded except a validated height or count. Responses use one envelope and error shape.

const V1_SCHEMA = 'monzero-api-v1';
const V1_NODE_RPC = 'http://node.monzero.org:6175';
const V1_SOURCE = 'node.monzero.org public node';
const V1_MAX_RESPONSE_BYTES = 1048576;
const V1_TARGET_SECONDS = 120;
const V1_CLIENT_LIMIT = 60;   // requests per client per minute
const V1_TOTAL_LIMIT = 1200;  // requests per deployment per minute
const V1_ALLOWED_METHODS = ['get_info', 'get_last_block_header', 'get_block_header_by_height', 'get_block_headers_range', 'hard_fork_info', 'get_block'];

function v1Headers(int $maxAge): void {
    header('Content-Type: application/json; charset=utf-8');
    header('X-Content-Type-Options: nosniff');
    header('Referrer-Policy: no-referrer');
    // Public, credential-free, read-only data: any origin may read it, nothing else is allowed.
    header('Access-Control-Allow-Origin: *');
    header('Access-Control-Allow-Methods: GET, HEAD, OPTIONS');
    header('Cache-Control: ' . ($maxAge > 0 ? "public, max-age=$maxAge" : 'no-store'));
}

function v1Error(string $code, string $message, int $status, int $retryAfter = 0): never {
    http_response_code($status);
    header('Cache-Control: no-store');
    if ($retryAfter > 0) header("Retry-After: $retryAfter");
    echo json_encode(['schema' => V1_SCHEMA, 'error' => ['code' => $code, 'message' => $message]], JSON_UNESCAPED_SLASHES);
    exit;
}

function v1Reply(array $data, ?int $observedAt = null, ?string $source = null): never {
    $body = ['schema' => V1_SCHEMA, 'data' => $data];
    if ($observedAt !== null) $body['observed_at'] = $observedAt;
    if ($source !== null) $body['source'] = $source;
    $json = json_encode($body, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE | JSON_THROW_ON_ERROR);
    if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'HEAD') echo $json;
    exit;
}

// Start every endpoint: method check, allowed query keys, then rate limit.
function v1Begin(int $maxAge, array $allowedQuery = []): void {
    v1Headers($maxAge);
    $method = $_SERVER['REQUEST_METHOD'] ?? '';
    if ($method === 'OPTIONS') { http_response_code(204); exit; }
    if ($method !== 'GET' && $method !== 'HEAD') { header('Allow: GET, HEAD, OPTIONS'); v1Error('method_not_allowed', 'Only GET is supported.', 405); }
    foreach (array_keys($_GET) as $key) {
        if (!in_array($key, $allowedQuery, true)) v1Error('invalid_parameter', 'Unknown query parameter.', 400);
        if (!is_string($_GET[$key])) v1Error('invalid_parameter', 'Query parameters must be single values.', 400);
    }
    v1Throttle();
}

function v1StateDirectory(): string {
    $directory = sys_get_temp_dir() . '/monzero-api-v1-' . substr(hash('sha256', __DIR__), 0, 16);
    if (!file_exists($directory)) @mkdir($directory, 0700);
    if (!is_dir($directory) || is_link($directory) || (fileperms($directory) & 0077) !== 0
        || (function_exists('posix_geteuid') && fileowner($directory) !== posix_geteuid()))
        v1Error('unavailable', 'The API is temporarily unavailable.', 503, 5);
    return $directory;
}

// Fixed one-minute window. Clients are keyed by an HMAC of the address with a salt that is
// replaced every window, so stored keys cannot be linked to visitors across minutes.
function v1Throttle(): void {
    $path = v1StateDirectory() . '/throttle.json';
    if (is_link($path)) v1Error('unavailable', 'The API is temporarily unavailable.', 503, 5);
    $file = @fopen($path, 'c+');
    if (!$file || !flock($file, LOCK_EX)) v1Error('unavailable', 'The API is temporarily unavailable.', 503, 2);
    try {
        @chmod($path, 0600);
        $state = json_decode((string)stream_get_contents($file, 4194305), true);
        $now = time();
        if (!is_array($state) || !is_string($state['salt'] ?? null) || !is_int($state['window'] ?? null)
            || $state['window'] > $now || $state['window'] <= $now - 60 || !is_array($state['clients'] ?? null))
            $state = ['salt' => bin2hex(random_bytes(16)), 'window' => $now, 'total' => 0, 'clients' => []];
        $client = hash_hmac('sha256', (string)($_SERVER['REMOTE_ADDR'] ?? 'unknown'), $state['salt']);
        $retry = max(1, $state['window'] + 60 - $now);
        if (($state['clients'][$client] ?? 0) >= V1_CLIENT_LIMIT || ($state['total'] ?? 0) >= V1_TOTAL_LIMIT)
            v1Error('rate_limited', 'Too many requests. Retry after the indicated delay.', 429, $retry);
        $state['clients'][$client] = ($state['clients'][$client] ?? 0) + 1;
        $state['total'] = ($state['total'] ?? 0) + 1;
        $bytes = json_encode($state);
        rewind($file);
        if (!ftruncate($file, 0) || fwrite($file, $bytes) !== strlen($bytes)) v1Error('unavailable', 'The API is temporarily unavailable.', 503, 2);
        fflush($file);
    } finally {
        flock($file, LOCK_UN);
        fclose($file);
    }
}

function v1Transport(string $body, string $path = '/json_rpc'): array {
    if (!in_array($path, ['/json_rpc', '/get_transactions'], true)) throw new LogicException('Path not allowed');
    $url = V1_NODE_RPC . $path;
    if (function_exists('curl_init')) {
        $response = '';
        $curl = curl_init($url);
        curl_setopt_array($curl, [
            CURLOPT_POST => true, CURLOPT_POSTFIELDS => $body,
            CURLOPT_HTTPHEADER => ['Content-Type: application/json'],
            CURLOPT_FOLLOWLOCATION => false, CURLOPT_PROTOCOLS => CURLPROTO_HTTP,
            CURLOPT_WRITEFUNCTION => static function ($handle, string $chunk) use (&$response): int {
                if (strlen($response) + strlen($chunk) > V1_MAX_RESPONSE_BYTES) return 0;
                $response .= $chunk;
                return strlen($chunk);
            },
            CURLOPT_CONNECTTIMEOUT => 2, CURLOPT_TIMEOUT => 5,
        ]);
        $ok = curl_exec($curl);
        $status = curl_getinfo($curl, CURLINFO_RESPONSE_CODE);
        curl_close($curl);
        return [$ok === false ? false : $response, $status];
    }
    $context = stream_context_create(['http' => [
        'method' => 'POST', 'header' => "Content-Type: application/json\r\nConnection: close\r\n",
        'content' => $body, 'timeout' => 5, 'ignore_errors' => true, 'follow_location' => 0,
    ]]);
    $deadline = microtime(true) + 5;
    $stream = @fopen($url, 'rb', false, $context);
    if (!$stream) return [false, 0];
    $response = '';
    try {
        while (!feof($stream)) {
            $remaining = $deadline - microtime(true);
            if ($remaining <= 0) return [false, 0];
            stream_set_timeout($stream, (int)$remaining, max(1, (int)(($remaining - (int)$remaining) * 1000000)));
            $chunk = fread($stream, 8192);
            if ($chunk === false || stream_get_meta_data($stream)['timed_out']
                || strlen($response) + strlen($chunk) > V1_MAX_RESPONSE_BYTES) return [false, 0];
            $response .= $chunk;
        }
    } finally { fclose($stream); }
    preg_match('/^HTTP\/\S+\s+(\d{3})\b/', $http_response_header[0] ?? '', $match);
    return [$response, isset($match[1]) ? (int)$match[1] : 0];
}

function v1Rpc(string $method, array $params = []): array {
    if (!in_array($method, V1_ALLOWED_METHODS, true)) throw new LogicException('Method not allowed');
    $payload = ['jsonrpc' => '2.0', 'id' => 'api-v1', 'method' => $method];
    if ($params) $payload['params'] = $params;
    [$response, $status] = v1Transport(json_encode($payload));
    $data = is_string($response) ? json_decode($response, true) : null;
    $result = $status === 200 && is_array($data) ? ($data['result'] ?? null) : null;
    if (!is_array($result) || ($result['status'] ?? null) !== 'OK') throw new RuntimeException('Node request failed');
    return $result;
}

// Cache a node observation under a fixed name; concurrent misses wait on one refresher.
function v1Cached(string $name, int $ttl, callable $fetch): array {
    if (!preg_match('/\A[a-z0-9-]{1,64}\z/', $name)) throw new LogicException('Invalid cache name');
    $path = v1StateDirectory() . "/cache-$name.json";
    if (is_link($path)) throw new RuntimeException('Cache unavailable');
    $file = @fopen($path, 'c+');
    if (!$file || !flock($file, LOCK_EX)) throw new RuntimeException('Cache unavailable');
    try {
        @chmod($path, 0600);
        $cached = json_decode((string)stream_get_contents($file, 2 * V1_MAX_RESPONSE_BYTES), true);
        $now = time();
        if (is_array($cached) && is_int($cached['observed_at'] ?? null) && $cached['observed_at'] <= $now
            && $now - $cached['observed_at'] < $ttl && is_array($cached['value'] ?? null)) return $cached;
        $entry = ['observed_at' => $now, 'value' => $fetch()];
        $bytes = json_encode($entry, JSON_THROW_ON_ERROR | JSON_UNESCAPED_SLASHES);
        rewind($file);
        if (ftruncate($file, 0) && fwrite($file, $bytes) === strlen($bytes)) fflush($file);
        return $entry;
    } finally {
        flock($file, LOCK_UN);
        fclose($file);
    }
}

function v1Hash(mixed $value): bool { return is_string($value) && preg_match('/\A[0-9a-f]{64}\z/', $value) === 1; }
function v1Uint(mixed $value): bool { return is_int($value) && $value >= 0; }

// Normalise one header; anything malformed makes the whole observation unavailable.
function v1Header(mixed $h): array {
    if (!is_array($h) || !v1Uint($h['height'] ?? null) || !v1Hash($h['hash'] ?? null) || !v1Hash($h['prev_hash'] ?? null)
        || !v1Uint($h['timestamp'] ?? null) || !v1Uint($h['difficulty'] ?? null) || $h['difficulty'] === 0
        || !v1Uint($h['reward'] ?? null) || !v1Uint($h['num_txes'] ?? null) || !v1Uint($h['block_size'] ?? null)
        || !v1Uint($h['depth'] ?? null) || !is_bool($h['orphan_status'] ?? null) || !v1Uint($h['major_version'] ?? null))
        throw new RuntimeException('Invalid block header');
    return [
        'height' => $h['height'], 'hash' => $h['hash'], 'prev_hash' => $h['prev_hash'], 'timestamp' => $h['timestamp'],
        'difficulty' => (string)$h['difficulty'], 'reward_atomic' => (string)$h['reward'],
        'transactions' => $h['num_txes'], 'size_bytes' => $h['block_size'],
        'weight' => v1Uint($h['block_weight'] ?? null) ? $h['block_weight'] : null,
        'depth' => $h['depth'], 'orphan' => $h['orphan_status'], 'major_version' => $h['major_version'],
    ];
}

// get_info plus the matching tip header; mismatched or non-mainnet observations are refused.
function v1NetworkObservation(): array {
    return v1Cached('network', 15, static function (): array {
        $info = v1Rpc('get_info');
        $tip = v1Rpc('get_last_block_header');
        $header = v1Header($tip['block_header'] ?? null);
        if (($info['nettype'] ?? null) !== 'mainnet' || !v1Uint($info['height'] ?? null) || !v1Uint($info['difficulty'] ?? null)
            || $info['difficulty'] === 0 || !is_bool($info['synchronized'] ?? null)
            || $header['height'] !== $info['height'] - 1 || !hash_equals((string)($info['top_block_hash'] ?? ''), $header['hash']))
            throw new RuntimeException('Inconsistent network observation');
        return ['height' => $info['height'], 'difficulty' => $info['difficulty'], 'synchronized' => $info['synchronized'],
            'tx_pool_size' => v1Uint($info['tx_pool_size'] ?? null) ? $info['tx_pool_size'] : null, 'tip' => $header];
    });
}

// Reviewed site catalogs are read from disk with a size bound; they never contain secrets.
function v1Catalog(string $relative, string $schema): array {
    $path = dirname(__DIR__, 2) . '/' . $relative;
    $bytes = @file_get_contents($path, false, null, 0, 4194305);
    $data = is_string($bytes) && strlen($bytes) <= 4194304 ? json_decode($bytes, true) : null;
    if (!is_array($data) || ($data['schema'] ?? null) !== $schema || !is_array($data['records'] ?? null)
        || !is_string($data['updated_at'] ?? null)) v1Error('unavailable', 'This catalog is temporarily unavailable.', 503, 30);
    return $data;
}

function v1Unavailable(): never {
    error_log('Monzero API v1: node observation unavailable');
    v1Error('node_unavailable', 'Mainnet data is temporarily unavailable. Please retry.', 502, 5);
}

// --- Supply, computed exactly from the chain ---
// Each block mints its coinbase minus its transaction fees (what the core counts as generated
// coins). Starting from a checkpoint audited on 2026-10-20 (all 13,148 blocks from genesis checked:
// none minted more than the emission rule allows), new blocks are added as they come; a block that
// mints more than allowed stops the calculation. The checkpoint moves to 200 blocks below the tip
// and is checked against the block hash, so a reorganisation cannot leave a wrong total.
const V1_SUPPLY_SEED = ['height' => 12949, 'generated' => '243976348592396971', 'hash' => '0044914f6efe48a9a2ae3c3f7fa8de51532edcda3811be62c6810ed4874851d7'];
const V1_MONEY_SUPPLY = '10000000000000000000';
const V1_TAIL_REWARD = 326000000000;
const V1_SPEED_SHIFT = 19;
const V1_UNLOCK_BLOCKS = 60;

function v1BlockFees(int $height): int {
    $block = v1Rpc('get_block', ['height' => $height]);
    $hashes = $block['tx_hashes'] ?? [];
    if (!is_array($hashes) || !$hashes) return 0;
    foreach ($hashes as $h) if (!v1Hash($h)) throw new RuntimeException('Bad transaction hash');
    [$response, $status] = v1Transport(json_encode(['txs_hashes' => array_values($hashes), 'decode_as_json' => true]), '/get_transactions');
    $data = is_string($response) ? json_decode($response, true) : null;
    if ($status !== 200 || ($data['status'] ?? null) !== 'OK' || count($data['txs'] ?? []) !== count($hashes)) throw new RuntimeException('Transactions unavailable');
    $fees = 0;
    foreach ($data['txs'] as $tx) {
        $json = json_decode((string)($tx['as_json'] ?? ''), true);
        $fee = $json['rct_signatures']['txnFee'] ?? null;
        if (!is_int($fee) || $fee < 0) throw new RuntimeException('Unknown fee');
        $fees += $fee;
    }
    return $fees;
}

function v1Supply(): array {
    return v1Cached('supply', 300, static function (): array {
        $path = v1StateDirectory() . '/supply-checkpoint.json';
        $saved = json_decode((string)@file_get_contents($path), true);
        $checkpoint = is_array($saved) && is_int($saved['height'] ?? null) && $saved['height'] >= V1_SUPPLY_SEED['height'] ? $saved : V1_SUPPLY_SEED;
        $info = v1Rpc('get_info');
        $tip = (int)$info['height'] - 1;
        $generated = gmp_init($checkpoint['generated']);
        $byHeight = [];
        for ($start = $checkpoint['height']; $start <= $tip; $start += 1000) {
            $range = v1Rpc('get_block_headers_range', ['start_height' => $start, 'end_height' => min($tip, $start + 999)]);
            foreach ($range['headers'] ?? [] as $h) $byHeight[(int)$h['height']] = $h;
        }
        if (($byHeight[$checkpoint['height']]['hash'] ?? null) !== $checkpoint['hash']) {
            if ($checkpoint !== V1_SUPPLY_SEED) { @unlink($path); throw new RuntimeException('Checkpoint left the main chain; recomputing from the audited seed'); }
            throw new RuntimeException('Audited checkpoint is not on this chain');
        }
        $cap = gmp_init(V1_MONEY_SUPPLY);
        $next = null;
        $atUnlock = null;
        for ($height = $checkpoint['height'] + 1; $height <= $tip; $height++) {
            $h = $byHeight[$height] ?? null;
            if (!is_array($h) || !is_int($h['reward'] ?? null)) throw new RuntimeException('Missing block header');
            $allowed = gmp_div_q(gmp_sub($cap, $generated), gmp_pow(2, V1_SPEED_SHIFT));
            if (gmp_cmp($allowed, V1_TAIL_REWARD) < 0) $allowed = gmp_init(V1_TAIL_REWARD);
            $minted = gmp_sub(gmp_init($h['reward']), (int)($h['num_txes'] ?? 0) > 0 ? v1BlockFees($height) : 0);
            if (gmp_cmp($minted, $allowed) > 0) throw new RuntimeException("Block $height minted more than allowed");
            $generated = gmp_add($generated, $minted);
            if ($height === $tip - V1_UNLOCK_BLOCKS) $atUnlock = gmp_strval($generated);
            if ($height === $tip - 200) $next = ['height' => $height, 'generated' => gmp_strval($generated), 'hash' => $h['hash']];
        }
        if ($next) @file_put_contents($path, json_encode($next), LOCK_EX);
        return ['height' => $tip + 1, 'total_atomic' => gmp_strval($generated), 'circulating_atomic' => $atUnlock ?? gmp_strval($generated)];
    });
}

// Atomic units to a decimal XMZ string (11 decimals, trailing zeros trimmed).
function v1Xmz(string $atomic): string {
    $padded = str_pad($atomic, 12, '0', STR_PAD_LEFT);
    $fraction = rtrim(substr($padded, -11), '0');
    return substr($padded, 0, -11) . ($fraction !== '' ? '.' . $fraction : '');
}
