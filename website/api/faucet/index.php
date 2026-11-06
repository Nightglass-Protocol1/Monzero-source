<?php
declare(strict_types=1);
// Public faucet status. Secrets and per-member data are never included.
require dirname(__DIR__, 2) . '/faucet/lib.php';
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');
if (!in_array($_SERVER['REQUEST_METHOD'] ?? '', ['GET', 'HEAD'], true)) {
    http_response_code(405);
    header('Allow: GET, HEAD');
    echo json_encode(['schema' => 'monzero-faucet-v1', 'error' => 'Claims are made on /faucet/ by signed-in members.']);
    exit;
}
$config = faucetConfig();
echo json_encode(['schema' => 'monzero-faucet-v1', 'status' => faucetClaimsOpen() ? 'open' : 'paused',
    'accounts_open' => faucetAccountsOpen(), 'amount_atomic' => (string)$config['amount_atomic'],
    'cooldown_seconds' => $config['cooldown_seconds']]);
