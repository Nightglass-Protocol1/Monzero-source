<?php
declare(strict_types=1);
require __DIR__ . '/lib.php';
require dirname(__DIR__) . '/lib/site-header.php';
require_once dirname(__DIR__) . '/lib/board.php';

if (($_SERVER['REQUEST_METHOD'] ?? 'GET') === 'GET' && ($_GET['view'] ?? '') === 'signup' && !defined('MONZERO_FORUM_SIGNUP')) {
    header('Location: /community/board/?view=signup', true, 302); exit;
}

const FAUCET_MESSAGES = [
    'signed-up' => 'Check your email for a confirmation link. You can sign in once your address is confirmed.',
    'verified' => 'Email confirmed. Sign in to set up two-factor authentication.',
    'resent' => 'If that account exists and is unconfirmed, a new confirmation link is on its way.',
    'reset-sent' => 'If that account exists, a password reset link is on its way.',
    'reset-done' => 'Password changed. Sign in with your new password.',
    'totp-on' => 'Two-factor authentication is on. You can now add a payout address and claim, and join the community forum below.',
    'address' => 'Payout address saved.',
    'claimed' => 'Claim received. It is paid by the faucet wallet, usually within a few minutes; the transaction ID appears below once sent.',
];

function page(string $title, string $body, ?array $member = null, bool $turnstile = false, bool $qr = false): void {
    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: no-store');
    header('Referrer-Policy: no-referrer');
    header('X-Frame-Options: DENY');
    $account = $member ? '<form method="post" action="/faucet/?do=logout" class="signout"><input type="hidden" name="csrf" value="' . authH($member['csrf']) . '"><span>' . authH($member['email']) . '</span><button type="submit">Sign out</button></form>' : '';
    echo '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>' . authH($title) . ' · Monzero faucet</title>'
        . '<meta name="description" content="Claim 0.1 XMZ once every 24 hours with a verified, two-factor protected account.">'
        . '<link rel="stylesheet" href="/styles.css?v=20261026-brand"><link rel="stylesheet" href="/theme.css?v=20261026-brand"><link rel="stylesheet" href="/community/community.css?v=20261026-brand"><link rel="stylesheet" href="/faucet/faucet.css?v=20261020-sitewide"><link rel="stylesheet" href="/site-nav.css?v=20261020-sitewide">'
        . ($turnstile ? '<script src="https://challenges.cloudflare.com/turnstile/v0/api.js" async defer></script>' : '')
        . '<link rel="stylesheet" href="/site-shell.css?v=20261026-brand"><noscript><link rel="stylesheet" href="/no-script.css?v=20261020-sitewide"></noscript></head><body class="site-shell"><a class="skip-link" href="#main">Skip to content</a>' . siteHeader('/faucet/')
        . '<main id="main" tabindex="-1"><p class="eyebrow">XMZ FAUCET</p><h1>' . authH($title) . '</h1>' . $account . $body . '</main><footer><p><a href="/community/">Monzero community</a> · <a href="/privacy/">Privacy Policy</a> · <a href="/terms/">Website Terms</a></p></footer>'
        . '<script src="/site-nav.js?v=20261020-sitewide" defer></script>' . ($qr ? '<script type="module" src="/faucet/faucet.mjs?v=20261021"></script>' : '') . '</body></html>';
}

function notice(string $text, bool $error = false): string { return '<p class="notice' . ($error ? ' error" role="alert"' : '" role="status"') . '>' . authH($text) . '</p>'; }
function field(string $name): string { $v = $_POST[$name] ?? ''; return is_string($v) ? $v : ''; }
function turnstileWidget(string $action): string { return '<div class="cf-turnstile" data-sitekey="' . authH(faucetConfig()['turnstile_site_key']) . '" data-action="' . authH($action) . '" data-theme="dark"></div>'; }
function csrf(array $member): string { return '<input type="hidden" name="csrf" value="' . authH($member['csrf']) . '">'; }
function go(string $query = ''): never { header('Location: /faucet/' . ($query ? '?' . $query : ''), true, 303); exit; }

function rules(): string {
    $c = faucetConfig();
    return '<section><h2>How it works</h2><ul><li>Each account can claim <strong>' . authH(faucetFormatXmz($c['amount_atomic'])) . ' XMZ once every 24 hours</strong>, paid to the payout address in your profile.</li>'
        . '<li>Accounts need a confirmed email, a password and two-factor authentication (an authenticator app). Each sign-in and claim also passes a Cloudflare check against bots.</li>'
        . '<li>A payout address can belong to one account and receive one claim per 24 hours. There is also a limit per network and a total daily budget.</li>'
        . '<li>Payments come from a separate faucet wallet. This website never holds keys and will never ask for your seed phrase.</li></ul></section>';
}

function loginForm(string $error = '', int $status = 200): void {
    http_response_code($status);
    $open = faucetAccountsOpen();
    $state = faucetClaimsOpen() ? notice('The faucet is open.') : notice('The faucet is paused: you can create an account and get ready, but claims are not being paid yet.');
    page('Claim free XMZ', ($error ? notice($error, true) : '') . (isset($_GET['msg'], FAUCET_MESSAGES[$_GET['msg']]) ? notice(FAUCET_MESSAGES[$_GET['msg']]) : '') . $state
        . ($open ? '<section><h2>Sign in</h2><form method="post" action="/faucet/?do=login' . (($_GET['return'] ?? '') === 'board' ? '&amp;return=board' : '') . '" class="stack">'
            . '<label>Email<input type="email" name="email" autocomplete="username" required></label>'
            . '<label>Password<input type="password" name="password" autocomplete="current-password" required></label>'
            . '<label>Authenticator code<input name="code" inputmode="numeric" autocomplete="one-time-code" pattern="[0-9]{6}" maxlength="6" placeholder="Leave empty if not set up yet"></label>'
            . turnstileWidget('login') . '<button type="submit">Sign in</button></form>'
            . '<p><a href="/community/board/?view=signup">Create an account</a> · <a href="/faucet/?view=forgot">Forgot password</a> · <a href="/faucet/?view=resend">Resend confirmation email</a></p></section>'
            : notice('Accounts are not open yet. Check back soon.')) . rules(), null, $open);
}

try { faucetDb(); } catch (Throwable $e) {
    error_log('Monzero faucet: storage unavailable');
    http_response_code(503); page('Unavailable', notice('The faucet is temporarily unavailable.', true)); exit;
}

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if (!in_array($method, ['GET', 'POST'], true)) { header('Allow: GET, POST'); http_response_code(405); exit; }
$do = is_string($_GET['do'] ?? null) ? $_GET['do'] : '';
$member = faucetCurrent();
$db = faucetDb();

if ($method === 'POST') {
    if (!authSameOrigin()) { http_response_code(403); page('Not allowed', notice('Request origin not allowed.', true)); exit; }
    $public = ['signup', 'login', 'resend', 'forgot', 'reset'];
    if (in_array($do, $public, true)) {
        if (!faucetAccountsOpen()) { loginForm('Accounts are not open yet.', 503); exit; }
        if ($do !== 'reset' && !faucetTurnstileOk($_POST['cf-turnstile-response'] ?? null, $do)) {
            if ($do === 'signup') { http_response_code(400); signupForm('The bot check did not pass. Please try again.'); exit; }
            loginForm('The bot check did not pass. Please try again.', 400); exit;
        }
    }
    switch ($do) {
        case 'signup':
            try {
                $registrationOptions = registrationOptions($_POST);
                $registrationAvatar = registrationAvatar(isset($_FILES['avatar']) && is_array($_FILES['avatar']) ? $_FILES['avatar'] : null);
            } catch (InvalidArgumentException $e) { http_response_code(400); signupForm($e->getMessage()); exit; }
            catch (Throwable) { http_response_code(503); signupForm('Registration is temporarily unavailable. Please try again later.'); exit; }
            $email = authEmail(field('email'));
            if (!$email) { http_response_code(400); signupForm('Enter a valid email address.'); exit; }
            $problem = authPasswordProblem(field('password'), field('confirm'), $email);
            if ($problem) { http_response_code(400); signupForm($problem); exit; }
            $username = trim(field('username'));
            try { $problem = boardUsernameProblem($username) ?? (boardUsernameTaken($username, 0, time()) ? 'That forum username is taken. Choose another.' : null); }
            catch (Throwable) { $problem = 'Sign-up is unavailable right now. Try again later.'; }
            if ($problem) { http_response_code(400); signupForm($problem); exit; }
            $clientKey = 'signup:' . faucetClientHash();
            if (faucetLimitCount($clientKey, 86400) >= faucetConfig()['signups_per_client_daily']) { http_response_code(429); signupForm('Too many sign-ups from this network today. Try again tomorrow.'); exit; }
            faucetLimitHit($clientKey);
            $exists = $db->prepare('SELECT 1 FROM members WHERE email = ?');
            $exists->execute([$email]);
            // Same response whether or not the email is already registered.
            if (!$exists->fetchColumn()) {
                $board = registrationDb();
                $db->beginTransaction(); $board->beginTransaction();
                try {
                    $db->prepare('INSERT INTO members (email, password_hash, created_at) VALUES (?, ?, ?)')->execute([$email, authHashPassword(field('password')), time()]);
                    $id = (int)$db->lastInsertId();
                    if ($problem = boardHoldUsername($id, $username, time())) throw new RuntimeException($problem);
                    registrationSave($id, $registrationOptions, $registrationAvatar);
                    $board->commit(); $db->commit();
                } catch (Throwable) {
                    if ($board->inTransaction()) $board->rollBack();
                    if ($db->inTransaction()) $db->rollBack();
                    http_response_code(503); signupForm('Registration could not be completed. Please try again.'); exit;
                }
                faucetEvent($id, 'member.signed_up');
                faucetSendVerify(['id' => $id, 'email' => $email]);
            }
            header('Location: /community/board/?view=signup&msg=signed-up', true, 303); exit;
        case 'resend':
            $email = authEmail(field('email')) ?? '';
            $key = 'resend:' . hash('sha256', $email);
            if ($email !== '' && faucetLimitCount($key, 86400) < 3) {
                faucetLimitHit($key);
                $stmt = $db->prepare("SELECT * FROM members WHERE email = ? AND email_verified_at IS NULL AND status = 'active'");
                $stmt->execute([$email]);
                if ($found = $stmt->fetch()) faucetSendVerify($found);
            }
            go('msg=resent');
        case 'forgot':
            $email = authEmail(field('email')) ?? '';
            $key = 'forgot:' . hash('sha256', $email);
            if ($email !== '' && faucetLimitCount($key, 86400) < 3) {
                faucetLimitHit($key);
                $stmt = $db->prepare("SELECT * FROM members WHERE email = ? AND status = 'active'");
                $stmt->execute([$email]);
                if ($found = $stmt->fetch()) { faucetSendReset($found); faucetEvent((int)$found['id'], 'member.reset_requested'); }
            }
            go('msg=reset-sent');
        case 'reset':
            $token = field('token');
            $target = faucetPeekToken($token, 'reset');
            if (!$target) { http_response_code(400); page('Link not valid', notice('This link has expired or was already used. Ask for a new one.', true)); exit; }
            $problem = authPasswordProblem(field('password'), field('confirm'), $target['email']);
            if (!$problem && $target['totp_secret'] && !faucetCheckTotp($target, field('code'))) $problem = 'The authenticator code is not correct.';
            if ($problem) { http_response_code(400); resetForm($token, $target, $problem); exit; }
            if (!faucetUseToken($token, 'reset')) { http_response_code(400); page('Link not valid', notice('This link was already used.', true)); exit; }
            $db->prepare('UPDATE members SET password_hash = ? WHERE id = ?')->execute([authHashPassword(field('password')), $target['id']]);
            $db->prepare('DELETE FROM sessions WHERE member_id = ?')->execute([$target['id']]);
            faucetEvent((int)$target['id'], 'member.password_reset');
            go('msg=reset-done');
        case 'login':
            $email = authEmail(field('email'));
            $emailKey = 'login:' . hash('sha256', (string)$email);
            $clientKey = 'login-client:' . faucetClientHash();
            if (!$email) { loginForm('Email, password or code is incorrect.', 401); exit; }
            if (faucetLimitCount($emailKey, 900) >= 5 || faucetLimitCount($clientKey, 900) >= 20) { faucetEvent(null, 'login.throttled'); loginForm('Too many attempts. Wait 15 minutes and try again.', 429); exit; }
            $stmt = $db->prepare("SELECT * FROM members WHERE email = ? AND status = 'active'");
            $stmt->execute([$email]);
            $found = $stmt->fetch();
            $ok = password_verify(field('password'), $found['password_hash'] ?? FAUCET_DUMMY_HASH) && $found;
            if ($ok && $found['totp_secret'] && !faucetCheckTotp($found, field('code'))) $ok = false;
            if (!$ok) { faucetLimitHit($emailKey); faucetLimitHit($clientKey); faucetEvent($found['id'] ?? null, 'login.failed'); loginForm('Email, password or code is incorrect.', 401); exit; }
            if (!$found['email_verified_at']) { loginForm('Confirm your email first. Use the link we sent, or resend it below.', 403); exit; }
            if (authNeedsRehash($found['password_hash'])) $db->prepare('UPDATE members SET password_hash = ? WHERE id = ?')->execute([authHashPassword(field('password')), $found['id']]);
            $db->prepare('UPDATE members SET last_login_at = ? WHERE id = ?')->execute([time(), $found['id']]);
            faucetEvent((int)$found['id'], 'login.succeeded');
            faucetStartSession((int)$found['id'], $found['totp_secret'] ? 'full' : 'setup');
            if ($found['totp_secret'] && ($_GET['return'] ?? '') === 'board') { header('Location: /community/board/', true, 303); exit; }
            go();
    }
    // Everything below needs a session and its CSRF token.
    if (!$member) go();
    if (!hash_equals($member['csrf'], field('csrf'))) { http_response_code(403); page('Form expired', notice('Your form expired. Reload the page and try again.', true), $member); exit; }
    $id = (int)$member['id'];
    switch ($do) {
        case 'logout':
            faucetEndSession(); go();
        case 'totp':
            if ($member['totp_secret']) go();
            $secret = field('secret');
            if (!authTotpSecretShape($secret) || !faucetCheckTotp($member, field('code'), $secret)) { http_response_code(400); totpForm($member, 'That code did not match. Check the time on your phone and try again.', $secret); exit; }
            $db->prepare('UPDATE members SET totp_secret = ? WHERE id = ?')->execute([$secret, $id]);
            $db->prepare("UPDATE sessions SET level = 'full' WHERE member_id = ?")->execute([$id]);
            faucetEvent($id, 'totp.enabled');
            go('msg=totp-on');
        case 'address':
            if ($member['level'] !== 'full') go();
            $parsed = xmzParseAddress(field('address'));
            if (isset($parsed['error'])) { http_response_code(400); dashboard($member, $parsed['error']); exit; }
            if ($parsed['network'] !== 'mainnet') { http_response_code(400); dashboard($member, 'Use a mainnet Monzero address.'); exit; }
            if (!faucetCheckTotp($member, field('code'))) { http_response_code(400); dashboard($member, 'The authenticator code is not correct.'); exit; }
            $hash = faucetAddressHash($parsed['address']);
            $taken = $db->prepare('SELECT 1 FROM members WHERE address_hash = ? AND id != ?');
            $taken->execute([$hash, $id]);
            if ($taken->fetchColumn()) { http_response_code(409); dashboard($member, 'That payout address is already used by another account.'); exit; }
            $db->prepare('UPDATE members SET address = ?, address_hash = ? WHERE id = ?')->execute([$parsed['address'], $hash, $id]);
            faucetEvent($id, 'address.changed', ['kind' => $parsed['kind']]);
            go('msg=address');
        case 'claim':
            if (!faucetTurnstileOk($_POST['cf-turnstile-response'] ?? null, 'claim')) { http_response_code(400); dashboard($member, 'The bot check did not pass. Please try again.'); exit; }
            // Serialise claims so concurrent requests cannot both pass the limits.
            $db->exec('BEGIN IMMEDIATE');
            $now = time();
            $block = faucetClaimBlock($member, $now);
            if ($block) { $db->exec('ROLLBACK'); http_response_code(429); dashboard($member, $block); exit; }
            $db->prepare("INSERT INTO claims (member_id, address, address_hash, client_hash, amount_atomic, status, created_at) VALUES (?, ?, ?, ?, ?, 'pending', ?)")
                ->execute([$id, $member['address'], $member['address_hash'], faucetClientHash(), faucetConfig()['amount_atomic'], $now]);
            $db->exec('COMMIT');
            faucetEvent($id, 'claim.created', ['amount_atomic' => faucetConfig()['amount_atomic']]);
            go('msg=claimed');
        default:
            http_response_code(400); page('Unknown action', notice('Unknown action.', true), $member); exit;
    }
}

function signupForm(string $error = ''): void {
    header('Content-Type: text/html; charset=utf-8');
    header('Cache-Control: no-store');
    header('Referrer-Policy: no-referrer');
    header('X-Frame-Options: DENY');
    require dirname(__DIR__) . '/community/board/registration-template.php';
}

function emailForm(string $title, string $action, string $intro): void {
    page($title, '<p>' . authH($intro) . '</p><form method="post" action="/faucet/?do=' . $action . '" class="stack"><label>Email<input type="email" name="email" autocomplete="email" required></label>'
        . turnstileWidget($action) . '<button type="submit">Send</button></form><p><a href="/faucet/">Back to sign in</a></p>', null, true);
}

function resetForm(string $token, array $target, string $error = ''): void {
    page('Choose a new password', ($error ? notice($error, true) : '') . '<form method="post" action="/faucet/?do=reset" class="stack"><input type="hidden" name="token" value="' . authH($token) . '">'
        . '<label>New password<input type="password" name="password" autocomplete="new-password" minlength="' . AUTH_PASSWORD_MIN . '" required></label>'
        . '<label>Repeat password<input type="password" name="confirm" autocomplete="new-password" required></label>'
        . ($target['totp_secret'] ? '<label>Authenticator code<input name="code" inputmode="numeric" autocomplete="one-time-code" pattern="[0-9]{6}" maxlength="6" required></label>' : '')
        . '<button type="submit">Change password</button></form>');
}

function totpForm(array $member, string $error = '', ?string $secret = null): void {
    $secret = authTotpSecretShape($secret) ? $secret : authNewTotpSecret();
    page('Set up two-factor authentication', ($error ? notice($error, true) : '')
        . '<p>Scan this code with an authenticator app, or type the key in by hand, then enter the 6-digit code it shows. You will need the app every time you sign in, change your payout address or reset your password.</p>'
        . '<div id="totp-qr" class="qr" data-uri="' . authH(authOtpauthUri('Monzero faucet:' . $member['email'], $secret)) . '"></div>'
        . '<p>Key: <code>' . authH(implode(' ', str_split($secret, 4))) . '</code></p>'
        . '<form method="post" action="/faucet/?do=totp" class="stack">' . csrf($member) . '<input type="hidden" name="secret" value="' . authH($secret) . '">'
        . '<label>Code from the app<input name="code" inputmode="numeric" autocomplete="one-time-code" pattern="[0-9]{6}" maxlength="6" required></label><button type="submit">Turn on</button></form>'
        . '<p class="note">Keep a backup of this key somewhere safe. If you lose the app, the account cannot be recovered by email alone.</p>', $member, false, true);
}

function dashboard(array $member, string $error = ''): void {
    $db = faucetDb();
    $flash = isset($_GET['msg'], FAUCET_MESSAGES[$_GET['msg']]) ? notice(FAUCET_MESSAGES[$_GET['msg']]) : '';
    $block = faucetClaimBlock($member, time());
    $claim = $block ? notice($block) : '<form method="post" action="/faucet/?do=claim" class="stack">' . csrf($member) . turnstileWidget('claim')
        . '<button type="submit">Claim ' . authH(faucetFormatXmz(faucetConfig()['amount_atomic'])) . ' XMZ</button></form>';
    $stmt = $db->prepare('SELECT * FROM claims WHERE member_id = ? ORDER BY id DESC LIMIT 20');
    $stmt->execute([$member['id']]);
    $rows = '';
    foreach ($stmt->fetchAll() as $c) {
        $status = ['pending' => 'Waiting to be sent', 'processing' => 'Sending', 'paid' => 'Sent', 'failed' => 'Not sent'][$c['status']];
        $tx = $c['txid'] ? '<a href="/explorer/#/tx/' . authH($c['txid']) . '"><code>' . authH(substr($c['txid'], 0, 16)) . '…</code></a>' : ($c['reason'] ? authH($c['reason']) : '—');
        $rows .= '<tr><td>' . authH(gmdate('Y-m-d H:i', (int)$c['created_at'])) . ' UTC</td><td>' . authH(faucetFormatXmz($c['amount_atomic'])) . ' XMZ</td><td>' . authH($status) . '</td><td>' . $tx . '</td></tr>';
    }
    // Members who came for the forum land here after setting up two-factor; point them on.
    try { $profile = boardProfile((int)$member['id']); } catch (Throwable) { $profile = null; }
    $forum = $profile
        ? '<p>You post as <strong>' . authH($profile['username']) . '</strong>. <a href="/community/board/">Go to the forum</a></p>'
        : '<p>Your account is ready for the forum. Choose a public username to join in (you can only pick it once).</p><p><a class="button primary" href="/community/board/?view=profile">Choose your forum username</a></p>';
    page('Your faucet', ($error ? notice($error, true) : '') . $flash
        . ($profile ? '' : '<section><h2>Community forum</h2>' . $forum . '</section>')
        . '<section><h2>Claim</h2>' . $claim . '</section>'
        . '<section><h2>Payout address</h2>' . ($member['address'] ? '<p class="address"><code>' . authH($member['address']) . '</code></p>' : '<p>No payout address yet.</p>')
        . '<form method="post" action="/faucet/?do=address" class="stack">' . csrf($member)
        . '<label>' . ($member['address'] ? 'Change' : 'Add') . ' payout address (mainnet)<textarea name="address" rows="3" maxlength="200" spellcheck="false" autocomplete="off" required></textarea></label>'
        . '<label>Authenticator code<input name="code" inputmode="numeric" autocomplete="one-time-code" pattern="[0-9]{6}" maxlength="6" required></label>'
        . '<button type="submit">Save address</button></form><p class="note">Use an address from your own wallet. Never enter a seed phrase or private key anywhere on this site.</p></section>'
        . '<section><h2>Your claims</h2>' . ($rows ? '<div class="table-scroll"><table><thead><tr><th scope="col">Requested</th><th scope="col">Amount</th><th scope="col">Status</th><th scope="col">Transaction</th></tr></thead><tbody>' . $rows . '</tbody></table></div>' : '<p>No claims yet.</p>') . '</section>'
        . ($profile ? '<section><h2>Community forum</h2>' . $forum . '</section>' : '')
        . rules(), $member, $block === null);
}

// ---------- GET ----------
if (isset($_GET['verify'])) {
    $verified = faucetUseToken($_GET['verify'], 'verify');
    if (!$verified) { http_response_code(400); page('Link not valid', notice('This confirmation link has expired or was already used. Request a new one from the sign-in page.', true)); exit; }
    $db->prepare('UPDATE members SET email_verified_at = ? WHERE id = ? AND email_verified_at IS NULL')->execute([time(), $verified['id']]);
    faucetEvent((int)$verified['id'], 'member.email_verified');
    try { boardClaimHeldUsername((int)$verified['id'], time()); registrationApply((int)$verified['id']); } catch (Throwable) {}
    go('msg=verified');
}
if (isset($_GET['reset'])) {
    $target = faucetPeekToken($_GET['reset'], 'reset');
    if (!$target) { http_response_code(400); page('Link not valid', notice('This link has expired or was already used. Ask for a new one.', true)); exit; }
    resetForm((string)$_GET['reset'], $target); exit;
}
$view = is_string($_GET['view'] ?? null) ? $_GET['view'] : '';
if (!$member) {
    if ($view === 'signup') { signupForm(); exit; }
    if ($view === 'resend' && faucetAccountsOpen()) { emailForm('Resend confirmation', 'resend', 'Enter the email you signed up with and we will send a new confirmation link.'); exit; }
    if ($view === 'forgot' && faucetAccountsOpen()) { emailForm('Reset your password', 'forgot', 'Enter your account email. You will also need your authenticator code to finish the reset.'); exit; }
    loginForm(); exit;
}
if ($member['level'] !== 'full' || !$member['totp_secret']) { totpForm($member); exit; }
dashboard($member);
