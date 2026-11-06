<?php
declare(strict_types=1);
// XMZ faucet: member accounts (email + password + mandatory TOTP), payout addresses,
// claim limits and the claim queue read by the off-site payout worker. This server never
// holds a wallet or keys. Storage and configuration live outside the document root.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once dirname(__DIR__) . '/lib/auth.php';
require_once dirname(__DIR__) . '/lib/xmz-address.php';

// Hash of a random discarded value: unknown emails cost the same to check as real ones.
const FAUCET_DUMMY_HASH = '$argon2id$v=19$m=65536,t=4,p=1$anhVc3lSUVdQL1JGdHMuaQ$dHEyNaN4Xzkb7GJGI6oQfwSx45tBjogSy037Y24mS48';
const FAUCET_SESSION_IDLE = 1800;
const FAUCET_SESSION_MAX = 43200;
const FAUCET_VERIFY_TTL = 86400;
const FAUCET_RESET_TTL = 3600;
const FAUCET_DEFAULTS = [
    'enabled' => false,                     // claims are refused until the owner turns this on
    'amount_atomic' => '10000000000',       // 0.1 XMZ (11 decimal places)
    'daily_cap_atomic' => '500000000000',   // 5 XMZ across everyone per rolling 24 hours
    'cooldown_seconds' => 86400,
    'per_client_daily' => 3,
    'signups_per_client_daily' => 5,
    'turnstile_site_key' => '',
    'turnstile_secret' => '',
    'turnstile_verify_url' => 'https://challenges.cloudflare.com/turnstile/v0/siteverify',
    'turnstile_hostnames' => ['monzero.org', 'www.monzero.org'],  // pages the token must come from
    'worker_secret' => '',
    'trusted_proxies' => [],                // e.g. Cloudflare ranges, only if the site is proxied
    'base_url' => 'https://monzero.org',
];

// Change one key of the private faucet config file, keeping a dated backup. Returns an error or null.
function faucetSetConfig(string $key, mixed $value): ?string {
    $path = getenv('MONZERO_FAUCET_CONFIG');
    $path = is_string($path) && $path !== '' ? $path : faucetPrivateDir() . '/faucet-config.php';
    if (!is_file($path) || is_link($path)) return 'The faucet is not configured on this server.';
    $config = (static fn() => require $path)();
    if (!is_array($config)) return 'The faucet configuration could not be read.';
    $backup = $path . '.bak-' . gmdate('Ymd\THis\Z');
    if (!copy($path, $backup) || !chmod($backup, 0600)) return 'Could not back up the faucet configuration.';
    $config[$key] = $value;
    $tmp = $path . '.' . bin2hex(random_bytes(6)) . '.tmp';
    $old = umask(077);
    $ok = file_put_contents($tmp, '<?php return ' . var_export($config, true) . ";\n") !== false && chmod($tmp, 0600) && rename($tmp, $path);
    umask($old);
    if (!$ok) { @unlink($tmp); return 'Could not write the faucet configuration.'; }
    if (function_exists('opcache_invalidate')) @opcache_invalidate($path, true);
    return null;
}

function faucetHeartbeatPath(): string {
    $configured = getenv('MONZERO_FAUCET_HEARTBEAT');
    return is_string($configured) && $configured !== '' ? $configured : faucetPrivateDir() . '/faucet-heartbeat.json';
}

function faucetPrivateDir(): string { return dirname(__DIR__, 2) . '/monzero-private'; }

function faucetConfig(): array {
    static $config = null;
    if ($config !== null) return $config;
    $path = getenv('MONZERO_FAUCET_CONFIG');
    $path = is_string($path) && $path !== '' ? $path : faucetPrivateDir() . '/faucet-config.php';
    $loaded = is_file($path) && !is_link($path) ? (static fn() => require $path)() : [];
    $config = array_replace(FAUCET_DEFAULTS, is_array($loaded) ? array_intersect_key($loaded, FAUCET_DEFAULTS) : []);
    if (!preg_match('/\A[1-9][0-9]{0,19}\z/', (string)$config['amount_atomic']) || !preg_match('/\A[0-9]{1,20}\z/', (string)$config['daily_cap_atomic'])
        || !is_int($config['cooldown_seconds']) || $config['cooldown_seconds'] < 3600 || !is_bool($config['enabled'])
        || !preg_match('#\Ahttps?://[a-z0-9.:-]+\z#i', (string)$config['base_url'])) {
        error_log('Monzero faucet: invalid configuration, faucet disabled');
        $config = array_replace(FAUCET_DEFAULTS, ['turnstile_site_key' => '', 'turnstile_secret' => '']);
    }
    return $config;
}

// Accounts need Turnstile to be configured; claims additionally need the enabled flag.
function faucetAccountsOpen(): bool { $c = faucetConfig(); return $c['turnstile_site_key'] !== '' && $c['turnstile_secret'] !== ''; }
function faucetClaimsOpen(): bool { return faucetAccountsOpen() && faucetConfig()['enabled'] === true; }

function faucetDb(): PDO {
    static $db = null;
    if ($db) return $db;
    $path = getenv('MONZERO_FAUCET_DB');
    $path = is_string($path) && $path !== '' ? $path : faucetPrivateDir() . '/faucet.sqlite';
    if (!is_dir(dirname($path))) @mkdir(dirname($path), 0700, true);
    if (is_link($path)) throw new RuntimeException('Faucet storage unavailable');
    $new = !file_exists($path);
    $db = new PDO('sqlite:' . $path, null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
    if ($new) @chmod($path, 0600);
    $db->exec('PRAGMA foreign_keys = ON; PRAGMA busy_timeout = 5000; PRAGMA journal_mode = WAL;');
    if ((int)$db->query('PRAGMA user_version')->fetchColumn() < 1) {
        $db->beginTransaction();
        $db->exec(<<<'SQL'
            CREATE TABLE members (
                id INTEGER PRIMARY KEY,
                email TEXT NOT NULL UNIQUE,
                password_hash TEXT NOT NULL,
                email_verified_at INTEGER,
                totp_secret TEXT,
                totp_last_step INTEGER NOT NULL DEFAULT 0,
                address TEXT,
                address_hash TEXT,
                status TEXT NOT NULL DEFAULT 'active' CHECK (status IN ('active', 'disabled')),
                created_at INTEGER NOT NULL,
                last_login_at INTEGER
            );
            CREATE TABLE tokens (
                hash TEXT PRIMARY KEY,
                member_id INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
                purpose TEXT NOT NULL CHECK (purpose IN ('verify', 'reset')),
                created_at INTEGER NOT NULL,
                expires_at INTEGER NOT NULL,
                used_at INTEGER
            );
            CREATE TABLE sessions (
                hash TEXT PRIMARY KEY,
                member_id INTEGER NOT NULL REFERENCES members(id) ON DELETE CASCADE,
                level TEXT NOT NULL CHECK (level IN ('setup', 'full')),
                csrf TEXT NOT NULL,
                created_at INTEGER NOT NULL,
                seen_at INTEGER NOT NULL
            );
            CREATE TABLE claims (
                id INTEGER PRIMARY KEY,
                member_id INTEGER NOT NULL REFERENCES members(id),
                address TEXT NOT NULL,
                address_hash TEXT NOT NULL,
                client_hash TEXT NOT NULL,
                amount_atomic TEXT NOT NULL,
                status TEXT NOT NULL CHECK (status IN ('pending', 'processing', 'paid', 'failed')),
                lease TEXT,
                leased_at INTEGER,
                txid TEXT,
                reason TEXT,
                created_at INTEGER NOT NULL,
                finished_at INTEGER
            );
            CREATE INDEX claims_member ON claims(member_id, created_at);
            CREATE INDEX claims_address ON claims(address_hash, created_at);
            CREATE INDEX claims_client ON claims(client_hash, created_at);
            CREATE INDEX claims_status ON claims(status, id);
            CREATE TABLE limits (key TEXT NOT NULL, at INTEGER NOT NULL);
            CREATE INDEX limits_key ON limits(key, at);
            CREATE TABLE worker_nonces (nonce TEXT PRIMARY KEY, at INTEGER NOT NULL);
            CREATE TABLE events (
                id INTEGER PRIMARY KEY,
                at INTEGER NOT NULL,
                member_id INTEGER,
                action TEXT NOT NULL,
                detail TEXT NOT NULL
            );
            CREATE TRIGGER events_no_update BEFORE UPDATE ON events BEGIN SELECT RAISE(ABORT, 'events are append-only'); END;
            CREATE TRIGGER events_no_delete BEFORE DELETE ON events BEGIN SELECT RAISE(ABORT, 'events are append-only'); END;
            PRAGMA user_version = 1;
        SQL);
        $db->commit();
    }
    return $db;
}

function faucetEvent(?int $member, string $action, array $detail = []): void {
    faucetDb()->prepare('INSERT INTO events (at, member_id, action, detail) VALUES (?, ?, ?, ?)')
        ->execute([time(), $member, $action, json_encode($detail, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE)]);
}

// A per-installation secret for HMACs (client and address hashes), kept with the database.
function faucetSecret(): string {
    static $secret = null;
    if ($secret !== null) return $secret;
    $path = faucetPrivateDir() . '/faucet-secret';
    $db = getenv('MONZERO_FAUCET_DB');
    if (is_string($db) && $db !== '') $path = dirname($db) . '/faucet-secret';
    if (!file_exists($path)) {
        $handle = @fopen($path, 'x');
        if ($handle) { fwrite($handle, bin2hex(random_bytes(32))); fclose($handle); @chmod($path, 0600); }
    }
    $secret = trim((string)@file_get_contents($path));
    if (!preg_match('/\A[0-9a-f]{64}\z/', $secret)) throw new RuntimeException('Faucet secret unavailable');
    return $secret;
}

function faucetIpInCidr(string $ip, string $cidr): bool {
    [$net, $bits] = array_pad(explode('/', $cidr, 2), 2, null);
    $ipBin = @inet_pton($ip); $netBin = @inet_pton((string)$net);
    if ($ipBin === false || $netBin === false || strlen($ipBin) !== strlen($netBin)) return false;
    $bits = $bits === null ? strlen($ipBin) * 8 : (int)$bits;
    $bytes = intdiv($bits, 8); $rest = $bits % 8;
    if (substr($ipBin, 0, $bytes) !== substr($netBin, 0, $bytes)) return false;
    if ($rest === 0) return true;
    $mask = chr((0xff << (8 - $rest)) & 0xff);
    return (($ipBin[$bytes] & $mask) === ($netBin[$bytes] & $mask));
}

// The visitor's address. Proxy headers are believed only from configured proxy ranges.
function faucetClientIp(): string {
    $remote = (string)($_SERVER['REMOTE_ADDR'] ?? '');
    foreach (faucetConfig()['trusted_proxies'] as $cidr) {
        if (is_string($cidr) && faucetIpInCidr($remote, $cidr)) {
            $forwarded = (string)($_SERVER['HTTP_CF_CONNECTING_IP'] ?? '');
            if (filter_var($forwarded, FILTER_VALIDATE_IP)) return $forwarded;
        }
    }
    return $remote;
}

function faucetClientHash(): string { return hash_hmac('sha256', 'client:' . faucetClientIp(), faucetSecret()); }
function faucetAddressHash(string $address): string { return hash_hmac('sha256', 'address:' . $address, faucetSecret()); }

function faucetLimitCount(string $key, int $window): int {
    $db = faucetDb();
    $db->prepare('DELETE FROM limits WHERE at < ?')->execute([time() - 172800]);
    $stmt = $db->prepare('SELECT COUNT(*) FROM limits WHERE key = ? AND at >= ?');
    $stmt->execute([$key, time() - $window]);
    return (int)$stmt->fetchColumn();
}
function faucetLimitHit(string $key): void { faucetDb()->prepare('INSERT INTO limits (key, at) VALUES (?, ?)')->execute([$key, time()]); }

// Server-side Turnstile check (Cloudflare siteverify). The token must succeed, have been issued
// for this form's action and on one of our hostnames; tokens are single-use at Cloudflare.
const FAUCET_TURNSTILE_ACTIONS = ['signup', 'login', 'resend', 'forgot', 'claim'];

function faucetTurnstileOk(mixed $token, string $action): bool {
    $config = faucetConfig();
    if (!in_array($action, FAUCET_TURNSTILE_ACTIONS, true) || !is_string($token) || $token === '' || strlen($token) > 2048 || $config['turnstile_secret'] === '') return false;
    $body = http_build_query(['secret' => $config['turnstile_secret'], 'response' => $token, 'remoteip' => faucetClientIp()]);
    $context = stream_context_create(['http' => ['method' => 'POST', 'header' => "Content-Type: application/x-www-form-urlencoded\r\n",
        'content' => $body, 'timeout' => 10, 'ignore_errors' => true, 'follow_location' => 0]]);
    $reply = @file_get_contents($config['turnstile_verify_url'], false, $context, 0, 65536);
    $status = isset($http_response_header[0]) && preg_match('#\AHTTP/\S+\s+200\b#', $http_response_header[0]);
    $data = $status && is_string($reply) ? json_decode($reply, true) : null;
    $hostnames = is_array($config['turnstile_hostnames']) ? $config['turnstile_hostnames'] : [];
    return is_array($data) && ($data['success'] ?? null) === true && ($data['action'] ?? null) === $action
        && is_string($data['hostname'] ?? null) && in_array(strtolower($data['hostname']), $hostnames, true);
}

function faucetCookieName(): string { return authSecureRequest() ? '__Host-mzfaucet' : 'mzfaucet'; }

function faucetStartSession(int $memberId, string $level): void {
    $token = authRandomToken();
    $now = time();
    faucetDb()->prepare('INSERT INTO sessions (hash, member_id, level, csrf, created_at, seen_at) VALUES (?, ?, ?, ?, ?, ?)')
        ->execute([authTokenHash($token), $memberId, $level, authRandomToken(), $now, $now]);
    setcookie(faucetCookieName(), $token, ['path' => '/', 'secure' => authSecureRequest(), 'httponly' => true, 'samesite' => 'Strict']);
}

function faucetEndSession(): void {
    $token = $_COOKIE[faucetCookieName()] ?? '';
    if (authTokenShape($token)) faucetDb()->prepare('DELETE FROM sessions WHERE hash = ?')->execute([authTokenHash($token)]);
    setcookie(faucetCookieName(), '', ['expires' => 1, 'path' => '/', 'secure' => authSecureRequest(), 'httponly' => true, 'samesite' => 'Strict']);
}

// The signed-in member plus session level and CSRF token, or null.
function faucetCurrent(): ?array {
    $token = $_COOKIE[faucetCookieName()] ?? '';
    if (!authTokenShape($token)) return null;
    $db = faucetDb();
    $now = time();
    $db->prepare('DELETE FROM sessions WHERE seen_at < ? OR created_at < ?')->execute([$now - FAUCET_SESSION_IDLE, $now - FAUCET_SESSION_MAX]);
    $stmt = $db->prepare("SELECT m.*, s.level, s.csrf, s.hash AS session_hash FROM sessions s JOIN members m ON m.id = s.member_id WHERE s.hash = ? AND m.status = 'active'");
    $stmt->execute([authTokenHash($token)]);
    $row = $stmt->fetch();
    if (!$row) return null;
    $db->prepare('UPDATE sessions SET seen_at = ? WHERE hash = ?')->execute([$now, $row['session_hash']]);
    return $row;
}

// Accept each TOTP step once per member, so a captured code cannot be replayed.
function faucetCheckTotp(array $member, string $code, ?string $secret = null): bool {
    $secret ??= (string)$member['totp_secret'];
    if (!authTotpSecretShape($secret)) return false;
    $step = authTotpStep($secret, trim($code));
    if ($step === null || $step <= (int)$member['totp_last_step']) return false;
    $update = faucetDb()->prepare('UPDATE members SET totp_last_step = ? WHERE id = ? AND totp_last_step < ?');
    $update->execute([$step, $member['id'], $step]);
    return $update->rowCount() === 1;
}

function faucetIssueToken(int $memberId, string $purpose): string {
    $token = authRandomToken();
    $db = faucetDb();
    $db->prepare('UPDATE tokens SET used_at = ? WHERE member_id = ? AND purpose = ? AND used_at IS NULL')->execute([time(), $memberId, $purpose]);
    $db->prepare('INSERT INTO tokens (hash, member_id, purpose, created_at, expires_at) VALUES (?, ?, ?, ?, ?)')
        ->execute([authTokenHash($token), $memberId, $purpose, time(), time() + ($purpose === 'verify' ? FAUCET_VERIFY_TTL : FAUCET_RESET_TTL)]);
    return $token;
}

// Marks the token used and returns its member, atomically; null if invalid or already used.
function faucetUseToken(mixed $token, string $purpose): ?array {
    if (!authTokenShape($token)) return null;
    $db = faucetDb();
    $db->beginTransaction();
    $stmt = $db->prepare("SELECT m.* FROM tokens t JOIN members m ON m.id = t.member_id WHERE t.hash = ? AND t.purpose = ? AND t.used_at IS NULL AND t.expires_at > ? AND m.status = 'active'");
    $stmt->execute([authTokenHash($token), $purpose, time()]);
    $member = $stmt->fetch();
    if ($member) $db->prepare('UPDATE tokens SET used_at = ? WHERE hash = ?')->execute([time(), authTokenHash($token)]);
    $db->commit();
    return $member ?: null;
}

function faucetPeekToken(mixed $token, string $purpose): ?array {
    if (!authTokenShape($token)) return null;
    $stmt = faucetDb()->prepare("SELECT m.* FROM tokens t JOIN members m ON m.id = t.member_id WHERE t.hash = ? AND t.purpose = ? AND t.used_at IS NULL AND t.expires_at > ? AND m.status = 'active'");
    $stmt->execute([authTokenHash($token), $purpose, time()]);
    return $stmt->fetch() ?: null;
}

function faucetMail(string $to, string $subject, string $body): bool { return authMail($to, $subject, $body, 'MONZERO_FAUCET_MAIL_LOG'); }

function faucetSendVerify(array $member): bool {
    $link = faucetConfig()['base_url'] . '/faucet/?verify=' . faucetIssueToken((int)$member['id'], 'verify');
    return faucetMail($member['email'], 'Monzero: confirm your account',
        "Confirm your email for your Monzero forum and faucet account (the link works once and expires in 24 hours):\n$link\n\n"
        . "If you did not sign up, ignore this email; the account stays unusable without confirmation.\n"
        . "Monzero will never ask for your password, seed phrase or keys by email.\n");
}

function faucetSendReset(array $member): bool {
    $link = faucetConfig()['base_url'] . '/faucet/?reset=' . faucetIssueToken((int)$member['id'], 'reset');
    return faucetMail($member['email'], 'Monzero faucet: reset your password',
        "Someone asked to reset the password for this Monzero faucet account.\n\nReset it here (works once, expires in 1 hour). You will also need your authenticator code:\n$link\n\n"
        . "If this was not you, ignore this email; your password stays the same.\n");
}

// Why a claim cannot be made right now, or null. Checked again inside the insert transaction.
function faucetClaimBlock(array $member, int $now): ?string {
    $config = faucetConfig();
    if (!faucetClaimsOpen()) return 'The faucet is paused. No claims are being accepted right now.';
    if ($member['level'] !== 'full' || !$member['totp_secret']) return 'Turn on two-factor authentication before claiming.';
    if (!$member['email_verified_at']) return 'Confirm your email address before claiming.';
    if (!$member['address']) return 'Add a payout address to your profile before claiming.';
    $db = faucetDb();
    $since = $now - $config['cooldown_seconds'];
    $mine = $db->prepare("SELECT MAX(created_at) FROM claims WHERE member_id = ? AND status != 'failed'");
    $mine->execute([$member['id']]);
    $last = $mine->fetchColumn();
    if ($last && (int)$last > $since) return 'You can claim again after ' . gmdate('Y-m-d H:i', (int)$last + $config['cooldown_seconds']) . ' UTC.';
    $address = $db->prepare("SELECT 1 FROM claims WHERE address_hash = ? AND created_at > ? AND status != 'failed'");
    $address->execute([$member['address_hash'], $since]);
    if ($address->fetchColumn()) return 'This payout address already received a claim in the last 24 hours.';
    $client = $db->prepare("SELECT COUNT(*) FROM claims WHERE client_hash = ? AND created_at > ? AND status != 'failed'");
    $client->execute([faucetClientHash(), $now - 86400]);
    if ((int)$client->fetchColumn() >= $config['per_client_daily']) return 'Too many claims from this network today. Try again tomorrow.';
    $total = $db->prepare("SELECT COUNT(*) FROM claims WHERE created_at > ? AND status != 'failed'");
    $total->execute([$now - 86400]);
    $paidToday = gmp_mul((int)$total->fetchColumn(), $config['amount_atomic']);
    if (gmp_cmp(gmp_add($paidToday, $config['amount_atomic']), $config['daily_cap_atomic']) > 0) return 'Today\'s faucet budget has been used up. Try again tomorrow.';
    return null;
}

function faucetFormatXmz(string $atomic): string {
    $padded = str_pad($atomic, 12, '0', STR_PAD_LEFT);
    return rtrim(rtrim(substr($padded, 0, -11) . '.' . substr($padded, -11), '0'), '.');
}
