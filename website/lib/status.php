<?php
declare(strict_types=1);
// Health of the website's background services, for /status/ and owner alert emails.
// Evaluated at most once a minute; alerts are sent when a check changes state (and daily while
// it stays bad). Nothing here exposes secrets, IP addresses or member data.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once __DIR__ . '/auth.php';
require_once __DIR__ . '/node-census.php';

const STATUS_SCHEMA = 'monzero-status-v1';
const STATUS_CACHE_SECONDS = 60;
const STATUS_REMIND_SECONDS = 86400;
const STATUS_PING_NODES = ['eu' => 'EU node (monzero-eu)', 'us' => 'US node (monzero-us)'];

function statusPrivateDir(): string {
    $configured = getenv('MONZERO_PRIVATE_DIR');
    return is_string($configured) && $configured !== '' ? $configured : dirname(__DIR__, 2) . '/monzero-private';
}

function statusConfig(): array {
    $path = statusPrivateDir() . '/status-config.php';
    $config = is_file($path) && !is_link($path) ? (static fn() => require $path)() : [];
    $config = is_array($config) ? $config : [];
    $secret = is_string($config['secret'] ?? null) && preg_match('/\A[0-9a-f]{64}\z/', $config['secret']) ? $config['secret'] : '';
    $to = authEmail($config['alert_to'] ?? 'security@monzero.org') ?? 'security@monzero.org';
    return ['secret' => $secret, 'alert_to' => $to, 'pool_url' => is_string($config['pool_url'] ?? null) ? $config['pool_url'] : 'https://pool.monzero.org/api/stats',
        'node_rpc' => is_string($config['node_rpc'] ?? null) ? $config['node_rpc'] : 'http://node.monzero.org:6175'];
}

function statusReadJson(string $path): ?array {
    if (!is_file($path) || is_link($path)) return null;
    $data = json_decode((string)@file_get_contents($path, false, null, 0, 8388608), true);
    return is_array($data) ? $data : null;
}

function statusWriteJson(string $path, array $data): void {
    $tmp = $path . '.' . bin2hex(random_bytes(6));
    $old = umask(077);
    try { if (file_put_contents($tmp, json_encode($data, JSON_UNESCAPED_SLASHES)) !== false) rename($tmp, $path); }
    finally { umask($old); @unlink($tmp); }
}

function statusAgeText(int $seconds): string {
    if ($seconds < 120) return "$seconds s";
    if ($seconds < 7200) return intdiv($seconds, 60) . ' min';
    if ($seconds < 172800) return intdiv($seconds, 3600) . ' h';
    return intdiv($seconds, 86400) . ' days';
}

// Age-based state: ok within $ok seconds, warn within $warn, otherwise down.
function statusByAge(?int $at, int $now, int $ok, int $warn): string {
    if ($at === null) return 'down';
    $age = $now - $at;
    return $age <= $ok ? 'ok' : ($age <= $warn ? 'warn' : 'down');
}

function statusRpc(string $base, string $method): ?array {
    $context = stream_context_create(['http' => ['method' => 'POST', 'header' => 'Content-Type: application/json', 'timeout' => 6, 'ignore_errors' => true,
        'content' => json_encode(['jsonrpc' => '2.0', 'id' => 0, 'method' => $method])]]);
    $body = @file_get_contents(rtrim($base, '/') . '/json_rpc', false, $context, 0, 1048576);
    $data = is_string($body) ? json_decode($body, true) : null;
    return is_array($data['result'] ?? null) ? $data['result'] : null;
}

function statusFetchJson(string $url): ?array {
    $context = stream_context_create(['http' => ['timeout' => 6, 'ignore_errors' => true, 'follow_location' => 0, 'header' => 'Accept: application/json']]);
    $body = @file_get_contents($url, false, $context, 0, 4194304);
    $data = is_string($body) ? json_decode($body, true) : null;
    return is_array($data) ? $data : null;
}

function statusChecks(int $now): array {
    $config = statusConfig();
    $private = statusPrivateDir();
    $checks = [];

    // Public node: the tip should keep moving.
    $tip = statusRpc($config['node_rpc'], 'get_last_block_header')['block_header'] ?? null;
    $info = statusRpc($config['node_rpc'], 'get_info');
    if (is_array($tip) && is_int($tip['timestamp'] ?? null) && is_array($info)) {
        $state = statusByAge($tip['timestamp'], $now, 1800, 7200);
        if ($state === 'ok' && ($info['synchronized'] ?? false) !== true) $state = 'warn';
        $height = (int)($info['height'] ?? 0);
        $checks['node'] = ['label' => 'Public node', 'state' => $state, 'at' => $tip['timestamp'],
            'detail' => 'Height ' . number_format($height) . ', last block ' . statusAgeText(max(0, $now - $tip['timestamp'])) . ' ago' . (($info['synchronized'] ?? false) ? '' : ', not synchronized')];
    } else {
        $height = 0;
        $checks['node'] = ['label' => 'Public node', 'state' => 'down', 'at' => null, 'detail' => 'node.monzero.org did not answer'];
    }

    // Pool: reachable and on the same chain height as the node.
    $pool = statusFetchJson($config['pool_url']);
    $poolHeight = is_int($pool['network']['height'] ?? null) ? $pool['network']['height'] : null;
    if ($poolHeight === null) {
        $checks['pool'] = ['label' => 'Mining pool', 'state' => 'down', 'at' => null, 'detail' => 'pool.monzero.org did not answer'];
    } else {
        $behind = $height > 0 ? abs($height - $poolHeight) : 0;
        $miners = is_int($pool['pool']['miners'] ?? null) ? $pool['pool']['miners'] : null;
        $checks['pool'] = ['label' => 'Mining pool', 'state' => $behind <= 3 ? 'ok' : 'warn', 'at' => $now,
            'detail' => 'Height ' . number_format($poolHeight) . ($behind > 3 ? ", $behind blocks from the public node" : '') . ($miners !== null ? ", $miners miners" : '')];
    }

    // Faucet: worker heartbeat (every cycle) and how long the funds last.
    $faucetConfig = is_file(dirname(__DIR__) . '/faucet/lib.php') ? (static function () { require_once dirname(__DIR__) . '/faucet/lib.php'; return faucetConfig(); })() : null;
    $beat = $faucetConfig ? statusReadJson(faucetHeartbeatPath()) : null;
    if (!$faucetConfig || $faucetConfig['enabled'] !== true) {
        $checks['faucet'] = ['label' => 'Faucet', 'state' => 'ok', 'at' => $beat['at'] ?? null, 'detail' => 'Paused by the owner'];
    } elseif (!$beat || !is_int($beat['at'] ?? null)) {
        $checks['faucet'] = ['label' => 'Faucet', 'state' => 'down', 'at' => null, 'detail' => 'No heartbeat from the payout worker yet'];
    } elseif (!empty($beat['halted'])) {
        $checks['faucet'] = ['label' => 'Faucet', 'state' => 'down', 'at' => $beat['at'], 'detail' => 'Payout worker halted: ' . mb_substr((string)$beat['halted'], 0, 160)];
    } else {
        $state = statusByAge($beat['at'], $now, 600, 1800);
        $balance = (int)($beat['balance_atomic'] ?? 0);
        $cap = max(1, (int)$faucetConfig['daily_cap_atomic']);
        $days = $balance / $cap;
        if ($state === 'ok' && $balance < (int)($beat['need_atomic'] ?? 0)) $state = 'down';
        elseif ($state === 'ok' && $days < 2) $state = 'warn';
        $checks['faucet'] = ['label' => 'Faucet', 'state' => $state, 'at' => $beat['at'], 'days_left' => round($days, 1),
            'detail' => sprintf('%.2f XMZ, about %.1f days at the daily maximum', $balance / 1e11, $days) . ($state === 'down' && $now - $beat['at'] > 1800 ? '; worker silent for ' . statusAgeText($now - $beat['at']) : '')];
    }

    // Node census (every 15 minutes) and network history uploads (every 5 minutes).
    $census = censusRead('latest.json');
    $at = is_int($census['observed_at'] ?? null) ? $census['observed_at'] : null;
    $checks['census'] = ['label' => 'Node map census', 'state' => statusByAge($at, $now, 2700, 10800), 'at' => $at,
        'detail' => $at ? ($census['totals']['observed'] ?? 0) . ' nodes, collected ' . statusAgeText($now - $at) . ' ago' : 'No census yet'];
    $dataDir = getenv('MONZERO_NETWORK_DATA_DIR');
    $history = statusReadJson((is_string($dataDir) && $dataDir !== '' ? $dataDir : dirname(__DIR__) . '/network/data') . '/24h.json');
    $at = is_int($history['generated_at'] ?? null) ? $history['generated_at'] : null;
    $checks['history'] = ['label' => 'Network history', 'state' => statusByAge($at, $now, 900, 3600), 'at' => $at,
        'detail' => $at ? 'Exported ' . statusAgeText($now - $at) . ' ago' : 'No export yet'];

    // Each node pings this site every 10 minutes, so a silent node is noticed even when its own jobs look fine.
    $pings = statusReadJson("$private/status-pings.json") ?? [];
    foreach (STATUS_PING_NODES as $node => $label) {
        $at = is_int($pings[$node] ?? null) ? $pings[$node] : null;
        $checks["ping-$node"] = ['label' => $label, 'state' => statusByAge($at, $now, 1800, 7200), 'at' => $at,
            'detail' => $at ? 'Last check-in ' . statusAgeText($now - $at) . ' ago' : 'Has not checked in yet'];
    }
    return $checks;
}

// Cached evaluation; on a fresh evaluation, email the owner about state changes.
function statusEvaluate(bool $force = false): array {
    $private = statusPrivateDir();
    if (!is_dir($private)) @mkdir($private, 0700, true);
    $lock = @fopen("$private/.status.lock", 'c');
    if ($lock) flock($lock, LOCK_EX);
    $now = time();
    $cache = statusReadJson("$private/status-cache.json");
    if (!$force && $cache && is_int($cache['evaluated_at'] ?? null) && $now - $cache['evaluated_at'] < STATUS_CACHE_SECONDS) return $cache;
    $checks = statusChecks($now);
    $overall = in_array('down', array_column($checks, 'state'), true) ? 'down' : (in_array('warn', array_column($checks, 'state'), true) ? 'warn' : 'ok');
    $result = ['schema' => STATUS_SCHEMA, 'evaluated_at' => $now, 'overall' => $overall, 'checks' => $checks];
    statusWriteJson("$private/status-cache.json", $result);
    statusAlert($checks, $now);
    return $result;
}

function statusAlert(array $checks, int $now): void {
    $private = statusPrivateDir();
    $memory = statusReadJson("$private/status-alerts.json") ?? [];
    $changes = [];
    foreach ($checks as $id => $check) {
        $previous = $memory[$id] ?? ['state' => 'ok', 'alerted_at' => 0];
        $state = $check['state'];
        if ($state !== $previous['state'] && ($state !== 'ok' || $previous['state'] !== 'ok')) {
            $changes[] = ($state === 'ok' ? 'RECOVERED' : strtoupper($state)) . ": {$check['label']} - {$check['detail']}";
            $memory[$id] = ['state' => $state, 'alerted_at' => $now];
        } elseif ($state !== 'ok' && $now - (int)$previous['alerted_at'] >= STATUS_REMIND_SECONDS) {
            $changes[] = 'STILL ' . strtoupper($state) . ": {$check['label']} - {$check['detail']}";
            $memory[$id] = ['state' => $state, 'alerted_at' => $now];
        } else {
            $memory[$id] = ['state' => $state, 'alerted_at' => (int)$previous['alerted_at']];
        }
    }
    statusWriteJson("$private/status-alerts.json", $memory);
    if (!$changes) return;
    $body = "Monzero service status changed (" . gmdate('Y-m-d H:i', $now) . " UTC):\n\n" . implode("\n", $changes)
        . "\n\nCurrent status: https://monzero.org/status/\n";
    authMail(statusConfig()['alert_to'], 'Monzero status: ' . (count($changes) === 1 ? $changes[0] : count($changes) . ' changes'), $body, 'MONZERO_STATUS_MAIL_LOG');
}

// Public view: labels, states, ages and details only.
function statusPublic(array $result): array {
    $checks = [];
    foreach ($result['checks'] as $id => $c)
        $checks[] = ['id' => $id, 'label' => $c['label'], 'state' => $c['state'], 'at' => $c['at'], 'detail' => $c['detail']] + (isset($c['days_left']) ? ['days_left' => $c['days_left']] : []);
    return ['schema' => STATUS_SCHEMA, 'evaluated_at' => $result['evaluated_at'], 'overall' => $result['overall'], 'checks' => $checks];
}
