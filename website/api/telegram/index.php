<?php
declare(strict_types=1);
// Telegram webhook (POST, verified by Telegram's secret token header) and a public GET that says
// whether alerts are available and under which bot username.
require dirname(__DIR__, 2) . '/lib/telegram.php';
header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

function reply(int $status, array $body): never { http_response_code($status); echo json_encode($body); exit; }

$config = tgConfig();
$method = $_SERVER['REQUEST_METHOD'] ?? '';
if ($method === 'GET') reply(200, ['enabled' => $config !== null, 'username' => $config['bot_username'] ?? null]);
if ($method !== 'POST') { header('Allow: GET, POST'); reply(405, ['error' => 'GET or POST only']); }
if (!$config) reply(503, ['error' => 'Not configured']);
if (!hash_equals($config['webhook_secret'], (string)($_SERVER['HTTP_X_TELEGRAM_BOT_API_SECRET_TOKEN'] ?? ''))) reply(401, ['error' => 'Bad secret']);
$body = (string)file_get_contents('php://input', false, null, 0, 65537);
if (strlen($body) > 65536) reply(413, ['error' => 'Too large']);
$update = json_decode($body, true);
if (!is_array($update)) reply(400, ['error' => 'Invalid JSON']);
try {
    $text = tgHandle($update);
    if ($text !== null) tgSend($config, (int)$update['message']['chat']['id'], $text);
} catch (Throwable $e) { error_log('Monzero telegram: ' . $e->getMessage()); }
// Always 200 so Telegram does not retry an update we have handled (or chosen to ignore).
reply(200, ['ok' => true]);
