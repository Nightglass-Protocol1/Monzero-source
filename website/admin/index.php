<?php
declare(strict_types=1);
require __DIR__ . '/lib.php';
require __DIR__ . '/content.php';
require __DIR__ . '/board.php';

// Hash of a random discarded value; verifying against it costs the same as a real account.
const ADMIN_DUMMY_HASH = '$argon2id$v=19$m=65536,t=4,p=1$Qjg0RGdWMU1WaHJLTWowdw$k/8Ak5f58lfla8q4RrXJgpL8Zm8HwOFDzZPSLviW2TY';

const ADMIN_MESSAGES = [
    'welcome' => 'Your password is set and you are signed in.',
    'password' => 'Your password was changed. Other sessions were signed out.',
    'totp-on' => 'Authenticator codes are now required when you sign in.',
    'totp-off' => 'Authenticator codes are turned off.',
    'totp-required' => 'Authenticator codes are required for every admin. Set them up below to continue.',
    'invited' => 'Invite sent.',
    'awaiting' => 'Invite created. It will be emailed once the owner approves it.',
    'approved' => 'Invite approved and emailed.',
    'resent' => 'A new link was emailed; the old one no longer works.',
    'updated' => 'Permissions updated.',
    'disabled' => 'Account disabled and signed out.',
    'enabled' => 'Account enabled.',
    'revoked' => 'Invite revoked.',
    'mail-failed' => 'The change was saved, but the email could not be sent. Use “Send a new link” to retry.',
];

try {
    adminDb();
} catch (Throwable $e) {
    error_log('Monzero admin: storage unavailable');
    http_response_code(503);
    adminPage('Unavailable', '<p class="notice error">Administration is temporarily unavailable.</p>', null);
    exit;
}

$method = $_SERVER['REQUEST_METHOD'] ?? 'GET';
if (!in_array($method, ['GET', 'POST'], true)) { header('Allow: GET, POST'); adminDeny('Method not allowed.', 405); }
$do = is_string($_GET['do'] ?? null) ? $_GET['do'] : '';
$user = adminCurrent();

function redirect(string $view = '', string $message = ''): never {
    $query = array_filter(['view' => $view, 'msg' => $message]);
    header('Location: /admin/' . ($query ? '?' . http_build_query($query) : ''), true, 303);
    exit;
}

function field(string $name): string { $v = $_POST[$name] ?? ''; return is_string($v) ? $v : ''; }

function requireUser(?array $user): array {
    if (!$user) redirect();
    return $user;
}

function targetUser(array $actor): array {
    $id = filter_var($_POST['user_id'] ?? null, FILTER_VALIDATE_INT);
    $stmt = adminDb()->prepare('SELECT * FROM users WHERE id = ?');
    $stmt->execute([$id === false ? 0 : $id]);
    $target = $stmt->fetch();
    if (!$target || (int)$target['is_owner'] === 1 || (int)$actor['is_owner'] !== 1) adminDeny('Only the owner can change other admins.');
    return $target;
}

function setupForm(string $token, string $email, string $error = ''): void {
    adminPage('Set your password', ($error ? '<p class="notice error" role="alert">' . adminH($error) . '</p>' : '')
        . '<p>Setting the password for <strong>' . adminH($email) . '</strong>. Use at least ' . ADMIN_PASSWORD_MIN . ' characters; a long passphrase is best. Store it in a password manager.</p>'
        . '<form method="post" action="/admin/?do=setup" class="stack"><input type="hidden" name="token" value="' . adminH($token) . '">'
        . '<label>New password<input type="password" name="password" autocomplete="new-password" minlength="' . ADMIN_PASSWORD_MIN . '" required></label>'
        . '<label>Repeat password<input type="password" name="confirm" autocomplete="new-password" required></label>'
        . '<button type="submit">Set password and sign in</button></form>', null);
}

function loginForm(string $error = ''): void {
    adminPage('Sign in', ($error ? '<p class="notice error" role="alert">' . adminH($error) . '</p>' : '')
        . '<p>Administration is invite-only. There is no public sign-up.</p>'
        . '<form method="post" action="/admin/?do=login" class="stack">'
        . '<label>Email<input type="email" name="email" autocomplete="username" required></label>'
        . '<label>Password<input type="password" name="password" autocomplete="current-password" required></label>'
        . '<label>Authenticator code (if enabled)<input name="code" inputmode="numeric" autocomplete="one-time-code" pattern="[0-9]{6}" maxlength="6"></label>'
        . '<button type="submit">Sign in</button></form>', null);
}

function permissionBoxes(array $grantor, array $checked = []): string {
    $html = '<fieldset><legend>Permissions</legend>';
    foreach (adminPermissions($grantor) as $key) {
        $html .= '<label class="check"><input type="checkbox" name="permissions[]" value="' . adminH($key) . '"' . (in_array($key, $checked, true) ? ' checked' : '') . '> ' . adminH(ADMIN_PERMISSIONS[$key]) . '</label>';
    }
    return $html . '</fieldset>';
}

function postedPermissions(): array {
    $list = $_POST['permissions'] ?? [];
    return is_array($list) ? $list : [];
}

// ---------- POST actions ----------
if ($method === 'POST') {
    if ($do === 'setup') {
        adminCheckPost(null);
        $token = field('token');
        $target = adminTokenUser($token);
        if (!$target) { http_response_code(400); adminPage('Link not valid', '<p class="notice error">This link has expired, was already used or was replaced. Ask the owner for a new one.</p>', null); exit; }
        $problem = adminPasswordProblem(field('password'), field('confirm'), $target['email']);
        if ($problem) { http_response_code(400); setupForm($token, $target['email'], $problem); exit; }
        $db = adminDb();
        $db->beginTransaction();
        $used = $db->prepare("UPDATE tokens SET state = 'used' WHERE hash = ? AND state = 'sent'");
        $used->execute([$target['token_hash']]);
        if ($used->rowCount() !== 1) { $db->rollBack(); adminDeny('This link was already used.'); }
        $db->prepare("UPDATE users SET password_hash = ?, status = 'active' WHERE id = ? AND status = 'pending'")->execute([adminHashPassword(field('password')), $target['id']]);
        $db->commit();
        adminAudit((int)$target['id'], 'account.activated', ['email' => $target['email']]);
        adminStartSession((int)$target['id']);
        redirect('', 'welcome');
    }
    if ($do === 'login') {
        adminCheckPost(null);
        $email = adminEmail(field('email')) ?? '';
        if ($email === '') { http_response_code(401); loginForm('Email, password or code is incorrect.'); exit; }
        if (adminThrottled($email)) {
            adminAudit(null, 'login.throttled', ['email' => $email]);
            http_response_code(429); loginForm('Too many attempts. Wait 15 minutes and try again.'); exit;
        }
        $stmt = adminDb()->prepare("SELECT * FROM users WHERE email = ? AND status = 'active'");
        $stmt->execute([$email]);
        $found = $stmt->fetch();
        // Verify against a dummy hash when the account is missing so timing does not reveal it.
        $hash = $found['password_hash'] ?? ADMIN_DUMMY_HASH;
        $ok = password_verify(field('password'), $hash) && $found;
        if ($ok && $found['totp_secret'] && !adminTotpValid($found['totp_secret'], trim(field('code')))) $ok = false;
        if (!$ok) {
            adminRecordFailure($email);
            adminAudit(null, 'login.failed', ['email' => $email]);
            http_response_code(401); loginForm('Email, password or code is incorrect.'); exit;
        }
        if (password_needs_rehash($found['password_hash'], defined('PASSWORD_ARGON2ID') ? PASSWORD_ARGON2ID : PASSWORD_BCRYPT))
            adminDb()->prepare('UPDATE users SET password_hash = ? WHERE id = ?')->execute([adminHashPassword(field('password')), $found['id']]);
        adminDb()->prepare('UPDATE users SET last_login_at = ? WHERE id = ?')->execute([time(), $found['id']]);
        adminAudit((int)$found['id'], 'login.succeeded');
        adminStartSession((int)$found['id']);
        redirect();
    }

    $user = requireUser($user);
    adminCheckPost($user);
    // Authenticator codes are required: until they are on, an admin can only set them up,
    // change the password or sign out.
    if (!$user['totp_secret'] && !in_array($do, ['totp-enable', 'password', 'logout'], true))
        adminDeny('Turn on authenticator codes in your account first. They are required for every admin.');
    $actor = (int)$user['id'];
    $db = adminDb();
    switch ($do) {
        case 'logout':
            adminEndSession();
            adminAudit($actor, 'logout');
            redirect();
        case 'password':
            if (!password_verify(field('current'), (string)$user['password_hash'])) adminDeny('Current password incorrect.', 400);
            $problem = adminPasswordProblem(field('password'), field('confirm'), $user['email']);
            if ($problem) adminDeny($problem, 400);
            $db->prepare('UPDATE users SET password_hash = ? WHERE id = ?')->execute([adminHashPassword(field('password')), $actor]);
            $db->prepare('DELETE FROM sessions WHERE user_id = ?')->execute([$actor]);
            adminAudit($actor, 'password.changed');
            adminStartSession($actor);
            redirect('account', 'password');
        case 'totp-enable':
            $secret = field('secret');
            if (!preg_match('/\A[A-Z2-7]{32}\z/', $secret) || !adminTotpValid($secret, trim(field('code')))) adminDeny('That code did not match. Check your device clock and try again.', 400);
            $db->prepare('UPDATE users SET totp_secret = ? WHERE id = ?')->execute([$secret, $actor]);
            adminAudit($actor, 'totp.enabled');
            redirect('account', 'totp-on');
        case 'totp-disable':
            // Kept as an explicit refusal: authenticator codes cannot be turned off.
            adminDeny('Authenticator codes are required for every admin and cannot be turned off. If you lost the device, the owner can reset your access.', 400);
        case 'invite':
            if (!adminCan($user, 'admins.invite')) adminDeny('You cannot invite admins.');
            $email = adminEmail(field('email'));
            if (!$email) adminDeny('Enter a valid email address.', 400);
            try { $permissions = adminGrantable($user, postedPermissions()); } catch (InvalidArgumentException $e) { adminDeny($e->getMessage(), 400); }
            $exists = $db->prepare('SELECT 1 FROM users WHERE email = ?');
            $exists->execute([$email]);
            if ($exists->fetchColumn()) adminDeny('That email already has an account or invite.', 409);
            $owner = (int)$user['is_owner'] === 1;
            $db->beginTransaction();
            $db->prepare("INSERT INTO users (email, permissions, status, invited_by, created_at) VALUES (?, ?, 'pending', ?, ?)")
                ->execute([$email, json_encode($permissions), $actor, time()]);
            $newId = (int)$db->lastInsertId();
            $token = adminIssueToken($newId, 'invite', $owner ? 'sent' : 'awaiting_approval', $actor);
            $db->commit();
            adminAudit($actor, $owner ? 'invite.sent' : 'invite.requested', ['email' => $email, 'permissions' => $permissions]);
            if (!$owner) redirect('admins', 'awaiting');
            redirect('admins', adminSendLink(['email' => $email], $token, 'invite') ? 'invited' : 'mail-failed');
        case 'approve':
        case 'resend':
            $target = targetUser($user);
            if ($target['status'] !== 'pending') adminDeny('That account is already set up.', 409);
            if ($do === 'approve') {
                $waiting = $db->prepare("SELECT 1 FROM tokens WHERE user_id = ? AND state = 'awaiting_approval'");
                $waiting->execute([$target['id']]);
                if (!$waiting->fetchColumn()) adminDeny('That invite is not waiting for approval.', 409);
            }
            $token = adminIssueToken((int)$target['id'], 'invite', 'sent', $actor);
            adminAudit($actor, $do === 'approve' ? 'invite.approved' : 'invite.resent', ['email' => $target['email']]);
            redirect('admins', adminSendLink($target, $token, 'invite') ? ($do === 'approve' ? 'approved' : 'resent') : 'mail-failed');
        case 'revoke':
            $target = targetUser($user);
            if ($target['status'] !== 'pending') adminDeny('Only pending invites can be revoked; disable active accounts instead.', 409);
            $db->prepare("UPDATE tokens SET state = 'revoked' WHERE user_id = ? AND state IN ('awaiting_approval', 'sent')")->execute([$target['id']]);
            $db->prepare("UPDATE users SET status = 'disabled' WHERE id = ?")->execute([$target['id']]);
            adminAudit($actor, 'invite.revoked', ['email' => $target['email']]);
            redirect('admins', 'revoked');
        case 'permissions':
            $target = targetUser($user);
            try { $permissions = adminGrantable($user, postedPermissions()); } catch (InvalidArgumentException $e) { adminDeny($e->getMessage(), 400); }
            $db->prepare('UPDATE users SET permissions = ? WHERE id = ?')->execute([json_encode($permissions), $target['id']]);
            adminAudit($actor, 'permissions.changed', ['email' => $target['email'], 'permissions' => $permissions]);
            redirect('admins', 'updated');
        case 'disable':
        case 'enable':
            $target = targetUser($user);
            if ($do === 'enable' && !$target['password_hash']) adminDeny('This account never finished setup. Send a new invite instead.', 409);
            $db->prepare('UPDATE users SET status = ? WHERE id = ?')->execute([$do === 'disable' ? 'disabled' : 'active', $target['id']]);
            if ($do === 'disable') {
                $db->prepare('DELETE FROM sessions WHERE user_id = ?')->execute([$target['id']]);
                $db->prepare("UPDATE tokens SET state = 'revoked' WHERE user_id = ? AND state IN ('awaiting_approval', 'sent')")->execute([$target['id']]);
            }
            adminAudit($actor, "account.{$do}d", ['email' => $target['email']]);
            redirect('admins', $do . 'd');
        default:
            if (contentAction($do, $user) || boardAdminAction($do, $user)) exit;
            adminDeny('Unknown action.', 400);
    }
}

// ---------- GET views ----------
if (isset($_GET['setup'])) {
    $token = is_string($_GET['setup']) ? $_GET['setup'] : '';
    $target = adminTokenUser($token);
    if (!$target) { http_response_code(400); adminPage('Link not valid', '<p class="notice error">This link has expired, was already used or was replaced. Ask the owner for a new one.</p>', null); exit; }
    setupForm($token, $target['email']);
    exit;
}
if (!$user) { loginForm(); exit; }
if (!$user['totp_secret'] && ($_GET['view'] ?? '') !== 'account') { header('Location: /admin/?view=account&msg=totp-required', true, 303); exit; }

$message = ADMIN_MESSAGES[$_GET['msg'] ?? ''] ?? '';
$flash = $message ? '<p class="notice" role="status">' . adminH($message) . '</p>' : '';
$csrf = '<input type="hidden" name="csrf" value="' . adminH($user['csrf']) . '">';
$view = is_string($_GET['view'] ?? null) ? $_GET['view'] : '';
$isOwner = (int)$user['is_owner'] === 1;

if ($view === 'account') {
    $totp = $user['totp_secret']
        ? '<p>Authenticator codes are <strong>on</strong>. They are required for every admin. If you lose the device, the owner can reset your access (the owner account itself is reset on the server with <code>admin/bootstrap.php --reset --clear-totp</code>).</p>'
        : (function () use ($csrf): string {
            $secret = adminBase32Encode(random_bytes(20));
            $uri = 'otpauth://totp/' . rawurlencode('Monzero admin') . '?secret=' . $secret . '&issuer=Monzero';
            return '<p>Add a second step to sign-in with an authenticator app. Scan the code, or enter the key by hand, then type the 6-digit code it shows.</p>'
                . '<div id="totp-qr" class="qr" data-uri="' . adminH($uri) . '"></div><p>Key: <code>' . adminH(implode(' ', str_split($secret, 4))) . '</code></p>'
                . '<form method="post" action="/admin/?do=totp-enable" class="stack">' . $csrf . '<input type="hidden" name="secret" value="' . adminH($secret) . '"><label>Code from the app<input name="code" inputmode="numeric" autocomplete="one-time-code" pattern="[0-9]{6}" maxlength="6" required></label><button type="submit">Turn on</button></form>'
                . '<script type="module" src="/admin/admin.mjs?v=20261021"></script>';
        })();
    adminPage('Your account', $flash . '<section><h2>Password</h2><form method="post" action="/admin/?do=password" class="stack">' . $csrf
        . '<label>Current password<input type="password" name="current" autocomplete="current-password" required></label>'
        . '<label>New password<input type="password" name="password" autocomplete="new-password" minlength="' . ADMIN_PASSWORD_MIN . '" required></label>'
        . '<label>Repeat new password<input type="password" name="confirm" autocomplete="new-password" required></label><button type="submit">Change password</button></form></section>'
        . '<section><h2>Authenticator codes</h2>' . $totp . '</section>', $user);
    exit;
}

if ($view === 'admins' && ($isOwner || adminCan($user, 'admins.invite'))) {
    $rows = adminDb()->query("SELECT u.*, i.email AS inviter,
        (SELECT state FROM tokens t WHERE t.user_id = u.id AND t.state IN ('awaiting_approval', 'sent') ORDER BY created_at DESC LIMIT 1) AS invite_state
        FROM users u LEFT JOIN users i ON i.id = u.invited_by ORDER BY u.is_owner DESC, u.created_at")->fetchAll();
    $list = '';
    foreach ($rows as $row) {
        if (!$isOwner && (int)$row['invited_by'] !== (int)$user['id'] && (int)$row['id'] !== (int)$user['id']) continue;
        $state = (int)$row['is_owner'] === 1 ? 'Owner' : ($row['status'] === 'pending' ? ($row['invite_state'] === 'awaiting_approval' ? 'Waiting for owner approval' : ($row['invite_state'] === 'sent' ? 'Invite sent' : 'Invite expired or revoked')) : ucfirst($row['status']));
        $perms = (int)$row['is_owner'] === 1 ? 'All permissions' : (adminPermissions($row) ? implode(', ', array_map(fn($p) => ADMIN_PERMISSIONS[$p], adminPermissions($row))) : 'None');
        $item = '<article><h3>' . adminH($row['email']) . '</h3><dl><dt>Status</dt><dd>' . adminH($state) . '</dd><dt>Permissions</dt><dd>' . adminH($perms) . '</dd><dt>Invited by</dt><dd>' . adminH($row['inviter'] ?? '—') . '</dd></dl>';
        if ($isOwner && (int)$row['is_owner'] !== 1) {
            $hidden = $csrf . '<input type="hidden" name="user_id" value="' . (int)$row['id'] . '">';
            $actions = [];
            if ($row['status'] === 'pending') {
                if ($row['invite_state'] === 'awaiting_approval') $actions[] = ['approve', 'Approve and email invite'];
                else $actions[] = ['resend', 'Send a new link'];
                $actions[] = ['revoke', 'Revoke invite'];
            } elseif ($row['status'] === 'active') $actions[] = ['disable', 'Disable account'];
            else if ($row['password_hash']) $actions[] = ['enable', 'Enable account'];
            $item .= '<div class="actions">';
            foreach ($actions as [$action, $label]) $item .= '<form method="post" action="/admin/?do=' . $action . '">' . $hidden . '<button type="submit">' . adminH($label) . '</button></form>';
            $item .= '</div><details><summary>Change permissions</summary><form method="post" action="/admin/?do=permissions" class="stack">' . $hidden . permissionBoxes($user, adminPermissions($row)) . '<button type="submit">Save permissions</button></form></details>';
        }
        $list .= $item . '</article>';
    }
    $invite = adminCan($user, 'admins.invite')
        ? '<section><h2>Invite an admin</h2><p>' . ($isOwner ? 'The invite is emailed straight away.' : 'The owner must approve your invite before it is emailed.') . ' You can only grant permissions you hold. Granting “Invite admins” lets them invite others, still subject to owner approval.</p>'
            . '<form method="post" action="/admin/?do=invite" class="stack">' . $csrf . '<label>Email<input type="email" name="email" required></label>' . permissionBoxes($user) . '<button type="submit">Create invite</button></form></section>'
        : '';
    adminPage('Admins', $flash . $invite . '<section><h2>' . ($isOwner ? 'All admins' : 'Your invites') . '</h2><div class="cards">' . $list . '</div></section>', $user);
    exit;
}

if ($view === 'audit' && adminCan($user, 'audit.view')) {
    $rows = adminDb()->query('SELECT a.*, u.email FROM audit a LEFT JOIN users u ON u.id = a.actor ORDER BY a.id DESC LIMIT 200')->fetchAll();
    $table = '';
    foreach ($rows as $row) $table .= '<tr><td>' . adminH(gmdate('Y-m-d H:i:s', (int)$row['at'])) . ' UTC</td><td>' . adminH($row['email'] ?? '—') . '</td><td>' . adminH($row['action']) . '</td><td><code>' . adminH($row['detail']) . '</code></td></tr>';
    adminPage('Audit log', '<p>The latest 200 events. Entries cannot be edited or deleted.</p><div class="table-scroll"><table><thead><tr><th scope="col">Time</th><th scope="col">Who</th><th scope="col">Action</th><th scope="col">Details</th></tr></thead><tbody>' . $table . '</tbody></table></div>', $user);
    exit;
}

if (contentView($view, $user) || boardAdminViewRoute($view, $user)) exit;

$permissions = adminPermissions($user);
adminPage('Overview', $flash . '<p>Signed in as <strong>' . adminH($user['email']) . '</strong>' . ($isOwner ? ' (owner)' : '') . '.</p>'
    . '<section><h2>Your permissions</h2><ul>' . ($permissions ? implode('', array_map(fn($p) => '<li>' . adminH(ADMIN_PERMISSIONS[$p]) . '</li>', $permissions)) : '<li>None yet. Ask the owner.</li>') . '</ul>'
    . '</section>' . contentLinks($user) . boardAdminLinks($user)
    , $user);
