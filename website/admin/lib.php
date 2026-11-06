<?php
declare(strict_types=1);
// Invite-only administration: accounts, delegated permissions, sessions, audit log.
// The database lives outside the document root. No secrets are hardcoded here.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }
require_once dirname(__DIR__) . '/lib/auth.php';

const ADMIN_PERMISSIONS = [
    'admins.invite' => 'Invite admins (invites need owner approval)',
    'audit.view' => 'View the audit log',
    'announcements' => 'Manage announcements',
    'roadmap' => 'Manage roadmap milestones',
    'benchmarks' => 'Moderate benchmark submissions',
    'bounties' => 'Manage bounty listings',
    'proposals' => 'Moderate community proposals',
    'board' => 'Moderate the community discussion board',
    'exchanges' => 'Manage exchange listing records',
    'fundraising' => 'Update fundraising campaigns',
    'founder' => 'Publish founder disclosures',
    'ecosystem' => 'Review ecosystem directory submissions',
    'features' => 'Configure feature availability (pause or resume the faucet)',
    'faucet' => 'View faucet claims and payout worker health',
];
const ADMIN_TOKEN_TTL = 172800;        // setup and invite links: 48 hours
const ADMIN_SESSION_IDLE = 1800;       // 30 minutes
const ADMIN_SESSION_MAX = 28800;       // 8 hours
const ADMIN_PASSWORD_MIN = AUTH_PASSWORD_MIN;

function adminDbPath(): string {
    $configured = getenv('MONZERO_ADMIN_DB');
    return is_string($configured) && $configured !== '' ? $configured : dirname(__DIR__, 2) . '/monzero-private/admin.sqlite';
}

function adminBaseUrl(): string {
    // Never derived from the Host header, so links in emails cannot be redirected.
    $configured = getenv('MONZERO_ADMIN_BASE_URL');
    return is_string($configured) && preg_match('#\Ahttps?://[a-z0-9.:-]+\z#i', $configured) ? $configured : 'https://monzero.org';
}

function adminDb(): PDO {
    static $db = null;
    if ($db) return $db;
    $path = adminDbPath();
    $directory = dirname($path);
    if (!is_dir($directory)) @mkdir($directory, 0700, true);
    if (!is_dir($directory) || is_link($path)) throw new RuntimeException('Admin storage unavailable');
    $new = !file_exists($path);
    $db = new PDO('sqlite:' . $path, null, null, [PDO::ATTR_ERRMODE => PDO::ERRMODE_EXCEPTION, PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC]);
    if ($new) @chmod($path, 0600);
    $db->exec('PRAGMA foreign_keys = ON; PRAGMA busy_timeout = 5000; PRAGMA journal_mode = WAL;');
    adminMigrate($db);
    return $db;
}

function adminMigrate(PDO $db): void {
    $version = (int)$db->query('PRAGMA user_version')->fetchColumn();
    if ($version < 1) {
        $db->beginTransaction();
        $db->exec(<<<'SQL'
            CREATE TABLE users (
                id INTEGER PRIMARY KEY,
                email TEXT NOT NULL UNIQUE,
                password_hash TEXT,
                is_owner INTEGER NOT NULL DEFAULT 0,
                permissions TEXT NOT NULL DEFAULT '[]',
                status TEXT NOT NULL CHECK (status IN ('pending', 'active', 'disabled')),
                totp_secret TEXT,
                invited_by INTEGER REFERENCES users(id),
                created_at INTEGER NOT NULL,
                last_login_at INTEGER
            );
            CREATE UNIQUE INDEX one_owner ON users(is_owner) WHERE is_owner = 1;
            CREATE TABLE tokens (
                hash TEXT PRIMARY KEY,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                purpose TEXT NOT NULL CHECK (purpose IN ('setup', 'invite')),
                state TEXT NOT NULL CHECK (state IN ('awaiting_approval', 'sent', 'used', 'revoked')),
                created_by INTEGER REFERENCES users(id),
                created_at INTEGER NOT NULL,
                expires_at INTEGER
            );
            CREATE TABLE sessions (
                hash TEXT PRIMARY KEY,
                user_id INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
                csrf TEXT NOT NULL,
                created_at INTEGER NOT NULL,
                seen_at INTEGER NOT NULL
            );
            CREATE TABLE login_failures (key TEXT NOT NULL, at INTEGER NOT NULL);
            CREATE INDEX login_failures_key ON login_failures(key, at);
            CREATE TABLE audit (
                id INTEGER PRIMARY KEY,
                at INTEGER NOT NULL,
                actor INTEGER,
                action TEXT NOT NULL,
                detail TEXT NOT NULL
            );
            CREATE TRIGGER audit_no_update BEFORE UPDATE ON audit BEGIN SELECT RAISE(ABORT, 'audit is append-only'); END;
            CREATE TRIGGER audit_no_delete BEFORE DELETE ON audit BEGIN SELECT RAISE(ABORT, 'audit is append-only'); END;
            PRAGMA user_version = 1;
        SQL);
        $db->commit();
    }
}

function adminAudit(?int $actor, string $action, array $detail = []): void {
    adminDb()->prepare('INSERT INTO audit (at, actor, action, detail) VALUES (?, ?, ?, ?)')
        ->execute([time(), $actor, $action, json_encode($detail, JSON_UNESCAPED_SLASHES | JSON_UNESCAPED_UNICODE)]);
}

function adminEmail(mixed $value): ?string { return authEmail($value); }

function adminPermissions(array $user): array {
    if ((int)$user['is_owner'] === 1) return array_keys(ADMIN_PERMISSIONS);
    $list = json_decode((string)$user['permissions'], true);
    return is_array($list) ? array_values(array_intersect(array_keys(ADMIN_PERMISSIONS), $list)) : [];
}

function adminCan(array $user, string $permission): bool {
    return in_array($permission, adminPermissions($user), true);
}

// Only permissions the granting admin holds can be passed on.
function adminGrantable(array $grantor, array $requested): array {
    $requested = array_values(array_unique(array_filter($requested, 'is_string')));
    $unknown = array_diff($requested, array_keys(ADMIN_PERMISSIONS));
    $excess = array_diff($requested, adminPermissions($grantor));
    if ($unknown || $excess) throw new InvalidArgumentException('You can only grant permissions you hold.');
    return array_values(array_intersect(array_keys(ADMIN_PERMISSIONS), $requested));
}

function adminHashPassword(string $password): string { return authHashPassword($password); }

function adminPasswordProblem(string $password, string $confirm, string $email): ?string { return authPasswordProblem($password, $confirm, $email); }

function adminRandomToken(): string { return authRandomToken(); }
function adminTokenHash(string $token): string { return authTokenHash($token); }

// Create a setup or invite link token; returns the plain token (never stored).
function adminIssueToken(int $userId, string $purpose, string $state, ?int $createdBy): string {
    $token = adminRandomToken();
    $db = adminDb();
    $db->prepare("UPDATE tokens SET state = 'revoked' WHERE user_id = ? AND state IN ('awaiting_approval', 'sent')")->execute([$userId]);
    $db->prepare('INSERT INTO tokens (hash, user_id, purpose, state, created_by, created_at, expires_at) VALUES (?, ?, ?, ?, ?, ?, ?)')
        ->execute([adminTokenHash($token), $userId, $purpose, $state, $createdBy, time(), $state === 'sent' ? time() + ADMIN_TOKEN_TTL : null]);
    return $token;
}

function adminTokenUser(string $token): ?array {
    if (!preg_match('/\A[A-Za-z0-9_-]{43}\z/', $token)) return null;
    $stmt = adminDb()->prepare("SELECT u.*, t.hash AS token_hash, t.purpose FROM tokens t JOIN users u ON u.id = t.user_id
        WHERE t.hash = ? AND t.state = 'sent' AND t.expires_at > ? AND u.status = 'pending'");
    $stmt->execute([adminTokenHash($token), time()]);
    return $stmt->fetch() ?: null;
}

function adminMail(string $to, string $subject, string $body): bool { return authMail($to, $subject, $body, 'MONZERO_ADMIN_MAIL_LOG'); }

function adminSendLink(array $user, string $token, string $purpose): bool {
    $link = adminBaseUrl() . '/admin/?setup=' . $token;
    $intro = $purpose === 'setup'
        ? "An owner account for the Monzero website administration has been created for this address."
        : "You have been invited to help administer the Monzero website.";
    $body = "$intro\n\nSet your password here (the link works once and expires in 48 hours):\n$link\n\n"
        . "If you did not expect this, ignore this email; nothing happens unless the link is used.\n"
        . "Monzero will never ask for your password, seed phrase or keys by email.\n";
    return adminMail($user['email'], 'Monzero admin: set your password', $body);
}

function adminClientKey(): string {
    return 'client:' . hash_hmac('sha256', (string)($_SERVER['REMOTE_ADDR'] ?? 'unknown'), adminSecret());
}

// A per-installation secret kept with the database, used for HMACs only.
function adminSecret(): string {
    static $secret = null;
    if ($secret !== null) return $secret;
    $path = dirname(adminDbPath()) . '/admin-secret';
    if (!file_exists($path)) {
        $fresh = bin2hex(random_bytes(32));
        $handle = @fopen($path, 'x');
        if ($handle) { fwrite($handle, $fresh); fclose($handle); @chmod($path, 0600); }
    }
    $secret = trim((string)@file_get_contents($path));
    if (!preg_match('/\A[0-9a-f]{64}\z/', $secret)) throw new RuntimeException('Admin secret unavailable');
    return $secret;
}

function adminThrottled(string $email): bool {
    $db = adminDb();
    $since = time() - 900;
    $db->prepare('DELETE FROM login_failures WHERE at < ?')->execute([time() - 86400]);
    $count = $db->prepare('SELECT COUNT(*) FROM login_failures WHERE key = ? AND at >= ?');
    $count->execute(['email:' . hash('sha256', $email), $since]);
    if ((int)$count->fetchColumn() >= 5) return true;
    $count->execute([adminClientKey(), $since]);
    return (int)$count->fetchColumn() >= 20;
}

function adminRecordFailure(string $email): void {
    $stmt = adminDb()->prepare('INSERT INTO login_failures (key, at) VALUES (?, ?)');
    $stmt->execute(['email:' . hash('sha256', $email), time()]);
    $stmt->execute([adminClientKey(), time()]);
}

function adminSecureRequest(): bool { return authSecureRequest(); }

function adminCookieName(): string { return adminSecureRequest() ? '__Host-mzadmin' : 'mzadmin'; }

function adminStartSession(int $userId): void {
    $token = adminRandomToken();
    $now = time();
    adminDb()->prepare('INSERT INTO sessions (hash, user_id, csrf, created_at, seen_at) VALUES (?, ?, ?, ?, ?)')
        ->execute([adminTokenHash($token), $userId, adminRandomToken(), $now, $now]);
    setcookie(adminCookieName(), $token, ['path' => '/', 'secure' => adminSecureRequest(), 'httponly' => true, 'samesite' => 'Strict']);
}

function adminEndSession(): void {
    $token = $_COOKIE[adminCookieName()] ?? '';
    if (is_string($token) && $token !== '') adminDb()->prepare('DELETE FROM sessions WHERE hash = ?')->execute([adminTokenHash($token)]);
    setcookie(adminCookieName(), '', ['expires' => 1, 'path' => '/', 'secure' => adminSecureRequest(), 'httponly' => true, 'samesite' => 'Strict']);
}

// Returns [user, csrf] for a valid session, refreshing its idle timer.
function adminCurrent(): ?array {
    $token = $_COOKIE[adminCookieName()] ?? '';
    if (!is_string($token) || !preg_match('/\A[A-Za-z0-9_-]{43}\z/', $token)) return null;
    $db = adminDb();
    $now = time();
    $db->prepare('DELETE FROM sessions WHERE seen_at < ? OR created_at < ?')->execute([$now - ADMIN_SESSION_IDLE, $now - ADMIN_SESSION_MAX]);
    $stmt = $db->prepare("SELECT u.*, s.csrf FROM sessions s JOIN users u ON u.id = s.user_id WHERE s.hash = ? AND u.status = 'active'");
    $stmt->execute([adminTokenHash($token)]);
    $row = $stmt->fetch();
    if (!$row) return null;
    $db->prepare('UPDATE sessions SET seen_at = ? WHERE hash = ?')->execute([$now, adminTokenHash($token)]);
    return $row;
}

// Same-origin POST with the session's CSRF token.
function adminCheckPost(?array $user): void {
    $origin = $_SERVER['HTTP_ORIGIN'] ?? null;
    if ($origin !== null && $origin !== 'null') {
        $host = parse_url($origin, PHP_URL_HOST) . (parse_url($origin, PHP_URL_PORT) ? ':' . parse_url($origin, PHP_URL_PORT) : '');
        if (!hash_equals((string)($_SERVER['HTTP_HOST'] ?? ''), $host)) adminDeny('Request origin not allowed.');
    }
    if ($user !== null && !hash_equals($user['csrf'], (string)($_POST['csrf'] ?? ''))) adminDeny('Your form expired. Reload the page and try again.');
}

function adminDeny(string $message, int $status = 403): never {
    http_response_code($status);
    adminPage('Not allowed', '<p class="notice error">' . adminH($message) . '</p><p><a href="/admin/">Back to administration</a></p>', null);
    exit;
}

function adminH(mixed $value): string { return authH($value); }

// --- TOTP (RFC 6238, SHA-1, 30 s, 6 digits) ---
function adminBase32Encode(string $bytes): string { return authBase32Encode($bytes); }

function adminBase32Decode(string $text): string { return authBase32Decode($text); }

function adminTotp(string $secret, int $counter): string { return authTotp($secret, $counter); }

function adminTotpValid(string $secret, string $code): bool { return authTotpValid($secret, $code); }

// --- Page shell ---
function adminPage(string $title, string $body, ?array $user): void {
    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: no-store');
    header('Referrer-Policy: no-referrer');
    header('X-Robots-Tag: noindex, nofollow');
    header('X-Frame-Options: DENY');
    $nav = '';
    if ($user) {
        $links = [['/admin/', 'Overview'], ['/admin/?view=account', 'Your account']];
        if ((int)$user['is_owner'] === 1 || adminCan($user, 'admins.invite')) $links[] = ['/admin/?view=admins', 'Admins'];
        if (adminCan($user, 'audit.view')) $links[] = ['/admin/?view=audit', 'Audit log'];
        foreach ($links as [$href, $label]) $nav .= '<a href="' . $href . '">' . adminH($label) . '</a>';
        $nav .= '<form method="post" action="/admin/?do=logout" class="inline"><input type="hidden" name="csrf" value="' . adminH($user['csrf']) . '"><button type="submit">Sign out</button></form>';
    }
    echo '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="noindex,nofollow"><title>'
        . adminH($title) . ' · Monzero admin</title><link rel="stylesheet" href="/styles.css?v=20261026-brand"><link rel="stylesheet" href="/theme.css?v=20261026-brand"><link rel="stylesheet" href="/admin/admin.css?v=20261020-sitewide"><link rel="stylesheet" href="/site-nav.css?v=20261020-sitewide"><link rel="stylesheet" href="/site-shell.css?v=20261026-brand"><noscript><link rel="stylesheet" href="/no-script.css?v=20261020-sitewide"></noscript></head><body class="site-shell" data-page="admin"><a class="skip-link" href="#main">Skip to content</a>'
        . '<header class="site-header"><a class="brand" href="/" aria-label="Monzero home"><img class="brand-mark brand-coin" src="/assets/monzero-emblem-20261026-96.webp" alt="" width="32" height="32"><span>MONZERO ADMIN</span></a>'
        . ($nav ? '<button class="menu-toggle" type="button" aria-expanded="false" aria-controls="site-nav">Menu</button><nav id="site-nav" aria-label="Administration">' . $nav . '</nav>' : '<a class="header-cta" href="/">Back to site</a>')
        . '</header><main tabindex="-1" id="main"><h1>' . adminH($title) . '</h1>' . $body . '</main><script src="/site-nav.js?v=20261020-sitewide" defer></script></body></html>';
}
