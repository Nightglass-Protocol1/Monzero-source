<?php
declare(strict_types=1);
// Scheduled airdrop tick, pushed every 15 minutes from monzero-eu (utils/website/airdrop_tick.py)
// because the webspace has no cron. HMAC-signed with the shared secret in
// <monzero-private>/airdrop-tick-secret. Carries the pool's fee total and each pool address's
// earnings total; records mining days for every participant and renews the budget.
require dirname(__DIR__) . '/lib/airdrop.php';
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

function tickOut(int $code, array $body): never { http_response_code($code); echo json_encode($body, JSON_UNESCAPED_SLASHES); exit; }

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') !== 'POST') tickOut(405, ['error' => 'POST only']);
$body = (string)file_get_contents('php://input', false, null, 0, 4 * 1024 * 1024 + 1);
if (strlen($body) > 4 * 1024 * 1024) tickOut(413, ['error' => 'Too large']);
$err = adTickVerify((string)($_SERVER['HTTP_X_AIRDROP_TIMESTAMP'] ?? ''), (string)($_SERVER['HTTP_X_AIRDROP_NONCE'] ?? ''),
    (string)($_SERVER['HTTP_X_AIRDROP_SIGNATURE'] ?? ''), $body);
if ($err === 'not configured') tickOut(503, ['error' => 'Tick secret not configured']);
if ($err === 'replayed') tickOut(409, ['error' => 'Replayed']);
if ($err !== null) tickOut(401, ['error' => 'Bad signature']);
$payload = json_decode($body, true);
if (!is_array($payload)) tickOut(400, ['error' => 'Bad JSON']);
tickOut(200, adTick($payload));
