<?php
declare(strict_types=1);
// Airdrop moderator panel. Hidden behind a login: nothing is shown until a moderator
// authenticates with their XMZ address + Telegram handle + password + PIN (all four set by the
// owner in airdrop-config.php). Reviews are credited to whoever is signed in. The owner can also
// use the master admin_token via ?key=... in a pinch.
// Sections: Overview, Review queue, Promote (referral link, QR, banners, ready-to-post copy),
// Leaderboard, Moderator rewards. Behaviour lives in admin.mjs (CSP: no inline script/style).
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
    header('Location: ' . $self . '#review'); exit; // PRG so a refresh doesn't resubmit
}

// CSV export: the final, budget-capped payout list (participants + moderators) in atomic units.
// Feed it to utils/website/airdrop_payout.py.
if ($authed && ($_GET['export'] ?? '') === 'csv') {
    header('Content-Type: text/csv; charset=utf-8');
    header('Content-Disposition: attachment; filename="monzero-airdrop-payouts.csv"');
    echo "kind,address,atomic,xmz\n";
    foreach (adFinalPayouts() as $row) printf("%s,%s,%d,%.11f\n", $row['kind'], $row['address'], $row['atomic'], $row['atomic'] / 1e11);
    exit;
}

$TASKS = [
    'wallet' => ['Wallet', 'Unlocks with Telegram or mining'], 'telegram' => ['Telegram', 'Verified by the bot'],
    'mining' => ['Mining', 'Seen mining on enough days'], 'forum_post' => ['Forum post', 'Check it is a genuine first post on the board'],
    'forum_active' => ['Forum regular', 'Check 5+ real posts over a week, no spam'], 'run_node' => ['Run a node', 'Check the node answers on the given host/port'],
    'share_x' => ['Share on X', 'Check the post is public and is about Monzero'], 'share_instagram' => ['Share on Instagram', 'Check the post is public and is about Monzero'],
];
$ago = function (int $t): string {
    $d = max(0, time() - $t);
    return $d < 3600 ? max(1, intdiv($d, 60)) . ' min ago' : ($d < 86400 ? intdiv($d, 3600) . ' h ago' : intdiv($d, 86400) . ' d ago');
};
?>
<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><meta name="robots" content="noindex,nofollow"><title>Airdrop moderators · Monzero</title><link rel="icon" href="/assets/monzero-emblem-20261026-96.webp"><link rel="stylesheet" href="/airdrop/admin.css?v=20261114"></head><body class="adm-body">
<?php if (!$authed): ?>
<main class="login-wrap">
  <div class="login-card">
    <img src="/assets/monzero-emblem-20261026-96.webp" alt="" width="56" height="56">
    <h1>Moderator sign in</h1>
    <p class="muted">Monzero community airdrop</p>
    <?php if ($loginError): ?><p class="alert err"><?= $e($loginError) ?></p><?php endif; ?>
    <form method="post" class="loginform" autocomplete="off">
      <input type="hidden" name="action" value="login">
      <label>XMZ address<input name="address" spellcheck="false" autocomplete="off" required></label>
      <label>Telegram handle<input name="telegram" placeholder="@yourhandle" autocomplete="off" required></label>
      <label>Password<input name="password" type="password" autocomplete="off" required></label>
      <label>PIN<input name="pin" type="password" inputmode="numeric" autocomplete="off" required></label>
      <button type="submit" class="btn primary">Sign in</button>
    </form>
    <p class="muted small">Access is restricted to Monzero airdrop moderators.</p>
  </div>
</main>
<?php else:
    $db = adDb();
    $budget = adBudget();
    $payouts = adPayouts();
    $closed = adClosedReason();
    $pending = $db->query("SELECT s.id, s.platform, s.url, s.created_at, p.address FROM submissions s JOIN participants p ON p.id = s.participant_id WHERE s.status = 'pending' ORDER BY s.created_at")->fetchAll();
    $participants = adParticipantCount();
    $done = [];
    foreach ($db->query('SELECT task, COUNT(*) AS n FROM progress GROUP BY task')->fetchAll() as $r) $done[$r['task']] = (int)$r['n'];
    foreach ($db->query("SELECT platform, COUNT(*) AS n FROM submissions WHERE status = 'approved' GROUP BY platform")->fetchAll() as $r) $done[$r['platform']] = (int)$r['n'];
    $adminPay = adAdminPayouts();
    $mine = ['confirmations' => 0, 'xmz' => 0.0];
    foreach ($adminPay as $a) if ($a['admin'] === $reviewer) $mine = $a;
    $reviewed = $db->prepare("SELECT COUNT(*) FROM submissions WHERE reviewer = ? AND status = 'rejected'"); $reviewed->execute([$reviewer]);
    $myRejected = (int)$reviewed->fetchColumn();
    // The signed-in moderator's own referral code, if their address joined the airdrop.
    $myRef = '';
    foreach ($cfg['admins'] as $a) if ($a['name'] === $reviewer && $a['address'] !== '') {
        $q = $db->prepare('SELECT ref_code FROM participants WHERE address = ?'); $q->execute([trim($a['address'])]);
        $myRef = (string)($q->fetchColumn() ?: '');
    }
    $pct = $budget['pool'] > 0 ? min(100, $budget['committed'] / $budget['pool'] * 100) : 0;
    $endsAt = (int)$cfg['ends_at'];
    $daysLeft = $endsAt > 0 ? max(0, (int)ceil(($endsAt - time()) / 86400)) : null;
    $endsLabel = $endsAt > 0 ? gmdate('j M Y', $endsAt) : 'not set';
    $cap = rtrim(rtrim(number_format((float)$cfg['per_person_cap'], 2, '.', ''), '0'), '.');
    $refReward = rtrim(rtrim(number_format(adRewardXmz('referral'), 2, '.', ''), '0'), '.');
    $maxDone = max([1, ...array_values($done)]);
?>
<header class="adm-top">
  <a class="adm-brand" href="/airdrop/"><img src="/assets/monzero-emblem-20261026-96.webp" alt="" width="34" height="34"><span><strong>Monzero</strong> airdrop moderators</span></a>
  <div class="adm-user"><span class="status-dot <?= $closed === null ? 'on' : 'off' ?>"></span><span><?= $closed === null ? 'Campaign open' : 'Closed: ' . $e($closed) ?></span><span class="sep"></span><span>Signed in as <strong><?= $e($reviewer) ?></strong></span><a class="btn ghost small" href="<?= $e($self) ?>?logout=1">Sign out</a></div>
</header>
<nav class="adm-tabs" aria-label="Sections">
  <a href="#overview" data-tab="overview">Overview</a>
  <a href="#review" data-tab="review">Review<?php if ($pending): ?> <span class="count"><?= count($pending) ?></span><?php endif; ?></a>
  <a href="#promote" data-tab="promote">Promote</a>
  <a href="#leaderboard" data-tab="leaderboard">Leaderboard</a>
  <a href="#rewards" data-tab="rewards">Rewards</a>
</nav>

<main class="adm" id="adm" data-ref="<?= $e($myRef) ?>" data-cap="<?= $e($cap) ?>" data-ends="<?= $e($endsLabel) ?>" data-refreward="<?= $e($refReward) ?>" data-telegram="<?= $e(ltrim($cfg['telegram'], '@')) ?>" data-x="<?= $e($cfg['x']) ?>">

<section id="overview" class="tab-panel">
  <div class="hero-card">
    <div>
      <p class="eyebrow">Community airdrop · round ends <?= $e($endsLabel) ?></p>
      <h1><?= $daysLeft === null ? 'No end date set' : ($daysLeft === 0 ? 'Final day' : $e((string)$daysLeft) . ' days to go') ?></h1>
      <p class="muted">Up to <?= $e($cap) ?> XMZ per person from tasks, plus <?= $e($refReward) ?> XMZ per verified referral.</p>
    </div>
    <div class="meter-block">
      <div class="meter-label"><span>Budget committed</span><strong><?= $e(number_format($budget['committed'], 2)) ?> / <?= $budget['pool'] > 0 ? $e(number_format($budget['pool'])) : '—' ?> XMZ</strong></div>
      <div class="meter <?= $pct >= 90 ? 'hot' : '' ?>"><span class="w<?= $pct > 0 ? max(5, (int)round($pct / 5) * 5) : 0 ?>"></span></div>
      <div class="meter-foot"><span><?= $e(number_format($pct, 1)) ?>% used</span><span><?= $e(number_format($budget['remaining'], 2)) ?> XMZ left</span></div>
    </div>
  </div>
  <?php if ($budget['scale'] < 1): ?><p class="alert err">Earned rewards exceed the budget. Payouts are scaled to <?= $e(number_format($budget['scale'] * 100, 2)) ?>% and registrations are closed.</p><?php endif; ?>
  <div class="kpis">
    <div class="kpi"><span>Participants</span><strong><?= $e(number_format($participants)) ?></strong><em><?= $e((string)count($payouts)) ?> earning</em></div>
    <a class="kpi link" href="#review" data-tab="review"><span>Pending reviews</span><strong><?= count($pending) ?></strong><em><?= $pending ? 'oldest ' . $e($ago((int)$pending[0]['created_at'])) : 'all clear' ?></em></a>
    <div class="kpi"><span>Your confirmations</span><strong><?= $e((string)$mine['confirmations']) ?></strong><em><?= $e(number_format((float)$mine['xmz'], 2)) ?> XMZ earned · <?= $myRejected ?> rejected</em></div>
    <div class="kpi"><span>Budget</span><strong><?= $budget['pool'] > 0 ? $e(number_format($budget['pool'])) : '—' ?></strong><em>XMZ<?= $cfg['auto_renew'] ? ', renewed from the pool fee' : '' ?></em></div>
  </div>
  <div class="grid2">
    <div class="panel">
      <h2>Task completions</h2>
      <ul class="bars">
        <?php foreach ($TASKS as $k => [$label]): $n = $done[$k] ?? 0; ?>
        <li><span><?= $e($label) ?></span><span class="bar"><span class="w<?= (int)round($n / $maxDone * 20) * 5 ?>"></span></span><strong><?= $n ?></strong></li>
        <?php endforeach; ?>
      </ul>
    </div>
    <div class="panel">
      <h2>Quick actions</h2>
      <div class="actions-list">
        <a class="action" href="#review" data-tab="review"><strong>Review submissions</strong><span><?= count($pending) ?> waiting</span></a>
        <a class="action" href="#promote" data-tab="promote"><strong>Get promo materials</strong><span>Your link, QR, banners, posts</span></a>
        <a class="action" href="<?= $e($self) ?>?export=csv"><strong>Export payout CSV</strong><span>Budget-capped, for the payout script</span></a>
        <a class="action" href="/airdrop/" target="_blank" rel="noopener"><strong>Open the public page ↗</strong><span>See what participants see</span></a>
      </div>
    </div>
  </div>
</section>

<section id="review" class="tab-panel">
  <div class="section-head"><h2>Review queue</h2><p class="muted">Approve only genuine, public work. Each approval pays the participant the task reward and you <?= $e((string)$cfg['admin_reward_xmz']) ?> XMZ.</p></div>
  <?php if (!$pending): ?>
    <div class="empty"><strong>All clear</strong><span>Nothing is waiting for review.</span></div>
  <?php else: ?>
  <div class="queue">
    <?php foreach ($pending as $row): [$label, $hint] = $TASKS[$row['platform']] ?? [$row['platform'], '']; $u = (string)$row['url']; ?>
    <article class="qcard">
      <div class="qhead"><span class="tag t-<?= $e($row['platform']) ?>"><?= $e($label) ?></span><span class="reward">+<?= $e((string)adRewardXmz((string)$row['platform'])) ?> XMZ</span><span class="muted small">#<?= $e($row['id']) ?> · <?= $e($ago((int)$row['created_at'])) ?></span></div>
      <div class="qbody">
        <?php if (preg_match('#^https?://#', $u)): ?><a class="detail" href="<?= $e($u) ?>" target="_blank" rel="noopener noreferrer nofollow"><?= $e(mb_strimwidth($u, 0, 80, '…')) ?> ↗</a>
        <?php else: ?><code class="detail"><?= $e($u) ?></code><?php endif; ?>
        <p class="muted small">From <span class="mono"><?= $e(adShort($row['address'])) ?></span><?= $hint ? ' · ' . $e($hint) : '' ?></p>
      </div>
      <div class="qactions">
        <form method="post"><input type="hidden" name="csrf" value="<?= $e($csrf) ?>"><input type="hidden" name="id" value="<?= $e($row['id']) ?>"><input type="hidden" name="decision" value="approve"><button class="btn approve">Approve</button></form>
        <form method="post"><input type="hidden" name="csrf" value="<?= $e($csrf) ?>"><input type="hidden" name="id" value="<?= $e($row['id']) ?>"><input type="hidden" name="decision" value="reject"><button class="btn reject">Reject</button></form>
      </div>
    </article>
    <?php endforeach; ?>
  </div>
  <?php endif; ?>
</section>

<section id="promote" class="tab-panel">
  <div class="section-head"><h2>Promote the airdrop</h2><p class="muted">Everything here uses your referral link, so people who join through you count towards your referrals.</p></div>

  <div class="panel ref-panel">
    <div class="ref-main">
      <h3>Your referral link</h3>
      <?php if ($myRef === ''): ?>
        <p class="alert info">Your moderator address hasn't joined the airdrop yet, so you don't have a personal link. <a href="/airdrop/" target="_blank" rel="noopener">Join with your address</a> to earn <?= $e($refReward) ?> XMZ for each verified referral, or paste an existing referral code below. Until then the materials use the plain link.</p>
      <?php endif; ?>
      <div class="field-row"><input id="ref-link" readonly aria-label="Referral link"><button class="btn primary" data-copy-input="ref-link">Copy</button></div>
      <label class="small muted ref-code-label">Referral code <input id="ref-code" maxlength="8" spellcheck="false" placeholder="8 characters"></label>
      <div class="share-row">
        <a class="share x" id="sh-x" target="_blank" rel="noopener">X</a>
        <a class="share tg" id="sh-tg" target="_blank" rel="noopener">Telegram</a>
        <a class="share wa" id="sh-wa" target="_blank" rel="noopener">WhatsApp</a>
        <a class="share rd" id="sh-rd" target="_blank" rel="noopener">Reddit</a>
        <a class="share fb" id="sh-fb" target="_blank" rel="noopener">Facebook</a>
        <a class="share em" id="sh-em">Email</a>
      </div>
    </div>
    <div class="qr-box">
      <canvas id="ref-qr" width="200" height="200" aria-label="QR code for your referral link"></canvas>
      <button class="btn ghost small" id="qr-download">Download QR</button>
    </div>
  </div>

  <h3 class="sub">Banners</h3>
  <p class="muted">Social sizes can be downloaded with your personal QR code stamped in the corner. Web ad sizes come with ready-made HTML and forum code that link through your referral link.</p>
  <div class="banner-filter" role="tablist">
    <button class="chip on" data-filter="all">All</button><button class="chip" data-filter="social">Social posts</button><button class="chip" data-filter="story">Stories</button><button class="chip" data-filter="header">Profile headers</button><button class="chip" data-filter="web">Website ads</button>
  </div>
  <div class="banners" id="banners"></div>

  <h3 class="sub">Ready-to-post copy</h3>
  <p class="muted">Pick a platform, tweak if you like, copy. Your link is already filled in.</p>
  <div class="snip-tabs" id="snip-tabs"></div>
  <div class="panel snip-panel"><div class="snip-meta"><strong id="snip-title"></strong><span class="muted small" id="snip-note"></span></div><textarea id="snip-text" rows="9" spellcheck="true" aria-label="Post text"></textarea><div class="snip-foot"><span class="muted small" id="snip-count"></span><button class="btn primary" data-copy-input="snip-text">Copy text</button></div></div>

  <div class="grid2">
    <div class="panel">
      <h3>Hashtags &amp; key facts</h3>
      <div class="chips" id="tags"></div>
      <ul class="facts">
        <li>Private by default, CPU-mined (RandomX), no premine, no ICO.</li>
        <li>Up to <?= $e($cap) ?> XMZ per person from tasks; <?= $e($refReward) ?> XMZ per verified referral.</li>
        <li>Rewards come from the mining pool's fee. Round ends <?= $e($endsLabel) ?>.</li>
        <li>Only official page: monzero.org/airdrop. No seed, keys or payment ever.</li>
      </ul>
    </div>
    <div class="panel">
      <h3>Brand kit</h3>
      <div class="actions-list">
        <a class="action" href="/marketing/" target="_blank" rel="noopener"><strong>Marketing kit ↗</strong><span>Key messages, post copy, app screenshots</span></a>
        <a class="action" href="/marketing/monzero-marketing-graphics.zip"><strong>All brand graphics (.zip)</strong><span>Logos, banners, avatars, quote cards</span></a>
        <a class="action" href="/press/" target="_blank" rel="noopener"><strong>Press kit ↗</strong><span>Logos and colours</span></a>
      </div>
    </div>
  </div>

  <div class="panel rules">
    <h3>Promotion rules</h3>
    <div class="grid2">
      <ul class="do"><li>Post in your own channels, groups that allow promotion, and to friends who'd want it.</li><li>Say you're a Monzero moderator when you post about the airdrop.</li><li>Link only to monzero.org/airdrop.</li><li>Answer questions honestly — there is no price and no listing yet.</li></ul>
      <ul class="dont"><li>No spamming, mass DMs, cold DMs or tagging strangers.</li><li>No promises of profit, price or "free money".</li><li>Never ask anyone for a seed, keys, or payment.</li><li>Don't approve your own referrals' submissions any differently from anyone else's.</li></ul>
    </div>
  </div>
</section>

<section id="leaderboard" class="tab-panel">
  <div class="section-head"><h2>Leaderboard</h2><p class="muted">Top 30 by XMZ earned (before any budget scaling).</p></div>
  <div class="panel table-panel">
    <table>
      <thead><tr><th>#</th><th>Address</th><th class="num">XMZ earned</th></tr></thead>
      <tbody>
      <?php foreach (array_slice($payouts, 0, 30) as $i => $row): ?>
        <tr><td class="rank r<?= $i + 1 ?>"><?= $i + 1 ?></td><td class="mono"><?= $e(adShort($row['address'])) ?></td><td class="num"><?= $e(number_format($row['xmz'], 2)) ?></td></tr>
      <?php endforeach; ?>
      <?php if (!$payouts): ?><tr><td colspan="3" class="muted">Nobody has earned anything yet.</td></tr><?php endif; ?>
      </tbody>
    </table>
  </div>
</section>

<section id="rewards" class="tab-panel">
  <div class="section-head"><h2>Moderator rewards</h2><p class="muted"><?= $e((string)$cfg['admin_reward_xmz']) ?> XMZ for each approved submission, paid with the airdrop.</p></div>
  <div class="panel table-panel">
    <table>
      <thead><tr><th>Moderator</th><th class="num">Confirmations</th><th class="num">XMZ</th><th>Payout address</th></tr></thead>
      <tbody>
      <?php foreach ($adminPay as $a): ?>
        <tr class="<?= $a['admin'] === $reviewer ? 'me' : '' ?>"><td><?= $e($a['admin']) ?><?= $a['admin'] === $reviewer ? ' <span class="tag">you</span>' : '' ?></td><td class="num"><?= $e((string)$a['confirmations']) ?></td><td class="num"><?= $e(number_format($a['xmz'], 2)) ?></td><td class="mono"><?= $e($a['address'] ? adShort($a['address']) : '—') ?></td></tr>
      <?php endforeach; ?>
      <?php if (!$adminPay): ?><tr><td colspan="4" class="muted">No approvals yet.</td></tr><?php endif; ?>
      </tbody>
    </table>
  </div>
</section>
</main>
<script type="module" src="/airdrop/admin.mjs?v=20261114"></script>
<?php endif; ?>
</body></html>
