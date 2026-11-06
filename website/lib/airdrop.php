<?php
declare(strict_types=1);
// Monzero community airdrop: quest tasks earn points; a fixed XMZ pool (set by the owner)
// is split proportionally at the end. Identity is the participant's XMZ payout address plus a
// secret dashboard token. No seed or private key is ever requested. Verifiable tasks (valid
// address, mining on the pool, Telegram membership) are checked automatically; social shares
// are submitted as links and approved by a moderator.
require_once __DIR__ . '/xmz-address.php';

function adPrivateDir(): string { $d = getenv('MONZERO_PRIVATE_DIR'); return is_string($d) && $d !== '' ? $d : dirname(__DIR__, 2) . '/monzero-private'; }

// Owner-set config (monzero-private/airdrop-config.php). Everything has a safe default so the
// campaign can be previewed before the owner funds a pool.
function adConfig(): array {
    $path = adPrivateDir() . '/airdrop-config.php';
    $c = is_file($path) && !is_link($path) ? (static fn() => require $path)() : [];
    if (!is_array($c)) $c = [];
    $int = fn($k, $d) => isset($c[$k]) && is_int($c[$k]) ? $c[$k] : $d;
    // The auto-renew cron (airdrop-topup.sh) writes the live budget here so it tracks the fee.
    $poolFile = adPrivateDir() . '/airdrop-pool.txt';
    $override = is_file($poolFile) ? trim((string)@file_get_contents($poolFile, false, null, 0, 64)) : '';
    $pool = is_numeric($override) ? (float)$override : (isset($c['pool_xmz']) && is_numeric($c['pool_xmz']) ? (float)$c['pool_xmz'] : 0.0);
    return [
        'enabled'      => ($c['enabled'] ?? false) === true,
        'pool_xmz'     => $pool,
        'starts_at'    => $int('starts_at', 0),
        'ends_at'      => $int('ends_at', 0),                 // 0 = open-ended
        'rewards'      => is_array($c['rewards'] ?? null) ? $c['rewards'] : (is_array($c['points'] ?? null) ? $c['points'] : []),
        'per_person_cap' => isset($c['per_person_cap']) && is_numeric($c['per_person_cap']) ? (float)$c['per_person_cap'] : 50.0,
        'auto_renew'   => ($c['auto_renew'] ?? false) === true,
        'mining_days_required' => isset($c['mining_days_required']) && is_int($c['mining_days_required']) && $c['mining_days_required'] >= 1 && $c['mining_days_required'] <= 31 ? $c['mining_days_required'] : 7,
        'max_per_ip'     => isset($c['max_per_ip']) && is_int($c['max_per_ip']) && $c['max_per_ip'] >= 0 ? $c['max_per_ip'] : 3,
        'trusted_proxies' => is_array($c['trusted_proxies'] ?? null) ? $c['trusted_proxies'] : [],
        'referral_cap'   => isset($c['referral_cap']) && is_numeric($c['referral_cap']) ? (float)$c['referral_cap'] : 25.0,
        'admin_reward_xmz' => isset($c['admin_reward_xmz']) && is_numeric($c['admin_reward_xmz']) ? (float)$c['admin_reward_xmz'] : 0.0,
        'admins'         => adParseAdmins($c['admins'] ?? null),
        'admin_token'  => is_string($c['admin_token'] ?? null) ? $c['admin_token'] : null,
        'pool_stats'   => is_string($c['pool_stats'] ?? null) ? $c['pool_stats'] : 'https://pool.monzero.org/api/miner',
        'telegram'     => is_string($c['telegram'] ?? null) ? $c['telegram'] : '@MonzeroOfficial',
        'x'            => is_string($c['x'] ?? null) ? $c['x'] : 'MonzeroXMZ',
        'instagram'    => is_string($c['instagram'] ?? null) ? $c['instagram'] : 'monzeroxmz',
    ];
}

// XMZ each task pays (owner can override in config['rewards']). Harder tasks pay more; the
// core tasks sum to 45 XMZ and up to 5 referrals (1 XMZ each) reach the 50 XMZ per-person cap.
function adRewardXmz(string $task): float {
    $defaults = ['wallet' => 2, 'telegram' => 2, 'mining' => 12, 'forum_post' => 5, 'forum_active' => 10,
        'run_node' => 8, 'share_x' => 3, 'share_instagram' => 3, 'referral' => 1];
    $v = adConfig()['rewards'][$task] ?? ($defaults[$task] ?? 0);
    return is_numeric($v) && $v >= 0 && $v <= 10000 ? (float)$v : (float)($defaults[$task] ?? 0);
}
// Internal value in centi-XMZ (so totals stay integer).
function adPoints(string $task): int { return (int)round(adRewardXmz($task) * 100); }
function adCapCenti(): int { return (int)round(adConfig()['per_person_cap'] * 100); }

// Review moderators: [['name'=>..., 'token'=>..., 'address'=>XMZ], ...] from the config.
function adParseAdmins($raw): array {
    if (!is_array($raw)) return [];
    $str = fn($a, $k) => isset($a[$k]) && is_string($a[$k]) ? $a[$k] : '';
    $out = [];
    foreach ($raw as $a) {
        if (!is_array($a)) continue;
        $name = mb_substr($str($a, 'name'), 0, 32);
        $entry = ['name' => $name, 'token' => $str($a, 'token'), 'address' => $str($a, 'address'),
            'telegram' => $str($a, 'telegram'), 'password' => $str($a, 'password'), 'pin' => $str($a, 'pin')];
        $hasToken = strlen($entry['token']) >= 16;
        $hasLogin = $entry['address'] !== '' && $entry['telegram'] !== '' && $entry['password'] !== '' && $entry['pin'] !== '';
        if ($name !== '' && ($hasToken || $hasLogin)) $out[] = $entry;
    }
    return $out;
}

// Verify a stored secret: bcrypt/argon hash (preferred) or a plaintext value in the private config.
function adVerifySecret(string $stored, string $given): bool {
    if ($stored === '') return false;
    if ($stored[0] === '$' && strlen($stored) >= 50) return password_verify($given, $stored);
    return hash_equals($stored, $given);
}

// Moderator login: all four must match an admin entry. Returns the moderator name or null.
function adAdminLogin(string $address, string $telegram, string $password, string $pin): ?string {
    $tg = ltrim(strtolower(trim($telegram)), '@');
    $address = trim($address);
    foreach (adConfig()['admins'] as $a) {
        if (!hash_equals((string)$a['address'], $address)) continue;
        if ($a['telegram'] === '' || ltrim(strtolower($a['telegram']), '@') !== $tg) continue;
        if (!adVerifySecret((string)$a['password'], $password)) continue;
        if (!adVerifySecret((string)$a['pin'], $pin)) continue;
        return $a['name'];
    }
    return null;
}

// Signed, stateless session cookie for the moderator panel (no server-side session store).
function adAdminCookie(string $name, int $exp): string {
    $payload = rtrim(strtr(base64_encode($name), '+/', '-_'), '=') . '.' . $exp;
    return $payload . '.' . hash_hmac('sha256', $payload, adSecret());
}
function adAdminFromCookie(string $cookie): ?string {
    $parts = explode('.', $cookie);
    if (count($parts) !== 3) return null;
    [$b64, $exp, $sig] = $parts;
    if (!hash_equals(hash_hmac('sha256', $b64 . '.' . $exp, adSecret()), $sig) || (int)$exp < time()) return null;
    $name = base64_decode(strtr($b64, '-_', '+/'), true);
    if ($name === false) return null;
    foreach (adConfig()['admins'] as $a) if ($a['name'] === $name) return $name;
    return null;
}
// Returns the reviewer name for a valid admin key, or null. The master admin_token reviews as 'admin'.
function adAdminAuth(string $key): ?string {
    if ($key === '') return null;
    $cfg = adConfig();
    if (is_string($cfg['admin_token']) && $cfg['admin_token'] !== '' && hash_equals($cfg['admin_token'], $key)) return 'admin';
    foreach ($cfg['admins'] as $a) if (hash_equals($a['token'], $key)) return $a['name'];
    return null;
}

function adDb(): PDO {
    static $db = null;
    if ($db) return $db;
    $path = adPrivateDir() . '/airdrop.sqlite';
    if (!is_dir(dirname($path))) @mkdir(dirname($path), 0700, true);
    $new = !file_exists($path);
    $db = new PDO('sqlite:' . $path, null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
    if ($new) @chmod($path, 0600);
    $db->exec('PRAGMA busy_timeout = 5000;');
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 1) {
        $db->exec(<<<'SQL'
            BEGIN;
            CREATE TABLE participants (id INTEGER PRIMARY KEY, address TEXT UNIQUE NOT NULL, token_hash TEXT NOT NULL,
                ref_code TEXT UNIQUE NOT NULL, referred_by TEXT, telegram_id INTEGER, created_at INTEGER NOT NULL);
            CREATE TABLE progress (participant_id INTEGER NOT NULL, task TEXT NOT NULL, points INTEGER NOT NULL DEFAULT 0,
                updated_at INTEGER NOT NULL, PRIMARY KEY (participant_id, task));
            CREATE TABLE submissions (id INTEGER PRIMARY KEY, participant_id INTEGER NOT NULL, platform TEXT NOT NULL,
                url TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'pending', points INTEGER NOT NULL DEFAULT 0, created_at INTEGER NOT NULL, reviewed_at INTEGER);
            CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            PRAGMA user_version = 1;
            COMMIT;
        SQL);
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 2) {
        $db->exec('ALTER TABLE submissions ADD COLUMN reviewer TEXT; PRAGMA user_version = 2;');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 3) {
        $db->exec('CREATE TABLE IF NOT EXISTS mining_days (participant_id INTEGER NOT NULL, day INTEGER NOT NULL, PRIMARY KEY (participant_id, day)); PRAGMA user_version = 3;');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 4) {
        $db->exec('ALTER TABLE participants ADD COLUMN ip_hash TEXT; CREATE INDEX IF NOT EXISTS ix_participants_ip ON participants(ip_hash); PRAGMA user_version = 4;');
    }
    return $db;
}

// Per-install secret for hashing client IPs (never stores a raw IP).
function adSecret(): string {
    static $secret = null;
    if ($secret !== null) return $secret;
    $path = adPrivateDir() . '/airdrop-secret';
    if (!file_exists($path)) { $h = @fopen($path, 'x'); if ($h) { fwrite($h, bin2hex(random_bytes(32))); fclose($h); @chmod($path, 0600); } }
    $secret = trim((string)@file_get_contents($path));
    if (!preg_match('/\A[0-9a-f]{64}\z/', $secret)) $secret = hash('sha256', $path); // safe fallback
    return $secret;
}
function adIpInCidr(string $ip, string $cidr): bool {
    [$net, $bits] = array_pad(explode('/', $cidr, 2), 2, null);
    $ipB = @inet_pton($ip); $netB = @inet_pton((string)$net);
    if ($ipB === false || $netB === false || strlen($ipB) !== strlen($netB)) return false;
    $bits = $bits === null ? strlen($ipB) * 8 : (int)$bits;
    $bytes = intdiv($bits, 8); $rest = $bits % 8;
    if (substr($ipB, 0, $bytes) !== substr($netB, 0, $bytes)) return false;
    if ($rest === 0) return true;
    $mask = chr((0xff << (8 - $rest)) & 0xff);
    return ($ipB[$bytes] & $mask) === ($netB[$bytes] & $mask);
}
// Visitor IP. Proxy headers are trusted only from configured proxy ranges; the site's own nginx
// real_ip already sets REMOTE_ADDR to the client otherwise.
function adClientIp(): string {
    $remote = (string)($_SERVER['REMOTE_ADDR'] ?? '');
    foreach (adConfig()['trusted_proxies'] as $cidr) {
        if (is_string($cidr) && adIpInCidr($remote, $cidr)) {
            $fwd = (string)($_SERVER['HTTP_CF_CONNECTING_IP'] ?? $_SERVER['HTTP_X_REAL_IP'] ?? '');
            if (filter_var($fwd, FILTER_VALIDATE_IP)) return $fwd;
        }
    }
    return $remote;
}
function adClientHash(): string { $ip = adClientIp(); return $ip === '' ? '' : hash_hmac('sha256', 'client:' . $ip, adSecret()); }

function adToken(): string { return bin2hex(random_bytes(16)); }
function adHash(string $t): string { return hash('sha256', $t); }
function adRefCode(): string { return substr(strtr(base64_encode(random_bytes(6)), '+/', 'xy'), 0, 8); }
function adShort(string $a): string { return substr($a, 0, 8) . '…' . substr($a, -6); }

// Register (or return) a participant for an address. Returns [participant row, token|null].
// token is only returned on first creation.
function adRegister(string $rawAddress, ?string $ref, ?string $ipHash = null): array {
    $parsed = xmzParseAddress($rawAddress);
    if (isset($parsed['error'])) return ['error' => $parsed['error']];
    if ($parsed['network'] !== 'mainnet' || $parsed['kind'] === 'integrated')
        return ['error' => 'Use your main mainnet XMZ address (the one your wallet shows to receive).'];
    $address = $parsed['address'];
    $db = adDb();
    $row = $db->prepare('SELECT * FROM participants WHERE address = ?');
    $row->execute([$address]);
    $p = $row->fetch();
    if ($p) return ['participant' => $p, 'token' => null, 'existing' => true];

    // Per-IP cap to deter one person farming many wallets (shared NAT gets a few slots).
    $max = adConfig()['max_per_ip'];
    if (is_string($ipHash) && $ipHash !== '' && $max > 0) {
        $cnt = $db->prepare('SELECT COUNT(*) FROM participants WHERE ip_hash = ?');
        $cnt->execute([$ipHash]);
        if ((int)$cnt->fetchColumn() >= $max) return ['error' => 'This connection already has the maximum number of airdrop entries. On a shared network, join from your own connection.'];
    }
    $token = adToken();
    $code = adRefCode();
    $referrer = null;
    if (is_string($ref) && preg_match('/\A[A-Za-z0-9]{8}\z/', $ref)) {
        $r = $db->prepare('SELECT ref_code FROM participants WHERE ref_code = ?');
        $r->execute([$ref]);
        $referrer = $r->fetchColumn() ?: null;
    }
    $db->prepare('INSERT INTO participants (address, token_hash, ref_code, referred_by, ip_hash, created_at) VALUES (?, ?, ?, ?, ?, ?)')
        ->execute([$address, adHash($token), $code, $referrer, is_string($ipHash) ? $ipHash : null, time()]);
    $id = (int)$db->lastInsertId();
    adAward($id, 'wallet');                 // valid address submitted
    $row->execute([$address]); $p = $row->fetch();
    return ['participant' => $p, 'token' => $token, 'existing' => false];
}

function adParticipantByToken(string $token): ?array {
    $db = adDb();
    $s = $db->prepare('SELECT * FROM participants WHERE token_hash = ?');
    $s->execute([adHash($token)]);
    return $s->fetch() ?: null;
}

function adAward(int $pid, string $task): void {
    adDb()->prepare('INSERT INTO progress (participant_id, task, points, updated_at) VALUES (?, ?, ?, ?)
        ON CONFLICT (participant_id, task) DO UPDATE SET points = excluded.points, updated_at = excluded.updated_at')
        ->execute([$pid, $task, adPoints($task), time()]);
}

// Number of distinct days a participant has been seen mining.
function adMiningDays(int $pid): int {
    $s = adDb()->prepare('SELECT COUNT(*) FROM mining_days WHERE participant_id = ?');
    $s->execute([$pid]);
    return (int)$s->fetchColumn();
}

// Mining must be SUSTAINED: a participant must be seen mining on at least
// mining_days_required distinct days (a week's worth) over the campaign. Each call records
// today if they are currently mining, and credits the task once the day count is reached.
// A scheduled job (utils/website/airdrop-mining-check.php) calls this for every participant so
// the count accrues even when they are not looking at their dashboard.
function adCheckMining(array $p): bool {
    $db = adDb();
    $done = $db->prepare('SELECT 1 FROM progress WHERE participant_id = ? AND task = ?');
    $done->execute([$p['id'], 'mining']);
    if ($done->fetchColumn()) return true;
    $cfg = adConfig();
    $url = rtrim($cfg['pool_stats'], '/') . '?address=' . rawurlencode($p['address']);
    $ctx = stream_context_create(['http' => ['timeout' => 6, 'ignore_errors' => true, 'follow_location' => 0]]);
    $body = @file_get_contents($url, false, $ctx, 0, 262144);
    if ($body !== false && is_array($d = json_decode($body, true))) {
        // "active today" = currently mining or holding a recent unpaid balance (not cumulative paid).
        $active = ((float)($d['hashrate'] ?? 0) > 0) || ((int)($d['pending'] ?? 0) > 0)
            || (is_array($d['workers'] ?? null) && count($d['workers']) > 0);
        if ($active) $db->prepare('INSERT OR IGNORE INTO mining_days (participant_id, day) VALUES (?, ?)')->execute([(int)$p['id'], intdiv(time(), 86400)]);
    }
    if (adMiningDays((int)$p['id']) >= $cfg['mining_days_required']) { adAward((int)$p['id'], 'mining'); return true; }
    return false;
}

// A participant counts toward a referrer once they have an auto-verified task (telegram or mining).
function adReferralPoints(array $p): int {
    $n = (int)adDb()->query('SELECT COUNT(DISTINCT pr.participant_id) FROM participants c
        JOIN progress pr ON pr.participant_id = c.id AND pr.task IN (\'telegram\',\'mining\')
        WHERE c.referred_by = ' . adDb()->quote($p['ref_code']))->fetchColumn();
    return $n * adPoints('referral');
}

function adStatus(array $p): array {
    $db = adDb();
    adCheckMining($p);                       // refresh mining on every view
    $prog = [];
    $rows = $db->prepare('SELECT task, points FROM progress WHERE participant_id = ?');
    $rows->execute([$p['id']]);
    foreach ($rows->fetchAll() as $r) $prog[$r['task']] = (int)$r['points'];
    $rank = ['todo' => 0, 'rejected' => 1, 'pending' => 2, 'approved' => 3];
    $review = [];
    foreach (adReviewTasks() as $rt) $review[$rt] = ['status' => 'todo', 'points' => 0];
    $subs = $db->prepare('SELECT platform AS task, status, points FROM submissions WHERE participant_id = ? ORDER BY created_at');
    $subs->execute([$p['id']]);
    foreach ($subs->fetchAll() as $s) {
        $t = $s['task']; if (!isset($review[$t])) continue;
        if (($rank[$s['status']] ?? 0) >= ($rank[$review[$t]['status']] ?? 0)) $review[$t] = ['status' => $s['status'], 'points' => (int)$s['points']];
    }
    $referrals = (int)$db->query('SELECT COUNT(*) FROM participants WHERE referred_by = ' . $db->quote($p['ref_code']))->fetchColumn();
    $refPoints = adReferralPoints($p);
    $taskCenti = array_sum($prog) + array_sum(array_map(fn($r) => $r['points'], $review));
    $taskEarned = min(adCapCenti(), $taskCenti);
    $refCapCenti = (int)round(adConfig()['referral_cap'] * 100);
    $refEarned = min($refCapCenti, $refPoints);
    $rewards = ['wallet' => adRewardXmz('wallet'), 'telegram' => adRewardXmz('telegram'), 'mining' => adRewardXmz('mining'), 'referral' => adRewardXmz('referral')];
    foreach (adReviewTasks() as $rt) $rewards[$rt] = adRewardXmz($rt);
    return [
        'address' => adShort($p['address']),
        'ref_code' => $p['ref_code'],
        'tasks' => array_merge([
            'wallet'   => isset($prog['wallet']) ? 'done' : 'todo',
            'telegram' => isset($prog['telegram']) ? 'done' : 'todo',
            'mining'   => isset($prog['mining']) ? 'done' : 'todo',
        ], array_map(fn($r) => $r['status'], $review)),
        'rewards' => $rewards,
        'referrals' => $referrals,
        'task_earned' => $taskEarned / 100,
        'cap' => adConfig()['per_person_cap'],
        'referral_earned' => $refEarned / 100,
        'referral_cap' => adConfig()['referral_cap'],
        'earned' => ($taskEarned + $refEarned) / 100,
        'mining_days' => adMiningDays((int)$p['id']),
        'mining_days_required' => adConfig()['mining_days_required'],
    ];
}

function adReviewTasks(): array { return ['share_x', 'share_instagram', 'forum_post', 'forum_active', 'run_node']; }

function adSubmit(array $p, string $task, string $detail): array {
    if (!in_array($task, adReviewTasks(), true)) return ['error' => 'Unknown task.'];
    $detail = trim($detail);
    if ($detail === '') return ['error' => 'Please enter the required detail.'];
    if (strlen($detail) > 400) return ['error' => 'That is too long.'];
    if ($task === 'share_x') {
        if (!preg_match('#\Ahttps://(www\.)?(x\.com|twitter\.com)/\S+\z#i', $detail)) return ['error' => 'Paste the full link to your public post on X.'];
    } elseif ($task === 'share_instagram') {
        if (!preg_match('#\Ahttps://(www\.)?instagram\.com/\S+\z#i', $detail)) return ['error' => 'Paste the full link to your public Instagram post.'];
    } elseif ($task === 'run_node') {
        if (!preg_match('/\A((\d{1,3}\.){3}\d{1,3}|[A-Za-z0-9][A-Za-z0-9.\-]{1,120})(:\d{2,5})?\z/', $detail))
            return ['error' => 'Enter your node address, e.g. 203.0.113.9:6174 or mynode.example.com.'];
        if (preg_match('/(\A|\.)monzero\.org/i', $detail)) return ['error' => "That's an official seed node — enter your own node's address."];
    } else { // forum_post, forum_active: a Monzero board link or a forum username
        $okUrl = preg_match('#\Ahttps://(www\.)?monzero\.org/community/board/\S+\z#i', $detail);
        $okUser = preg_match('/\A[A-Za-z0-9_.-]{3,30}\z/', $detail);
        if (!$okUrl && !$okUser) return ['error' => 'Enter your forum username, or a link to your profile or post on the Monzero board.'];
    }
    $db = adDb();
    $open = $db->prepare("SELECT 1 FROM submissions WHERE participant_id = ? AND platform = ? AND status IN ('pending','approved')");
    $open->execute([$p['id'], $task]);
    if ($open->fetchColumn()) return ['error' => 'You already submitted this task; it is pending review or approved.'];
    $db->prepare('INSERT INTO submissions (participant_id, platform, url, status, created_at) VALUES (?, ?, ?, \'pending\', ?)')
        ->execute([$p['id'], $task, $detail, time()]);
    return ['ok' => true];
}


// Admin rewards: XMZ for each confirmed (approved) review, paid to each moderator's address.
function adAdminPayouts(): array {
    $cfg = adConfig();
    $reward = (float)$cfg['admin_reward_xmz'];
    $addr = [];
    foreach ($cfg['admins'] as $a) $addr[$a['name']] = $a['address'];
    $rows = adDb()->query("SELECT reviewer, COUNT(*) AS n FROM submissions WHERE status = 'approved' AND reviewer IS NOT NULL AND reviewer != '' GROUP BY reviewer")->fetchAll();
    $out = [];
    foreach ($rows as $r) $out[] = ['admin' => $r['reviewer'], 'address' => $addr[$r['reviewer']] ?? '', 'confirmations' => (int)$r['n'], 'xmz' => round($reward * (int)$r['n'], 8)];
    usort($out, fn($a, $b) => $b['confirmations'] <=> $a['confirmations']);
    return $out;
}

// Final payout list: each participant's full address and capped XMZ earned.
function adPayouts(): array {
    $db = adDb();
    $cap = adCapCenti();
    $refCap = (int)round(adConfig()['referral_cap'] * 100);
    $rows = $db->query('SELECT p.id, p.address, p.ref_code,
        COALESCE((SELECT SUM(points) FROM progress WHERE participant_id = p.id), 0)
        + COALESCE((SELECT SUM(points) FROM submissions WHERE participant_id = p.id AND status = \'approved\'), 0) AS base
        FROM participants p')->fetchAll();
    $out = [];
    foreach ($rows as $r) {
        $ref = (int)$db->query('SELECT COUNT(DISTINCT pr.participant_id) FROM participants c
            JOIN progress pr ON pr.participant_id = c.id AND pr.task IN (\'telegram\',\'mining\')
            WHERE c.referred_by = ' . $db->quote($r['ref_code']))->fetchColumn();
        $centi = min($cap, (int)$r['base']) + min($refCap, $ref * adPoints('referral'));
        if ($centi > 0) $out[] = ['address' => $r['address'], 'xmz' => $centi / 100];
    }
    usort($out, fn($a, $b) => $b['xmz'] <=> $a['xmz']);
    return $out;
}

function adLeaderboard(int $limit = 20): array {
    // Recompute totals from progress + approved submissions; referral points approximated by count.
    $db = adDb();
    $rows = $db->query('SELECT p.address, p.ref_code,
        COALESCE((SELECT SUM(points) FROM progress WHERE participant_id = p.id), 0)
        + COALESCE((SELECT SUM(points) FROM submissions WHERE participant_id = p.id AND status = \'approved\'), 0) AS base
        FROM participants p')->fetchAll();
    $cap = adCapCenti();
    $refCap = (int)round(adConfig()['referral_cap'] * 100);
    $out = [];
    foreach ($rows as $r) {
        $ref = (int)$db->query('SELECT COUNT(DISTINCT pr.participant_id) FROM participants c
            JOIN progress pr ON pr.participant_id = c.id AND pr.task IN (\'telegram\',\'mining\')
            WHERE c.referred_by = ' . $db->quote($r['ref_code']))->fetchColumn();
        $centi = min($cap, (int)$r['base']) + min($refCap, $ref * adPoints('referral'));
        $out[] = ['address' => adShort($r['address']), 'xmz' => $centi / 100];
    }
    usort($out, fn($a, $b) => $b['xmz'] <=> $a['xmz']);
    return array_slice($out, 0, max(1, min(100, $limit)));
}
