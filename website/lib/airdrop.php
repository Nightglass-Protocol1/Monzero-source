<?php
declare(strict_types=1);
// Monzero community airdrop: each task pays a fixed amount of XMZ (capped per person) out of a
// budget funded by the mining pool's fee. If what has been earned ever exceeds the budget, the
// final payout is scaled down so it never pays more than the budget. Identity is the
// participant's XMZ payout address plus a secret dashboard token. No seed or private key is ever
// requested. Telegram membership and sustained mining are checked automatically (the wallet
// reward unlocks with either one); social shares and forum/node tasks are approved by a moderator.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once __DIR__ . '/xmz-address.php';

function adPrivateDir(): string { $d = getenv('MONZERO_PRIVATE_DIR'); return is_string($d) && $d !== '' ? $d : dirname(__DIR__, 2) . '/monzero-private'; }

// Owner-set config (monzero-private/airdrop-config.php). Everything has a safe default so the
// campaign can be previewed before the owner funds a pool.
function adConfig(): array {
    static $cache = null;                    // read once per request
    if ($cache !== null) return $cache;
    return $cache = adLoadConfig();
}
function adLoadConfig(): array {
    $path = adPrivateDir() . '/airdrop-config.php';
    $c = is_file($path) && !is_link($path) ? (static fn() => require $path)() : [];
    if (!is_array($c)) $c = [];
    $int = fn($k, $d) => isset($c[$k]) && is_int($c[$k]) ? $c[$k] : $d;
    // The auto-renew cron (airdrop-topup.sh) writes the live budget here so it tracks the fee.
    $poolFile = adPrivateDir() . '/airdrop-pool.txt';
    $override = is_file($poolFile) ? trim((string)@file_get_contents($poolFile, false, null, 0, 64)) : '';
    $base = isset($c['pool_xmz']) && is_numeric($c['pool_xmz']) ? max(0.0, (float)$c['pool_xmz']) : 0.0;
    $autoRenew = ($c['auto_renew'] ?? false) === true;
    $pool = $autoRenew && is_numeric($override) ? max(0.0, (float)$override) : $base;
    return [
        'enabled'      => ($c['enabled'] ?? false) === true,
        'pool_xmz'     => $pool,                              // live budget (base, or the renewed figure)
        'base_pool_xmz' => $base,
        'topup_buffer' => isset($c['topup_buffer']) && is_numeric($c['topup_buffer']) ? max(0.0, (float)$c['topup_buffer']) : 1000.0,
        'starts_at'    => $int('starts_at', 0),
        'ends_at'      => $int('ends_at', 0),                 // 0 = open-ended
        'rewards'      => is_array($c['rewards'] ?? null) ? $c['rewards'] : (is_array($c['points'] ?? null) ? $c['points'] : []),
        'per_person_cap' => isset($c['per_person_cap']) && is_numeric($c['per_person_cap']) ? (float)$c['per_person_cap'] : 25.0,
        'auto_renew'   => $autoRenew,
        'mining_days_required' => isset($c['mining_days_required']) && is_int($c['mining_days_required']) && $c['mining_days_required'] >= 1 && $c['mining_days_required'] <= 31 ? $c['mining_days_required'] : 7,
        'max_per_ip'     => isset($c['max_per_ip']) && is_int($c['max_per_ip']) && $c['max_per_ip'] >= 0 ? $c['max_per_ip'] : 3,
        'trusted_proxies' => is_array($c['trusted_proxies'] ?? null) ? $c['trusted_proxies'] : [],
        'referral_cap'   => isset($c['referral_cap']) && is_numeric($c['referral_cap']) ? (float)$c['referral_cap'] : 12.5,
        'admin_reward_xmz' => isset($c['admin_reward_xmz']) && is_numeric($c['admin_reward_xmz']) ? (float)$c['admin_reward_xmz'] : 0.0,
        'admins'         => adParseAdmins($c['admins'] ?? null),
        'admin_token'  => is_string($c['admin_token'] ?? null) ? $c['admin_token'] : null,
        'pool_stats'   => is_string($c['pool_stats'] ?? null) ? $c['pool_stats'] : 'https://pool.monzero.org/api/miner',
        'telegram'     => is_string($c['telegram'] ?? null) ? $c['telegram'] : '@MonzeroOfficial',
        'x'            => is_string($c['x'] ?? null) ? $c['x'] : 'MonzeroXMZ',
        'instagram'    => is_string($c['instagram'] ?? null) ? $c['instagram'] : 'monzeroxmz',
    ];
}

// Why the campaign is not open, or null when it is. It needs a budget and a real end date.
function adClosedReason(?int $now = null): ?string {
    $c = adConfig(); $now ??= time();
    if (!$c['enabled']) return 'disabled in the config';
    if ($c['pool_xmz'] <= 0) return 'no budget (pool_xmz) set';
    if ($c['ends_at'] <= 0) return 'no end date (ends_at) set';
    if ($c['starts_at'] > 0 && $now < $c['starts_at']) return 'not started yet';
    if ($now > $c['ends_at']) return 'ended';
    return null;
}
function adLive(?int $now = null): bool { return adClosedReason($now) === null; }

// XMZ each task pays (owner can override in config['rewards']). Harder tasks pay more; the
// core tasks sum to 22.5 XMZ (per-person task cap 25); referrals pay 0.5 XMZ each, capped at 12.5.
function adRewardXmz(string $task): float {
    $defaults = ['wallet' => 1, 'telegram' => 1, 'mining' => 6, 'forum_post' => 2.5, 'forum_active' => 5,
        'run_node' => 4, 'share_x' => 1.5, 'share_instagram' => 1.5, 'referral' => 0.5];
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
            'telegram' => $str($a, 'telegram'), 'password' => $str($a, 'password'), 'pin' => $str($a, 'pin'),
            'source' => 'config', 'status' => 'active', 'id' => null];
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
    foreach (adModerators(true) as $a) {
        if (!hash_equals((string)$a['address'], $address)) continue;
        if ($a['telegram'] === '' || ltrim(strtolower($a['telegram']), '@') !== $tg) continue;
        if (!adVerifySecret((string)$a['password'], $password)) continue;
        if (!adVerifySecret((string)$a['pin'], $pin)) continue;
        if ($a['id'] !== null) adDb()->prepare('UPDATE moderators SET last_login_at = ? WHERE id = ?')->execute([time(), $a['id']]);
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
    foreach (adModerators(true) as $a) if ($a['name'] === $name) return $name;   // suspension ends a session at once
    return null;
}
// Returns the reviewer name for a valid admin key, or null. The master admin_token reviews as 'admin'.
function adAdminAuth(string $key): ?string {
    if ($key === '') return null;
    $cfg = adConfig();
    if (is_string($cfg['admin_token']) && $cfg['admin_token'] !== '' && hash_equals($cfg['admin_token'], $key)) return 'admin';
    foreach ($cfg['admins'] as $a) if (strlen($a['token']) >= 16 && hash_equals($a['token'], $key)) return $a['name'];
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
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 5) {
        // mining_seen: last pool earnings total (pending + paid, atomic units) seen per participant,
        // so a mining day is only counted when earnings actually grow. tick_nonces: replay guard.
        $db->exec('CREATE TABLE IF NOT EXISTS mining_seen (participant_id INTEGER PRIMARY KEY, earned INTEGER NOT NULL, updated_at INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS tick_nonces (nonce TEXT PRIMARY KEY, at INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS ix_participants_ref ON participants(referred_by);
            PRAGMA user_version = 5;');
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 6) {
        // moderators: managed from /admin/ (config-file moderators still work alongside).
        // review_log: every review decision and every owner override. mod_login_fails: throttle.
        $db->exec("CREATE TABLE IF NOT EXISTS moderators (id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE,
                address TEXT NOT NULL, telegram TEXT NOT NULL, password_hash TEXT NOT NULL, pin_hash TEXT NOT NULL,
                status TEXT NOT NULL DEFAULT 'active', note TEXT NOT NULL DEFAULT '', created_at INTEGER NOT NULL,
                created_by TEXT NOT NULL DEFAULT '', last_login_at INTEGER);
            CREATE TABLE IF NOT EXISTS review_log (id INTEGER PRIMARY KEY, submission_id INTEGER NOT NULL, actor TEXT NOT NULL,
                action TEXT NOT NULL, from_status TEXT, to_status TEXT, note TEXT NOT NULL DEFAULT '', at INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS ix_review_log_sub ON review_log(submission_id);
            CREATE TABLE IF NOT EXISTS mod_login_fails (ip_hash TEXT NOT NULL, at INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS ix_submissions_reviewer ON submissions(reviewer, status);
            PRAGMA user_version = 6;");
    }
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 7) {
        // Owner tools: disqualify/reinstate entries, XMZ adjustments with a reason, and a per-entry log.
        $db->exec("ALTER TABLE participants ADD COLUMN status TEXT NOT NULL DEFAULT 'active';
            ALTER TABLE participants ADD COLUMN status_reason TEXT NOT NULL DEFAULT '';
            CREATE TABLE IF NOT EXISTS adjustments (id INTEGER PRIMARY KEY, participant_id INTEGER NOT NULL, centi INTEGER NOT NULL,
                reason TEXT NOT NULL, actor TEXT NOT NULL, at INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS ix_adjustments_p ON adjustments(participant_id);
            CREATE TABLE IF NOT EXISTS participant_log (id INTEGER PRIMARY KEY, participant_id INTEGER NOT NULL, actor TEXT NOT NULL,
                action TEXT NOT NULL, detail TEXT NOT NULL DEFAULT '', at INTEGER NOT NULL);
            CREATE INDEX IF NOT EXISTS ix_participant_log_p ON participant_log(participant_id);
            PRAGMA user_version = 7;");
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

    $budget = adBudget();
    if ($budget['pool'] <= 0 || $budget['committed'] >= $budget['pool'])
        return ['error' => 'The airdrop budget is fully committed, so new entries are closed. Follow the Telegram for the next round.'];
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
    // The wallet reward is NOT paid here: it unlocks once Telegram or mining is verified, so a
    // bare address (which costs nothing to make) earns nothing on its own.
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
    $ins = adDb()->prepare('INSERT INTO progress (participant_id, task, points, updated_at) VALUES (?, ?, ?, ?)
        ON CONFLICT (participant_id, task) DO UPDATE SET points = excluded.points, updated_at = excluded.updated_at');
    $ins->execute([$pid, $task, adPoints($task), time()]);
    // A verified Telegram account or real mining unlocks the wallet reward.
    if (in_array($task, ['telegram', 'mining'], true))
        adDb()->prepare('INSERT OR IGNORE INTO progress (participant_id, task, points, updated_at) VALUES (?, ?, ?, ?)')
            ->execute([$pid, 'wallet', adPoints('wallet'), time()]);
}

// Number of distinct days a participant has been seen mining.
function adMiningDays(int $pid): int {
    $s = adDb()->prepare('SELECT COUNT(*) FROM mining_days WHERE participant_id = ?');
    $s->execute([$pid]);
    return (int)$s->fetchColumn();
}

// Mining must be SUSTAINED: a participant must be seen mining on at least mining_days_required
// distinct (UTC) days during the campaign. A day counts when, that day, either
//   - the pool's earnings total for the address (pending + paid) has grown since the last look, or
//   - the pool reports live hashrate / connected workers for it right now.
// An unpaid balance on its own does NOT count: a few minutes of mining leaves a balance below the
// pool's payout threshold that never moves, and must not earn a "day" every day after.
// $earned: pool earnings total in atomic units (null = unknown). Returns true once credited.
function adObserveMining(int $pid, ?int $earned, bool $activeNow, ?int $now = null): bool {
    $db = adDb(); $now ??= time();
    $done = $db->prepare('SELECT 1 FROM progress WHERE participant_id = ? AND task = ?');
    $done->execute([$pid, 'mining']);
    if ($done->fetchColumn()) return true;
    $counts = adLive($now);                  // days only accrue while the campaign is open
    $grew = false;
    if ($earned !== null && $earned >= 0) {
        $s = $db->prepare('SELECT earned FROM mining_seen WHERE participant_id = ?');
        $s->execute([$pid]);
        $prev = $s->fetchColumn();
        // First sighting only sets the baseline: earnings from before joining don't count.
        if ($prev !== false && $earned > (int)$prev) $grew = true;
        if ($prev === false || $earned > (int)$prev)   // only ever moves up, so growth is never counted twice
            $db->prepare('INSERT INTO mining_seen (participant_id, earned, updated_at) VALUES (?, ?, ?)
                ON CONFLICT (participant_id) DO UPDATE SET earned = excluded.earned, updated_at = excluded.updated_at')
                ->execute([$pid, $earned, $now]);
    }
    if ($counts && ($grew || $activeNow))
        $db->prepare('INSERT OR IGNORE INTO mining_days (participant_id, day) VALUES (?, ?)')->execute([$pid, intdiv($now, 86400)]);
    if ($counts && adMiningDays($pid) >= adConfig()['mining_days_required']) { adAward($pid, 'mining'); return true; }
    return false;
}

// On-view check against the public pool API (the scheduled tick covers everyone else).
function adCheckMining(array $p): bool {
    $db = adDb();
    $done = $db->prepare('SELECT 1 FROM progress WHERE participant_id = ? AND task = ?');
    $done->execute([$p['id'], 'mining']);
    if ($done->fetchColumn()) return true;
    $url = rtrim(adConfig()['pool_stats'], '/') . '?address=' . rawurlencode($p['address']);
    $ctx = stream_context_create(['http' => ['timeout' => 6, 'ignore_errors' => true, 'follow_location' => 0]]);
    $body = @file_get_contents($url, false, $ctx, 0, 262144);
    if ($body === false || !is_array($d = json_decode($body, true))) return false;
    $active = ((float)($d['hashrate'] ?? 0) > 0) || (is_array($d['workers'] ?? null) && count($d['workers']) > 0);
    $earned = is_numeric($d['pending'] ?? null) && is_numeric($d['paid'] ?? null) ? (int)$d['pending'] + (int)$d['paid'] : null;
    return adObserveMining((int)$p['id'], $earned, $active);
}

// A participant counts toward a referrer once they have an auto-verified task (telegram or mining).
function adReferralPoints(array $p): int {
    $n = (int)adDb()->query('SELECT COUNT(DISTINCT pr.participant_id) FROM participants c
        JOIN progress pr ON pr.participant_id = c.id AND pr.task IN (\'telegram\',\'mining\')
        WHERE c.status = \'active\' AND c.referred_by = ' . adDb()->quote($p['ref_code']))->fetchColumn();
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
    $tot = adTotals((int)$p['id'])[0] ?? ['centi' => 0, 'adj' => 0, 'status' => 'active'];
    return [
        'disqualified' => $tot['status'] !== 'active',
        'adjustment' => $tot['adj'] / 100,
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
        'earned' => $tot['centi'] / 100,
        'mining_days' => adMiningDays((int)$p['id']),
        'mining_days_required' => adConfig()['mining_days_required'],
    ];
}

function adReviewTasks(): array { return ['share_x', 'share_instagram', 'forum_post', 'forum_active', 'run_node']; }

function adSubmit(array $p, string $task, string $detail): array {
    if (!in_array($task, adReviewTasks(), true)) return ['error' => 'Unknown task.'];
    if (($p['status'] ?? 'active') !== 'active') return ['error' => 'This airdrop entry has been disqualified.'];
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
    foreach (adModerators() as $a) $addr[$a['name']] = $a['address'];   // suspended moderators keep what they earned
    $rows = adDb()->query("SELECT reviewer, COUNT(*) AS n FROM submissions WHERE status = 'approved' AND reviewer IS NOT NULL AND reviewer != '' GROUP BY reviewer")->fetchAll();
    $out = [];
    foreach ($rows as $r) $out[] = ['admin' => $r['reviewer'], 'address' => $addr[$r['reviewer']] ?? '', 'confirmations' => (int)$r['n'], 'xmz' => round($reward * (int)$r['n'], 8)];
    usort($out, fn($a, $b) => $b['confirmations'] <=> $a['confirmations']);
    return $out;
}

// Every participant's total in centi-XMZ, in one query: tasks (capped) + verified referrals
// (capped, active referred entries only) + owner adjustments, never below 0. Disqualified = 0.
// $pid limits it to one participant.
function adTotals(?int $pid = null): array {
    $db = adDb();
    $q = $db->prepare("SELECT p.id, p.address, p.status,
        COALESCE((SELECT SUM(points) FROM progress WHERE participant_id = p.id), 0)
        + COALESCE((SELECT SUM(points) FROM submissions WHERE participant_id = p.id AND status = 'approved'), 0) AS base,
        (SELECT COUNT(*) FROM participants c WHERE c.referred_by = p.ref_code AND c.status = 'active'
            AND EXISTS (SELECT 1 FROM progress pr WHERE pr.participant_id = c.id AND pr.task IN ('telegram','mining'))) AS refs,
        COALESCE((SELECT SUM(centi) FROM adjustments WHERE participant_id = p.id), 0) AS adj
        FROM participants p" . ($pid !== null ? ' WHERE p.id = ?' : ''));
    $q->execute($pid !== null ? [$pid] : []);
    $cap = adCapCenti();
    $refCap = (int)round(adConfig()['referral_cap'] * 100);
    $out = [];
    foreach ($q->fetchAll() as $r) {
        $tasks = min($cap, (int)$r['base']); $refs = min($refCap, (int)$r['refs'] * adPoints('referral'));
        $centi = $r['status'] === 'active' ? max(0, $tasks + $refs + (int)$r['adj']) : 0;
        $out[] = ['id' => (int)$r['id'], 'address' => $r['address'], 'status' => $r['status'], 'tasks' => $tasks, 'refs' => $refs,
            'referred_verified' => (int)$r['refs'], 'adj' => (int)$r['adj'], 'centi' => $centi];
    }
    return $out;
}

// Earned (unscaled) per participant, largest first.
function adPayouts(): array {
    $out = [];
    foreach (adTotals() as $t) if ($t['centi'] > 0) $out[] = ['address' => $t['address'], 'xmz' => $t['centi'] / 100];
    usort($out, fn($a, $b) => $b['xmz'] <=> $a['xmz']);
    return $out;
}

// Budget position. Moderator rewards are paid first; participants share what is left, scaled
// down (never up) if they have earned more than that.
function adBudget(): array {
    $pool = (float)adConfig()['pool_xmz'];
    $participants = array_sum(array_column(adTotals(), 'centi')) / 100;
    $admins = array_sum(array_column(adAdminPayouts(), 'xmz'));
    $avail = max(0.0, $pool - $admins);
    $scale = $participants > 0 ? min(1.0, $avail / $participants) : 1.0;
    return ['pool' => $pool, 'participant_xmz' => round($participants, 8), 'admin_xmz' => round($admins, 8),
        'committed' => round($participants + $admins, 8), 'remaining' => round(max(0.0, $pool - $participants - $admins), 8), 'scale' => $scale];
}

// Final payout list in atomic units (1 XMZ = 1e11), never more than the budget in total.
// kind = participant | moderator. Moderators without an address are left out (paid by hand).
function adFinalPayouts(): array {
    $b = adBudget();
    $out = [];
    foreach (adTotals() as $t) {
        if ($t['centi'] <= 0) continue;
        $atomic = (int)floor($t['centi'] * 1e9 * $b['scale']);
        if ($atomic > 0) $out[] = ['kind' => 'participant', 'address' => $t['address'], 'atomic' => $atomic, 'xmz' => $atomic / 1e11];
    }
    foreach (adAdminPayouts() as $a) {
        if ($a['address'] === '' || $a['xmz'] <= 0) continue;
        $atomic = (int)round($a['xmz'] * 1e11);
        $out[] = ['kind' => 'moderator', 'address' => $a['address'], 'atomic' => $atomic, 'xmz' => $atomic / 1e11];
    }
    usort($out, fn($a, $b) => $b['atomic'] <=> $a['atomic']);
    return $out;
}

function adLeaderboard(int $limit = 20): array {
    $out = [];
    foreach (adTotals() as $t) if ($t['status'] === 'active') $out[] = ['address' => adShort($t['address']), 'xmz' => $t['centi'] / 100];
    usort($out, fn($a, $b) => $b['xmz'] <=> $a['xmz']);
    return array_slice($out, 0, max(1, min(100, $limit)));
}
function adParticipantCount(): int { return (int)adDb()->query('SELECT COUNT(*) FROM participants')->fetchColumn(); }

// ---- Scheduled tick from monzero-eu (utils/website/airdrop_tick.py) ----

function adTickSecret(): ?string {
    $path = adPrivateDir() . '/airdrop-tick-secret';
    $s = is_file($path) && !is_link($path) ? trim((string)@file_get_contents($path, false, null, 0, 256)) : '';
    return preg_match('/\A[0-9a-f]{64}\z/', $s) ? $s : null;
}

// Verify an HMAC-signed tick (same scheme as /api/status/). Returns an error string or null.
function adTickVerify(string $ts, string $nonce, string $signature, string $body, ?int $now = null): ?string {
    $now ??= time();
    $secret = adTickSecret();
    if ($secret === null) return 'not configured';
    if (!preg_match('/\A\d{1,12}\z/', $ts) || abs($now - (int)$ts) > 300 || !preg_match('/\A[0-9a-f]{32}\z/', $nonce)
        || !hash_equals(hash_hmac('sha256', "$ts\n$nonce\n" . $body, $secret), $signature)) return 'bad signature';
    $db = adDb();
    $db->prepare('DELETE FROM tick_nonces WHERE at < ?')->execute([$now - 900]);
    $ins = $db->prepare('INSERT OR IGNORE INTO tick_nonces (nonce, at) VALUES (?, ?)');
    $ins->execute([$nonce, $now]);
    return $ins->rowCount() === 1 ? null : 'replayed';
}

// $payload: {"fee_atomic": int|null, "balances": {address: pending+paid atomic}}.
// Records mining days for every participant and renews the budget from the collected fee:
//   budget = max(base, committed + buffer), never more than the fee collected.
function adTick(array $payload, ?int $now = null): array {
    $now ??= time();
    $db = adDb();
    $balances = is_array($payload['balances'] ?? null) ? $payload['balances'] : null;
    $checked = 0; $credited = 0;
    if ($balances !== null) {
        $db->beginTransaction();
        try {
            $rows = $db->query("SELECT p.id, p.address, EXISTS (SELECT 1 FROM progress pr WHERE pr.participant_id = p.id AND pr.task = 'mining') AS done FROM participants p")->fetchAll();
            foreach ($rows as $r) {
                if ($r['done']) continue;
                $v = $balances[$r['address']] ?? 0;   // absent = never earned on the pool
                if (!is_int($v) || $v < 0) continue;
                $checked++;
                if (adObserveMining((int)$r['id'], $v, false, $now)) $credited++;
            }
            $db->commit();
        } catch (Throwable $e) { $db->rollBack(); throw $e; }
    }
    $cfg = adConfig();
    $pool = $cfg['pool_xmz'];
    $fee = $payload['fee_atomic'] ?? null;
    if ($cfg['auto_renew'] && is_int($fee) && $fee >= 0) {
        $b = adBudget();
        $target = min(max($cfg['base_pool_xmz'], $b['committed'] + $cfg['topup_buffer']), $fee / 1e11);
        $target = round($target, 2);
        $file = adPrivateDir() . '/airdrop-pool.txt';
        $tmp = $file . '.tmp';
        if (@file_put_contents($tmp, $target . "\n") !== false) { @chmod($tmp, 0600); @rename($tmp, $file); $pool = $target; }
    }
    return ['ok' => true, 'checked' => $checked, 'newly_credited' => $credited, 'pool_xmz' => $pool];
}

// ---- Moderators (owner-managed from /admin/?view=airdrop) ----

const AD_MOD_LOGIN_MAX_FAILS = 10;      // per client, per 15 minutes

// Every moderator: config-file entries (source=config) plus database ones (source=db).
// $activeOnly drops suspended database moderators (they cannot sign in or review).
function adModerators(bool $activeOnly = false): array {
    $out = adConfig()['admins'];
    foreach (adDb()->query('SELECT * FROM moderators ORDER BY name COLLATE NOCASE')->fetchAll() as $m) {
        if ($activeOnly && $m['status'] !== 'active') continue;
        $out[] = ['id' => (int)$m['id'], 'name' => $m['name'], 'address' => $m['address'], 'telegram' => $m['telegram'],
            'password' => $m['password_hash'], 'pin' => $m['pin_hash'], 'token' => '', 'source' => 'db', 'status' => $m['status'],
            'note' => $m['note'], 'created_at' => (int)$m['created_at'], 'created_by' => $m['created_by'], 'last_login_at' => $m['last_login_at'] !== null ? (int)$m['last_login_at'] : null];
    }
    return $out;
}

function adModLoginThrottled(string $ipHash): bool {
    if ($ipHash === '') return false;
    $db = adDb();
    $db->prepare('DELETE FROM mod_login_fails WHERE at < ?')->execute([time() - 900]);
    $s = $db->prepare('SELECT COUNT(*) FROM mod_login_fails WHERE ip_hash = ?');
    $s->execute([$ipHash]);
    return (int)$s->fetchColumn() >= AD_MOD_LOGIN_MAX_FAILS;
}
function adModLoginFailed(string $ipHash): void {
    if ($ipHash !== '') adDb()->prepare('INSERT INTO mod_login_fails (ip_hash, at) VALUES (?, ?)')->execute([$ipHash, time()]);
}

function adModGeneratePassword(): string {
    $alpha = 'abcdefghjkmnpqrstuvwxyz23456789';
    $groups = [];
    for ($g = 0; $g < 4; $g++) { $x = ''; for ($i = 0; $i < 4; $i++) $x .= $alpha[random_int(0, strlen($alpha) - 1)]; $groups[] = $x; }
    return implode('-', $groups);
}
function adModGeneratePin(): string { return str_pad((string)random_int(0, 999999), 6, '0', STR_PAD_LEFT); }

// Validate moderator fields. Returns [normalised fields] or ['error' => ...].
function adModValidate(string $name, string $address, string $telegram, ?int $selfId = null): array {
    $name = preg_replace('/\s+/', ' ', trim($name)); $telegram = trim($telegram);
    if (!preg_match('/\A[A-Za-z0-9_.-]([A-Za-z0-9_. -]{0,30}[A-Za-z0-9_.-])?\z/', $name) || strlen($name) < 2) return ['error' => 'Name: 2 to 32 letters, digits, spaces, dot, dash or underscore.'];
    if (in_array(strtolower($name), ['admin', 'console', 'owner', 'system'], true)) return ['error' => 'That name is reserved.'];
    foreach (adModerators() as $m) if (strcasecmp($m['name'], $name) === 0 && $m['id'] !== $selfId) return ['error' => 'A moderator with that name already exists.'];
    $parsed = xmzParseAddress($address);
    if (isset($parsed['error'])) return ['error' => 'XMZ address: ' . $parsed['error']];
    if ($parsed['network'] !== 'mainnet' || $parsed['kind'] === 'integrated') return ['error' => 'Use a main mainnet XMZ address (rewards are paid to it).'];
    if (!preg_match('/\A@?([A-Za-z0-9_]{5,32})\z/', $telegram, $m)) return ['error' => 'Telegram handle: 5 to 32 letters, digits or underscores.'];
    return ['name' => $name, 'address' => $parsed['address'], 'telegram' => '@' . $m[1]];
}

// Owner-chosen secrets: password 10+ characters, PIN 6 to 8 digits. null = OK.
function adModSecretProblem(string $password, string $pin): ?string {
    if (strlen($password) < 10 || strlen($password) > 128) return 'Password: at least 10 characters.';
    if (!preg_match('/\A\d{6,8}\z/', $pin)) return 'PIN: 6 to 8 digits.';
    return null;
}

// Create a moderator. Password/PIN are generated unless the owner supplies both.
// Returns ['id', 'name', 'password', 'pin', ...] (shown once) or ['error'].
function adModCreate(string $name, string $address, string $telegram, string $note, string $by, string $password = '', string $pin = ''): array {
    $v = adModValidate($name, $address, $telegram);
    if (isset($v['error'])) return $v;
    if ($password !== '' || $pin !== '') {
        if ($problem = adModSecretProblem($password, $pin)) return ['error' => $problem];
    } else { $password = adModGeneratePassword(); $pin = adModGeneratePin(); }
    adDb()->prepare('INSERT INTO moderators (name, address, telegram, password_hash, pin_hash, status, note, created_at, created_by) VALUES (?, ?, ?, ?, ?, \'active\', ?, ?, ?)')
        ->execute([$v['name'], $v['address'], $v['telegram'], password_hash($password, PASSWORD_DEFAULT), password_hash($pin, PASSWORD_DEFAULT), mb_substr(trim($note), 0, 300), time(), $by]);
    return ['id' => (int)adDb()->lastInsertId(), 'name' => $v['name'], 'address' => $v['address'], 'telegram' => $v['telegram'], 'password' => $password, 'pin' => $pin];
}

function adModById(int $id): ?array {
    foreach (adModerators() as $m) if ($m['id'] === $id) return $m;
    return null;
}

function adModUpdate(int $id, string $address, string $telegram, string $note): ?string {
    $m = adModById($id);
    if (!$m) return 'No such moderator.';
    $v = adModValidate($m['name'], $address, $telegram, $id);
    if (isset($v['error'])) return $v['error'];
    adDb()->prepare('UPDATE moderators SET address = ?, telegram = ?, note = ? WHERE id = ?')->execute([$v['address'], $v['telegram'], mb_substr(trim($note), 0, 300), $id]);
    return null;
}

function adModSetStatus(int $id, string $status): bool {
    if (!in_array($status, ['active', 'suspended'], true)) return false;
    $s = adDb()->prepare('UPDATE moderators SET status = ? WHERE id = ?');
    $s->execute([$status, $id]);
    return $s->rowCount() === 1;
}

// New password + PIN (old ones stop working at once). Returns them for one-time display.
function adModResetSecrets(int $id): ?array {
    $password = adModGeneratePassword(); $pin = adModGeneratePin();
    $s = adDb()->prepare('UPDATE moderators SET password_hash = ?, pin_hash = ? WHERE id = ?');
    $s->execute([password_hash($password, PASSWORD_DEFAULT), password_hash($pin, PASSWORD_DEFAULT), $id]);
    return $s->rowCount() === 1 ? ['password' => $password, 'pin' => $pin] : null;
}

// Removing forfeits unpaid moderator rewards (no payout address any more); suspend to keep them.
function adModDelete(int $id): bool {
    $s = adDb()->prepare('DELETE FROM moderators WHERE id = ?');
    $s->execute([$id]);
    return $s->rowCount() === 1;
}

// A moderator's own participant entry (same address) and referral code, if they joined.
function adModParticipant(string $address): ?array {
    $s = adDb()->prepare('SELECT id, ref_code FROM participants WHERE address = ?');
    $s->execute([$address]);
    return $s->fetch() ?: null;
}

// Moderator review: pending -> approved|rejected. A moderator may not review their own entry.
function adReview(int $id, string $decision, string $reviewer): ?string {
    if (!in_array($decision, ['approve', 'reject'], true)) return 'Bad decision.';
    $db = adDb();
    $s = $db->prepare("SELECT s.platform, p.address FROM submissions s JOIN participants p ON p.id = s.participant_id WHERE s.id = ? AND s.status = 'pending'");
    $s->execute([$id]);
    $row = $s->fetch();
    if (!$row) return 'Not found or already reviewed.';
    foreach (adModerators() as $m) if ($m['name'] === $reviewer && $m['address'] !== '' && hash_equals($m['address'], $row['address']))
        return 'You cannot review your own submission. Another moderator has to.';
    $to = $decision === 'approve' ? 'approved' : 'rejected';
    $now = time();
    $db->prepare('UPDATE submissions SET status = ?, points = ?, reviewed_at = ?, reviewer = ? WHERE id = ?')
        ->execute([$to, $to === 'approved' ? adPoints((string)$row['platform']) : 0, $now, $reviewer, $id]);
    $db->prepare('INSERT INTO review_log (submission_id, actor, action, from_status, to_status, at) VALUES (?, ?, ?, ?, ?, ?)')
        ->execute([$id, $reviewer, 'review', 'pending', $to, $now]);
    return null;
}

// Owner override from /admin/: approved|rejected|pending. The original reviewer stays in the
// log; an override decision is credited to "console" (no moderator reward for it).
function adOverride(int $id, string $to, string $actor, string $note = ''): ?string {
    if (!in_array($to, ['approved', 'rejected', 'pending'], true)) return 'Bad status.';
    $db = adDb();
    $s = $db->prepare('SELECT platform, status FROM submissions WHERE id = ?');
    $s->execute([$id]);
    $row = $s->fetch();
    if (!$row) return 'No such submission.';
    if ($row['status'] === $to) return 'It already has that status.';
    $now = time();
    if ($to === 'pending') $db->prepare("UPDATE submissions SET status = 'pending', points = 0, reviewed_at = NULL, reviewer = NULL WHERE id = ?")->execute([$id]);
    else $db->prepare('UPDATE submissions SET status = ?, points = ?, reviewed_at = ?, reviewer = ? WHERE id = ?')
        ->execute([$to, $to === 'approved' ? adPoints((string)$row['platform']) : 0, $now, 'console', $id]);
    $db->prepare('INSERT INTO review_log (submission_id, actor, action, from_status, to_status, note, at) VALUES (?, ?, ?, ?, ?, ?, ?)')
        ->execute([$id, $actor, 'override', $row['status'], $to, mb_substr(trim($note), 0, 300), $now]);
    return null;
}

// Per-moderator numbers for the owner: approvals, rejections, last 24 h, last review, and two
// warning signs: approving their own entry, and approving people they referred.
function adModStats(): array {
    $db = adDb();
    $out = [];
    $since = time() - 86400;
    foreach (adModerators() as $m) {
        $own = adModParticipant($m['address']);
        $q = $db->prepare("SELECT
              SUM(status = 'approved') AS approved, SUM(status = 'rejected') AS rejected,
              SUM(reviewed_at >= ?) AS day, MAX(reviewed_at) AS last
            FROM submissions WHERE reviewer = ?");
        $q->execute([$since, $m['name']]);
        $r = $q->fetch();
        $self = 0; $referred = 0;
        if ($own) {
            $a = $db->prepare("SELECT COUNT(*) FROM submissions WHERE reviewer = ? AND status = 'approved' AND participant_id = ?");
            $a->execute([$m['name'], $own['id']]); $self = (int)$a->fetchColumn();
            $b = $db->prepare("SELECT COUNT(*) FROM submissions s JOIN participants p ON p.id = s.participant_id WHERE s.reviewer = ? AND s.status = 'approved' AND p.referred_by = ?");
            $b->execute([$m['name'], $own['ref_code']]); $referred = (int)$b->fetchColumn();
        }
        $o = $db->prepare("SELECT COUNT(*) FROM review_log l JOIN review_log r ON r.submission_id = l.submission_id AND r.action = 'review' AND r.actor = ?
            WHERE l.action = 'override'");
        $o->execute([$m['name']]);
        $approved = (int)($r['approved'] ?? 0); $rejected = (int)($r['rejected'] ?? 0);
        $out[$m['name']] = ['approved' => $approved, 'rejected' => $rejected, 'day' => (int)($r['day'] ?? 0),
            'last' => $r['last'] !== null ? (int)$r['last'] : null, 'self' => $self, 'referred' => $referred, 'overridden' => (int)$o->fetchColumn(),
            'rate' => $approved + $rejected > 0 ? $approved / ($approved + $rejected) : null,
            'reward' => round($approved * (float)adConfig()['admin_reward_xmz'], 8), 'joined' => $own !== null,
            'referrals' => $own ? (int)$db->query('SELECT COUNT(*) FROM participants WHERE referred_by = ' . $db->quote($own['ref_code']))->fetchColumn() : 0];
    }
    return $out;
}

// Reviewed submissions, newest first, with warning flags. $reviewer/$status filter (optional).
function adReviewHistory(?string $reviewer = null, ?string $status = null, int $limit = 200): array {
    $db = adDb();
    $where = ["s.status IN ('approved','rejected')"]; $args = [];
    if ($reviewer !== null && $reviewer !== '') { $where[] = 's.reviewer = ?'; $args[] = $reviewer; }
    if ($status !== null && in_array($status, ['approved', 'rejected'], true)) { $where[] = 's.status = ?'; $args[] = $status; }
    $q = $db->prepare('SELECT s.id, s.platform, s.url, s.status, s.points, s.reviewer, s.reviewed_at, s.created_at, p.address, p.referred_by
        FROM submissions s JOIN participants p ON p.id = s.participant_id WHERE ' . implode(' AND ', $where) . ' ORDER BY s.reviewed_at DESC, s.id DESC LIMIT ' . max(1, min(1000, $limit)));
    $q->execute($args);
    $mods = [];
    foreach (adModerators() as $m) { $own = adModParticipant($m['address']); $mods[$m['name']] = ['address' => $m['address'], 'ref' => $own['ref_code'] ?? null]; }
    $out = [];
    foreach ($q->fetchAll() as $r) {
        $flags = [];
        $mod = $mods[$r['reviewer']] ?? null;
        if ($mod && $mod['address'] !== '' && hash_equals($mod['address'], $r['address'])) $flags[] = 'own entry';
        if ($mod && $mod['ref'] !== null && $r['referred_by'] === $mod['ref'] && $r['status'] === 'approved') $flags[] = 'their referral';
        if ($r['reviewed_at'] !== null && (int)$r['reviewed_at'] - (int)$r['created_at'] < 60 && $r['status'] === 'approved') $flags[] = 'approved within a minute';
        $r['flags'] = $flags;
        $out[] = $r;
    }
    return $out;
}

function adReviewLog(int $limit = 100): array {
    return adDb()->query('SELECT * FROM review_log ORDER BY id DESC LIMIT ' . max(1, min(1000, $limit)))->fetchAll();
}

// ---- Participants (owner console) ----

const AD_AUTO_TASKS = ['wallet', 'telegram', 'mining'];

function adParticipantLog(int $pid, string $actor, string $action, string $detail = ''): void {
    adDb()->prepare('INSERT INTO participant_log (participant_id, actor, action, detail, at) VALUES (?, ?, ?, ?, ?)')
        ->execute([$pid, $actor, $action, mb_substr($detail, 0, 500), time()]);
}

// List for the console. $filter: organic (joined without a referral link) | referred | all | disqualified | flagged.
// $q: part of an address or a referral code. Largest earners first.
function adParticipantsList(string $filter = 'organic', string $q = '', int $limit = 300): array {
    $db = adDb();
    $where = []; $args = [];
    if ($filter === 'organic') $where[] = "(p.referred_by IS NULL OR p.referred_by = '') AND p.status = 'active'";
    elseif ($filter === 'referred') $where[] = "p.referred_by IS NOT NULL AND p.referred_by != '' AND p.status = 'active'";
    elseif ($filter === 'disqualified') $where[] = "p.status != 'active'";
    elseif ($filter === 'flagged') $where[] = "p.status = 'active' AND p.ip_hash IS NOT NULL AND (SELECT COUNT(*) FROM participants o WHERE o.ip_hash = p.ip_hash) > 1";
    $q = trim($q);
    if ($q !== '') { $where[] = '(p.address LIKE ? OR p.ref_code = ?)'; $args[] = '%' . str_replace(['%', '_'], ['\%', '\_'], $q) . '%'; $args[] = $q; }
    $sql = "SELECT p.id, p.address, p.ref_code, p.referred_by, p.status, p.status_reason, p.created_at, p.telegram_id IS NOT NULL AS telegram,
        (SELECT COUNT(*) FROM mining_days d WHERE d.participant_id = p.id) AS mining_days,
        (SELECT COUNT(*) FROM submissions s WHERE s.participant_id = p.id AND s.status = 'pending') AS pending,
        (SELECT COUNT(*) FROM submissions s WHERE s.participant_id = p.id AND s.status = 'approved') AS approved,
        (SELECT COUNT(*) FROM progress pr WHERE pr.participant_id = p.id) AS auto_done,
        (SELECT COUNT(*) FROM participants c WHERE c.referred_by = p.ref_code) AS referrals,
        CASE WHEN p.ip_hash IS NULL THEN 1 ELSE (SELECT COUNT(*) FROM participants o WHERE o.ip_hash = p.ip_hash) END AS same_network
        FROM participants p" . ($where ? ' WHERE ' . implode(' AND ', $where) : '') . ' ORDER BY p.created_at DESC';
    $s = $db->prepare($sql); $s->execute($args);
    $rows = $s->fetchAll();
    $tot = [];
    foreach (adTotals() as $t) $tot[$t['id']] = $t;
    foreach ($rows as &$r) { $t = $tot[(int)$r['id']] ?? null; $r['centi'] = $t['centi'] ?? 0; $r['adj'] = $t['adj'] ?? 0; }
    unset($r);
    usort($rows, fn($a, $b) => [$b['centi'], $b['created_at']] <=> [$a['centi'], $a['created_at']]);
    return array_slice($rows, 0, max(1, min(2000, $limit)));
}

function adParticipantCounts(): array {
    $db = adDb();
    return [
        'all' => (int)$db->query('SELECT COUNT(*) FROM participants')->fetchColumn(),
        'organic' => (int)$db->query("SELECT COUNT(*) FROM participants WHERE (referred_by IS NULL OR referred_by = '') AND status = 'active'")->fetchColumn(),
        'referred' => (int)$db->query("SELECT COUNT(*) FROM participants WHERE referred_by IS NOT NULL AND referred_by != '' AND status = 'active'")->fetchColumn(),
        'disqualified' => (int)$db->query("SELECT COUNT(*) FROM participants WHERE status != 'active'")->fetchColumn(),
        'flagged' => (int)$db->query("SELECT COUNT(*) FROM participants p WHERE p.status = 'active' AND p.ip_hash IS NOT NULL AND (SELECT COUNT(*) FROM participants o WHERE o.ip_hash = p.ip_hash) > 1")->fetchColumn(),
    ];
}

// Everything about one entry for the detail page.
function adParticipantDetail(int $id): ?array {
    $db = adDb();
    $s = $db->prepare('SELECT * FROM participants WHERE id = ?'); $s->execute([$id]);
    $p = $s->fetch();
    if (!$p) return null;
    $prog = $db->prepare('SELECT task, points, updated_at FROM progress WHERE participant_id = ?'); $prog->execute([$id]);
    $subs = $db->prepare('SELECT id, platform, url, status, points, reviewer, created_at, reviewed_at FROM submissions WHERE participant_id = ? ORDER BY created_at DESC'); $subs->execute([$id]);
    $days = $db->prepare('SELECT day FROM mining_days WHERE participant_id = ? ORDER BY day'); $days->execute([$id]);
    $refs = $db->prepare("SELECT c.id, c.address, c.status, c.created_at, EXISTS (SELECT 1 FROM progress pr WHERE pr.participant_id = c.id AND pr.task IN ('telegram','mining')) AS verified FROM participants c WHERE c.referred_by = ? ORDER BY c.created_at"); $refs->execute([$p['ref_code']]);
    $referrer = null;
    if ($p['referred_by']) { $r = $db->prepare('SELECT id, address FROM participants WHERE ref_code = ?'); $r->execute([$p['referred_by']]); $referrer = $r->fetch() ?: null; }
    $same = [];
    if ($p['ip_hash']) { $n = $db->prepare('SELECT id, address, status, created_at FROM participants WHERE ip_hash = ? AND id != ? ORDER BY created_at'); $n->execute([$p['ip_hash'], $id]); $same = $n->fetchAll(); }
    $adj = $db->prepare('SELECT * FROM adjustments WHERE participant_id = ? ORDER BY id DESC'); $adj->execute([$id]);
    $log = $db->prepare('SELECT * FROM participant_log WHERE participant_id = ? ORDER BY id DESC LIMIT 100'); $log->execute([$id]);
    $seen = $db->prepare('SELECT earned FROM mining_seen WHERE participant_id = ?'); $seen->execute([$id]);
    return ['p' => $p, 'totals' => adTotals($id)[0], 'progress' => $prog->fetchAll(), 'submissions' => $subs->fetchAll(),
        'mining_days' => array_map('intval', $days->fetchAll(PDO::FETCH_COLUMN)), 'pool_earned' => ($v = $seen->fetchColumn()) === false ? null : (int)$v,
        'referrals' => $refs->fetchAll(), 'referrer' => $referrer, 'same_network' => $same, 'adjustments' => $adj->fetchAll(), 'log' => $log->fetchAll()];
}

function adDisqualify(int $id, string $reason, string $actor): ?string {
    $reason = trim($reason);
    if ($reason === '') return 'Give a reason (kept in the log).';
    $s = adDb()->prepare("UPDATE participants SET status = 'disqualified', status_reason = ? WHERE id = ? AND status = 'active'");
    $s->execute([mb_substr($reason, 0, 300), $id]);
    if ($s->rowCount() !== 1) return 'That entry is not active.';
    adParticipantLog($id, $actor, 'disqualified', $reason);
    return null;
}

function adReinstate(int $id, string $actor): ?string {
    $s = adDb()->prepare("UPDATE participants SET status = 'active', status_reason = '' WHERE id = ? AND status != 'active'");
    $s->execute([$id]);
    if ($s->rowCount() !== 1) return 'That entry is not disqualified.';
    adParticipantLog($id, $actor, 'reinstated');
    return null;
}

// Add or take away XMZ (e.g. +2 for great work, -1.5 for a deleted post). Kept within ±per-person cap.
function adAdjust(int $id, string $xmz, string $reason, string $actor): ?string {
    $reason = trim($reason);
    if (!preg_match('/\A[+-]?\d{1,4}(\.\d{1,2})?\z/', trim($xmz))) return 'Amount: a number of XMZ like 2, -1.5 or +0.25.';
    $centi = (int)round((float)$xmz * 100);
    if ($centi === 0) return 'Amount cannot be zero.';
    if (abs($centi) > adCapCenti()) return 'One adjustment can be at most the per-person cap.';
    if ($reason === '') return 'Give a reason (kept in the log).';
    $db = adDb();
    $s = $db->prepare('SELECT 1 FROM participants WHERE id = ?'); $s->execute([$id]);
    if (!$s->fetchColumn()) return 'No such entry.';
    $db->prepare('INSERT INTO adjustments (participant_id, centi, reason, actor, at) VALUES (?, ?, ?, ?, ?)')->execute([$id, $centi, mb_substr($reason, 0, 300), $actor, time()]);
    adParticipantLog($id, $actor, 'adjusted', sprintf('%+.2f XMZ: %s', $centi / 100, $reason));
    return null;
}

// Take back an automatically credited task. Telegram also unlinks the account so it can be re-verified;
// mining also clears the counted days. Removing telegram/mining does not remove the wallet unlock.
function adRevokeTask(int $id, string $task, string $reason, string $actor): ?string {
    if (!in_array($task, AD_AUTO_TASKS, true)) return 'Unknown task.';
    $reason = trim($reason);
    if ($reason === '') return 'Give a reason (kept in the log).';
    $db = adDb();
    $s = $db->prepare('DELETE FROM progress WHERE participant_id = ? AND task = ?'); $s->execute([$id, $task]);
    if ($s->rowCount() !== 1) return 'That task is not credited.';
    if ($task === 'telegram') $db->prepare('UPDATE participants SET telegram_id = NULL WHERE id = ?')->execute([$id]);
    if ($task === 'mining') $db->prepare('DELETE FROM mining_days WHERE participant_id = ?')->execute([$id]);
    adParticipantLog($id, $actor, 'revoked ' . $task, $reason);
    return null;
}

// Pending work for the owner's review queue. $organicOnly: only people who joined without a referral link.
function adPendingSubmissions(bool $organicOnly = true): array {
    $sql = "SELECT s.id, s.platform, s.url, s.created_at, p.id AS pid, p.address, p.referred_by, p.created_at AS joined,
        CASE WHEN p.ip_hash IS NULL THEN 1 ELSE (SELECT COUNT(*) FROM participants o WHERE o.ip_hash = p.ip_hash) END AS same_network,
        (SELECT COUNT(*) FROM submissions a WHERE a.participant_id = p.id AND a.status = 'approved') AS approved_before,
        (SELECT COUNT(*) FROM submissions r WHERE r.participant_id = p.id AND r.status = 'rejected') AS rejected_before
        FROM submissions s JOIN participants p ON p.id = s.participant_id
        WHERE s.status = 'pending' AND p.status = 'active'" . ($organicOnly ? " AND (p.referred_by IS NULL OR p.referred_by = '')" : '') . ' ORDER BY s.created_at';
    return adDb()->query($sql)->fetchAll();
}
function adPendingCounts(): array {
    $db = adDb();
    return ['organic' => (int)$db->query("SELECT COUNT(*) FROM submissions s JOIN participants p ON p.id = s.participant_id WHERE s.status = 'pending' AND p.status = 'active' AND (p.referred_by IS NULL OR p.referred_by = '')")->fetchColumn(),
        'all' => (int)$db->query("SELECT COUNT(*) FROM submissions s JOIN participants p ON p.id = s.participant_id WHERE s.status = 'pending' AND p.status = 'active'")->fetchColumn()];
}
