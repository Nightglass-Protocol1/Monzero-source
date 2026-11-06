<?php
declare(strict_types=1);
// GET: public service status (cached for a minute). POST: signed check-in from a node
// (utils/website/status_ping.py), which also re-evaluates the checks and sends owner alerts.
require dirname(__DIR__, 2) . '/lib/status.php';
header('Content-Type: application/json; charset=utf-8');
header('X-Content-Type-Options: nosniff');

function reply(int $status, array $body, string $cache = 'no-store'): never {
    http_response_code($status); header("Cache-Control: $cache"); echo json_encode($body, JSON_UNESCAPED_SLASHES); exit;
}

$method = $_SERVER['REQUEST_METHOD'] ?? '';
if ($method === 'GET' || $method === 'HEAD') {
    try { $result = statusEvaluate(); } catch (Throwable $e) { reply(503, ['error' => 'Status unavailable']); }
    reply(200, statusPublic($result), 'public, max-age=30');
}
if ($method !== 'POST') { header('Allow: GET, POST'); reply(405, ['error' => 'GET or POST only']); }

$secret = statusConfig()['secret'];
if ($secret === '') reply(503, ['error' => 'Not configured']);
$body = (string)file_get_contents('php://input', false, null, 0, 4097);
if (strlen($body) > 4096) reply(413, ['error' => 'Too large']);
$ts = (string)($_SERVER['HTTP_X_STATUS_TIMESTAMP'] ?? '');
$nonce = (string)($_SERVER['HTTP_X_STATUS_NONCE'] ?? '');
$signature = (string)($_SERVER['HTTP_X_STATUS_SIGNATURE'] ?? '');
$now = time();
if (!preg_match('/\A\d{1,12}\z/', $ts) || abs($now - (int)$ts) > 300 || !preg_match('/\A[0-9a-f]{32}\z/', $nonce)
    || !hash_equals(hash_hmac('sha256', "$ts\n$nonce\n" . $body, $secret), $signature)) reply(401, ['error' => 'Bad signature']);
$input = json_decode($body, true);
$node = is_array($input) ? ($input['node'] ?? null) : null;
if (!is_string($node) || !isset(STATUS_PING_NODES[$node])) reply(400, ['error' => 'Unknown node']);

$private = statusPrivateDir();
if (!is_dir($private)) @mkdir($private, 0700, true);
$lock = @fopen("$private/.status-ping.lock", 'c');
if (!$lock || !flock($lock, LOCK_EX)) reply(503, ['error' => 'Storage unavailable']);
$pings = statusReadJson("$private/status-pings.json") ?? [];
$nonces = array_filter((array)($pings['nonces'] ?? []), fn($at) => is_int($at) && $at > $now - 900);
if (isset($nonces[$nonce])) reply(409, ['error' => 'Replayed']);
$nonces[$nonce] = $now;
$pings['nonces'] = $nonces;
$pings[$node] = $now;
statusWriteJson("$private/status-pings.json", $pings);
flock($lock, LOCK_UN);
$result = statusEvaluate(true);
// Node check-ins also drive the Telegram miner alerts (rate limited inside; off when not configured).
try { require_once dirname(__DIR__, 2) . '/lib/telegram.php'; tgCheckAll(time()); } catch (Throwable $e) { error_log('Monzero telegram check: ' . $e->getMessage()); }
reply(200, ['ok' => true, 'overall' => $result['overall']]);
