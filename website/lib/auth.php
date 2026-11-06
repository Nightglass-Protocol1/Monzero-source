<?php
declare(strict_types=1);
// Shared account primitives for /admin/ and /faucet/: password hashing, one-time tokens,
// TOTP (RFC 6238), escaping and outgoing mail. No storage or sessions live here.
if (realpath($_SERVER['SCRIPT_FILENAME'] ?? '') === __FILE__) { http_response_code(404); exit; }

const AUTH_PASSWORD_MIN = 12;
const AUTH_PASSWORD_MAX = 256;

function authH(mixed $value): string { return htmlspecialchars((string)$value, ENT_QUOTES | ENT_SUBSTITUTE, 'UTF-8'); }

function authEmail(mixed $value): ?string {
    if (!is_string($value)) return null;
    $email = strtolower(trim($value));
    return strlen($email) <= 254 && filter_var($email, FILTER_VALIDATE_EMAIL) && !preg_match('/[\r\n]/', $email) ? $email : null;
}

function authHashPassword(string $password): string {
    return password_hash($password, defined('PASSWORD_ARGON2ID') ? PASSWORD_ARGON2ID : PASSWORD_BCRYPT);
}

function authNeedsRehash(string $hash): bool {
    return password_needs_rehash($hash, defined('PASSWORD_ARGON2ID') ? PASSWORD_ARGON2ID : PASSWORD_BCRYPT);
}

function authPasswordProblem(string $password, string $confirm, string $email): ?string {
    if ($password !== $confirm) return 'The passwords do not match.';
    $length = mb_strlen($password);
    if ($length < AUTH_PASSWORD_MIN) return 'Use at least ' . AUTH_PASSWORD_MIN . ' characters.';
    if ($length > AUTH_PASSWORD_MAX) return 'That password is too long.';
    if (stripos($password, explode('@', $email)[0]) !== false || preg_match('/\A(.)\1+\z/u', $password)) return 'Choose a password that does not contain your email name or repeat one character.';
    return null;
}

function authRandomToken(): string { return rtrim(strtr(base64_encode(random_bytes(32)), '+/', '-_'), '='); }
function authTokenHash(string $token): string { return hash('sha256', $token); }
function authTokenShape(mixed $token): bool { return is_string($token) && preg_match('/\A[A-Za-z0-9_-]{43}\z/', $token) === 1; }

function authSecureRequest(): bool {
    return (!empty($_SERVER['HTTPS']) && $_SERVER['HTTPS'] !== 'off') || ($_SERVER['SERVER_PORT'] ?? '') === '443';
}

// Same-origin check for POSTs; browsers send Origin on cross-site form posts.
function authSameOrigin(): bool {
    $origin = $_SERVER['HTTP_ORIGIN'] ?? null;
    if ($origin === null || $origin === 'null') return true;
    $host = parse_url($origin, PHP_URL_HOST) . (parse_url($origin, PHP_URL_PORT) ? ':' . parse_url($origin, PHP_URL_PORT) : '');
    return hash_equals((string)($_SERVER['HTTP_HOST'] ?? ''), $host);
}

// $extra: additional header lines (name => value), e.g. List-Unsubscribe. Values must not contain line breaks.
function authMail(string $to, string $subject, string $body, string $logVariable, array $extra = []): bool {
    $extra = array_filter($extra, fn($v, $k) => is_string($v) && !preg_match('/[\r\n]/', $v . $k) && preg_match('/\A[A-Za-z-]+\z/', (string)$k), ARRAY_FILTER_USE_BOTH);
    $log = getenv($logVariable);
    if (is_string($log) && $log !== '') {
        return file_put_contents($log, json_encode(['to' => $to, 'subject' => $subject, 'body' => $body] + ($extra ? ['headers' => $extra] : [])) . "\n", FILE_APPEND | LOCK_EX) !== false;
    }
    $headers = "From: Monzero website <noreply@monzero.org>\r\nReply-To: security@monzero.org\r\nContent-Type: text/plain; charset=utf-8\r\nAuto-Submitted: auto-generated";
    foreach ($extra as $name => $value) $headers .= "\r\n$name: $value";
    return mail($to, $subject, $body, $headers, '-fnoreply@monzero.org');
}

// --- TOTP (RFC 6238, SHA-1, 30 s, 6 digits) ---
function authBase32Encode(string $bytes): string {
    $alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
    $bits = '';
    foreach (str_split($bytes) as $c) $bits .= str_pad(decbin(ord($c)), 8, '0', STR_PAD_LEFT);
    $out = '';
    foreach (str_split($bits, 5) as $chunk) $out .= $alphabet[bindec(str_pad($chunk, 5, '0'))];
    return $out;
}

function authBase32Decode(string $text): string {
    $alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';
    $bits = '';
    foreach (str_split(strtoupper($text)) as $c) {
        $i = strpos($alphabet, $c);
        if ($i === false) return '';
        $bits .= str_pad(decbin($i), 5, '0', STR_PAD_LEFT);
    }
    $out = '';
    foreach (str_split($bits, 8) as $byte) if (strlen($byte) === 8) $out .= chr(bindec($byte));
    return $out;
}

function authNewTotpSecret(): string { return authBase32Encode(random_bytes(20)); }
function authTotpSecretShape(mixed $secret): bool { return is_string($secret) && preg_match('/\A[A-Z2-7]{32}\z/', $secret) === 1; }

function authTotp(string $secret, int $counter): string {
    $hash = hash_hmac('sha1', pack('J', $counter), authBase32Decode($secret), true);
    $offset = ord($hash[19]) & 0x0f;
    $value = ((ord($hash[$offset]) & 0x7f) << 24) | (ord($hash[$offset + 1]) << 16) | (ord($hash[$offset + 2]) << 8) | ord($hash[$offset + 3]);
    return str_pad((string)($value % 1000000), 6, '0', STR_PAD_LEFT);
}

// Returns the matched time step (to block reuse of the same code) or null.
function authTotpStep(string $secret, string $code): ?int {
    if (!preg_match('/\A\d{6}\z/', $code)) return null;
    $counter = intdiv(time(), 30);
    foreach ([-1, 0, 1] as $drift) if (hash_equals(authTotp($secret, $counter + $drift), $code)) return $counter + $drift;
    return null;
}

function authTotpValid(string $secret, string $code): bool { return authTotpStep($secret, $code) !== null; }

function authOtpauthUri(string $label, string $secret): string {
    return 'otpauth://totp/' . rawurlencode($label) . '?secret=' . $secret . '&issuer=Monzero';
}
