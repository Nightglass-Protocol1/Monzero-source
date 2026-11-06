<?php
declare(strict_types=1);
// Airdrop moderator panel. Hidden behind a login: nothing is shown until a moderator
// authenticates with their XMZ address + Telegram handle + password + PIN (all four set by the
// owner in airdrop-config.php). Reviews are credited to whoever is signed in. The owner can also
// use the master admin_token via ?key=... in a pinch.
require dirname(__DIR__) . '/lib/airdrop.php';
header('X-Content-Type-Options: nosniff');
header('Referrer-Policy: no-referrer');
header('Cache-Control: no-store');

$cfg = adConfig();
$e = fn($s) => htmlspecialchars((string)$s, ENT_QUOTES, 'UTF-8');
$self = strtok((string)($_SERVER['REQUEST_URI'] ?? '/airdrop/admin.php'), '?');
$cookieOpts = ['path' => '/airdrop/', 'secure' => ($_SERVER['HTTPS'] ?? '') !== '' || ((string)($_SERVER['SERVER_PORT'] ?? '')) === '443', 'httponly' => true, 'samesite' => 'Lax'];
$loginError = '';

// Logout
if (isset($_GET['logout'])) { setcookie('ad_admin', '', ['expires' => time() - 3600] + $cookieOpts); header('Location: ' . $self); exit; }

// Login
if (($_POST['action'] ?? '') === 'login') {
    $name = adAdminLogin((string)($_POST['address'] ?? ''), (string)($_POST['telegram'] ?? ''), (string)($_POST['password'] ?? ''), (string)($_POST['pin'] ?? ''));
    if ($name !== null) {
        setcookie('ad_admin', adAdminCookie($name, time() + 7200), ['expires' => time() + 7200] + $cookieOpts);
        header('Location: ' . $self); exit;
    }
    $loginError = 'Login failed. Check your address, Telegram handle, password and PIN.';
}

// Who is signed in: session cookie, or the master token via ?key=/POST key.
$reviewer = adAdminFromCookie((string)($_COOKIE['ad_admin'] ?? ''));
if ($reviewer === null) $reviewer = adAdminAuth((string)($_GET['key'] ?? $_POST['key'] ?? ''));
$authed = $reviewer !== null;
$csrf = $authed ? hash_hmac('sha256', 'act:' . $reviewer, adSecret()) : '';

// Review action (requires a valid session/token + CSRF)
$msg = '';
if ($authed && ($_SERVER['REQUEST_METHOD'] ?? '') === 'POST' && isset($_POST['id'], $_POST['decision'])) {
    if (!hash_equals($csrf, (string)($_POST['csrf'] ?? ''))) { http_response_code(400); exit('Bad request'); }
    $id = (int)$_POST['id'];
    $decision = $_POST['decision'] === 'approve' ? 'approved' : 'rejected';
    $db = adDb();
    $s = $db->prepare("SELECT platform FROM submissions WHERE id = ? AND status = 'pending'");
    $s->execute([$id]);
    $plat = $s->fetchColumn();
    if ($plat !== false) {
        $pts = $decision === 'approved' ? adPoints((string)$plat) : 0;
        $db->prepare('UPDATE submissions SET status = ?, points = ?, reviewed_at = ?, reviewer = ? WHERE id = ?')->execute([$decision, $pts, time(), $reviewer, $id]);
    }
    header('Location: ' . $self); exit; // PRG so a refresh doesn't resubmit
}

// CSV export
if ($authed && ($_GET['export'] ?? '') === 'csv') {
    header('Content-Type: text/csv; charset=utf-8');
    header('Content-Disposition: attachment; filename="monzero-airdrop-payouts.csv"');
    echo "address,xmz\n";
    foreach (adPayouts() as $row) printf("%s,%.8f\n", $row['address'], $row['xmz']);
    exit;
}
?>
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="noindex,nofollow"><title>Moderator panel</title><link rel="stylesheet" href="/airdrop/admin.css"></head><body>
<main class="adm">
<?php if (!$authed): ?>
  <h1>Moderator sign in</h1>
  <?php if ($loginError): ?><p class="err"><?= $e($loginError) ?></p><?php endif; ?>
  <form method="post" class="loginform" autocomplete="off">
    <input type="hidden" name="action" value="login">
    <label>XMZ address<input name="address" spellcheck="false" autocomplete="off" required></label>
    <label>Telegram handle<input name="telegram" placeholder="@yourhandle" autocomplete="off" required></label>
    <label>Password<input name="password" type="password" autocomplete="off" required></label>
    <label>PIN<input name="pin" type="password" inputmode="numeric" autocomplete="off" required></label>
    <button type="submit">Sign in</button>
  </form>
  <p class="muted">Access is restricted to Monzero airdrop moderators.</p>
<?php else: ?>
  <div class="topbar"><h1>Airdrop review</h1><span>Signed in as <strong><?= $e($reviewer) ?></strong> · <a href="<?= $e($self) ?>?logout=1">sign out</a></span></div>
  <?php
    $pool = (float)$cfg['pool_xmz'];
    $payouts = adPayouts();
    $totalXmz = array_sum(array_column($payouts, 'xmz'));
    $pending = adDb()->query("SELECT s.id, s.platform, s.url, s.created_at, p.address FROM submissions s JOIN participants p ON p.id = s.participant_id WHERE s.status = 'pending' ORDER BY s.created_at")->fetchAll();
  ?>
  <div class="stats">
    <div><span>Pool / budget</span><strong><?= $pool > 0 ? $e(number_format($pool)) . ' XMZ' : 'not set' ?></strong></div>
    <div><span>Participants earning</span><strong><?= $e((string)count($payouts)) ?></strong></div>
    <div><span>Committed so far</span><strong><?= $e(number_format($totalXmz, 2)) ?> XMZ</strong></div>
    <div><span>Pending reviews</span><strong><?= $e((string)count($pending)) ?></strong></div>
  </div>
  <p><a class="btn" href="<?= $e($self) ?>?export=csv">Export payout CSV</a></p>

  <h2>Pending share submissions</h2>
  <?php if (!$pending): ?><p class="muted">Nothing to review.</p><?php else: ?>
  <table>
    <tr><th>#</th><th>Task</th><th>Address</th><th>Link / detail</th><th>Action</th></tr>
    <?php foreach ($pending as $row): ?>
      <tr>
        <td><?= $e($row['id']) ?></td>
        <td><?= $e($row['platform']) ?></td>
        <td class="mono"><?= $e(adShort($row['address'])) ?></td>
        <td><?php $u = (string)$row['url']; if (preg_match('#^https?://#', $u)): ?><a href="<?= $e($u) ?>" target="_blank" rel="noopener noreferrer nofollow">open ↗</a><?php else: ?><span class="mono"><?= $e($u) ?></span><?php endif; ?></td>
        <td class="actions">
          <form method="post"><input type="hidden" name="csrf" value="<?= $e($csrf) ?>"><input type="hidden" name="id" value="<?= $e($row['id']) ?>"><input type="hidden" name="decision" value="approve"><button class="approve">Approve</button></form>
          <form method="post"><input type="hidden" name="csrf" value="<?= $e($csrf) ?>"><input type="hidden" name="id" value="<?= $e($row['id']) ?>"><input type="hidden" name="decision" value="reject"><button class="reject">Reject</button></form>
        </td>
      </tr>
    <?php endforeach; ?>
  </table>
  <?php endif; ?>

  <h2>Leaderboard (top 30 by XMZ earned)</h2>
  <table>
    <tr><th>#</th><th>Address</th><th>XMZ earned</th></tr>
    <?php foreach (array_slice($payouts, 0, 30) as $i => $row): ?>
      <tr><td><?= $i + 1 ?></td><td class="mono"><?= $e(adShort($row['address'])) ?></td><td><?= $e(number_format($row['xmz'], 2)) ?></td></tr>
    <?php endforeach; ?>
  </table>

  <?php $adminPay = adAdminPayouts(); if ($adminPay || (float)$cfg['admin_reward_xmz'] > 0): ?>
  <h2>Moderator rewards (<?= $e((string)$cfg['admin_reward_xmz']) ?> XMZ per confirmation)</h2>
  <table>
    <tr><th>Moderator</th><th>Confirmations</th><th>XMZ</th><th>Address</th></tr>
    <?php foreach ($adminPay as $a): ?>
      <tr><td><?= $e($a['admin']) ?></td><td><?= $e((string)$a['confirmations']) ?></td><td><?= $e(number_format($a['xmz'], 2)) ?></td><td class="mono"><?= $e($a['address'] ? adShort($a['address']) : '—') ?></td></tr>
    <?php endforeach; ?>
  </table>
  <?php endif; ?>
<?php endif; ?>
</main>
</body></html>
