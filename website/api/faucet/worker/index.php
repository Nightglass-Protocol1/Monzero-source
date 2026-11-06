<?php
declare(strict_types=1);
// Payout worker interface. Every request is signed with the shared worker secret:
//   X-Faucet-Timestamp, X-Faucet-Nonce (32 hex), X-Faucet-Signature = hex HMAC-SHA256(secret, "ts\nnonce\nbody").
// Leasing moves claims to "processing"; they never return to "pending" automatically, so a
// worker crash can never cause a second payment. The worker reports each result exactly once.
require dirname(__DIR__, 3) . '/faucet/lib.php';
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

function reply(array $data, int $status = 200): never { http_response_code($status); echo json_encode($data, JSON_UNESCAPED_SLASHES); exit; }

if (($_SERVER['REQUEST_METHOD'] ?? '') !== 'POST') { header('Allow: POST'); reply(['error' => 'POST required'], 405); }
$secret = (string)faucetConfig()['worker_secret'];
if (!preg_match('/\A[0-9a-f]{64}\z/', $secret)) reply(['error' => 'Worker interface not configured'], 503);
$body = (string)file_get_contents('php://input', false, null, 0, 65537);
if (strlen($body) > 65536) reply(['error' => 'Body too large'], 413);
$ts = (string)($_SERVER['HTTP_X_FAUCET_TIMESTAMP'] ?? '');
$nonce = (string)($_SERVER['HTTP_X_FAUCET_NONCE'] ?? '');
$signature = (string)($_SERVER['HTTP_X_FAUCET_SIGNATURE'] ?? '');
if (!preg_match('/\A\d{1,12}\z/', $ts) || abs(time() - (int)$ts) > 300 || !preg_match('/\A[0-9a-f]{32}\z/', $nonce)
    || !hash_equals(hash_hmac('sha256', "$ts\n$nonce\n$body", $secret), $signature)) reply(['error' => 'Bad signature'], 401);
$db = faucetDb();
$db->prepare('DELETE FROM worker_nonces WHERE at < ?')->execute([time() - 900]);
try { $db->prepare('INSERT INTO worker_nonces (nonce, at) VALUES (?, ?)')->execute([$nonce, time()]); }
catch (PDOException $e) { reply(['error' => 'Replayed request'], 409); }
$input = json_decode($body, true);
if (!is_array($input)) reply(['error' => 'Invalid JSON'], 400);

switch ($input['action'] ?? null) {
    case 'lease':
        if (!faucetClaimsOpen()) reply(['paused' => true, 'claims' => []]);
        $limit = filter_var($input['limit'] ?? 10, FILTER_VALIDATE_INT, ['options' => ['min_range' => 1, 'max_range' => 50]]);
        if ($limit === false) reply(['error' => 'limit must be 1-50'], 400);
        $db->exec('BEGIN IMMEDIATE');
        $rows = $db->prepare("SELECT id, address, amount_atomic FROM claims WHERE status = 'pending' ORDER BY id LIMIT ?");
        $rows->execute([$limit]);
        $claims = [];
        foreach ($rows->fetchAll() as $row) {
            $lease = bin2hex(random_bytes(16));
            $db->prepare("UPDATE claims SET status = 'processing', lease = ?, leased_at = ? WHERE id = ? AND status = 'pending'")->execute([$lease, time(), $row['id']]);
            $claims[] = ['id' => (int)$row['id'], 'address' => $row['address'], 'amount_atomic' => $row['amount_atomic'], 'lease' => $lease];
        }
        $db->exec('COMMIT');
        if ($claims) faucetEvent(null, 'worker.leased', ['ids' => array_column($claims, 'id')]);
        reply(['paused' => false, 'claims' => $claims]);
    case 'heartbeat':
        // Sent every worker cycle (also in dry run and when halting) so /status/ notices a silent worker.
        $amounts = [];
        foreach (['balance_atomic', 'unlocked_atomic', 'need_atomic'] as $k) {
            if (!is_string($input[$k] ?? null) || !preg_match('/\A[0-9]{1,20}\z/', $input[$k])) reply(['error' => "$k must be a decimal string"], 400);
            $amounts[$k] = $input[$k];
        }
        $halted = isset($input['halted']) ? mb_substr(preg_replace('/[^\P{C}]/u', ' ', (string)$input['halted']), 0, 200) : null;
        $beat = $amounts + ['at' => time(), 'payouts' => ($input['payouts'] ?? false) === true, 'halted' => $halted];
        $path = faucetHeartbeatPath();
        $tmp = $path . '.' . bin2hex(random_bytes(6));
        if (file_put_contents($tmp, json_encode($beat)) === false || !chmod($tmp, 0600) || !rename($tmp, $path)) { @unlink($tmp); reply(['error' => 'Storage unavailable'], 503); }
        reply(['ok' => true]);
    case 'report':
        $id = filter_var($input['id'] ?? null, FILTER_VALIDATE_INT);
        $lease = (string)($input['lease'] ?? '');
        $status = $input['status'] ?? null;
        if ($id === false || !preg_match('/\A[0-9a-f]{32}\z/', $lease) || !in_array($status, ['paid', 'failed'], true)) reply(['error' => 'Invalid report'], 400);
        if ($status === 'paid' && !preg_match('/\A[0-9a-f]{64}\z/', (string)($input['txid'] ?? ''))) reply(['error' => 'txid required'], 400);
        $reason = $status === 'failed' ? mb_substr(preg_replace('/[^\P{C}]/u', ' ', (string)($input['reason'] ?? 'Not sent')), 0, 200) : null;
        $update = $db->prepare("UPDATE claims SET status = ?, txid = ?, reason = ?, finished_at = ? WHERE id = ? AND lease = ? AND status = 'processing'");
        $update->execute([$status, $status === 'paid' ? $input['txid'] : null, $reason, time(), $id, $lease]);
        if ($update->rowCount() !== 1) reply(['error' => 'No matching processing claim'], 409);
        faucetEvent(null, "claim.$status", ['id' => $id] + ($status === 'paid' ? ['txid' => $input['txid']] : ['reason' => $reason]));
        reply(['ok' => true]);
    default:
        reply(['error' => 'Unknown action'], 400);
}
