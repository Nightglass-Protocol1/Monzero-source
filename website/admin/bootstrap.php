<?php
declare(strict_types=1);
// Command line only: create the single owner account and email its one-time setup link.
//   php admin/bootstrap.php security@monzero.org
// Re-running for the same pending owner replaces the link. It refuses if another owner exists.
// Forgotten owner password (needs shell access on the server, so there is no web reset):
//   php admin/bootstrap.php --reset security@monzero.org [--clear-totp]
// clears the password, ends the owner's sessions and emails a new setup link. The
// authenticator stays enrolled unless --clear-totp is given.
if (PHP_SAPI !== 'cli') { http_response_code(404); exit; }
require __DIR__ . '/lib.php';

$args = array_slice($argv, 1);
$reset = in_array('--reset', $args, true);
$clearTotp = in_array('--clear-totp', $args, true);
$positional = array_values(array_filter($args, fn($a) => !str_starts_with($a, '--')));
$unknown = array_diff(array_filter($args, fn($a) => str_starts_with($a, '--')), ['--reset', '--clear-totp']);
$email = count($positional) === 1 ? adminEmail($positional[0]) : null;
if (!$email || $unknown || ($clearTotp && !$reset)) { fwrite(STDERR, "Usage: php admin/bootstrap.php [--reset [--clear-totp]] OWNER_EMAIL\n"); exit(2); }
$db = adminDb();
adminSecret(); // create the HMAC secret now so storage problems surface here
$db->beginTransaction();
$owner = $db->query('SELECT * FROM users WHERE is_owner = 1')->fetch();
if ($owner && $owner['email'] !== $email) { $db->rollBack(); fwrite(STDERR, "An owner already exists ({$owner['email']}). Refusing.\n"); exit(1); }
if ($reset) {
    if (!$owner || $owner['status'] !== 'active') { $db->rollBack(); fwrite(STDERR, "There is no active owner account to reset.\n"); exit(1); }
    $db->prepare("UPDATE users SET password_hash = NULL, status = 'pending'" . ($clearTotp ? ', totp_secret = NULL' : '') . ' WHERE id = ?')->execute([$owner['id']]);
    $db->prepare('DELETE FROM sessions WHERE user_id = ?')->execute([$owner['id']]);
    $token = adminIssueToken((int)$owner['id'], 'setup', 'sent', null);
    $db->commit();
    adminAudit(null, 'owner.password_reset', ['email' => $email, 'totp_cleared' => $clearTotp]);
    if (!adminSendLink(['email' => $email], $token, 'setup')) { fwrite(STDERR, "Password cleared, but the email could not be sent. Re-run without --reset to try again.\n"); exit(1); }
    echo "Password cleared and a new setup link emailed to $email. It expires in 48 hours.\n";
    exit(0);
}
if ($owner && $owner['status'] !== 'pending') { $db->rollBack(); fwrite(STDERR, "The owner account is already set up. Use the sign-in page, or --reset if the password is lost.\n"); exit(1); }
if (!$owner) {
    $taken = $db->prepare('SELECT 1 FROM users WHERE email = ?');
    $taken->execute([$email]);
    if ($taken->fetchColumn()) { $db->rollBack(); fwrite(STDERR, "That email already belongs to a non-owner account. Refusing.\n"); exit(1); }
    $db->prepare("INSERT INTO users (email, is_owner, permissions, status, created_at) VALUES (?, 1, '[]', 'pending', ?)")->execute([$email, time()]);
    $owner = ['id' => (int)$db->lastInsertId(), 'email' => $email];
}
$token = adminIssueToken((int)$owner['id'], 'setup', 'sent', null);
$db->commit();
adminAudit(null, 'owner.setup_link_issued', ['email' => $email]);
if (!adminSendLink(['email' => $email], $token, 'setup')) { fwrite(STDERR, "Owner created, but the email could not be sent. Re-run to try again.\n"); exit(1); }
echo "Setup link emailed to $email. It expires in 48 hours.\n";
