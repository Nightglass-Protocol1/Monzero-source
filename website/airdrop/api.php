<?php
declare(strict_types=1);
// Monzero airdrop API. JSON in/out. Never asks for or stores seeds/keys — only the public
// XMZ receive address and a secret dashboard token the participant keeps.
require dirname(__DIR__) . '/lib/airdrop.php';

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store');
header('X-Content-Type-Options: nosniff');

function out(int $code, array $body): never { http_response_code($code); echo json_encode($body, JSON_UNESCAPED_SLASHES); exit; }
function body(): array { $raw = (string)file_get_contents('php://input', false, null, 0, 16385); $j = json_decode($raw, true); return is_array($j) ? $j : []; }
function str_param(array $a, string $k, int $max = 200): string { $v = $a[$k] ?? ''; return is_string($v) ? mb_substr(trim($v), 0, $max) : ''; }

$cfg = adConfig();
$action = (string)($_GET['action'] ?? '');
$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
$now = time();
$live = adLive($now);   // needs enabled + a budget + a real end date

// ---- Admin actions (gated by a shared secret from the config) ----
if (str_starts_with($action, 'admin.')) {
    $given = (string)($_SERVER['HTTP_X_AIRDROP_ADMIN'] ?? $_GET['key'] ?? '');
    $reviewer = adAdminAuth($given);
    if ($reviewer === null) out(401, ['error' => 'Unauthorized']);
    $db = adDb();
    if ($action === 'admin.pending') {
        $rows = $db->query('SELECT s.id, s.platform, s.url, s.created_at, p.address FROM submissions s JOIN participants p ON p.id = s.participant_id WHERE s.status = \'pending\' ORDER BY s.created_at')->fetchAll();
        foreach ($rows as &$r) $r['address'] = adShort($r['address']);
        out(200, ['pending' => $rows]);
    }
    if ($action === 'admin.review' && $method === 'POST') {
        $b = body(); $id = (int)($b['id'] ?? 0); $decision = str_param($b, 'decision', 10);
        if ($id <= 0 || !in_array($decision, ['approve', 'reject'], true)) out(400, ['error' => 'Bad request']);
        $err = adReview($id, $decision, $reviewer);
        if ($err !== null) out(str_contains($err, 'own') ? 403 : 404, ['error' => $err]);
        out(200, ['ok' => true, 'reviewer' => $reviewer]);
    }
    if ($action === 'admin.export') {
        // payouts: final amounts in atomic units, scaled so the total never exceeds the budget.
        $b = adBudget();
        out(200, ['pool_xmz' => $b['pool'], 'per_person_cap' => $cfg['per_person_cap'], 'earned_xmz' => $b['participant_xmz'],
            'admin_xmz' => $b['admin_xmz'], 'scale' => $b['scale'], 'payouts' => adFinalPayouts()]);
    }
    out(404, ['error' => 'Unknown admin action']);
}

// ---- Public, read-only ----
if ($action === 'config') {
    $rewards = ['wallet' => adRewardXmz('wallet'), 'telegram' => adRewardXmz('telegram'), 'mining' => adRewardXmz('mining'), 'referral' => adRewardXmz('referral')];
    foreach (adReviewTasks() as $rt) $rewards[$rt] = adRewardXmz($rt);
    $b = adBudget();
    out(200, ['enabled' => $cfg['enabled'], 'live' => $live, 'pool_xmz' => $cfg['pool_xmz'], 'per_person_cap' => $cfg['per_person_cap'],
        'full' => $b['pool'] > 0 && $b['committed'] >= $b['pool'], 'participants' => adParticipantCount(),
        'starts_at' => $cfg['starts_at'], 'ends_at' => $cfg['ends_at'],
        'telegram' => $cfg['telegram'], 'x' => $cfg['x'], 'instagram' => $cfg['instagram'],
        'rewards' => $rewards]);
}
if ($action === 'leaderboard') out(200, ['leaderboard' => adLeaderboard(20)]);

if (!$live) out(503, ['error' => 'The airdrop is not open right now.']);

// ---- Participant actions ----
if ($action === 'register' && $method === 'POST') {
    $b = body();
    $device = adDeviceHash(str_param($b, 'device', 32));
    $res = adRegister(str_param($b, 'address', 120), str_param($b, 'ref', 8), adClientHash(), $device);
    if (isset($res['error'])) out(400, ['error' => $res['error']]);
    $p = $res['participant'];
    if ($res['existing']) { adSeen((int)$p['id'], adClientHash(), $device); $p = adParticipantById((int)$p['id']) ?? $p; }
    out(200, ['ok' => true, 'existing' => $res['existing'], 'token' => $res['token'], 'ref_code' => $p['ref_code'], 'status' => adStatus($p)]);
}
if ($action === 'status') {
    $p = adParticipantByToken(str_param($_GET, 'token', 64));
    if (!$p) out(404, ['error' => 'Unknown dashboard token. Register your address first.']);
    adSeen((int)$p['id'], adClientHash(), adDeviceHash(str_param($_GET, 'device', 32)));
    out(200, ['status' => adStatus($p)]);
}
if (($action === 'submit' || $action === 'share') && $method === 'POST') {
    $b = body();
    $p = adParticipantByToken(str_param($b, 'token', 64));
    if (!$p) out(404, ['error' => 'Register first.']);
    adSeen((int)$p['id'], adClientHash(), adDeviceHash(str_param($b, 'device', 32)));
    $p = adParticipantById((int)$p['id']);    // the check above may just have disqualified it
    $task = str_param($b, 'task', 20);
    if ($task === '') { $plat = str_param($b, 'platform', 12); $task = $plat === 'instagram' ? 'share_instagram' : ($plat === 'x' ? 'share_x' : ''); }
    $detail = str_param($b, 'detail', 400) ?: str_param($b, 'url', 400);
    $res = adSubmit($p, $task, $detail);
    if (isset($res['error'])) out(400, ['error' => $res['error']]);
    out(200, ['ok' => true, 'status' => adStatus($p)]);
}
out(404, ['error' => 'Unknown action']);
